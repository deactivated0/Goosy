/*
 * Goosy — Terminal Graphics Bridge (Sixel & Kitty Graphics Protocols)
 * Software decoding and Pixman compositing for inline images.
 */

#define _POSIX_C_SOURCE 200809L

#include "goosy_image.h"

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ACTIVE_IMAGES 128
#define MAX_IMAGE_W       4096
#define MAX_IMAGE_H       4096
#define MAX_IMAGE_PIXELS  (16 * 1024 * 1024)

typedef struct {
    uint32_t        id;
    uint32_t        user_id;
    int32_t         col;
    int32_t         abs_row;
    int32_t         width_px;
    int32_t         height_px;
    uint32_t       *pixels; /* ARGB */
    pixman_image_t *pix_img;
} term_image_t;

static term_image_t g_images[MAX_ACTIVE_IMAGES];
static int32_t      g_image_count = 0;
static uint32_t     g_next_img_id = 1;

#define MAX_KITTY_CHUNK_PAYLOAD (32 * 1024 * 1024)

typedef struct {
    uint32_t image_id;
    int32_t  format;
    int32_t  width;
    int32_t  height;
    int32_t  action;
    int32_t  col;
    int32_t  abs_row;
    uint8_t *payload;
    size_t   payload_len;
    size_t   payload_cap;
    bool     active;
} kitty_chunk_ctx_t;

static kitty_chunk_ctx_t g_kitty_chunk = {0};

static void reset_kitty_chunk(void) {
    if (g_kitty_chunk.payload) {
        free(g_kitty_chunk.payload);
        g_kitty_chunk.payload = NULL;
    }
    memset(&g_kitty_chunk, 0, sizeof(g_kitty_chunk));
}

static void free_image_slot(int32_t idx) {
    if (idx < 0 || idx >= g_image_count) return;
    if (g_images[idx].pix_img) {
        pixman_image_unref(g_images[idx].pix_img);
        g_images[idx].pix_img = NULL;
    }
    if (g_images[idx].pixels) {
        free(g_images[idx].pixels);
        g_images[idx].pixels = NULL;
    }
}

void goosy_image_clear(void) {
    for (int32_t i = 0; i < g_image_count; i++) {
        free_image_slot(i);
    }
    g_image_count = 0;
    reset_kitty_chunk();
}

void goosy_image_close(void) {
    goosy_image_clear();
}

void goosy_image_scroll(int32_t evicted_lines) {
    if (evicted_lines <= 0 || g_image_count == 0) return;
    int32_t write_idx = 0;
    for (int32_t i = 0; i < g_image_count; i++) {
        g_images[i].abs_row -= evicted_lines;
        if (g_images[i].abs_row + 100 < 0) {
            free_image_slot(i);
        } else {
            if (write_idx != i) {
                g_images[write_idx] = g_images[i];
            }
            write_idx++;
        }
    }
    g_image_count = write_idx;
}

