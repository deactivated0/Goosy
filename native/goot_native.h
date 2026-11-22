#ifndef GOOT_NATIVE_H
#define GOOT_NATIVE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef GOOT_WITH_GOOSE_H
#include "goose.h"
#else
/* Fallback declarations for standalone C unit tests compiled outside goose build */
typedef struct {
    void *stack;
    void *hdr;
} gs_rref;
#endif

/* Goose slice ABI (spec §7.10): { data, len } */
typedef struct {
    const uint8_t *data;
    int64_t len;
} goot_const_bytes_t;

typedef struct {
    uint8_t *data;
    int64_t len;
} goot_bytes_t;

/* -------------------------------------------------------------------------
 * Event types returned from native Wayland/input/PTY bridge to Goose
 * ------------------------------------------------------------------------- */
enum {
    GOOT_EV_NONE            = 0,
    GOOT_EV_CLOSE           = 1,
    GOOT_EV_CONFIGURE       = 2,  /* a=width_px, b=height_px, c=scale */
    GOOT_EV_FRAME_DONE      = 3,
    GOOT_EV_KEY             = 4,  /* a=keysym, b=mods, c=utf8_len */
    GOOT_EV_POINTER_MOTION  = 5,  /* a=x_px, b=y_px, c=mods */
    GOOT_EV_POINTER_BUTTON  = 6,  /* a=button, b=pressed(1/0), c=mods */
    GOOT_EV_POINTER_AXIS    = 7,  /* a=delta_y (positive=down, negative=up), b=mods */
    GOOT_EV_FOCUS           = 8,  /* a=focused(1/0) */
    GOOT_EV_CLIPBOARD_DATA  = 9,  /* clipboard paste payload ready */
    GOOT_EV_PRIMARY_DATA    = 10, /* primary selection paste payload ready */
    GOOT_EV_TOUCH_DOWN      = 11, /* a=x_px, b=y_px, c=touch_id */
    GOOT_EV_TOUCH_UP        = 12, /* a=touch_id */
    GOOT_EV_TOUCH_MOTION    = 13  /* a=x_px, b=y_px, c=touch_id */
};

/* Modifier bitmask shared with Goose input module */
enum {
    GOOT_MOD_SHIFT = 1 << 0,
    GOOT_MOD_CTRL  = 1 << 1,
    GOOT_MOD_ALT   = 1 << 2,
    GOOT_MOD_SUPER = 1 << 3
};

/* Poll bitmask returned by goot_poll_wait */
enum {
    GOOT_POLL_ERR     = -1,
    GOOT_POLL_TIMEOUT = 0,
    GOOT_POLL_WAYLAND = 1 << 0,
    GOOT_POLL_PTY     = 1 << 1,
    GOOT_POLL_SIGNAL  = 1 << 2,
    GOOT_POLL_PTY_HUP = 1 << 3,
    GOOT_POLL_TIMER   = 1 << 4
};

/* -------------------------------------------------------------------------
 * POSIX PTY, Spawn & URL Launch API (native/goot_pty.c)
 * ------------------------------------------------------------------------- */
int32_t goot_pty_spawn(goot_const_bytes_t shell_override,
                       goot_const_bytes_t term_env,
                       int32_t cols, int32_t rows,
                       int32_t width_px, int32_t height_px);
int32_t goot_pty_fd(void);
int32_t goot_pty_child_pid(void);
int64_t goot_pty_read(goot_bytes_t buf);
int64_t goot_pty_write(goot_const_bytes_t data);
int32_t goot_pty_has_pending_write(void);
int64_t goot_pty_flush(void);
int32_t goot_pty_resize(int32_t cols, int32_t rows, int32_t width_px, int32_t height_px);
int32_t goot_pty_check_child(void); /* 0=alive, 1=exited, -1=error */
int32_t goot_pty_exit_code(void);
void    goot_pty_close(void);

/* Safe process spawning (never invokes a shell) */
int32_t goot_spawn_self(goot_const_bytes_t cwd);
int32_t goot_validate_url(goot_const_bytes_t url);
int32_t goot_open_url(goot_const_bytes_t url);

/* -------------------------------------------------------------------------
 * Linux epoll + signalfd Event Loop API (native/goot_poll.c)
 * ------------------------------------------------------------------------- */
int32_t goot_poll_init(int32_t wl_fd, int32_t pty_fd);
int32_t goot_poll_wait(int32_t timeout_ms);
int32_t goot_poll_terminated(void);
void    goot_poll_arm_repeat(int32_t delay_ms, int32_t interval_ms);
void    goot_poll_disarm_repeat(void);
void    goot_poll_close(void);
int64_t goot_time_monotonic_ns(void);

/* -------------------------------------------------------------------------
 * Native Wayland + xdg-shell + wl_shm + Clipboard + Touch (native/goot_wayland.c)
 * ------------------------------------------------------------------------- */
int32_t goot_wl_init(goot_const_bytes_t title, goot_const_bytes_t app_id,
                     int32_t init_width, int32_t init_height);
