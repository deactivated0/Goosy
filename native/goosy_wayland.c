/*
 * Goosy — Native Wayland Client Bridge
 * Implements libwayland-client, xdg-shell toplevel, wl_shm double buffering,
 * HiDPI scale tracking, frame callbacks, wl_seat keyboard/pointer/touch input,
 * Wayland clipboard (wl_data_device), and primary selection buffer.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "goosy_native.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "primary-selection-unstable-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

#define GOOSY_EV_QUEUE_CAP 256
#define GOOSY_CLIP_MAX     (256 * 1024)
#define GOOSY_MAX_DIM      8192

typedef struct {
    int32_t type;
    int32_t a;
    int32_t b;
    int32_t c;
    uint8_t utf8[16];
    int32_t utf8_len;
} goosy_ev_t;

static struct wl_display             *g_display    = NULL;
static struct wl_registry            *g_registry   = NULL;
static struct wl_compositor          *g_compositor = NULL;
static struct wl_shm                 *g_shm        = NULL;
static struct xdg_wm_base            *g_wm_base    = NULL;
static struct wl_seat                *g_seat       = NULL;
static struct wl_keyboard            *g_keyboard   = NULL;
static struct wl_pointer             *g_pointer    = NULL;
static struct wl_touch               *g_touch      = NULL;
static struct wl_data_device_manager *g_ddm        = NULL;
static struct wl_data_device         *g_data_dev   = NULL;
static struct wl_data_source         *g_clip_src   = NULL;
static struct wl_data_offer          *g_clip_offer = NULL;

static struct wl_surface             *g_surface    = NULL;
static struct xdg_surface            *g_xdg_surf   = NULL;
static struct xdg_toplevel           *g_toplevel   = NULL;
static struct wl_callback            *g_frame_cb   = NULL;

static struct zxdg_decoration_manager_v1        *g_deco_mgr        = NULL;
static struct zxdg_toplevel_decoration_v1       *g_toplevel_deco   = NULL;
static int32_t                                   g_deco_pref       = 2; /* 0=auto, 1=server, 2=client, 3=none */
static uint32_t                                  g_negotiated_deco = 0;

static struct zwp_primary_selection_device_manager_v1 *g_prim_mgr   = NULL;
static struct zwp_primary_selection_device_v1         *g_prim_dev   = NULL;
static struct zwp_primary_selection_source_v1         *g_prim_src   = NULL;
static struct zwp_primary_selection_offer_v1          *g_prim_offer = NULL;

static struct wp_fractional_scale_manager_v1          *g_fract_mgr  = NULL;
static struct wp_fractional_scale_v1                  *g_fract_scale= NULL;
static struct wp_viewporter                           *g_viewporter = NULL;
static struct wp_viewport                             *g_viewport   = NULL;
static uint32_t                                        g_preferred_scale_120ths = 120;

static struct wl_cursor_theme *g_cursor_theme   = NULL;
static struct wl_surface      *g_cursor_surface = NULL;
static int32_t                 g_cursor_shape   = -1;

static bool g_is_maximized  = false;
static bool g_is_fullscreen = false;
static bool g_is_activated  = true;

static int32_t  g_width       = 800;
static int32_t  g_height      = 600;
static int32_t  g_scale       = 1;
static bool     g_configured  = false;
static bool     g_frame_ready = true;
static uint32_t g_last_serial = 0;

static struct goosy_shm_buffer g_bufs[2];

static goosy_ev_t g_ev_q[GOOSY_EV_QUEUE_CAP];
static uint32_t  g_ev_head = 0;
static uint32_t  g_ev_tail = 0;
static goosy_ev_t g_ev_cur;

/* Clipboard & Primary selection storage */
static uint8_t  g_clip_copy_buf[GOOSY_CLIP_MAX];
static int64_t  g_clip_copy_len = 0;
static uint8_t  g_clip_paste_buf[GOOSY_CLIP_MAX];
static int64_t  g_clip_paste_len = 0;

static uint8_t  g_primary_buf[GOOSY_CLIP_MAX];
static int64_t  g_primary_len = 0;

static void push_ev(const goosy_ev_t *ev) {
    uint32_t next = (g_ev_tail + 1U) % GOOSY_EV_QUEUE_CAP;
    if (next == g_ev_head) {
        g_ev_head = (g_ev_head + 1U) % GOOSY_EV_QUEUE_CAP;
    }
    g_ev_q[g_ev_tail] = *ev;
    g_ev_tail = next;
}

static void push_ev3(int32_t type, int32_t a, int32_t b, int32_t c) {
    goosy_ev_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.a = a;
    ev.b = b;
    ev.c = c;
    push_ev(&ev);
}

/* -------------------------------------------------------------------------
 * wl_shm Double Buffer Lifecycle
 * ------------------------------------------------------------------------- */
static void destroy_shm_buffer(struct goosy_shm_buffer *b) {
    if (b->wl_buffer) {
        wl_buffer_destroy((struct wl_buffer *)b->wl_buffer);
        b->wl_buffer = NULL;
    }
    if (b->pixels && b->size > 0) {
        munmap(b->pixels, b->size);
        b->pixels = NULL;
    }
    b->width = 0; b->height = 0; b->stride = 0; b->size = 0; b->busy = false;
}

static void buffer_release(void *data, struct wl_buffer *wl_buf) {
    (void)wl_buf;
    ((struct goosy_shm_buffer *)data)->busy = false;
}

static const struct wl_buffer_listener g_buffer_listener = {
    .release = buffer_release,
};

