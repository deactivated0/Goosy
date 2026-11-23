/*
 * Goosy — Pixman Software Rendering Backend
 * Pure CPU software rendering directly to wl_shm buffers.
 * Universal fallback with zero GPU driver dependencies.
 */

#define _POSIX_C_SOURCE 200809L

#include "goosy_render.h"
#include "goosy_image.h"

#include <pixman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct goosy_shm_buffer *g_cur_buf = NULL;
static pixman_image_t         *g_cur_img = NULL;
static uint32_t               *g_cur_pixels = NULL;
static int32_t                 g_cur_stride_words = 0;
static int32_t                 g_cur_w = 0;
static int32_t                 g_cur_h = 0;

static int32_t g_dmg_x0 = 0;
static int32_t g_dmg_y0 = 0;
static int32_t g_dmg_x1 = 0;
static int32_t g_dmg_y1 = 0;
static bool    g_has_dmg = false;

static inline pixman_image_t *get_cur_pixman_img(void) {
    if (!g_cur_img && g_cur_buf && g_cur_buf->pixels) {
        g_cur_img = pixman_image_create_bits(
            PIXMAN_x8r8g8b8, g_cur_buf->width, g_cur_buf->height,
            g_cur_buf->pixels, g_cur_buf->stride);
    }
    return g_cur_img;
}

static inline void update_damage(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
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

static bool pixman_init(void) {
    return true;
}

static int32_t pixman_begin(uint32_t bg_rgb) {
    (void)bg_rgb;
    g_cur_buf = goosy_wl_acquire_buffer();
    if (!g_cur_buf || !g_cur_buf->pixels) {
        return -1;
    }
    g_cur_pixels = g_cur_buf->pixels;
    g_cur_stride_words = g_cur_buf->stride / (int32_t)sizeof(uint32_t);
    g_cur_w = g_cur_buf->width;
    g_cur_h = g_cur_buf->height;
    g_cur_img = NULL;
    g_has_dmg = false;
    return 0;
}

static void pixman_damage(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!g_cur_pixels || w <= 0 || h <= 0) return;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > g_cur_w ? g_cur_w : x + w;
    int32_t y1 = y + h > g_cur_h ? g_cur_h : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    update_damage(x0, y0, x1, y1);
}

static void pixman_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb) {
    if (!g_cur_pixels || w <= 0 || h <= 0) return;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > g_cur_w ? g_cur_w : x + w;
    int32_t y1 = y + h > g_cur_h ? g_cur_h : y + h;
    if (x0 >= x1 || y0 >= y1) return;

    int32_t fill_w = x1 - x0;
    int32_t fill_h = y1 - y0;
    uint32_t val = 0xFF000000U | (rgb & 0x00FFFFFFU);

    if (x0 == 0 && fill_w == g_cur_stride_words) {
        size_t total_words = (size_t)fill_w * (size_t)fill_h;
        uint32_t *dst = g_cur_pixels + (size_t)y0 * (size_t)g_cur_stride_words;
        uint64_t val64 = ((uint64_t)val << 32) | (uint64_t)val;
        uint64_t *dst64 = (uint64_t *)dst;
        size_t count64 = total_words / 2;
        for (size_t i = 0; i < count64; i++) {
            dst64[i] = val64;
        }
        if (total_words & 1) {
            dst[total_words - 1] = val;
        }
    } else {
        uint64_t val64 = ((uint64_t)val << 32) | (uint64_t)val;
        for (int32_t r = 0; r < fill_h; r++) {
            uint32_t *row = g_cur_pixels + (y0 + r) * g_cur_stride_words + x0;
            int32_t c = 0;
            if (((uintptr_t)row & 7) != 0 && c < fill_w) {
                row[c++] = val;
            }
            int32_t rem = fill_w - c;
            int32_t count64 = rem / 2;
            uint64_t *row64 = (uint64_t *)(row + c);
            for (int32_t i = 0; i < count64; i++) {
                row64[i] = val64;
            }
            if (rem & 1) {
                row[fill_w - 1] = val;
            }
        }
    }
    update_damage(x0, y0, x1, y1);
}

static inline uint32_t div255_fast(uint32_t v) {
    uint32_t t = v + 128U;
    return (t + (t >> 8)) >> 8;
}

