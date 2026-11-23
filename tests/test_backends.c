/*
 * Goosy — Three-Backend Renderer Verification Test
 * Tests Vulkan, OpenGL/EGL, and Pixman initialization, drawing, and automatic fallback.
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "goosy_native.h"
#include "goosy_render.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static goosy_const_bytes_t cstr(const char *s) {
    goosy_const_bytes_t b = { (const uint8_t *)s, (int64_t)strlen(s) };
    return b;
}

int main(void) {
    printf("=== 4. Running Multi-Backend Renderer Tests (Pixman, OpenGL, Vulkan) ===\n");

    printf("[test_backends] Initializing Wayland surface for renderer tests...\n");
    int32_t wl_ok = goosy_wl_init(cstr("Goosy Backend Verification"), cstr("goosy"), 640, 480);
    if (wl_ok != 0) {
        printf("[test_backends] Warning: Wayland display unavailable; skipping live GPU surface tests.\n");
        return 0;
    }

    /* 1. Test Pixman Software Renderer */
    printf("[test_backends] 1. Testing Pixman software backend...\n");
    assert(goosy_renderer_init(GOOSY_RENDERER_PIXMAN) == 0);
    assert(goosy_renderer_get_active_type() == GOOSY_RENDERER_PIXMAN);
    printf("[test_backends]    Active: %s\n", goosy_renderer_get_name());
    assert(goosy_render_begin(0x181818) == 0);
    goosy_render_fill_rect(0, 0, 100, 100, 0xFF0000);
    goosy_render_cell_ext(10, 10, 8, 16, 0x2500, 0, 0xFFFFFF, 0x000000, 0, 1, 0, 0); // Box drawing horizontal line
    goosy_render_end(false);
    printf("[test_backends]    Pixman render cycle passed.\n");

    /* 2. Test OpenGL / EGL Hardware Backend */
    printf("[test_backends] 2. Testing OpenGL / EGL hardware backend...\n");
    int32_t gl_res = goosy_renderer_init(GOOSY_RENDERER_OPENGL);
    if (gl_res == 0 && goosy_renderer_get_active_type() == GOOSY_RENDERER_OPENGL) {
        printf("[test_backends]    Active: %s\n", goosy_renderer_get_name());
        assert(goosy_render_begin(0x181818) == 0);
        goosy_render_fill_rect(0, 0, 100, 100, 0x00FF00);
        goosy_render_cell_ext(10, 10, 8, 16, 0x2500, 0, 0xFFFFFF, 0x000000, 0, 1, 0, 0); // Box drawing horizontal line
        goosy_render_end(false);
        printf("[test_backends]    OpenGL render cycle passed.\n");
    } else {
        printf("[test_backends]    OpenGL unavailable on current display; fallback invoked.\n");
    }

    /* 3. Test Vulkan Hardware Backend */
    printf("[test_backends] 3. Testing Vulkan hardware backend...\n");
    int32_t vk_res = goosy_renderer_init(GOOSY_RENDERER_VULKAN);
    if (vk_res == 0 && goosy_renderer_get_active_type() == GOOSY_RENDERER_VULKAN) {
        printf("[test_backends]    Active: %s\n", goosy_renderer_get_name());
        assert(goosy_render_begin(0x181818) == 0);
        goosy_render_fill_rect(0, 0, 100, 100, 0x0000FF);
        goosy_render_cell_ext(10, 10, 8, 16, 0x2500, 0, 0xFFFFFF, 0x000000, 0, 1, 0, 0); // Box drawing horizontal line
        goosy_render_end(false);
        printf("[test_backends]    Vulkan render cycle passed.\n");
    } else {
        printf("[test_backends]    Vulkan unavailable on current display; fallback invoked.\n");
    }

    /* 4. Test Auto-Probing Fallback Chain */
    printf("[test_backends] 4. Testing Auto-probing fallback chain...\n");
    assert(goosy_renderer_init(GOOSY_RENDERER_AUTO) == 0);
    printf("[test_backends]    Auto selected: %s\n", goosy_renderer_get_name());
    assert(goosy_renderer_get_active_type() >= GOOSY_RENDERER_VULKAN &&
           goosy_renderer_get_active_type() <= GOOSY_RENDERER_PIXMAN);

    goosy_render_close();
    goosy_wl_close();
    printf("[test_backends] ALL MULTI-BACKEND RENDERER TESTS PASSED SUCCESSFULLY.\n");
    return 0;
}
