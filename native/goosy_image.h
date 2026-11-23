#ifndef GOOSY_IMAGE_H
#define GOOSY_IMAGE_H

#include "goosy_native.h"
#include <pixman.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Terminal Image API */
uint32_t *goosy_decode_png_to_argb(const uint8_t *data, size_t len, int32_t *out_w, int32_t *out_h);
int32_t   goosy_sixel_decode(goosy_const_bytes_t data, int32_t col, int32_t abs_row, int32_t cell_w, int32_t cell_h);
int32_t goosy_kitty_graphics(goosy_const_bytes_t data, int32_t col, int32_t abs_row, int32_t cell_w, int32_t cell_h);
void    goosy_image_render(pixman_image_t *dest, int32_t pad_px, int32_t title_h,
                          int32_t view_top_abs_row, int32_t rows, int32_t cols,
                          int32_t cell_w, int32_t cell_h);
void    goosy_image_scroll(int32_t evicted_lines);
void    goosy_image_clear(void);
void    goosy_image_close(void);

#ifdef __cplusplus
}
#endif

#endif /* GOOSY_IMAGE_H */
