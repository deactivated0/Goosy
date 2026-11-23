/*
 * Goosy — Unified Render Coordinator
 * Dispatches drawing calls to the active backend (Vulkan, OpenGL, or Pixman)
 * with automatic capability probing, GPU detection, and seamless zero-crash fallbacks.
 */

#define _POSIX_C_SOURCE 200809L

#include "goosy_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static goosy_renderer_backend_t *g_active_backend = &g_backend_pixman;
static int32_t                   g_backend_mode   = GOOSY_RENDERER_AUTO;
static bool                      g_initialized    = false;

int32_t goosy_renderer_init(int32_t pref_type) {
    g_backend_mode = pref_type;

    if (pref_type == GOOSY_RENDERER_VULKAN) {
        if (g_backend_vk.init()) {
            g_active_backend = &g_backend_vk;
            g_initialized = true;
            return 0;
        }
        fprintf(stderr, "[goosy] Vulkan initialization failed, attempting OpenGL fallback...\n");
        pref_type = GOOSY_RENDERER_OPENGL;
    }

    if (pref_type == GOOSY_RENDERER_OPENGL) {
        if (g_backend_gl.init()) {
            g_active_backend = &g_backend_gl;
            g_initialized = true;
            return 0;
        }
        fprintf(stderr, "[goosy] OpenGL initialization failed, falling back to Pixman software renderer...\n");
        pref_type = GOOSY_RENDERER_PIXMAN;
    }

    if (pref_type == GOOSY_RENDERER_PIXMAN) {
        g_backend_pixman.init();
        g_active_backend = &g_backend_pixman;
        g_initialized = true;
        return 0;
    }

    /* Auto Mode: Vulkan -> OpenGL -> Pixman */
    if (g_backend_vk.init()) {
        g_active_backend = &g_backend_vk;
        g_initialized = true;
        return 0;
    }

    if (g_backend_gl.init()) {
        g_active_backend = &g_backend_gl;
        g_initialized = true;
        return 0;
    }

    g_backend_pixman.init();
    g_active_backend = &g_backend_pixman;
    g_initialized = true;
    return 0;
}

int32_t goosy_renderer_get_active_type(void) {
    return g_active_backend ? (int32_t)g_active_backend->type : (int32_t)GOOSY_RENDERER_PIXMAN;
}

const char *goosy_renderer_get_name(void) {
    return g_active_backend ? g_active_backend->name : "Unknown";
}

void goosy_renderer_print_info(void) {
    printf("==============================================================================\n");
    printf("                  GOOSY MULTI-BACKEND RENDERING SUBSYSTEM\n");
    printf("==============================================================================\n");
    printf("Active Backend:    %s\n", goosy_renderer_get_name());
    printf("\nAvailable Backends:\n");
    printf("  1. Vulkan:       Hardware GPU via VK_KHR_wayland_surface + SPIR-V\n");
    printf("  2. OpenGL:       Hardware GPU via EGL + OpenGL ES 2.0+ & wayland-egl\n");
    printf("  3. Pixman:       High-Performance Software Universal Fallback (wl_shm)\n");
    printf("\nSelection Options:\n");
    printf("  --renderer=auto    Automatically probe Vulkan -> OpenGL -> Pixman (default)\n");
    printf("  --renderer=vulkan  Force hardware-accelerated Vulkan backend\n");
    printf("  --renderer=opengl  Force hardware-accelerated OpenGL / EGL backend\n");
    printf("  --renderer=pixman  Force software Pixman universal fallback\n");
    printf("==============================================================================\n");
}

void goosy_renderer_resize(int32_t width_px, int32_t height_px, int32_t scale) {
    if (g_active_backend && g_active_backend->resize) {
        g_active_backend->resize(width_px, height_px, scale);
    }
}

/* -------------------------------------------------------------------------
 * Goose FFI Dispatch Wrappers
 * ------------------------------------------------------------------------- */

int32_t goosy_render_begin(uint32_t bg_rgb) {
    if (!g_initialized) {
        goosy_renderer_init(GOOSY_RENDERER_AUTO);
    }
    return g_active_backend->begin(bg_rgb);
}

void goosy_render_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb) {
    if (g_active_backend) {
        g_active_backend->fill_rect(x, y, w, h, rgb);
    }
}

void goosy_render_cell_ext(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                          uint32_t codepoint, uint32_t combining, uint32_t fg_rgb, uint32_t bg_rgb,
                          uint32_t attr_flags, int32_t span_cols,
                          uint8_t ul_style, uint32_t ul_color) {
    if (g_active_backend) {
        g_active_backend->cell_ext(px, py, cell_w, cell_h, codepoint, combining, fg_rgb, bg_rgb,
                                   attr_flags, span_cols, ul_style, ul_color);
    }
}

void goosy_render_cell(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                      uint32_t codepoint, uint32_t fg_rgb, uint32_t bg_rgb,
                      uint32_t attr_flags, int32_t span_cols) {
    if (g_active_backend) {
        g_active_backend->cell_ext(px, py, cell_w, cell_h, codepoint, 0, fg_rgb, bg_rgb,
                                   attr_flags, span_cols, 0, 0);
    }
}

void goosy_render_cursor(int32_t cx_px, int32_t cy_px, int32_t cell_w, int32_t cell_h,
                         uint32_t color_rgb, int32_t style) {
    if (g_active_backend) {
        g_active_backend->cursor(cx_px, cy_px, cell_w, cell_h, color_rgb, style);
    }
}

int32_t goosy_render_title_icon(int32_t x, int32_t y, int32_t size_px) {
    return g_active_backend ? g_active_backend->title_icon(x, y, size_px) : 0;
}

void goosy_render_images(int32_t pad_px, int32_t title_h, int32_t view_top_abs_row,
                         int32_t rows, int32_t cols, int32_t cell_w, int32_t cell_h) {
    if (g_active_backend) {
        g_active_backend->images(pad_px, title_h, view_top_abs_row, rows, cols, cell_w, cell_h);
    }
}

void goosy_render_damage(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (g_active_backend) {
        g_active_backend->damage(x, y, w, h);
    }
}

void goosy_render_end(bool request_frame_cb) {
    if (g_active_backend) {
        g_active_backend->end(request_frame_cb);
    }
}

void goosy_render_close(void) {
    if (!g_initialized) return;
    if (g_active_backend && g_active_backend->cleanup) {
        g_active_backend->cleanup();
    }
    g_active_backend = NULL;
    g_initialized = false;
}