static int create_shm_buffer(struct goosy_shm_buffer *b, int32_t w, int32_t h) {
    if (!g_shm || w <= 0 || h <= 0 || w > GOOSY_MAX_DIM || h > GOOSY_MAX_DIM) {
        return -1;
    }
    int32_t stride = w * 4;
    size_t size = (size_t)stride * (size_t)h;
    if (size / (size_t)stride != (size_t)h) return -1;

    int fd = memfd_create("goosy-shm", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return -1; }

    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) { close(fd); return -1; }

    struct wl_shm_pool *pool = wl_shm_create_pool(g_shm, fd, (int32_t)size);
    close(fd);
    if (!pool) { munmap(pixels, size); return -1; }

    struct wl_buffer *wl_buf = wl_shm_pool_create_buffer(
        pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    if (!wl_buf) { munmap(pixels, size); return -1; }

    wl_buffer_add_listener(wl_buf, &g_buffer_listener, b);
    b->wl_buffer = wl_buf; b->pixels = pixels;
    b->width = w; b->height = h; b->stride = stride; b->size = size; b->busy = false;
    return 0;
}

struct goosy_shm_buffer *goosy_wl_acquire_buffer(void) {
    int32_t pw = g_width * g_scale;
    int32_t ph = g_height * g_scale;
    if (pw <= 0 || ph <= 0 || pw > GOOSY_MAX_DIM || ph > GOOSY_MAX_DIM) return NULL;
    for (int i = 0; i < 2; i++) {
        if (!g_bufs[i].busy) {
            if (g_bufs[i].width != pw || g_bufs[i].height != ph) {
                destroy_shm_buffer(&g_bufs[i]);
                if (create_shm_buffer(&g_bufs[i], pw, ph) != 0) return NULL;
            }
            return &g_bufs[i];
        }
    }
    return NULL;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time_ms) {
    (void)data; (void)time_ms;
    wl_callback_destroy(cb);
    if (g_frame_cb == cb) g_frame_cb = NULL;
    g_frame_ready = true;
    push_ev3(GOOSY_EV_FRAME_DONE, 0, 0, 0);
}

static void fract_preferred_scale(void *data, struct wp_fractional_scale_v1 *fs, uint32_t scale_120ths) {
    (void)data; (void)fs;
    if (scale_120ths > 0) {
        g_preferred_scale_120ths = scale_120ths;
        int32_t sc = (scale_120ths + 59) / 120;
        if (sc < 1) sc = 1;
        if (sc != g_scale) {
            g_scale = sc;
            push_ev3(GOOSY_EV_CONFIGURE, g_width, g_height, g_scale);
        }
    }
}
static const struct wp_fractional_scale_v1_listener g_fract_scale_listener = {
    .preferred_scale = fract_preferred_scale,
};

static const struct wl_callback_listener g_frame_listener = { .done = frame_done };

void goosy_wl_commit_damage(struct goosy_shm_buffer *buf,
                           int32_t x, int32_t y, int32_t w, int32_t h,
                           bool request_frame_cb) {
    if (!g_surface || !buf || !buf->wl_buffer) return;
    if (g_viewport) {
        wl_surface_set_buffer_scale(g_surface, 1);
        if (g_width > 0 && g_height > 0) {
            wp_viewport_set_destination(g_viewport, g_width, g_height);
        }
    } else {
        wl_surface_set_buffer_scale(g_surface, g_scale);
    }
    wl_surface_attach(g_surface, (struct wl_buffer *)buf->wl_buffer, 0, 0);
    if (w > 0 && h > 0) wl_surface_damage_buffer(g_surface, x, y, w, h);
    if (request_frame_cb && !g_frame_cb) {
        g_frame_cb = wl_surface_frame(g_surface);
        wl_callback_add_listener(g_frame_cb, &g_frame_listener, NULL);
        g_frame_ready = false;
    }
    buf->busy = true;
    wl_surface_commit(g_surface);
    wl_display_flush(g_display);
}

/* -------------------------------------------------------------------------
 * xdg-shell Listeners
 * ------------------------------------------------------------------------- */
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data; xdg_wm_base_pong(wm_base, serial);
}
static const struct xdg_wm_base_listener g_wm_base_listener = { .ping = xdg_wm_base_ping };

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(xdg_surface, serial);
    g_configured = true;
    push_ev3(GOOSY_EV_CONFIGURE, g_width, g_height, g_scale);
}
static const struct xdg_surface_listener g_xdg_surface_listener = { .configure = xdg_surface_configure };

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height, struct wl_array *states) {
    (void)data; (void)toplevel;
    if (width > 0 && width <= GOOSY_MAX_DIM)   g_width  = width;
    if (height > 0 && height <= GOOSY_MAX_DIM) g_height = height;

    g_is_maximized = false;
    g_is_fullscreen = false;
    g_is_activated = false;
    if (states && states->data) {
        uint32_t *state;
        wl_array_for_each(state, states) {
            if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)  g_is_maximized = true;
            if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN) g_is_fullscreen = true;
            if (*state == XDG_TOPLEVEL_STATE_ACTIVATED)  g_is_activated = true;
        }
    }
}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)data; (void)toplevel; push_ev3(GOOSY_EV_CLOSE, 0, 0, 0);
}
static const struct xdg_toplevel_listener g_xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close     = xdg_toplevel_close,
};

/* -------------------------------------------------------------------------
 * Keyboard, Pointer & Touch Listeners
 * ------------------------------------------------------------------------- */
