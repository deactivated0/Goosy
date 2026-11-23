/*
 * Goosy — libxkbcommon Keyboard Translation Bridge
 */

#define _POSIX_C_SOURCE 200809L

#include "goosy_native.h"

#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

static struct xkb_context *g_xkb_ctx   = NULL;
static struct xkb_keymap  *g_xkb_map   = NULL;
static struct xkb_state   *g_xkb_state = NULL;

static xkb_mod_index_t g_mod_shift = XKB_MOD_INVALID;
static xkb_mod_index_t g_mod_ctrl  = XKB_MOD_INVALID;
static xkb_mod_index_t g_mod_alt   = XKB_MOD_INVALID;
static xkb_mod_index_t g_mod_super = XKB_MOD_INVALID;

int32_t goosy_xkb_init(void) {
    if (g_xkb_ctx) {
        return 0;
    }
    g_xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    return g_xkb_ctx ? 0 : -1;
}

int32_t goosy_xkb_set_keymap_fd(int32_t fd, uint32_t size) {
    if (!g_xkb_ctx || fd < 0 || size == 0 || size > (16U * 1024U * 1024U)) {
        if (fd >= 0) close(fd);
        return -1;
    }

    char *map_shm = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map_shm == MAP_FAILED) {
        return -1;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_string(
        g_xkb_ctx, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_shm, size);

    if (!keymap) {
        return -1;
    }

    struct xkb_state *state = xkb_state_new(keymap);
    if (!state) {
        xkb_keymap_unref(keymap);
        return -1;
    }

    if (g_xkb_state) xkb_state_unref(g_xkb_state);
    if (g_xkb_map)   xkb_keymap_unref(g_xkb_map);

    g_xkb_map   = keymap;
    g_xkb_state = state;

    g_mod_shift = xkb_keymap_mod_get_index(g_xkb_map, XKB_MOD_NAME_SHIFT);
    g_mod_ctrl  = xkb_keymap_mod_get_index(g_xkb_map, XKB_MOD_NAME_CTRL);
    g_mod_alt   = xkb_keymap_mod_get_index(g_xkb_map, XKB_MOD_NAME_ALT);
    g_mod_super = xkb_keymap_mod_get_index(g_xkb_map, XKB_MOD_NAME_LOGO);
    return 0;
}

void goosy_xkb_update_mods(uint32_t depressed, uint32_t latched,
                          uint32_t locked, uint32_t group) {
    if (!g_xkb_state) return;
    xkb_state_update_mask(g_xkb_state, depressed, latched, locked, 0, 0, group);
}

int32_t goosy_xkb_current_mods(void) {
    if (!g_xkb_state) return 0;
    int32_t m = 0;
    if (g_mod_shift != XKB_MOD_INVALID &&
        xkb_state_mod_index_is_active(g_xkb_state, g_mod_shift, XKB_STATE_MODS_EFFECTIVE) > 0) {
        m |= GOOSY_MOD_SHIFT;
    }
    if (g_mod_ctrl != XKB_MOD_INVALID &&
        xkb_state_mod_index_is_active(g_xkb_state, g_mod_ctrl, XKB_STATE_MODS_EFFECTIVE) > 0) {
        m |= GOOSY_MOD_CTRL;
    }
    if (g_mod_alt != XKB_MOD_INVALID &&
        xkb_state_mod_index_is_active(g_xkb_state, g_mod_alt, XKB_STATE_MODS_EFFECTIVE) > 0) {
        m |= GOOSY_MOD_ALT;
    }
    if (g_mod_super != XKB_MOD_INVALID &&
        xkb_state_mod_index_is_active(g_xkb_state, g_mod_super, XKB_STATE_MODS_EFFECTIVE) > 0) {
        m |= GOOSY_MOD_SUPER;
    }
    return m;
}

int32_t goosy_xkb_translate_key(uint32_t keycode, bool pressed,
                               uint32_t *out_keysym, int32_t *out_mods,
                               uint8_t *utf8_buf, int32_t utf8_cap,
                               int32_t *out_utf8_len) {
    if (!g_xkb_state || !pressed) {
        return 0;
    }
    xkb_keycode_t xkb_kc = (xkb_keycode_t)(keycode + 8U);
    xkb_keysym_t sym = xkb_state_key_get_one_sym(g_xkb_state, xkb_kc);
    *out_keysym = (uint32_t)sym;
    *out_mods   = goosy_xkb_current_mods();
    *out_utf8_len = 0;

    if (utf8_buf && utf8_cap > 0) {
        int n = xkb_state_key_get_utf8(g_xkb_state, xkb_kc, (char *)utf8_buf, (size_t)utf8_cap);
        if (n > 0 && n < utf8_cap) {
            *out_utf8_len = (int32_t)n;
        }
    }
    return 1;
}

void goosy_xkb_close(void) {
    if (g_xkb_state) { xkb_state_unref(g_xkb_state); g_xkb_state = NULL; }
    if (g_xkb_map)   { xkb_keymap_unref(g_xkb_map);  g_xkb_map = NULL; }
    if (g_xkb_ctx)   { xkb_context_unref(g_xkb_ctx); g_xkb_ctx = NULL; }
}
