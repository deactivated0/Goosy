#ifndef GOOT_IMAGE_H
#define GOOT_IMAGE_H

#include "goot_native.h"
#include <pixman.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Terminal Image API */
uint32_t *goot_decode_png_to_argb(const uint8_t *data, size_t len, int32_t *out_w, int32_t *out_h);
int32_t   goot_sixel_decode(goot_const_bytes_t data, int32_t col, int32_t abs_row, int32_t cell_w, int32_t cell_h);
int32_t goot_kitty_graphics(goot_const_bytes_t data, int32_t col, int32_t abs_row, int32_t cell_w, int32_t cell_h);
void    goot_image_render(pixman_image_t *dest, int32_t pad_px, int32_t title_h,
                          int32_t view_top_abs_row, int32_t rows, int32_t cols,
                          int32_t cell_w, int32_t cell_h);
void    goot_image_scroll(int32_t evicted_lines);
void    goot_image_clear(void);
void    goot_image_close(void);

#ifdef __cplusplus
}
#endif

#endif /* GOOT_IMAGE_H */