static void kb_keymap(void *data, struct wl_keyboard *kb, uint32_t format, int32_t fd, uint32_t size) {
    (void)data; (void)kb;
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) goosy_xkb_set_keymap_fd(fd, size);
    else if (fd >= 0) close(fd);
}
static void kb_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
                     struct wl_surface *sf, struct wl_array *keys) {
    (void)data; (void)kb; (void)sf; (void)keys;
    g_last_serial = serial;
    push_ev3(GOOSY_EV_FOCUS, 1, 0, 0);
}
static void kb_leave(void *data, struct wl_keyboard *kb, uint32_t serial, struct wl_surface *sf) {
    (void)data; (void)kb; (void)serial; (void)sf;
    push_ev3(GOOSY_EV_FOCUS, 0, 0, 0);
}
static int32_t  g_repeat_rate  = 25;
static int32_t  g_repeat_delay = 400;
static uint32_t g_repeat_key   = 0;

static void kb_key(void *data, struct wl_keyboard *kb, uint32_t serial,
                   uint32_t time_ms, uint32_t key, uint32_t state) {
    (void)data; (void)kb; (void)time_ms;
    g_last_serial = serial;
    bool pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
    if (pressed) {
        g_repeat_key = key;
        int32_t interval = (g_repeat_rate > 0) ? (1000 / g_repeat_rate) : 40;
        if (interval < 5) interval = 5;
        goosy_poll_arm_repeat(g_repeat_delay, interval);
    } else {
        if (g_repeat_key == key) {
            goosy_poll_disarm_repeat();
            g_repeat_key = 0;
        }
    }
    uint32_t sym = 0;
    int32_t mods = 0;
    goosy_ev_t ev;
    memset(&ev, 0, sizeof(ev));
    if (goosy_xkb_translate_key(key, pressed, &sym, &mods,
                               ev.utf8, (int32_t)sizeof(ev.utf8), &ev.utf8_len)) {
        ev.type = GOOSY_EV_KEY;
        ev.a    = (int32_t)sym;
        ev.b    = mods;
        ev.c    = ev.utf8_len;
        push_ev(&ev);
    }
}
static void kb_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
                         uint32_t dep, uint32_t lat, uint32_t loc, uint32_t grp) {
    (void)data; (void)kb; (void)serial;
    goosy_xkb_update_mods(dep, lat, loc, grp);
}
static void kb_repeat_info(void *data, struct wl_keyboard *kb, int32_t rate, int32_t delay) {
    (void)data; (void)kb;
    if (rate > 0) g_repeat_rate = rate;
    if (delay > 0) g_repeat_delay = delay;
}
static const struct wl_keyboard_listener g_kb_listener = {
    .keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave,
    .key = kb_key, .modifiers = kb_modifiers, .repeat_info = kb_repeat_info,
};

void goosy_wl_handle_timer_repeat(void) {
    if (g_repeat_key == 0) return;
    uint32_t sym = 0;
    int32_t mods = 0;
    goosy_ev_t ev;
    memset(&ev, 0, sizeof(ev));
    if (goosy_xkb_translate_key(g_repeat_key, true, &sym, &mods,
                               ev.utf8, (int32_t)sizeof(ev.utf8), &ev.utf8_len)) {
        ev.type = GOOSY_EV_KEY;
        ev.a    = (int32_t)sym;
        ev.b    = mods;
        ev.c    = ev.utf8_len;
        push_ev(&ev);
    }
}

void goosy_wl_set_cursor_shape(int32_t shape) {
    if (!g_pointer || !g_shm || !g_display) return;
    if (g_cursor_shape == shape && g_cursor_surface) return;
    g_cursor_shape = shape;

    if (!g_cursor_theme) {
        const char *size_str = getenv("XCURSOR_SIZE");
        int size = 24;
        if (size_str) {
            int s = atoi(size_str);
            if (s >= 16 && s <= 128) size = s;
        }
        size *= (g_scale > 0 ? g_scale : 1);
        const char *theme = getenv("XCURSOR_THEME");
        g_cursor_theme = wl_cursor_theme_load(theme, size, g_shm);
    }
    if (!g_cursor_theme) return;

    const char *names[7][4] = {
        { "text", "xterm", "ibeam", NULL },
        { "default", "left_ptr", "arrow", NULL },
        { "pointer", "hand2", "hand", NULL },
        { "ns-resize", "v_double_arrow", "size_ver", "s-resize" },
        { "ew-resize", "h_double_arrow", "size_hor", "e-resize" },
        { "nwse-resize", "size_fdiag", "top_left_corner", "bottom_right_corner" },
        { "nesw-resize", "size_bdiag", "top_right_corner", "bottom_left_corner" },
    };
    int idx = (shape >= 0 && shape <= 6) ? shape : 1;
    struct wl_cursor *cur = NULL;
    for (int k = 0; k < 4; k++) {
        if (!names[idx][k]) break;
        cur = wl_cursor_theme_get_cursor(g_cursor_theme, names[idx][k]);
        if (cur) break;
    }
    if (!cur && idx != 1) {
        for (int k = 0; k < 3; k++) {
            cur = wl_cursor_theme_get_cursor(g_cursor_theme, names[1][k]);
            if (cur) break;
        }
    }
    if (!cur || cur->image_count < 1) return;

    struct wl_cursor_image *img = cur->images[0];
    struct wl_buffer *buf = wl_cursor_image_get_buffer(img);
    if (!buf) return;

    if (!g_cursor_surface && g_compositor) {
        g_cursor_surface = wl_compositor_create_surface(g_compositor);
    }
    if (!g_cursor_surface) return;

    wl_surface_attach(g_cursor_surface, buf, 0, 0);
    wl_surface_damage_buffer(g_cursor_surface, 0, 0, (int32_t)img->width, (int32_t)img->height);
    wl_surface_commit(g_cursor_surface);

    wl_pointer_set_cursor(g_pointer, g_last_serial, g_cursor_surface,
                          (int32_t)img->hotspot_x, (int32_t)img->hotspot_y);
}