static inline void blend_glyph_a8(int32_t dx, int32_t dy, const goosy_glyph_t *g, uint32_t fg_rgb) {
    if (!g_cur_pixels || !g->bitmap || g->width <= 0 || g->height <= 0) return;
    int32_t x0 = dx < 0 ? 0 : dx;
    int32_t y0 = dy < 0 ? 0 : dy;
    int32_t x1 = dx + g->width  > g_cur_w ? g_cur_w : dx + g->width;
    int32_t y1 = dy + g->height > g_cur_h ? g_cur_h : dy + g->height;
    if (x0 >= x1 || y0 >= y1) return;

    int32_t src_x0 = x0 - dx;
    int32_t src_y0 = y0 - dy;
    int32_t w = x1 - x0;
    int32_t h = y1 - y0;

    uint32_t fr = (fg_rgb >> 16) & 0xFFU;
    uint32_t fg = (fg_rgb >> 8)  & 0xFFU;
    uint32_t fb = fg_rgb & 0xFFU;
    uint32_t solid_fg = 0xFF000000U | (fg_rgb & 0x00FFFFFFU);

    for (int32_t r = 0; r < h; r++) {
        uint32_t *d = g_cur_pixels + (y0 + r) * g_cur_stride_words + x0;
        const uint8_t *s = g->bitmap + (src_y0 + r) * g->stride + src_x0;
        for (int32_t c = 0; c < w; c++) {
            uint32_t a = s[c];
            if (a == 0) continue;
            if (a == 255U) {
                d[c] = solid_fg;
            } else {
                uint32_t dp = d[c];
                uint32_t dr = (dp >> 16) & 0xFFU;
                uint32_t dg = (dp >> 8)  & 0xFFU;
                uint32_t db = dp & 0xFFU;
                uint32_t inv_a = 255U - a;
                uint32_t out_r = div255_fast(fr * a + dr * inv_a);
                uint32_t out_g = div255_fast(fg * a + dg * inv_a);
                uint32_t out_b = div255_fast(fb * a + db * inv_a);
                d[c] = 0xFF000000U | (out_r << 16) | (out_g << 8) | out_b;
            }
        }
    }
    update_damage(x0, y0, x1, y1);
}

