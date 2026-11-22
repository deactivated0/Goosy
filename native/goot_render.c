/*
 * Goot — Pixman Software Rendering Bridge
 * Composites dirty terminal cells and cursor onto the active wl_shm buffer
 * with accumulated damage rectangle tracking and zero per-frame allocations.
 */

#define _POSIX_C_SOURCE 200809L

#include "goot_native.h"
#include "goot_image.h"

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

static bool render_procedural_glyph(int32_t px, int32_t py, int32_t w, int32_t h,
                                    uint32_t cp, uint32_t fg_rgb, uint32_t bg_rgb) {
    if (cp < 0x2500 || cp > 0x259F) return false;

    int32_t t = h >= 28 ? 2 : 1;
    int32_t th = t * 2;
    int32_t mx = px + w / 2;
    int32_t my = py + h / 2;

    /* Block Elements U+2580..U+259F */
    if (cp >= 0x2580 && cp <= 0x259F) {
        switch (cp) {
            case 0x2580: goot_render_fill_rect(px, py, w, h / 2, fg_rgb); return true;
            case 0x2581: goot_render_fill_rect(px, py + h - (h / 8 > 0 ? h / 8 : 1), w, h / 8 > 0 ? h / 8 : 1, fg_rgb); return true;
            case 0x2582: goot_render_fill_rect(px, py + h - h / 4, w, h / 4, fg_rgb); return true;
            case 0x2583: goot_render_fill_rect(px, py + h - (h * 3) / 8, w, (h * 3) / 8, fg_rgb); return true;
            case 0x2584: goot_render_fill_rect(px, py + h / 2, w, h - h / 2, fg_rgb); return true;
            case 0x2585: goot_render_fill_rect(px, py + h - (h * 5) / 8, w, (h * 5) / 8, fg_rgb); return true;
            case 0x2586: goot_render_fill_rect(px, py + h - (h * 3) / 4, w, (h * 3) / 4, fg_rgb); return true;
            case 0x2587: goot_render_fill_rect(px, py + h - (h * 7) / 8, w, (h * 7) / 8, fg_rgb); return true;
            case 0x2588: goot_render_fill_rect(px, py, w, h, fg_rgb); return true;
            case 0x2589: goot_render_fill_rect(px, py, (w * 7) / 8, h, fg_rgb); return true;
            case 0x258A: goot_render_fill_rect(px, py, (w * 3) / 4, h, fg_rgb); return true;
            case 0x258B: goot_render_fill_rect(px, py, (w * 5) / 8, h, fg_rgb); return true;
            case 0x258C: goot_render_fill_rect(px, py, w / 2, h, fg_rgb); return true;
            case 0x258D: goot_render_fill_rect(px, py, (w * 3) / 8, h, fg_rgb); return true;
            case 0x258E: goot_render_fill_rect(px, py, w / 4, h, fg_rgb); return true;
            case 0x258F: goot_render_fill_rect(px, py, w / 8 > 0 ? w / 8 : 1, h, fg_rgb); return true;
            case 0x2590: goot_render_fill_rect(px + w / 2, py, w - w / 2, h, fg_rgb); return true;
            case 0x2591: {
                uint32_t r = (((fg_rgb >> 16) & 0xFF) + 3 * ((bg_rgb >> 16) & 0xFF)) / 4;
                uint32_t g = (((fg_rgb >> 8)  & 0xFF) + 3 * ((bg_rgb >> 8)  & 0xFF)) / 4;
                uint32_t b = ((fg_rgb & 0xFF)        + 3 * (bg_rgb & 0xFF))        / 4;
                goot_render_fill_rect(px, py, w, h, (r << 16) | (g << 8) | b);
                return true;
            }
            case 0x2592: {
                uint32_t r = (((fg_rgb >> 16) & 0xFF) + ((bg_rgb >> 16) & 0xFF)) / 2;
                uint32_t g = (((fg_rgb >> 8)  & 0xFF) + ((bg_rgb >> 8)  & 0xFF)) / 2;
                uint32_t b = ((fg_rgb & 0xFF)        + (bg_rgb & 0xFF))        / 2;
                goot_render_fill_rect(px, py, w, h, (r << 16) | (g << 8) | b);
                return true;
            }
            case 0x2593: {
                uint32_t r = (3 * ((fg_rgb >> 16) & 0xFF) + ((bg_rgb >> 16) & 0xFF)) / 4;
                uint32_t g = (3 * ((fg_rgb >> 8)  & 0xFF) + ((bg_rgb >> 8)  & 0xFF)) / 4;
                uint32_t b = (3 * (fg_rgb & 0xFF)        + (bg_rgb & 0xFF))        / 4;
                goot_render_fill_rect(px, py, w, h, (r << 16) | (g << 8) | b);
                return true;
            }
            case 0x2594: goot_render_fill_rect(px, py, w, h / 8 > 0 ? h / 8 : 1, fg_rgb); return true;
            case 0x2595: goot_render_fill_rect(px + w - (w / 8 > 0 ? w / 8 : 1), py, w / 8 > 0 ? w / 8 : 1, h, fg_rgb); return true;
            case 0x2596: goot_render_fill_rect(px, py + h / 2, w / 2, h - h / 2, fg_rgb); return true;
            case 0x2597: goot_render_fill_rect(px + w / 2, py + h / 2, w - w / 2, h - h / 2, fg_rgb); return true;
            case 0x2598: goot_render_fill_rect(px, py, w / 2, h / 2, fg_rgb); return true;
            case 0x259A:
                goot_render_fill_rect(px, py, w / 2, h / 2, fg_rgb);
                goot_render_fill_rect(px + w / 2, py + h / 2, w - w / 2, h - h / 2, fg_rgb);
                return true;
            case 0x259D: goot_render_fill_rect(px + w / 2, py, w - w / 2, h / 2, fg_rgb); return true;
            default: break;
        }
    }

    /* Box Drawing U+2500..U+257F */
    int l = 0, r = 0, u = 0, d = 0;
    switch (cp) {
        case 0x2500: case 0x2504: case 0x2508: l = 1; r = 1; break;
        case 0x2501: case 0x2505: case 0x2509: l = 2; r = 2; break;
        case 0x2502: case 0x2506: case 0x250A: u = 1; d = 1; break;
        case 0x2503: case 0x2507: case 0x250B: u = 2; d = 2; break;
        case 0x250C: case 0x256D: d = 1; r = 1; break;
        case 0x250D: d = 1; r = 2; break;
        case 0x250E: d = 2; r = 1; break;
        case 0x250F: d = 2; r = 2; break;
        case 0x2510: case 0x256E: d = 1; l = 1; break;
        case 0x2511: d = 1; l = 2; break;
        case 0x2512: d = 2; l = 1; break;
        case 0x2513: d = 2; l = 2; break;
        case 0x2514: case 0x2570: u = 1; r = 1; break;
        case 0x2515: u = 1; r = 2; break;
        case 0x2516: u = 2; r = 1; break;
        case 0x2517: u = 2; r = 2; break;
        case 0x2518: case 0x256F: u = 1; l = 1; break;
        case 0x2519: u = 1; l = 2; break;
        case 0x251A: u = 2; l = 1; break;
        case 0x251B: u = 2; l = 2; break;
        case 0x251C: u = 1; d = 1; r = 1; break;
        case 0x251D: u = 1; d = 1; r = 2; break;
        case 0x251E: u = 2; d = 1; r = 1; break;
        case 0x251F: u = 1; d = 2; r = 1; break;
        case 0x2520: u = 2; d = 2; r = 1; break;
        case 0x2521: u = 1; d = 2; r = 2; break;
        case 0x2522: u = 2; d = 1; r = 2; break;
        case 0x2523: u = 2; d = 2; r = 2; break;
        case 0x2524: u = 1; d = 1; l = 1; break;
        case 0x2525: u = 1; d = 1; l = 2; break;
        case 0x2526: u = 2; d = 1; l = 1; break;
        case 0x2527: u = 1; d = 2; l = 1; break;
        case 0x2528: u = 2; d = 2; l = 1; break;
        case 0x2529: u = 1; d = 2; l = 2; break;
        case 0x252A: u = 2; d = 1; l = 2; break;
        case 0x252B: u = 2; d = 2; l = 2; break;
        case 0x252C: l = 1; r = 1; d = 1; break;
        case 0x252D: l = 2; r = 1; d = 1; break;
        case 0x252E: l = 1; r = 2; d = 1; break;
        case 0x252F: l = 2; r = 2; d = 1; break;
        case 0x2530: l = 1; r = 1; d = 2; break;
        case 0x2531: l = 2; r = 1; d = 2; break;
        case 0x2532: l = 1; r = 2; d = 2; break;
        case 0x2533: l = 2; r = 2; d = 2; break;
        case 0x2534: l = 1; r = 1; u = 1; break;
        case 0x2535: l = 2; r = 1; u = 1; break;
        case 0x2536: l = 1; r = 2; u = 1; break;
        case 0x2537: l = 2; r = 2; u = 1; break;
        case 0x2538: l = 1; r = 1; u = 2; break;
        case 0x2539: l = 2; r = 1; u = 2; break;
        case 0x253A: l = 1; r = 2; u = 2; break;
        case 0x253B: l = 2; r = 2; u = 2; break;
        case 0x253C: l = 1; r = 1; u = 1; d = 1; break;
        case 0x253D: l = 2; r = 1; u = 1; d = 1; break;
        case 0x253E: l = 1; r = 2; u = 1; d = 1; break;
        case 0x253F: l = 2; r = 2; u = 1; d = 1; break;
        case 0x2540: l = 1; r = 1; u = 2; d = 1; break;
        case 0x2541: l = 1; r = 1; u = 1; d = 2; break;
        case 0x2542: l = 1; r = 1; u = 2; d = 2; break;
        case 0x254B: l = 2; r = 2; u = 2; d = 2; break;
        case 0x2550: l = 3; r = 3; break;
        case 0x2551: u = 3; d = 3; break;
        case 0x2554: d = 3; r = 3; break;
        case 0x2557: d = 3; l = 3; break;
        case 0x255A: u = 3; r = 3; break;
        case 0x255D: u = 3; l = 3; break;
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
            goot_render_fill_rect(px, my - t - 1, mx - px + 1, 1, fg_rgb);
            goot_render_fill_rect(px, my + t, mx - px + 1, 1, fg_rgb);
        } else {
            goot_render_fill_rect(px, my - lw / 2, mx - px + (r ? 1 : 0), lw, fg_rgb);
        }
    }
    if (r) {
        int32_t rw = r == 3 ? 1 : (r == 2 ? th : t);
        if (r == 3) {
            goot_render_fill_rect(mx, my - t - 1, px + w - mx, 1, fg_rgb);
            goot_render_fill_rect(mx, my + t, px + w - mx, 1, fg_rgb);
        } else {
            goot_render_fill_rect(mx - (l ? 0 : rw / 2), my - rw / 2, px + w - mx + (l ? 0 : rw / 2), rw, fg_rgb);
        }
    }
    if (u) {
        int32_t uw = u == 3 ? 1 : (u == 2 ? th : t);
        if (u == 3) {
            goot_render_fill_rect(mx - t - 1, py, 1, my - py + 1, fg_rgb);
            goot_render_fill_rect(mx + t, py, 1, my - py + 1, fg_rgb);
        } else {
            goot_render_fill_rect(mx - uw / 2, py, uw, my - py + (d ? 1 : 0), fg_rgb);
        }
    }
    if (d) {
        int32_t dw = d == 3 ? 1 : (d == 2 ? th : t);
        if (d == 3) {
            goot_render_fill_rect(mx - t - 1, my, 1, py + h - my, fg_rgb);
            goot_render_fill_rect(mx + t, my, 1, py + h - my, fg_rgb);
        } else {
            goot_render_fill_rect(mx - dw / 2, my - (u ? 0 : dw / 2), dw, py + h - my + (u ? 0 : dw / 2), fg_rgb);
        }
    }
    return true;
}