static void ptr_enter(void *data, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *sf, wl_fixed_t sx, wl_fixed_t sy) {
    (void)data; (void)p; (void)sf;
    g_last_serial = serial;
    g_cursor_shape = -1;
    goosy_wl_set_cursor_shape(1);
    push_ev3(GOOSY_EV_POINTER_MOTION, wl_fixed_to_int(sx), wl_fixed_to_int(sy), goosy_xkb_current_mods());
}
static void ptr_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf) {
    (void)data; (void)p; (void)serial; (void)sf;
}
static void ptr_motion(void *data, struct wl_pointer *p, uint32_t time_ms, wl_fixed_t sx, wl_fixed_t sy) {
    (void)data; (void)p; (void)time_ms;
    push_ev3(GOOSY_EV_POINTER_MOTION, wl_fixed_to_int(sx), wl_fixed_to_int(sy), goosy_xkb_current_mods());
}
static void ptr_button(void *data, struct wl_pointer *p, uint32_t serial,
                       uint32_t time_ms, uint32_t button, uint32_t state) {
    (void)data; (void)p; (void)time_ms;
    g_last_serial = serial;
    push_ev3(GOOSY_EV_POINTER_BUTTON, (int32_t)button,
             state == WL_POINTER_BUTTON_STATE_PRESSED ? 1 : 0, goosy_xkb_current_mods());
}
static void ptr_axis(void *data, struct wl_pointer *p, uint32_t time_ms, uint32_t axis, wl_fixed_t value) {
    (void)data; (void)p; (void)time_ms;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        push_ev3(GOOSY_EV_POINTER_AXIS, wl_fixed_to_int(value), goosy_xkb_current_mods(), 0);
    }
}
static void ptr_frame(void *data, struct wl_pointer *p) { (void)data; (void)p; }
static void ptr_axis_source(void *data, struct wl_pointer *p, uint32_t axis_source) { (void)data; (void)p; (void)axis_source; }
static void ptr_axis_stop(void *data, struct wl_pointer *p, uint32_t time_ms, uint32_t axis) { (void)data; (void)p; (void)time_ms; (void)axis; }
static void ptr_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t discrete) { (void)data; (void)p; (void)axis; (void)discrete; }
static void ptr_axis_value120(void *data, struct wl_pointer *p, uint32_t axis, int32_t value120) { (void)data; (void)p; (void)axis; (void)value120; }
static void ptr_axis_relative_direction(void *data, struct wl_pointer *p, uint32_t axis, uint32_t direction) { (void)data; (void)p; (void)axis; (void)direction; }
static void ptr_warp(void *data, struct wl_pointer *p, wl_fixed_t sx, wl_fixed_t sy) { (void)data; (void)p; (void)sx; (void)sy; }

static const struct wl_pointer_listener g_ptr_listener = {
    .enter = ptr_enter, .leave = ptr_leave, .motion = ptr_motion,
    .button = ptr_button, .axis = ptr_axis,
    .frame = ptr_frame,
    .axis_source = ptr_axis_source,
    .axis_stop = ptr_axis_stop,
    .axis_discrete = ptr_axis_discrete,
    .axis_value120 = ptr_axis_value120,
    .axis_relative_direction = ptr_axis_relative_direction,
    .warp = ptr_warp,
};

static void touch_down(void *data, struct wl_touch *t, uint32_t serial, uint32_t time_ms,
                       struct wl_surface *sf, int32_t id, wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)t; (void)time_ms; (void)sf;
    g_last_serial = serial;
    push_ev3(GOOSY_EV_TOUCH_DOWN, wl_fixed_to_int(x), wl_fixed_to_int(y), id);
}
static void touch_up(void *data, struct wl_touch *t, uint32_t serial, uint32_t time_ms, int32_t id) {
    (void)data; (void)t; (void)time_ms;
    g_last_serial = serial;
    push_ev3(GOOSY_EV_TOUCH_UP, id, 0, 0);
}
static void touch_motion(void *data, struct wl_touch *t, uint32_t time_ms, int32_t id,
                         wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)t; (void)time_ms;
    push_ev3(GOOSY_EV_TOUCH_MOTION, wl_fixed_to_int(x), wl_fixed_to_int(y), id);
}
static void touch_frame(void *data, struct wl_touch *t) { (void)data; (void)t; }
static void touch_cancel(void *data, struct wl_touch *t) { (void)data; (void)t; }

static const struct wl_touch_listener g_touch_listener = {
    .down = touch_down, .up = touch_up, .motion = touch_motion,
    .frame = touch_frame, .cancel = touch_cancel,
};

/* -------------------------------------------------------------------------
 * Wayland Clipboard (wl_data_device) & Primary Selection
 * ------------------------------------------------------------------------- */
static void data_source_target(void *data, struct wl_data_source *src, const char *mime) {
    (void)data; (void)src; (void)mime;
}
static void data_source_send(void *data, struct wl_data_source *src, const char *mime, int32_t fd) {
    (void)data; (void)src;
    if (mime && (strcmp(mime, "text/plain;charset=utf-8") == 0 ||
                 strcmp(mime, "text/plain") == 0 || strcmp(mime, "UTF8_STRING") == 0)) {
        int64_t off = 0;
        while (off < g_clip_copy_len) {
            ssize_t n = write(fd, g_clip_copy_buf + off, (size_t)(g_clip_copy_len - off));
            if (n <= 0) break;
            off += (int64_t)n;
        }
    }
    close(fd);
}
static void data_source_cancelled(void *data, struct wl_data_source *src) {
    (void)data;
    if (g_clip_src == src) g_clip_src = NULL;
    wl_data_source_destroy(src);
}
static const struct wl_data_source_listener g_data_source_listener = {
    .target = data_source_target, .send = data_source_send, .cancelled = data_source_cancelled,
};

