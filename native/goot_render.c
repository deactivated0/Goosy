/*
 * Goot — Pixman Software Rendering Bridge
 * Composites dirty terminal cells and cursor onto the active wl_shm buffer
 * with accumulated damage rectangle tracking and zero per-frame allocations.
 */

#define _POSIX_C_SOURCE 200809L

#include "goot_native.h"

#include <pixman.h>
#include <string.h>

static struct goot_shm_buffer *g_cur_buf = NULL;
static pixman_image_t         *g_cur_img = NULL;

static int32_t g_dmg_x0 = 0;
static int32_t g_dmg_y0 = 0;
static int32_t g_dmg_x1 = 0;
static int32_t g_dmg_y1 = 0;
static bool    g_has_dmg = false;

static pixman_color_t rgb_to_pixman(uint32_t rgb) {
    pixman_color_t c;
    c.red   = (uint16_t)(((rgb >> 16) & 0xFFU) * 257U);
    c.green = (uint16_t)(((rgb >> 8)  & 0xFFU) * 257U);
    c.blue  = (uint16_t)((rgb         & 0xFFU) * 257U);
    c.alpha = 0xFFFFU;
    return c;
}

int32_t goot_render_begin(uint32_t bg_rgb) {
    (void)bg_rgb;
    g_cur_buf = goot_wl_acquire_buffer();
    if (!g_cur_buf || !g_cur_buf->pixels) {
        return -1;
    }
    g_cur_img = pixman_image_create_bits(
        PIXMAN_x8r8g8b8, g_cur_buf->width, g_cur_buf->height,
        g_cur_buf->pixels, g_cur_buf->stride);
    if (!g_cur_img) {
        g_cur_buf = NULL;
        return -1;
    }
    g_has_dmg = false;
    return 0;
}

void goot_render_damage(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!g_cur_buf || w <= 0 || h <= 0) return;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > g_cur_buf->width  ? g_cur_buf->width  : x + w;
    int32_t y1 = y + h > g_cur_buf->height ? g_cur_buf->height : y + h;
    if (x0 >= x1 || y0 >= y1) return;

    if (!g_has_dmg) {
        g_dmg_x0 = x0; g_dmg_y0 = y0;
        g_dmg_x1 = x1; g_dmg_y1 = y1;
        g_has_dmg = true;
    } else {
        if (x0 < g_dmg_x0) g_dmg_x0 = x0;
        if (y0 < g_dmg_y0) g_dmg_y0 = y0;
        if (x1 > g_dmg_x1) g_dmg_x1 = x1;
        if (y1 > g_dmg_y1) g_dmg_y1 = y1;
    }
}

void goot_render_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb) {
    if (!g_cur_img || w <= 0 || h <= 0) return;
    pixman_color_t col = rgb_to_pixman(rgb);
    pixman_rectangle16_t rect = { (int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h };
    pixman_image_fill_rectangles(PIXMAN_OP_SRC, g_cur_img, &col, 1, &rect);
    goot_render_damage(x, y, w, h);
}

void goot_render_cell(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                      uint32_t codepoint, uint32_t fg_rgb, uint32_t bg_rgb,
                      uint32_t attr_flags, int32_t span_cols) {
    if (!g_cur_img || cell_w <= 0 || cell_h <= 0) return;
    if (span_cols < 1) span_cols = 1;
    int32_t total_w = cell_w * span_cols;

    /* 1. Fill cell background */
    goot_render_fill_rect(px, py, total_w, cell_h, bg_rgb);

    /* 2. Composite rasterized glyph if printable */
    if (codepoint > 0x20U) {
        uint8_t style = (uint8_t)(attr_flags & 0x03U); /* bit0=bold, bit1=italic */
        const goot_glyph_t *g = goot_font_get_glyph(codepoint, style);
        if (g && g->bitmap && g->width > 0 && g->height > 0) {
            int32_t gx = px + g->bearing_x;
            int32_t gy = py + goot_font_baseline() - g->bearing_y;
            if (g->is_color) {
                pixman_image_t *src = pixman_image_create_bits(
                    PIXMAN_a8r8g8b8, g->width, g->height,
                    (uint32_t *)g->bitmap, g->stride);
                if (src) {
                    pixman_image_composite32(
                        PIXMAN_OP_OVER, src, NULL, g_cur_img,
                        0, 0, 0, 0, gx, gy, (uint32_t)g->width, (uint32_t)g->height);
                    pixman_image_unref(src);
                }
            } else {
                pixman_color_t fg = rgb_to_pixman(fg_rgb);
                pixman_image_t *src = pixman_image_create_solid_fill(&fg);
                pixman_image_t *mask = pixman_image_create_bits(
                    PIXMAN_a8, g->width, g->height,
                    (uint32_t *)g->bitmap, g->stride);
                if (src && mask) {
                    pixman_image_composite32(
                        PIXMAN_OP_OVER, src, mask, g_cur_img,
                        0, 0, 0, 0, gx, gy, (uint32_t)g->width, (uint32_t)g->height);
                }
                if (mask) pixman_image_unref(mask);
                if (src)  pixman_image_unref(src);
            }
        }
    }

    /* 3. Underline (bit 2) & Strikethrough (bit 5) */
    if (attr_flags & (1U << 2)) {
        int32_t uy = py + goot_font_baseline() + 2;
        if (uy >= py + cell_h) uy = py + cell_h - 1;
        goot_render_fill_rect(px, uy, total_w, 1, fg_rgb);
    }
    if (attr_flags & (1U << 5)) {
        int32_t sy = py + cell_h / 2;
        goot_render_fill_rect(px, sy, total_w, 1, fg_rgb);
    }
}

void goot_render_cursor(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                        uint32_t color_rgb, int32_t style) {
    if (!g_cur_img || cell_w <= 0 || cell_h <= 0) return;
    /* style: 0=block, 1=underline, 2=beam */
    if (style == 1) {
        goot_render_fill_rect(px, py + cell_h - 2, cell_w, 2, color_rgb);
    } else if (style == 2) {
        goot_render_fill_rect(px, py, 2, cell_h, color_rgb);
    } else {
        goot_render_fill_rect(px, py, cell_w, cell_h, color_rgb);
    }
}

void goot_render_end(bool request_frame_cb) {
    if (g_cur_img) {
        pixman_image_unref(g_cur_img);
        g_cur_img = NULL;
    }
    if (g_cur_buf) {
        if (g_has_dmg) {
            goot_wl_commit_damage(
                g_cur_buf, g_dmg_x0, g_dmg_y0,
                g_dmg_x1 - g_dmg_x0, g_dmg_y1 - g_dmg_y0,
                request_frame_cb);
        }
        g_cur_buf = NULL;
    }
    g_has_dmg = false;
}