static int32_t add_image(uint32_t user_id, int32_t col, int32_t abs_row, int32_t w, int32_t h, uint32_t *pixels, int32_t cell_h) {
    if (!pixels || w <= 0 || h <= 0) return 0;

    int32_t slot = -1;
    if (user_id > 0) {
        for (int32_t i = 0; i < g_image_count; i++) {
            if (g_images[i].user_id == user_id) {
                free_image_slot(i);
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) {
        if (g_image_count >= MAX_ACTIVE_IMAGES) {
            free_image_slot(0);
            memmove(&g_images[0], &g_images[1], sizeof(term_image_t) * (MAX_ACTIVE_IMAGES - 1));
            g_image_count = MAX_ACTIVE_IMAGES - 1;
        }
        slot = g_image_count++;
    }

    pixman_image_t *img = pixman_image_create_bits(
        PIXMAN_a8r8g8b8, w, h, pixels, w * (int32_t)sizeof(uint32_t));
    if (!img) {
        free(pixels);
        return 0;
    }

    g_images[slot].id        = g_next_img_id++;
    g_images[slot].user_id   = user_id;
    g_images[slot].col       = col;
    g_images[slot].abs_row   = abs_row;
    g_images[slot].width_px  = w;
    g_images[slot].height_px = h;
    g_images[slot].pixels    = pixels;
    g_images[slot].pix_img   = img;

    int32_t ch = cell_h > 0 ? cell_h : 16;
    return (h + ch - 1) / ch;
}

void goosy_image_render(pixman_image_t *dest, int32_t pad_px, int32_t title_h,
                      int32_t view_top_abs_row, int32_t rows, int32_t cols,
                      int32_t cell_w, int32_t cell_h) {
    (void)cols;
    if (!dest || g_image_count == 0) return;
    int32_t view_bot_abs_row = view_top_abs_row + rows;

    for (int32_t i = 0; i < g_image_count; i++) {
        term_image_t *img = &g_images[i];
        if (!img->pix_img) continue;
        int32_t img_rows = (img->height_px + cell_h - 1) / cell_h;
        if (img->abs_row + img_rows < view_top_abs_row || img->abs_row > view_bot_abs_row) {
            continue;
        }

        int32_t dx = pad_px + img->col * cell_w;
        int32_t dy = title_h + pad_px + (img->abs_row - view_top_abs_row) * cell_h;
        pixman_image_composite32(
            PIXMAN_OP_OVER, img->pix_img, NULL, dest,
            0, 0, 0, 0, dx, dy, (uint32_t)img->width_px, (uint32_t)img->height_px);
    }
}

/* -------------------------------------------------------------------------
 * Sixel Graphics Decoder
 * ------------------------------------------------------------------------- */
int32_t goosy_sixel_decode(goosy_const_bytes_t data, int32_t col, int32_t abs_row, int32_t cell_w, int32_t cell_h) {
    (void)cell_w;
    if (!data.data || data.len < 2) return 0;

    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        pal[i] = 0xFF000000 | (uint32_t)(i * 0x010101);
    }
    /* Standard 16 ANSI colors for sixel defaults */
    static const uint32_t s_ansi16[16] = {
        0xFF000000, 0xFF000080, 0xFF800000, 0xFF800080,
        0xFF008000, 0xFF008080, 0xFF808000, 0xFFC0C0C0,
        0xFF808080, 0xFF0000FF, 0xFFFF0000, 0xFFFF00FF,
        0xFF00FF00, 0xFF00FFFF, 0xFFFFFF00, 0xFFFFFFFF
    };
    for (int i = 0; i < 16; i++) pal[i] = s_ansi16[i];

    int32_t cap_w = 512, cap_h = 512;
    uint32_t *pixels = (uint32_t *)calloc((size_t)cap_w * (size_t)cap_h, sizeof(uint32_t));
    if (!pixels) return 0;

    int32_t cur_x = 0;
    int32_t cur_y = 0;
    int32_t max_x = 0;
    int32_t max_y = 0;
    uint32_t cur_col = pal[15];

    int64_t i = 0;
    /* Skip DCS introducer and parameters up to 'q' */
    while (i < data.len && data.data[i] != 'q' && data.data[i] != 'Q') i++;
    if (i < data.len) i++; // skip 'q'

    while (i < data.len) {
        uint8_t c = data.data[i++];
        if (c == '\0' || c == '\n' || c == '\r') continue;
        if (c == 0x1B || c == 0x9C) break; // ST

        if (c == '#') {
            /* Color selector or introducer: #<idx> or #<idx>;2;r;g;b */
            int32_t p[5] = {0, 0, 0, 0, 0};
            int p_idx = 0;
            while (i < data.len && ((data.data[i] >= '0' && data.data[i] <= '9') || data.data[i] == ';')) {
                if (data.data[i] == ';') {
                    if (p_idx < 4) p_idx++;
                } else {
                    p[p_idx] = p[p_idx] * 10 + (data.data[i] - '0');
                }
                i++;
            }
            int idx = p[0] & 0xFF;
            if (p_idx >= 4 && p[1] == 2) {
                /* RGB 0..100 % */
                uint32_t r = (p[2] * 255) / 100;
                uint32_t g = (p[3] * 255) / 100;
                uint32_t b = (p[4] * 255) / 100;
                pal[idx] = 0xFF000000 | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
            }
            cur_col = pal[idx];
        } else if (c == '!') {
            /* Repeat: !<count><char> */
            int32_t count = 0;
            while (i < data.len && (data.data[i] >= '0' && data.data[i] <= '9')) {
                count = count * 10 + (data.data[i] - '0');
                if (count > MAX_IMAGE_W) count = MAX_IMAGE_W;
                i++;
            }
            if (i < data.len && count > 0) {
                uint8_t rep_ch = data.data[i++];
                if (rep_ch >= '?' && rep_ch <= '~') {
                    uint8_t bits = rep_ch - '?';
                    for (int r = 0; r < count; r++) {
                        if (cur_x >= cap_w) {
                            if (cap_w >= MAX_IMAGE_W) break;
                            int32_t n_w = cap_w * 2;
                            if (n_w > MAX_IMAGE_W) n_w = MAX_IMAGE_W;
                            uint32_t *np = (uint32_t *)calloc((size_t)n_w * (size_t)cap_h, sizeof(uint32_t));
                            if (!np) break;
                            for (int y = 0; y < cap_h; y++) {
                                memcpy(np + y * n_w, pixels + y * cap_w, (size_t)cap_w * sizeof(uint32_t));
                            }
                            free(pixels);
                            pixels = np;
                            cap_w = n_w;
                        }
                        for (int bit = 0; bit < 6; bit++) {
                            if (bits & (1 << bit)) {
                                int y = cur_y + bit;
                                if (y < cap_h) pixels[y * cap_w + cur_x] = cur_col;
                            }
                        }
                        cur_x++;
                    }
                    if (cur_x > max_x) max_x = cur_x;
                    if (cur_y + 6 > max_y) max_y = cur_y + 6;
                }
            }
        } else if (c == '$') {
            /* CR: return to left */
            cur_x = 0;
        } else if (c == '-') {
            /* LF: return to left and advance 6 pixels */
            cur_x = 0;
            cur_y += 6;
            if (cur_y + 6 > cap_h) {
                if (cap_h >= MAX_IMAGE_H) break;
                int32_t n_h = cap_h * 2;
                if (n_h > MAX_IMAGE_H) n_h = MAX_IMAGE_H;
                uint32_t *np = (uint32_t *)calloc((size_t)cap_w * (size_t)n_h, sizeof(uint32_t));
                if (np) {
                    memcpy(np, pixels, (size_t)cap_w * (size_t)cap_h * sizeof(uint32_t));
                    free(pixels);
                    pixels = np;
                    cap_h = n_h;
                }
            }
        } else if (c >= '?' && c <= '~') {
            /* 6-pixel slice */
            if (cur_x >= cap_w) {
                if (cap_w >= MAX_IMAGE_W) break;
                int32_t n_w = cap_w * 2;
                if (n_w > MAX_IMAGE_W) n_w = MAX_IMAGE_W;
                uint32_t *np = (uint32_t *)calloc((size_t)n_w * (size_t)cap_h, sizeof(uint32_t));
                if (np) {
                    for (int y = 0; y < cap_h; y++) {
                        memcpy(np + y * n_w, pixels + y * cap_w, (size_t)cap_w * sizeof(uint32_t));
                    }
                    free(pixels);
                    pixels = np;
                    cap_w = n_w;
                }
            }
            uint8_t bits = c - '?';
            for (int bit = 0; bit < 6; bit++) {
                if (bits & (1 << bit)) {
                    int y = cur_y + bit;
                    if (y < cap_h) pixels[y * cap_w + cur_x] = cur_col;
                }
            }
            cur_x++;
            if (cur_x > max_x) max_x = cur_x;
            if (cur_y + 6 > max_y) max_y = cur_y + 6;
        }
    }

    if (max_x <= 0 || max_y <= 0 || (uint64_t)max_x * (uint64_t)max_y > MAX_IMAGE_PIXELS) {
        free(pixels);
        return 0;
    }

    /* Compact to exact tight image */
    uint32_t *tight = (uint32_t *)malloc((size_t)max_x * (size_t)max_y * sizeof(uint32_t));
    if (!tight) {
        free(pixels);
        return 0;
    }
    for (int y = 0; y < max_y; y++) {
        memcpy(tight + y * max_x, pixels + y * cap_w, (size_t)max_x * sizeof(uint32_t));
    }
    free(pixels);
    pixels = tight;

    return add_image(0, col, abs_row, max_x, max_y, pixels, cell_h);
}

/* -------------------------------------------------------------------------
 * Kitty Graphics Protocol Decoder (Raw RGB, RGBA, and PNG via libpng)
 * ------------------------------------------------------------------------- */
static const uint8_t b64_table[256] = {
    ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
    ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
    ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
    ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
    ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
    ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
    ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
    ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63
};

static uint8_t *base64_decode(const uint8_t *src, int64_t len, int64_t *out_len) {
    if (!src || len <= 0) return NULL;
    size_t cap = (size_t)(len * 3 / 4 + 4);
    uint8_t *out = (uint8_t *)malloc(cap);
    if (!out) return NULL;

    size_t o = 0;
    uint32_t buf = 0;
    int bits = 0;
    for (int64_t i = 0; i < len; i++) {
        uint8_t ch = src[i];
        if (ch == '=') break;
        if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
        buf = (buf << 6) | b64_table[ch];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (uint8_t)((buf >> bits) & 0xFF);
        }
    }
    *out_len = (int64_t)o;
    return out;
}

