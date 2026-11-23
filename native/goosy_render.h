#ifndef GOOSY_RENDER_H
#define GOOSY_RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "goosy_native.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GOOSY_RENDERER_AUTO   = 0,
    GOOSY_RENDERER_VULKAN = 1,
    GOOSY_RENDERER_OPENGL = 2,
    GOOSY_RENDERER_PIXMAN = 3
} goosy_renderer_type_t;

typedef struct goosy_renderer_backend {
    const char *name;
    goosy_renderer_type_t type;
    bool (*init)(void);
    int32_t (*begin)(uint32_t bg_rgb);
    void (*fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb);
    void (*cell_ext)(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                     uint32_t codepoint, uint32_t combining, uint32_t fg_rgb, uint32_t bg_rgb,
                     uint32_t attr_flags, int32_t span_cols,
                     uint8_t ul_style, uint32_t ul_color);
    void (*cursor)(int32_t cx_px, int32_t cy_px, int32_t cell_w, int32_t cell_h,
                   uint32_t color_rgb, int32_t style);
    int32_t (*title_icon)(int32_t x, int32_t y, int32_t size_px);
    void (*images)(int32_t pad_px, int32_t title_h, int32_t view_top_abs_row,
                   int32_t rows, int32_t cols, int32_t cell_w, int32_t cell_h);
    void (*damage)(int32_t x, int32_t y, int32_t w, int32_t h);
    void (*end)(bool request_frame_cb);
    void (*cleanup)(void);
    void (*resize)(int32_t width_px, int32_t height_px, int32_t scale);
} goosy_renderer_backend_t;

/* Backends */
extern goosy_renderer_backend_t g_backend_pixman;
extern goosy_renderer_backend_t g_backend_gl;
extern goosy_renderer_backend_t g_backend_vk;

/* Coordinator API */
int32_t goosy_renderer_init(int32_t pref_type);
int32_t goosy_renderer_get_active_type(void);
const char *goosy_renderer_get_name(void);
void goosy_renderer_print_info(void);
void goosy_renderer_resize(int32_t width_px, int32_t height_px, int32_t scale);
void goosy_render_close(void);

#ifdef __cplusplus
}
#endif

#endif /* GOOSY_RENDER_H */