/* Procedural rendering for Box-Drawing (U+2500..U+257F) and Block Elements (U+2580..U+259F) */
static bool render_procedural_glyph(int32_t px, int32_t py, int32_t w, int32_t h,
                                    uint32_t cp, uint32_t fg_rgb, uint32_t bg_rgb) {
    (void)bg_rgb;
    if (cp < 0x2500 || cp > 0x259F) return false;

    int32_t t = h >= 28 ? 2 : 1;
    int32_t th = t * 2;
    int32_t mx = px + w / 2;
    int32_t my = py + h / 2;

    /* Block Elements U+2580..U+259F */
    if (cp >= 0x2580 && cp <= 0x259F) {
        switch (cp) {
            case 0x2580: pixman_fill_rect(px, py, w, h / 2, fg_rgb); return true;
            case 0x2581: pixman_fill_rect(px, py + h * 7 / 8, w, h - h * 7 / 8, fg_rgb); return true;
            case 0x2582: pixman_fill_rect(px, py + h * 3 / 4, w, h - h * 3 / 4, fg_rgb); return true;
            case 0x2583: pixman_fill_rect(px, py + h * 5 / 8, w, h - h * 5 / 8, fg_rgb); return true;
            case 0x2584: pixman_fill_rect(px, py + h / 2, w, h - h / 2, fg_rgb); return true;
            case 0x2585: pixman_fill_rect(px, py + h * 3 / 8, w, h - h * 3 / 8, fg_rgb); return true;
            case 0x2586: pixman_fill_rect(px, py + h / 4, w, h - h / 4, fg_rgb); return true;
            case 0x2587: pixman_fill_rect(px, py + h / 8, w, h - h / 8, fg_rgb); return true;
            case 0x2588: pixman_fill_rect(px, py, w, h, fg_rgb); return true;
            case 0x2589: pixman_fill_rect(px, py, w * 7 / 8, h, fg_rgb); return true;
            case 0x258A: pixman_fill_rect(px, py, w * 3 / 4, h, fg_rgb); return true;
            case 0x258B: pixman_fill_rect(px, py, w * 5 / 8, h, fg_rgb); return true;
            case 0x258C: pixman_fill_rect(px, py, w / 2, h, fg_rgb); return true;
            case 0x258D: pixman_fill_rect(px, py, w * 3 / 8, h, fg_rgb); return true;
            case 0x258E: pixman_fill_rect(px, py, w / 4, h, fg_rgb); return true;
            case 0x258F: pixman_fill_rect(px, py, w / 8, h, fg_rgb); return true;
            case 0x2590: pixman_fill_rect(px + w / 2, py, w - w / 2, h, fg_rgb); return true;
            case 0x2594: pixman_fill_rect(px, py, w, h / 8, fg_rgb); return true;
            case 0x2595: pixman_fill_rect(px + w * 7 / 8, py, w - w * 7 / 8, h, fg_rgb); return true;
            case 0x2591:
            case 0x2592:
            case 0x2593: {
                uint32_t step = (cp == 0x2591) ? 4 : ((cp == 0x2592) ? 2 : 1);
                for (int32_t dy = 0; dy < h; dy++) {
                    for (int32_t dx = 0; dx < w; dx++) {
                        if (((dx + dy) % step) == 0) {
                            pixman_fill_rect(px + dx, py + dy, 1, 1, fg_rgb);
                        }
                    }
                }
                return true;
            }
            default: return false;
        }
    }

    /* Box-Drawing U+2500..U+257F */
    int32_t l = 0, r = 0, u = 0, d = 0;
    switch (cp) {
        case 0x2500: l = 1; r = 1; break;
        case 0x2501: l = 2; r = 2; break;
        case 0x2502: u = 1; d = 1; break;
        case 0x2503: u = 2; d = 2; break;
        case 0x250C: r = 1; d = 1; break;
        case 0x250D: r = 2; d = 1; break;
        case 0x250E: r = 1; d = 2; break;
        case 0x250F: r = 2; d = 2; break;
        case 0x2510: l = 1; d = 1; break;
        case 0x2511: l = 2; d = 1; break;
        case 0x2512: l = 1; d = 2; break;
        case 0x2513: l = 2; d = 2; break;
        case 0x2514: u = 1; r = 1; break;
        case 0x2515: u = 1; r = 2; break;
        case 0x2516: u = 2; r = 1; break;
        case 0x2517: u = 2; r = 2; break;
        case 0x2518: u = 1; l = 1; break;
        case 0x2519: u = 1; l = 2; break;
        case 0x251A: u = 2; l = 1; break;
        case 0x251B: u = 2; l = 2; break;
        case 0x251C: u = 1; d = 1; r = 1; break;
        case 0x251D: u = 1; d = 1; r = 2; break;
        case 0x2520: u = 2; d = 2; r = 1; break;
        case 0x2523: u = 2; d = 2; r = 2; break;
        case 0x2524: u = 1; d = 1; l = 1; break;
        case 0x2525: u = 1; d = 1; l = 2; break;
        case 0x2528: u = 2; d = 2; l = 1; break;
        case 0x252B: u = 2; d = 2; l = 2; break;
        case 0x252C: l = 1; r = 1; d = 1; break;
        case 0x252F: l = 2; r = 2; d = 1; break;
        case 0x2530: l = 1; r = 1; d = 2; break;
        case 0x2533: l = 2; r = 2; d = 2; break;
        case 0x2534: l = 1; r = 1; u = 1; break;
        case 0x2537: l = 2; r = 2; u = 1; break;
        case 0x2538: l = 1; r = 1; u = 2; break;
        case 0x253B: l = 2; r = 2; u = 2; break;
        case 0x253C: l = 1; r = 1; u = 1; d = 1; break;
        case 0x253F: l = 2; r = 2; u = 1; d = 1; break;
        case 0x2542: l = 1; r = 1; u = 2; d = 2; break;
        case 0x254B: l = 2; r = 2; u = 2; d = 2; break;
        case 0x2550: l = 3; r = 3; break;
        case 0x2551: u = 3; d = 3; break;
        case 0x2554: r = 3; d = 3; break;
        case 0x2557: l = 3; d = 3; break;
        case 0x255A: r = 3; u = 3; break;
        case 0x255D: l = 3; u = 3; break;
        case 0x2560: u = 3; d = 3; r = 3; break;
        case 0x2563: u = 3; d = 3; l = 3; break;
        case 0x2566: l = 3; r = 3; d = 3; break;
        case 0x2569: l = 3; r = 3; u = 3; break;
        case 0x256C: l = 3; r = 3; u = 3; d = 3; break;
        case 0x2574: l = 1; break;
        case 0x2575: u = 1; break;
        case 0x2576: r = 1; break;
        case 0x2577: d = 1; break;
        case 0x2578: l = 2; break;
        case 0x2579: u = 2; break;
        case 0x257A: r = 2; break;
        case 0x257B: d = 2; break;
        default: return false;
    }

    if (l) {
        int32_t lw = l == 3 ? 1 : (l == 2 ? th : t);
        if (l == 3) {
            pixman_fill_rect(px, my - t - 1, mx - px + 1, 1, fg_rgb);
            pixman_fill_rect(px, my + t, mx - px + 1, 1, fg_rgb);
        } else {
            pixman_fill_rect(px, my - lw / 2, mx - px + (r ? 1 : 0), lw, fg_rgb);
        }
    }
    if (r) {
        int32_t rw = r == 3 ? 1 : (r == 2 ? th : t);
        if (r == 3) {
            pixman_fill_rect(mx, my - t - 1, px + w - mx, 1, fg_rgb);
            pixman_fill_rect(mx, my + t, px + w - mx, 1, fg_rgb);
        } else {
            pixman_fill_rect(mx - (l ? 0 : rw / 2), my - rw / 2, px + w - mx + (l ? 0 : rw / 2), rw, fg_rgb);
        }
    }
    if (u) {
        int32_t uw = u == 3 ? 1 : (u == 2 ? th : t);
        if (u == 3) {
            pixman_fill_rect(mx - t - 1, py, 1, my - py + 1, fg_rgb);
            pixman_fill_rect(mx + t, py, 1, my - py + 1, fg_rgb);
        } else {
            pixman_fill_rect(mx - uw / 2, py, uw, my - py + (d ? 1 : 0), fg_rgb);
        }
    }
    if (d) {
        int32_t dw = d == 3 ? 1 : (d == 2 ? th : t);
        if (d == 3) {
            pixman_fill_rect(mx - t - 1, my, 1, py + h - my, fg_rgb);
            pixman_fill_rect(mx + t, my, 1, py + h - my, fg_rgb);
        } else {
            pixman_fill_rect(mx - dw / 2, my - (u ? 0 : dw / 2), dw, py + h - my + (u ? 0 : dw / 2), fg_rgb);
        }
    }
    return true;
}