struct png_mem_stream {
    const uint8_t *data;
    size_t len;
    size_t off;
};

static void png_mem_reader(png_structp png_ptr, png_bytep out_bytes, png_size_t count) {
    struct png_mem_stream *s = (struct png_mem_stream *)png_get_io_ptr(png_ptr);
    if (!s || s->off + count > s->len) {
        png_error(png_ptr, "PNG read error from memory");
        return;
    }
    memcpy(out_bytes, s->data + s->off, count);
    s->off += count;
}

uint32_t *goosy_decode_png_to_argb(const uint8_t *data, size_t len, int32_t *out_w, int32_t *out_h) {
    if (!data || len < 8 || png_sig_cmp((png_const_bytep)data, 0, 8) != 0) return NULL;

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) return NULL;
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) { png_destroy_read_struct(&png_ptr, NULL, NULL); return NULL; }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    struct png_mem_stream stream = { data, len, 0 };
    png_set_read_fn(png_ptr, &stream, png_mem_reader);
    png_read_info(png_ptr, info_ptr);

    png_uint_32 width = 0, height = 0;
    int bit_depth = 0, color_type = 0;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

    if (width == 0 || height == 0 || width > MAX_IMAGE_W || height > MAX_IMAGE_H ||
        (uint64_t)width * (uint64_t)height > MAX_IMAGE_PIXELS) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    if (bit_depth == 16) png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY) png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    uint32_t *pixels = (uint32_t *)malloc((size_t)width * (size_t)height * sizeof(uint32_t));
    if (!pixels) { png_destroy_read_struct(&png_ptr, &info_ptr, NULL); return NULL; }

    png_bytep *row_pointers = (png_bytep *)malloc(height * sizeof(png_bytep));
    if (!row_pointers) {
        free(pixels);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    uint8_t *raw_rows = (uint8_t *)malloc(rowbytes * height);
    if (!raw_rows) {
        free(row_pointers);
        free(pixels);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    for (png_uint_32 y = 0; y < height; y++) {
        row_pointers[y] = raw_rows + y * rowbytes;
    }
    png_read_image(png_ptr, row_pointers);

    /* Convert RGBA to ARGB */
    for (png_uint_32 y = 0; y < height; y++) {
        const uint8_t *src_row = row_pointers[y];
        uint32_t *dst_row = pixels + y * width;
        for (png_uint_32 x = 0; x < width; x++) {
            uint8_t r = src_row[x * 4 + 0];
            uint8_t g = src_row[x * 4 + 1];
            uint8_t b = src_row[x * 4 + 2];
            uint8_t a = src_row[x * 4 + 3];
            dst_row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    free(raw_rows);
    free(row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    *out_w = (int32_t)width;
    *out_h = (int32_t)height;
    return pixels;
}

int32_t goosy_kitty_graphics(goosy_const_bytes_t data, int32_t col, int32_t abs_row, int32_t cell_w, int32_t cell_h) {
    (void)cell_w;
    if (!data.data || data.len < 2) return 0;

    int32_t format = 32; /* 32 = RGBA, 24 = RGB, 100 = PNG */
    int32_t width = 0, height = 0;
    int32_t action = 't';
    int32_t image_id = 0;
    int32_t more_chunks = 0;
    int32_t del_target = 0;

    /* Parse control keys until ';' or end of sequence */
    int64_t semi = -1;
    for (int64_t i = 0; i < data.len; i++) {
        if (data.data[i] == ';') { semi = i; break; }
    }
    if (semi < 0) {
        semi = data.len;
    }

    int64_t p = 0;
    while (p < semi) {
        uint8_t key = data.data[p++];
        if (p < semi && data.data[p] == '=') p++;
        int32_t val = 0;
        int32_t char_val = 0;
        if (p < semi && data.data[p] >= 'a' && data.data[p] <= 'z') {
            char_val = data.data[p++];
        } else {
            while (p < semi && data.data[p] >= '0' && data.data[p] <= '9') {
                val = val * 10 + (data.data[p++] - '0');
            }
        }
        if (key == 'a') action = char_val != 0 ? char_val : val;
        else if (key == 'i') image_id = val;
        else if (key == 'f') format = val;
        else if (key == 's') width = val;
        else if (key == 'v') height = val;
        else if (key == 'm') more_chunks = val;
        else if (key == 'd') del_target = char_val != 0 ? char_val : val;
        while (p < semi && data.data[p] == ',') p++;
    }

    if (action == 'q') {
        char rep[64];
        int rlen = snprintf(rep, sizeof(rep), "\x1b_Gi=%d;OK\x1b\\", (int)image_id);
        if (rlen > 0) {
            goosy_const_bytes_t resp = { (const uint8_t *)rep, rlen };
            goosy_pty_write(resp);
        }
        return 0;
    }

    if (action == 'd') {
        if (del_target == 'i' || (image_id > 0 && del_target != 'a')) {
            int32_t write_idx = 0;
            for (int32_t i = 0; i < g_image_count; i++) {
                if (g_images[i].user_id == (uint32_t)image_id || g_images[i].id == (uint32_t)image_id) {
                    free_image_slot(i);
                } else {
                    if (write_idx != i) {
                        g_images[write_idx] = g_images[i];
                    }
                    write_idx++;
                }
            }
            g_image_count = write_idx;
        } else {
            goosy_image_clear();
        }
        return 0;
    }

    int64_t chunk_payload_len = (semi < data.len) ? (data.len - (semi + 1)) : 0;
    const uint8_t *chunk_payload = (semi < data.len) ? (data.data + (semi + 1)) : NULL;

    /* Handle multi-packet chunking: m=1 appends chunk to buffer */
    if (more_chunks == 1) {
        if (!g_kitty_chunk.active) {
            reset_kitty_chunk();
            g_kitty_chunk.active = true;
            g_kitty_chunk.image_id = (uint32_t)image_id;
            g_kitty_chunk.format = format;
            g_kitty_chunk.width = width;
            g_kitty_chunk.height = height;
            g_kitty_chunk.action = action;
            g_kitty_chunk.col = col;
            g_kitty_chunk.abs_row = abs_row;
        }
        if (chunk_payload && chunk_payload_len > 0) {
            size_t new_len = g_kitty_chunk.payload_len + (size_t)chunk_payload_len;
            if (new_len <= MAX_KITTY_CHUNK_PAYLOAD) {
                if (new_len > g_kitty_chunk.payload_cap) {
                    size_t new_cap = g_kitty_chunk.payload_cap == 0 ? 65536 : g_kitty_chunk.payload_cap * 2;
                    while (new_cap < new_len) new_cap *= 2;
                    uint8_t *new_p = (uint8_t *)realloc(g_kitty_chunk.payload, new_cap);
                    if (new_p) {
                        g_kitty_chunk.payload = new_p;
                        g_kitty_chunk.payload_cap = new_cap;
                    }
                }
                if (g_kitty_chunk.payload && g_kitty_chunk.payload_cap >= new_len) {
                    memcpy(g_kitty_chunk.payload + g_kitty_chunk.payload_len, chunk_payload, (size_t)chunk_payload_len);
                    g_kitty_chunk.payload_len = new_len;
                }
            }
        }
        return 0; /* Await next chunk */
    }

    /* more_chunks == 0: either final chunk of a sequence or single-packet transfer */
    const uint8_t *final_payload = chunk_payload;
    int64_t final_payload_len = chunk_payload_len;

    if (g_kitty_chunk.active) {
        if (chunk_payload && chunk_payload_len > 0) {
            size_t new_len = g_kitty_chunk.payload_len + (size_t)chunk_payload_len;
            if (new_len <= MAX_KITTY_CHUNK_PAYLOAD) {
                if (new_len > g_kitty_chunk.payload_cap) {
                    uint8_t *new_p = (uint8_t *)realloc(g_kitty_chunk.payload, new_len + 1);
                    if (new_p) {
                        g_kitty_chunk.payload = new_p;
                        g_kitty_chunk.payload_cap = new_len + 1;
                    }
                }
                if (g_kitty_chunk.payload && g_kitty_chunk.payload_cap >= new_len) {
                    memcpy(g_kitty_chunk.payload + g_kitty_chunk.payload_len, chunk_payload, (size_t)chunk_payload_len);
                    g_kitty_chunk.payload_len = new_len;
                }
            }
        }
        final_payload = g_kitty_chunk.payload;
        final_payload_len = (int64_t)g_kitty_chunk.payload_len;
        if (format == 32 && g_kitty_chunk.format != 32) format = g_kitty_chunk.format;
        if (width == 0) width = g_kitty_chunk.width;
        if (height == 0) height = g_kitty_chunk.height;
        if (image_id == 0) image_id = (int32_t)g_kitty_chunk.image_id;
        col = g_kitty_chunk.col;
        abs_row = g_kitty_chunk.abs_row;
    }

    if (!final_payload || final_payload_len <= 0) {
        reset_kitty_chunk();
        return 0;
    }

    if (width > MAX_IMAGE_W || height > MAX_IMAGE_H ||
        (width > 0 && height > 0 && (uint64_t)width * (uint64_t)height > MAX_IMAGE_PIXELS)) {
        reset_kitty_chunk();
        return 0;
    }

    int64_t dec_len = 0;
    uint8_t *decoded = base64_decode(final_payload, final_payload_len, &dec_len);
    reset_kitty_chunk();
    if (!decoded || dec_len <= 0) return 0;

    uint32_t *pixels = NULL;
    int32_t w = width, h = height;

    if (format == 100) {
        /* PNG format */
        pixels = goosy_decode_png_to_argb(decoded, (size_t)dec_len, &w, &h);
    } else if (format == 32 && w > 0 && h > 0) {
        /* Raw RGBA */
        pixels = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
        if (pixels) {
            for (int32_t i = 0; i < w * h && (i * 4 + 3) < dec_len; i++) {
                uint8_t r = decoded[i * 4 + 0];
                uint8_t g = decoded[i * 4 + 1];
                uint8_t b = decoded[i * 4 + 2];
                uint8_t a = decoded[i * 4 + 3];
                pixels[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    } else if (format == 24 && w > 0 && h > 0) {
        /* Raw RGB */
        pixels = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
        if (pixels) {
            for (int32_t i = 0; i < w * h && (i * 3 + 2) < dec_len; i++) {
                uint8_t r = decoded[i * 3 + 0];
                uint8_t g = decoded[i * 3 + 1];
                uint8_t b = decoded[i * 3 + 2];
                pixels[i] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    }

    free(decoded);
    if (!pixels || w <= 0 || h <= 0) return 0;

    return add_image((uint32_t)image_id, col, abs_row, w, h, pixels, cell_h);
}