int32_t goot_wl_fd(void);
int32_t goot_wl_dispatch_pending(void);
int32_t goot_wl_prepare_and_read(void);
int32_t goot_wl_flush(void);
int32_t goot_wl_poll_event(void);
int32_t goot_wl_ev_a(void);
int32_t goot_wl_ev_b(void);
int32_t goot_wl_ev_c(void);
int64_t goot_wl_ev_utf8(goot_bytes_t out_buf);
void    goot_wl_set_title(goot_const_bytes_t title);
int32_t goot_wl_width(void);
int32_t goot_wl_height(void);
int32_t goot_wl_scale(void);
bool    goot_wl_frame_ready(void);
int32_t goot_wl_is_maximized(void);
int32_t goot_wl_is_fullscreen(void);
int32_t goot_wl_is_activated(void);
int32_t goot_wl_is_csd(void);
void    goot_wl_set_decoration_mode(int32_t pref);
void    goot_wl_toplevel_move(void);
void    goot_wl_toplevel_resize(int32_t edges);
void    goot_wl_toplevel_minimize(void);
void    goot_wl_toplevel_toggle_maximize(void);
void    goot_wl_handle_timer_repeat(void);
void    goot_wl_set_cursor_shape(int32_t shape);
int32_t goot_wl_clipboard_set(goot_const_bytes_t text);
int32_t goot_wl_clipboard_request(void);
int64_t goot_wl_clipboard_read(goot_bytes_t out_buf);
int32_t goot_wl_primary_set(goot_const_bytes_t text);
int32_t goot_wl_primary_request(void);
int64_t goot_wl_primary_read(goot_bytes_t out_buf);
void    goot_wl_close(void);

/* Internal bridge hooks between Wayland, XKB, and Renderer */
struct goot_shm_buffer {
    void    *wl_buffer;
    uint32_t *pixels;
    int32_t  width;
    int32_t  height;
    int32_t  stride;
    size_t   size;
    bool     busy;
};

struct goot_shm_buffer *goot_wl_acquire_buffer(void);
void goot_wl_commit_damage(struct goot_shm_buffer *buf,
                           int32_t x, int32_t y, int32_t w, int32_t h,
                           bool request_frame_cb);

/* -------------------------------------------------------------------------
 * libxkbcommon Keyboard API (native/goot_xkb.c)
 * ------------------------------------------------------------------------- */
int32_t goot_xkb_init(void);
int32_t goot_xkb_set_keymap_fd(int32_t fd, uint32_t size);
void    goot_xkb_update_mods(uint32_t depressed, uint32_t latched,
                             uint32_t locked, uint32_t group);
int32_t goot_xkb_current_mods(void);
int32_t goot_xkb_translate_key(uint32_t keycode, bool pressed,
                               uint32_t *out_keysym, int32_t *out_mods,
                               uint8_t *utf8_buf, int32_t utf8_cap,
                               int32_t *out_utf8_len);
void    goot_xkb_close(void);

/* -------------------------------------------------------------------------
 * FreeType + Fontconfig + HarfBuzz Font Engine API (native/goot_font.c)
 * ------------------------------------------------------------------------- */
typedef struct {
    int32_t  width;
    int32_t  height;
    int32_t  stride;
    int32_t  bearing_x;
    int32_t  bearing_y;
    int32_t  advance;
    uint8_t  is_color; /* 1 = BGRA emoji bitmap, 0 = 8-bit grayscale alpha */
    uint8_t *bitmap;   /* owned by glyph cache */
} goot_glyph_t;

int32_t goot_font_init(goot_const_bytes_t family, int32_t size_pt, int32_t dpi, int32_t line_spacing_px);
int32_t goot_font_resize(int32_t size_pt, int32_t dpi);
int32_t goot_font_cell_width(void);
int32_t goot_font_cell_height(void);
int32_t goot_font_baseline(void);
const goot_glyph_t *goot_font_get_glyph(uint32_t codepoint, uint8_t style_flags);
const goot_glyph_t *goot_font_shape_ligature(const uint32_t *cps, int32_t len, uint8_t style_flags);
int32_t             goot_font_is_rtl(uint32_t cp);
void                goot_font_close(void);

/* -------------------------------------------------------------------------
 * Pixman Software Renderer & Graphics API (native/goot_render.c, goot_image.c)
 * ------------------------------------------------------------------------- */
int32_t goot_render_begin(uint32_t bg_rgb);
void    goot_render_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb);
void    goot_render_cell(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                         uint32_t codepoint, uint32_t fg_rgb, uint32_t bg_rgb,
                         uint32_t attr_flags, int32_t span_cols);
void    goot_render_cell_ext(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                             uint32_t codepoint, uint32_t fg_rgb, uint32_t bg_rgb,
                             uint32_t attr_flags, int32_t span_cols,
                             uint8_t ul_style, uint32_t ul_color);
void    goot_render_cursor(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                           uint32_t color_rgb, int32_t style);
void    goot_render_damage(int32_t x, int32_t y, int32_t w, int32_t h);
void    goot_render_images(int32_t pad_px, int32_t title_h, int32_t view_top_abs_row,
                           int32_t rows, int32_t cols, int32_t cell_w, int32_t cell_h);
void    goot_render_end(bool request_frame_cb);

/* Sixel & Kitty Graphics */
int32_t goot_sixel_decode(goot_const_bytes_t data, int32_t col, int32_t abs_row, int32_t cell_w, int32_t cell_h);
int32_t goot_kitty_graphics(goot_const_bytes_t data, int32_t col, int32_t abs_row, int32_t cell_w, int32_t cell_h);
void    goot_image_scroll(int32_t evicted_lines);
void    goot_image_clear(void);

/* -------------------------------------------------------------------------
 * Config / Environment Helper API (native/goot_pty.c)
 * ------------------------------------------------------------------------- */
int64_t goot_env_get(goot_const_bytes_t name, goot_bytes_t out_buf);
int64_t goot_file_read(goot_const_bytes_t path, goot_bytes_t out_buf);

#endif /* GOOT_NATIVE_H */