static void pixman_cell_ext(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                            uint32_t codepoint, uint32_t combining, uint32_t fg_rgb, uint32_t bg_rgb,
                            uint32_t attr_flags, int32_t span_cols,
                            uint8_t ul_style, uint32_t ul_color) {
    if (!g_cur_pixels || cell_w <= 0 || cell_h <= 0) return;
    if (span_cols < 1) span_cols = 1;
    int32_t total_w = cell_w * span_cols;

    /* 1. Fill cell background */
    pixman_fill_rect(px, py, total_w, cell_h, bg_rgb);

    /* 2. Procedural rendering for Box-Drawing and Block Elements */
    if (render_procedural_glyph(px, py, total_w, cell_h, codepoint, fg_rgb, bg_rgb)) {
        /* handled procedurally with pixel precision */
    } else if (codepoint > 0x20U) {
        uint8_t style = (uint8_t)(attr_flags & 0x03U);
        bool shaped = false;

        /* Try HarfBuzz ligature/combining mark shaping */
        if (combining != 0) {
            uint32_t cps[2] = { codepoint, combining };
            const goosy_glyph_t *sh = goosy_font_shape_ligature(cps, 2, style);
            if (sh && sh->bitmap && sh->width > 0 && sh->height > 0) {
                int32_t gx = px + sh->bearing_x;
                int32_t gy = py + goosy_font_baseline() - sh->bearing_y;
                blend_glyph_a8(gx, gy, sh, fg_rgb);
                shaped = true;
            }
        }

        if (!shaped) {
            const goosy_glyph_t *g = goosy_font_get_glyph(codepoint, style);
            if (g && g->bitmap && g->width > 0 && g->height > 0) {
                int32_t gx = px + g->bearing_x;
                int32_t gy = py + goosy_font_baseline() - g->bearing_y;
                if (g->is_color) {
                    pixman_image_t *dst_img = get_cur_pixman_img();
                    if (dst_img) {
                        pixman_image_t *src = pixman_image_create_bits(
                            PIXMAN_a8r8g8b8, g->width, g->height,
                            (uint32_t *)g->bitmap, g->stride);
                        if (src) {
                            if (g->width > total_w || g->height > cell_h) {
                                pixman_f_transform_t ft;
                                pixman_f_transform_init_identity(&ft);
                                double sx = (double)g->width / (double)total_w;
                                double sy = (double)g->height / (double)cell_h;
                                double sc = (sx > sy) ? sx : sy;
                                pixman_f_transform_scale(&ft, NULL, sc, sc);
                                pixman_transform_t t;
                                pixman_transform_from_pixman_f_transform(&t, &ft);
                                pixman_image_set_transform(src, &t);
                                pixman_image_set_filter(src, PIXMAN_FILTER_BILINEAR, NULL, 0);
                                int32_t dw = (int32_t)(g->width / sc);
                                int32_t dh = (int32_t)(g->height / sc);
                                int32_t ox = px + (total_w - dw) / 2;
                                int32_t oy = py + (cell_h - dh) / 2;
                                pixman_image_composite32(PIXMAN_OP_OVER, src, NULL, dst_img,
                                                         0, 0, 0, 0, ox, oy, dw, dh);
                                pixman_damage(ox, oy, dw, dh);
                            } else {
                                int32_t ox = px + (total_w - g->width) / 2;
                                int32_t oy = py + (cell_h - g->height) / 2;
                                pixman_image_composite32(PIXMAN_OP_OVER, src, NULL, dst_img,
                                                         0, 0, 0, 0, ox, oy, (uint32_t)g->width, (uint32_t)g->height);
                                pixman_damage(ox, oy, g->width, g->height);
                            }
                            pixman_image_unref(src);
                        }
                    }
                } else {
                    blend_glyph_a8(gx, gy, g, fg_rgb);
                }
            }

            if (combining != 0) {
                const goosy_glyph_t *cg = goosy_font_get_glyph(combining, style);
                if (cg && cg->bitmap && cg->width > 0 && cg->height > 0) {
                    int32_t cx = px + (total_w - cg->width) / 2;
                    int32_t cy = py + goosy_font_baseline() - cg->bearing_y;
                    blend_glyph_a8(cx, cy, cg, fg_rgb);
                }
            }
        }
    }

    /* 3. Render Underline / Strikethrough */
    uint32_t line_color = (ul_color != 0) ? ul_color : fg_rgb;
    int32_t base_y = py + goosy_font_baseline();
    int32_t line_th = cell_h >= 28 ? 2 : 1;

    if (ul_style == 1 || (attr_flags & 0x04U)) {
        int32_t uy = base_y + 2;
        if (uy + line_th > py + cell_h) uy = py + cell_h - line_th;
        pixman_fill_rect(px, uy, total_w, line_th, line_color);
    } else if (ul_style == 2) {
        int32_t uy1 = base_y + 1;
        int32_t uy2 = base_y + 3;
        if (uy2 + line_th > py + cell_h) { uy2 = py + cell_h - line_th; uy1 = uy2 - 2; }
        pixman_fill_rect(px, uy1, total_w, line_th, line_color);
        pixman_fill_rect(px, uy2, total_w, line_th, line_color);
    } else if (ul_style == 3) {
        int32_t uy = base_y + 2;
        if (uy + 3 > py + cell_h) uy = py + cell_h - 3;
        for (int32_t cx = 0; cx < total_w; cx++) {
            int32_t wave = (cx % 4 < 2) ? 0 : 2;
            pixman_fill_rect(px + cx, uy + wave, 1, 1, line_color);
        }
    } else if (ul_style == 4) {
        int32_t uy = base_y + 2;
        if (uy + line_th > py + cell_h) uy = py + cell_h - line_th;
        for (int32_t cx = 0; cx < total_w; cx += 3) {
            pixman_fill_rect(px + cx, uy, 1, line_th, line_color);
        }
    } else if (ul_style == 5) {
        int32_t uy = base_y + 2;
        if (uy + line_th > py + cell_h) uy = py + cell_h - line_th;
        for (int32_t cx = 0; cx < total_w; cx += 6) {
            int32_t dw = (cx + 3 <= total_w) ? 3 : (total_w - cx);
            pixman_fill_rect(px + cx, uy, dw, line_th, line_color);
        }
    }

    if (attr_flags & 0x20U) {
        int32_t sy = py + cell_h / 2;
        pixman_fill_rect(px, sy, total_w, line_th, fg_rgb);
    }
}