void goot_render_cell_ext(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                          uint32_t codepoint, uint32_t fg_rgb, uint32_t bg_rgb,
                          uint32_t attr_flags, int32_t span_cols,
                          uint8_t ul_style, uint32_t ul_color) {
    if (!g_cur_img || cell_w <= 0 || cell_h <= 0) return;
    if (span_cols < 1) span_cols = 1;
    int32_t total_w = cell_w * span_cols;

    /* 1. Fill cell background */
    goot_render_fill_rect(px, py, total_w, cell_h, bg_rgb);

    /* 2. Procedural rendering for Box-Drawing and Block Elements */
    if (render_procedural_glyph(px, py, total_w, cell_h, codepoint, fg_rgb, bg_rgb)) {
        /* handled procedurally with pixel precision */
    } else if (codepoint > 0x20U) {
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
                        pixman_image_composite32(PIXMAN_OP_OVER, src, NULL, g_cur_img,
                                                 0, 0, 0, 0, ox, oy, dw, dh);
                    } else {
                        int32_t ox = px + (total_w - g->width) / 2;
                        int32_t oy = py + (cell_h - g->height) / 2;
                        pixman_image_composite32(PIXMAN_OP_OVER, src, NULL, g_cur_img,
                                                 0, 0, 0, 0, ox, oy, (uint32_t)g->width, (uint32_t)g->height);
                    }
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

    /* 3. Underline (bit 2 or ul_style > 0) */
    if ((attr_flags & (1U << 2)) || ul_style > 0) {
        uint32_t ucol = (ul_color != 0) ? ul_color : fg_rgb;
        int32_t uy = py + goot_font_baseline() + 2;
        if (uy >= py + cell_h) uy = py + cell_h - 1;
        if (ul_style == 2) {
            /* Double underline */
            goot_render_fill_rect(px, uy - 1, total_w, 1, ucol);
            goot_render_fill_rect(px, uy + 1, total_w, 1, ucol);
        } else if (ul_style == 3) {
            /* Curly underline (undercurl) */
            for (int32_t x = px; x < px + total_w; x++) {
                int phase = (x - px) % 4;
                int dy = (phase == 0 || phase == 1) ? 0 : 2;
                goot_render_fill_rect(x, uy + dy, 1, 1, ucol);
            }
        } else if (ul_style == 4) {
            /* Dotted underline */
            for (int32_t x = px; x < px + total_w; x += 3) {
                goot_render_fill_rect(x, uy, 1, 1, ucol);
            }
        } else if (ul_style == 5) {
            /* Dashed underline */
            for (int32_t x = px; x < px + total_w; x += 5) {
                int32_t dw = (x + 3 <= px + total_w) ? 3 : (px + total_w - x);
                goot_render_fill_rect(x, uy, dw, 1, ucol);
            }
        } else {
            /* Single straight underline */
            goot_render_fill_rect(px, uy, total_w, 1, ucol);
        }
    }

    /* 4. Strikethrough (bit 5) */
    if (attr_flags & (1U << 5)) {
        int32_t sy = py + cell_h / 2;
        goot_render_fill_rect(px, sy, total_w, 1, fg_rgb);
    }
}

void goot_render_cell(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                      uint32_t codepoint, uint32_t fg_rgb, uint32_t bg_rgb,
                      uint32_t attr_flags, int32_t span_cols) {
    uint8_t ul_style = (attr_flags & (1U << 2)) ? 1 : 0;
    goot_render_cell_ext(px, py, cell_w, cell_h, codepoint, fg_rgb, bg_rgb,
                         attr_flags, span_cols, ul_style, 0);
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

void goot_render_images(int32_t pad_px, int32_t title_h, int32_t view_top_abs_row,
                        int32_t rows, int32_t cols, int32_t cell_w, int32_t cell_h) {
    if (!g_cur_img) return;
    goot_image_render(g_cur_img, pad_px, title_h, view_top_abs_row, rows, cols, cell_w, cell_h);
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