static void data_dev_offer(void *d, struct wl_data_device *dev, struct wl_data_offer *id) { (void)d;(void)dev;(void)id; }
static void data_dev_enter(void *d, struct wl_data_device *dev, uint32_t s, struct wl_surface *sf,
                           wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *id) { (void)d;(void)dev;(void)s;(void)sf;(void)x;(void)y;(void)id; }
static void data_dev_leave(void *d, struct wl_data_device *dev) { (void)d;(void)dev; }
static void data_dev_motion(void *d, struct wl_data_device *dev, uint32_t t, wl_fixed_t x, wl_fixed_t y) { (void)d;(void)dev;(void)t;(void)x;(void)y; }
static void data_dev_drop(void *d, struct wl_data_device *dev) { (void)d;(void)dev; }
static void data_dev_selection(void *d, struct wl_data_device *dev, struct wl_data_offer *id) {
    (void)d; (void)dev;
    if (g_clip_offer) wl_data_offer_destroy(g_clip_offer);
    g_clip_offer = id;
}
static const struct wl_data_device_listener g_data_dev_listener = {
    .data_offer = data_dev_offer, .enter = data_dev_enter, .leave = data_dev_leave,
    .motion = data_dev_motion, .drop = data_dev_drop, .selection = data_dev_selection,
};

static void deco_configure(void *data, struct zxdg_toplevel_decoration_v1 *deco, uint32_t mode) {
    (void)data; (void)deco;
    g_negotiated_deco = mode;
}
static const struct zxdg_toplevel_decoration_v1_listener g_deco_listener = {
    .configure = deco_configure,
};

static void prim_source_send(void *data, struct zwp_primary_selection_source_v1 *src,
                             const char *mime, int32_t fd) {
    (void)data; (void)src;
    if (mime && (strcmp(mime, "text/plain;charset=utf-8") == 0 ||
                 strcmp(mime, "text/plain") == 0 || strcmp(mime, "UTF8_STRING") == 0)) {
        int64_t off = 0;
        while (off < g_primary_len) {
            ssize_t n = write(fd, g_primary_buf + off, (size_t)(g_primary_len - off));
            if (n <= 0) break;
            off += (int64_t)n;
        }
    }
    close(fd);
}
static void prim_source_cancelled(void *data, struct zwp_primary_selection_source_v1 *src) {
    (void)data;
    if (g_prim_src == src) g_prim_src = NULL;
    zwp_primary_selection_source_v1_destroy(src);
}
static const struct zwp_primary_selection_source_v1_listener g_prim_source_listener = {
    .send      = prim_source_send,
    .cancelled = prim_source_cancelled,
};

static void prim_dev_offer(void *d, struct zwp_primary_selection_device_v1 *dev,
                           struct zwp_primary_selection_offer_v1 *id) {
    (void)d; (void)dev; (void)id;
}
static void prim_dev_selection(void *d, struct zwp_primary_selection_device_v1 *dev,
                               struct zwp_primary_selection_offer_v1 *id) {
    (void)d; (void)dev;
    if (g_prim_offer) zwp_primary_selection_offer_v1_destroy(g_prim_offer);
    g_prim_offer = id;
}
static const struct zwp_primary_selection_device_v1_listener g_prim_dev_listener = {
    .data_offer = prim_dev_offer,
    .selection  = prim_dev_selection,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    (void)data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g_keyboard) {
        g_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g_keyboard, &g_kb_listener, NULL);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !g_pointer) {
        g_pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g_pointer, &g_ptr_listener, NULL);
    }
    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !g_touch) {
        g_touch = wl_seat_get_touch(seat);
        wl_touch_add_listener(g_touch, &g_touch_listener, NULL);
    }
    if (g_prim_mgr && g_seat && !g_prim_dev) {
        g_prim_dev = zwp_primary_selection_device_manager_v1_get_device(g_prim_mgr, g_seat);
        if (g_prim_dev) {
            zwp_primary_selection_device_v1_add_listener(g_prim_dev, &g_prim_dev_listener, NULL);
        }
    }
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) { (void)data;(void)seat;(void)name; }
static const struct wl_seat_listener g_seat_listener = { .capabilities = seat_capabilities, .name = seat_name };

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *interface, uint32_t version) {
    (void)data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        g_compositor = wl_registry_bind(reg, name, &wl_compositor_interface, version >= 4 ? 4 : version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        g_shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        g_wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(g_wm_base, &g_wm_base_listener, NULL);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        g_seat = wl_registry_bind(reg, name, &wl_seat_interface, version >= 5 ? 5 : version);
        wl_seat_add_listener(g_seat, &g_seat_listener, NULL);
    } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        g_ddm = wl_registry_bind(reg, name, &wl_data_device_manager_interface, 1);
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        g_deco_mgr = wl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (strcmp(interface, zwp_primary_selection_device_manager_v1_interface.name) == 0) {
        g_prim_mgr = wl_registry_bind(reg, name, &zwp_primary_selection_device_manager_v1_interface, 1);
    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        g_fract_mgr = wl_registry_bind(reg, name, &wp_fractional_scale_manager_v1_interface, 1);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        g_viewporter = wl_registry_bind(reg, name, &wp_viewporter_interface, 1);
    }
}
static void registry_remove(void *data, struct wl_registry *reg, uint32_t name) { (void)data;(void)reg;(void)name; }
static const struct wl_registry_listener g_registry_listener = {
    .global = registry_global, .global_remove = registry_remove,
};

/* -------------------------------------------------------------------------
 * Public Wayland FFI Functions for Goose
 * ------------------------------------------------------------------------- */