static void pixman_cursor(int32_t cx_px, int32_t cy_px, int32_t cell_w, int32_t cell_h,
                          uint32_t color_rgb, int32_t style) {
    if (!g_cur_pixels || cell_w <= 0 || cell_h <= 0) return;
    int32_t line_th = (cell_h >= 28) ? 2 : 1;

    switch (style) {
        case 0:
            pixman_fill_rect(cx_px, cy_px, cell_w, cell_h, color_rgb);
            break;
        case 1: {
            int32_t uy = cy_px + cell_h - line_th * 2;
            pixman_fill_rect(cx_px, uy, cell_w, line_th * 2, color_rgb);
            break;
        }
        case 2:
            pixman_fill_rect(cx_px, cy_px, line_th * 2, cell_h, color_rgb);
            break;
        case 3:
            pixman_fill_rect(cx_px, cy_px, cell_w, line_th, color_rgb);
            pixman_fill_rect(cx_px, cy_px + cell_h - line_th, cell_w, line_th, color_rgb);
            pixman_fill_rect(cx_px, cy_px, line_th, cell_h, color_rgb);
            pixman_fill_rect(cx_px + cell_w - line_th, cy_px, line_th, cell_h, color_rgb);
            break;
        default:
            pixman_fill_rect(cx_px, cy_px, cell_w, cell_h, color_rgb);
            break;
    }
}

static pixman_image_t *g_title_icon = NULL;
static int32_t         g_title_icon_w = 0;
static int32_t         g_title_icon_h = 0;
static pixman_image_t *g_cached_scaled_icon = NULL;
static int32_t         g_cached_scaled_icon_size = 0;

static void load_title_icon_once(void) {
    if (g_title_icon) return;
    const char *icon_paths[] = {
        "/usr/share/icons/hicolor/48x48/apps/goosy.png",
        "/usr/share/pixmaps/goosy.png",
        "assets/goosy.png",
        "../assets/goosy.png",
        "assets/goot.png",
        NULL
    };
    for (int i = 0; icon_paths[i]; i++) {
        FILE *f = fopen(icon_paths[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 1024 * 1024) { fclose(f); continue; }
        uint8_t *data = (uint8_t *)malloc((size_t)sz);
        if (!data) { fclose(f); continue; }
        if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
            free(data);
            fclose(f);
            continue;
        }
        fclose(f);

        int32_t w = 0, h = 0;
        uint32_t *pixels = goosy_decode_png_to_argb(data, (size_t)sz, &w, &h);
        free(data);
        if (pixels && w > 0 && h > 0) {
            g_title_icon = pixman_image_create_bits(
                PIXMAN_a8r8g8b8, w, h, pixels, w * (int32_t)sizeof(uint32_t));
            if (g_title_icon) {
                g_title_icon_w = w;
                g_title_icon_h = h;
                break;
            } else {
                free(pixels);
            }
        }
    }
}