int32_t goosy_wl_init(goosy_const_bytes_t title, goosy_const_bytes_t app_id,
                     int32_t init_width, int32_t init_height) {
    if (init_width > 0 && init_width <= GOOSY_MAX_DIM)   g_width  = init_width;
    if (init_height > 0 && init_height <= GOOSY_MAX_DIM) g_height = init_height;
    if (goosy_xkb_init() != 0) return -1;

    g_display = wl_display_connect(NULL);
    if (!g_display) return -1;

    g_registry = wl_display_get_registry(g_display);
    wl_registry_add_listener(g_registry, &g_registry_listener, NULL);
    wl_display_roundtrip(g_display);

    if (!g_compositor || !g_shm || !g_wm_base) {
        goosy_wl_close();
        return -1;
    }

    if (g_ddm && g_seat) {
        g_data_dev = wl_data_device_manager_get_data_device(g_ddm, g_seat);
        if (g_data_dev) wl_data_device_add_listener(g_data_dev, &g_data_dev_listener, NULL);
    }

    g_surface  = wl_compositor_create_surface(g_compositor);
    g_xdg_surf = xdg_wm_base_get_xdg_surface(g_wm_base, g_surface);
    xdg_surface_add_listener(g_xdg_surf, &g_xdg_surface_listener, NULL);

    g_toplevel = xdg_surface_get_toplevel(g_xdg_surf);
    xdg_toplevel_add_listener(g_toplevel, &g_xdg_toplevel_listener, NULL);

    const char *custom_title = goosy_cmdline_title();
    if (custom_title && custom_title[0] != '\0') {
        xdg_toplevel_set_title(g_toplevel, custom_title);
    } else {
        goosy_wl_set_title(title);
    }
    char aid[128] = "goosy";
    if (app_id.data && app_id.len > 0 && app_id.len < (int64_t)sizeof(aid)) {
        memcpy(aid, app_id.data, (size_t)app_id.len);
        aid[app_id.len] = '\0';
    }
    xdg_toplevel_set_app_id(g_toplevel, aid);
    xdg_toplevel_set_min_size(g_toplevel, 240, 160);

    if (g_deco_mgr && g_toplevel) {
        g_toplevel_deco = zxdg_decoration_manager_v1_get_toplevel_decoration(g_deco_mgr, g_toplevel);
        if (g_toplevel_deco) {
            zxdg_toplevel_decoration_v1_add_listener(g_toplevel_deco, &g_deco_listener, NULL);
            if (g_deco_pref == 1) {
                zxdg_toplevel_decoration_v1_set_mode(g_toplevel_deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
            } else {
                zxdg_toplevel_decoration_v1_set_mode(g_toplevel_deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
            }
        }
    }

    if (g_fract_mgr && g_surface) {
        g_fract_scale = wp_fractional_scale_manager_v1_get_fractional_scale(g_fract_mgr, g_surface);
        if (g_fract_scale) {
            wp_fractional_scale_v1_add_listener(g_fract_scale, &g_fract_scale_listener, NULL);
        }
    }
    if (g_viewporter && g_surface) {
        g_viewport = wp_viewporter_get_viewport(g_viewporter, g_surface);
    }

    wl_surface_commit(g_surface);
    wl_display_roundtrip(g_display);
    return 0;
}

int32_t goosy_wl_fd(void) { return g_display ? wl_display_get_fd(g_display) : -1; }
int32_t goosy_wl_dispatch_pending(void) { return g_display ? wl_display_dispatch_pending(g_display) : -1; }

int32_t goosy_wl_prepare_and_read(void) {
    if (!g_display) return -1;
    while (wl_display_prepare_read(g_display) != 0) {
        if (wl_display_dispatch_pending(g_display) < 0) return -1;
    }
    if (wl_display_flush(g_display) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(g_display);
        return -1;
    }
    if (wl_display_read_events(g_display) < 0) return -1;
    return wl_display_dispatch_pending(g_display);
}

int32_t goosy_wl_flush(void) {
    if (!g_display) return -1;
    int r = wl_display_flush(g_display);
    return (r < 0 && errno != EAGAIN) ? -1 : 0;
}

int32_t goosy_wl_poll_event(void) {
    if (g_ev_head == g_ev_tail) return GOOSY_EV_NONE;
    g_ev_cur = g_ev_q[g_ev_head];
    g_ev_head = (g_ev_head + 1U) % GOOSY_EV_QUEUE_CAP;
    return g_ev_cur.type;
}

int32_t goosy_wl_ev_a(void) { return g_ev_cur.a; }
int32_t goosy_wl_ev_b(void) { return g_ev_cur.b; }
int32_t goosy_wl_ev_c(void) { return g_ev_cur.c; }

int64_t goosy_wl_ev_utf8(goosy_bytes_t out_buf) {
    if (!out_buf.data || out_buf.len <= 0 || g_ev_cur.utf8_len <= 0) return 0;
    int32_t n = g_ev_cur.utf8_len;
    if ((int64_t)n > out_buf.len) n = (int32_t)out_buf.len;
    memcpy(out_buf.data, g_ev_cur.utf8, (size_t)n);
    return (int64_t)n;
}

void goosy_wl_set_title(goosy_const_bytes_t title) {
    if (!g_toplevel) return;
    char buf[256] = "Goosy";
    if (title.data && title.len > 0) {
        int64_t n = title.len < (int64_t)(sizeof(buf) - 1) ? title.len : (int64_t)(sizeof(buf) - 1);
        memcpy(buf, title.data, (size_t)n);
        buf[n] = '\0';
    }
    xdg_toplevel_set_title(g_toplevel, buf);
}

int32_t goosy_wl_width(void)       { return g_width; }
int32_t goosy_wl_height(void)      { return g_height; }
int32_t goosy_wl_scale(void)       { return g_scale; }
bool    goosy_wl_frame_ready(void) { return g_frame_ready; }
void    goosy_wl_request_frame_callback(void) {
    if (!g_surface) return;
    if (!g_frame_cb) {
        g_frame_cb = wl_surface_frame(g_surface);
        wl_callback_add_listener(g_frame_cb, &g_frame_listener, NULL);
        g_frame_ready = false;
        wl_surface_commit(g_surface);
        wl_display_flush(g_display);
    }
}
struct wl_display *goosy_wl_get_display(void) { return g_display; }
struct wl_surface *goosy_wl_get_surface(void) { return g_surface; }

int32_t goosy_wl_is_maximized(void)  { return g_is_maximized ? 1 : 0; }
int32_t goosy_wl_is_fullscreen(void) { return g_is_fullscreen ? 1 : 0; }
int32_t goosy_wl_is_activated(void)  { return g_is_activated ? 1 : 0; }

int32_t goosy_wl_is_csd(void) {
    if (g_deco_pref == 3) return 0; /* none */
    if (g_is_fullscreen) return 0;  /* fullscreen has no decorations */
    if (g_deco_pref == 1 && g_negotiated_deco == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) return 0;
    return 1;
}

void goosy_wl_set_decoration_mode(int32_t pref) {
    g_deco_pref = pref;
    if (g_toplevel_deco) {
        if (pref == 1) {
            zxdg_toplevel_decoration_v1_set_mode(g_toplevel_deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        } else if (pref == 0 || pref == 2) {
            zxdg_toplevel_decoration_v1_set_mode(g_toplevel_deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
        }
    }
}

void goosy_wl_toplevel_move(void) {
    if (g_toplevel && g_seat && g_last_serial > 0) {
        xdg_toplevel_move(g_toplevel, g_seat, g_last_serial);
    }
}

void goosy_wl_toplevel_resize(int32_t edges) {
    if (g_toplevel && g_seat && g_last_serial > 0 && edges > 0) {
        xdg_toplevel_resize(g_toplevel, g_seat, g_last_serial, (uint32_t)edges);
    }
}

void goosy_wl_toplevel_minimize(void) {
    if (g_toplevel) {
        xdg_toplevel_set_minimized(g_toplevel);
    }
}

void goosy_wl_toplevel_toggle_maximize(void) {
    if (!g_toplevel) return;
    if (g_is_maximized) {
        xdg_toplevel_unset_maximized(g_toplevel);
    } else {
        xdg_toplevel_set_maximized(g_toplevel);
    }
}

int32_t goosy_wl_clipboard_set(goosy_const_bytes_t text) {
    if (!g_ddm || !g_data_dev || !text.data || text.len <= 0) return -1;
    int64_t n = text.len > GOOSY_CLIP_MAX ? GOOSY_CLIP_MAX : text.len;
    memcpy(g_clip_copy_buf, text.data, (size_t)n);
    g_clip_copy_len = n;

    if (g_clip_src) { wl_data_source_destroy(g_clip_src); g_clip_src = NULL; }
    g_clip_src = wl_data_device_manager_create_data_source(g_ddm);
    if (!g_clip_src) return -1;
    wl_data_source_add_listener(g_clip_src, &g_data_source_listener, NULL);
    wl_data_source_offer(g_clip_src, "text/plain;charset=utf-8");
    wl_data_source_offer(g_clip_src, "text/plain");
    wl_data_source_offer(g_clip_src, "UTF8_STRING");
    wl_data_device_set_selection(g_data_dev, g_clip_src, g_last_serial);
    wl_display_flush(g_display);
    return 0;
}

int32_t goosy_wl_clipboard_request(void) {
    if (!g_clip_offer || !g_display) return -1;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return -1;
    wl_data_offer_receive(g_clip_offer, "text/plain;charset=utf-8", fds[1]);
    close(fds[1]);
    wl_display_flush(g_display);

    g_clip_paste_len = 0;
    while (g_clip_paste_len < GOOSY_CLIP_MAX) {
        ssize_t n = read(fds[0], g_clip_paste_buf + g_clip_paste_len,
                         (size_t)(GOOSY_CLIP_MAX - g_clip_paste_len));
        if (n <= 0) break;
        g_clip_paste_len += (int64_t)n;
    }
    close(fds[0]);
    if (g_clip_paste_len > 0) {
        push_ev3(GOOSY_EV_CLIPBOARD_DATA, (int32_t)g_clip_paste_len, 0, 0);
    }
    return (int32_t)g_clip_paste_len;
}

int64_t goosy_wl_clipboard_read(goosy_bytes_t out_buf) {
    if (!out_buf.data || out_buf.len <= 0 || g_clip_paste_len <= 0) return 0;
    int64_t n = g_clip_paste_len < out_buf.len ? g_clip_paste_len : out_buf.len;
    memcpy(out_buf.data, g_clip_paste_buf, (size_t)n);
    g_clip_paste_len = 0;
    return n;
}

int32_t goosy_wl_primary_set(goosy_const_bytes_t text) {
    if (!text.data || text.len <= 0) return -1;
    int64_t n = text.len > GOOSY_CLIP_MAX ? GOOSY_CLIP_MAX : text.len;
    memcpy(g_primary_buf, text.data, (size_t)n);
    g_primary_len = n;

    if (g_prim_mgr && g_prim_dev) {
        if (g_prim_src) {
            zwp_primary_selection_source_v1_destroy(g_prim_src);
            g_prim_src = NULL;
        }
        g_prim_src = zwp_primary_selection_device_manager_v1_create_source(g_prim_mgr);
        if (g_prim_src) {
            zwp_primary_selection_source_v1_add_listener(g_prim_src, &g_prim_source_listener, NULL);
            zwp_primary_selection_source_v1_offer(g_prim_src, "text/plain;charset=utf-8");
            zwp_primary_selection_source_v1_offer(g_prim_src, "text/plain");
            zwp_primary_selection_source_v1_offer(g_prim_src, "UTF8_STRING");
            zwp_primary_selection_device_v1_set_selection(g_prim_dev, g_prim_src, g_last_serial);
            wl_display_flush(g_display);
        }
    }
    return 0;
}

int32_t goosy_wl_primary_request(void) {
    if (g_prim_offer && g_display) {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) == 0) {
            zwp_primary_selection_offer_v1_receive(g_prim_offer, "text/plain;charset=utf-8", fds[1]);
            close(fds[1]);
            wl_display_flush(g_display);

            g_primary_len = 0;
            for (int retry = 0; retry < 5 && g_primary_len < GOOSY_CLIP_MAX; retry++) {
                ssize_t n = read(fds[0], g_primary_buf + g_primary_len, (size_t)(GOOSY_CLIP_MAX - g_primary_len));
                if (n > 0) {
                    g_primary_len += (int64_t)n;
                } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    struct timespec ts = { 0, 2000000 }; /* 2ms */
                    nanosleep(&ts, NULL);
                } else {
                    break;
                }
            }
            close(fds[0]);
            if (g_primary_len > 0) {
                push_ev3(GOOSY_EV_PRIMARY_DATA, (int32_t)g_primary_len, 0, 0);
                return (int32_t)g_primary_len;
            }
        }
    }
    if (g_primary_len > 0) {
        push_ev3(GOOSY_EV_PRIMARY_DATA, (int32_t)g_primary_len, 0, 0);
        return (int32_t)g_primary_len;
    }
    return 0;
}

int64_t goosy_wl_primary_read(goosy_bytes_t out_buf) {
    if (!out_buf.data || out_buf.len <= 0 || g_primary_len <= 0) return 0;
    int64_t n = g_primary_len < out_buf.len ? g_primary_len : out_buf.len;
    memcpy(out_buf.data, g_primary_buf, (size_t)n);
    return n;
}

void goosy_wl_close(void) {
    destroy_shm_buffer(&g_bufs[0]);
    destroy_shm_buffer(&g_bufs[1]);
    if (g_cursor_surface) { wl_surface_destroy(g_cursor_surface); g_cursor_surface = NULL; }
    if (g_cursor_theme)   { wl_cursor_theme_destroy(g_cursor_theme); g_cursor_theme = NULL; }
    if (g_fract_scale)    { wp_fractional_scale_v1_destroy(g_fract_scale); g_fract_scale = NULL; }
    if (g_viewport)       { wp_viewport_destroy(g_viewport); g_viewport = NULL; }
    if (g_fract_mgr)      { wp_fractional_scale_manager_v1_destroy(g_fract_mgr); g_fract_mgr = NULL; }
    if (g_viewporter)     { wp_viewporter_destroy(g_viewporter); g_viewporter = NULL; }
    if (g_prim_src)       { zwp_primary_selection_source_v1_destroy(g_prim_src); g_prim_src = NULL; }
    if (g_prim_offer)     { zwp_primary_selection_offer_v1_destroy(g_prim_offer); g_prim_offer = NULL; }
    if (g_prim_dev)       { zwp_primary_selection_device_v1_destroy(g_prim_dev); g_prim_dev = NULL; }
    if (g_prim_mgr)       { zwp_primary_selection_device_manager_v1_destroy(g_prim_mgr); g_prim_mgr = NULL; }
    if (g_toplevel_deco)  { zxdg_toplevel_decoration_v1_destroy(g_toplevel_deco); g_toplevel_deco = NULL; }
    if (g_deco_mgr)       { zxdg_decoration_manager_v1_destroy(g_deco_mgr); g_deco_mgr = NULL; }
    if (g_frame_cb)   { wl_callback_destroy(g_frame_cb); g_frame_cb = NULL; }
    if (g_clip_src)   { wl_data_source_destroy(g_clip_src); g_clip_src = NULL; }
    if (g_clip_offer) { wl_data_offer_destroy(g_clip_offer); g_clip_offer = NULL; }
    if (g_data_dev)   { wl_data_device_destroy(g_data_dev); g_data_dev = NULL; }
    if (g_ddm)        { wl_data_device_manager_destroy(g_ddm); g_ddm = NULL; }
    if (g_touch)      { wl_touch_destroy(g_touch); g_touch = NULL; }
    if (g_keyboard)   { wl_keyboard_destroy(g_keyboard); g_keyboard = NULL; }
    if (g_pointer)    { wl_pointer_destroy(g_pointer); g_pointer = NULL; }
    if (g_seat)       { wl_seat_destroy(g_seat); g_seat = NULL; }
    if (g_toplevel)   { xdg_toplevel_destroy(g_toplevel); g_toplevel = NULL; }
    if (g_xdg_surf)   { xdg_surface_destroy(g_xdg_surf); g_xdg_surf = NULL; }
    if (g_surface)    { wl_surface_destroy(g_surface); g_surface = NULL; }
    if (g_wm_base)    { xdg_wm_base_destroy(g_wm_base); g_wm_base = NULL; }
    if (g_shm)        { wl_shm_destroy(g_shm); g_shm = NULL; }
    if (g_compositor) { wl_compositor_destroy(g_compositor); g_compositor = NULL; }
    if (g_registry)   { wl_registry_destroy(g_registry); g_registry = NULL; }
    if (g_display)    { wl_display_disconnect(g_display); g_display = NULL; }
    goosy_xkb_close();
}