static int32_t pixman_title_icon(int32_t x, int32_t y, int32_t size_px) {
    if (!g_cur_pixels || size_px <= 0) return 0;
    load_title_icon_once();
    if (!g_title_icon || g_title_icon_w <= 0 || g_title_icon_h <= 0) return 0;

    pixman_image_t *dst_img = get_cur_pixman_img();
    if (!dst_img) return 0;

    if (!g_cached_scaled_icon || g_cached_scaled_icon_size != size_px) {
        if (g_cached_scaled_icon) {
            pixman_image_unref(g_cached_scaled_icon);
            g_cached_scaled_icon = NULL;
        }
        g_cached_scaled_icon = pixman_image_create_bits(
            PIXMAN_a8r8g8b8, size_px, size_px, NULL, size_px * (int32_t)sizeof(uint32_t));
        if (g_cached_scaled_icon) {
            pixman_f_transform_t ft;
            pixman_f_transform_init_identity(&ft);
            double sx = (double)g_title_icon_w / (double)size_px;
            double sy = (double)g_title_icon_h / (double)size_px;
            pixman_f_transform_scale(&ft, NULL, sx, sy);
            pixman_transform_t t;
            pixman_transform_from_pixman_f_transform(&t, &ft);
            pixman_image_set_transform(g_title_icon, &t);
            pixman_image_set_filter(g_title_icon, PIXMAN_FILTER_BILINEAR, NULL, 0);

            pixman_image_composite32(
                PIXMAN_OP_SRC, g_title_icon, NULL, g_cached_scaled_icon,
                0, 0, 0, 0, 0, 0, (uint32_t)size_px, (uint32_t)size_px);
            g_cached_scaled_icon_size = size_px;
        }
    }

    if (g_cached_scaled_icon) {
        pixman_image_composite32(
            PIXMAN_OP_OVER, g_cached_scaled_icon, NULL, dst_img,
            0, 0, 0, 0, x, y, (uint32_t)size_px, (uint32_t)size_px);
    } else {
        pixman_image_composite32(
            PIXMAN_OP_OVER, g_title_icon, NULL, dst_img,
            0, 0, 0, 0, x, y, (uint32_t)size_px, (uint32_t)size_px);
    }

    pixman_damage(x, y, size_px, size_px);
    return 1;
}

static void pixman_images(int32_t pad_px, int32_t title_h, int32_t view_top_abs_row,
                          int32_t rows, int32_t cols, int32_t cell_w, int32_t cell_h) {
    pixman_image_t *img = get_cur_pixman_img();
    if (!img) return;
    goosy_image_render(img, pad_px, title_h, view_top_abs_row, rows, cols, cell_w, cell_h);
}

static void pixman_end(bool request_frame_cb) {
    const char *cap_path = getenv("GOOSY_CAPTURE_PATH");
    if (cap_path && g_cur_buf && g_cur_buf->pixels) {
        FILE *f = fopen(cap_path, "wb");
        if (f) {
            fwrite(g_cur_buf->pixels, 1, (size_t)g_cur_buf->stride * (size_t)g_cur_buf->height, f);
            fclose(f);
            FILE *df = fopen("/tmp/goosy_cap_dims.txt", "w");
            if (df) {
                fprintf(df, "%d %d\n", (int)g_cur_buf->width, (int)g_cur_buf->height);
                fclose(df);
            }
        }
    }
    if (g_cur_img) {
        pixman_image_unref(g_cur_img);
        g_cur_img = NULL;
    }
    if (g_cur_buf) {
        if (g_has_dmg) {
            goosy_wl_commit_damage(
                g_cur_buf, g_dmg_x0, g_dmg_y0,
                g_dmg_x1 - g_dmg_x0, g_dmg_y1 - g_dmg_y0,
                request_frame_cb);
        }
        g_cur_buf = NULL;
    }
    g_cur_pixels = NULL;
    g_has_dmg = false;
}

static void pixman_cleanup(void) {
    if (g_cached_scaled_icon) {
        pixman_image_unref(g_cached_scaled_icon);
        g_cached_scaled_icon = NULL;
    }
    if (g_title_icon) {
        pixman_image_unref(g_title_icon);
        g_title_icon = NULL;
    }
}

static void pixman_resize(int32_t width_px, int32_t height_px, int32_t scale) {
    (void)width_px; (void)height_px; (void)scale;
}

goosy_renderer_backend_t g_backend_pixman = {
    .name       = "Pixman (Software)",
    .type       = GOOSY_RENDERER_PIXMAN,
    .init       = pixman_init,
    .begin      = pixman_begin,
    .fill_rect  = pixman_fill_rect,
    .cell_ext   = pixman_cell_ext,
    .cursor     = pixman_cursor,
    .title_icon = pixman_title_icon,
    .images     = pixman_images,
    .damage     = pixman_damage,
    .end        = pixman_end,
    .cleanup    = pixman_cleanup,
    .resize     = pixman_resize
};
