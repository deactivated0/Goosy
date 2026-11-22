#define _POSIX_C_SOURCE 200809L
#include "goot_native.h"
#include <pixman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

// We mock goot_wl_acquire_buffer and goot_wl_commit_damage for headless rendering benchmark
static struct goot_shm_buffer g_mock_buf;
struct goot_shm_buffer *goot_wl_acquire_buffer(void) {
    return &g_mock_buf;
}
void goot_wl_commit_damage(struct goot_shm_buffer *buf,
                           int32_t x, int32_t y, int32_t w, int32_t h,
                           bool request_frame_cb) {
    (void)buf; (void)x; (void)y; (void)w; (void)h; (void)request_frame_cb;
}

int main(void) {
    goot_const_bytes_t fam = { (const uint8_t *)"monospace", 9 };
    if (goot_font_init(fam, 11, 96, 0) != 0) {
        printf("Failed font init\n");
        return 1;
    }

    int32_t cw = goot_font_cell_width();
    int32_t ch = goot_font_cell_height();
    int32_t cols = 120;
    int32_t rows = 40;
    int32_t win_w = cols * cw;
    int32_t win_h = rows * ch;

    g_mock_buf.width = win_w;
    g_mock_buf.height = win_h;
    g_mock_buf.stride = win_w * 4;
    g_mock_buf.size = (size_t)g_mock_buf.stride * (size_t)win_h;
    g_mock_buf.pixels = (uint32_t *)calloc(1, g_mock_buf.size);
    g_mock_buf.wl_buffer = (void *)0x1234;
    g_mock_buf.busy = false;

    // Benchmark 1: 1,000,000 font glyph lookups
    int64_t t0 = now_ns();
    for (int i = 0; i < 1000000; i++) {
        uint32_t cp = 0x20 + (i % 95);
        const goot_glyph_t *g = goot_font_get_glyph(cp, i & 3);
        (void)g;
    }
    int64_t t1 = now_ns();
    int64_t font_lookup_ms = (t1 - t0) / 1000000;

    // Benchmark 2: Rendering 100 full frames of 120x40 cells (480,000 cells rendered)
    int64_t t2 = now_ns();
    for (int frame = 0; frame < 100; frame++) {
        goot_render_begin(0x000000);
        for (int r = 0; r < rows; r++) {
            int py = r * ch;
            for (int c = 0; c < cols; c++) {
                int px = c * cw;
                uint32_t cp = 0x20 + ((r + c + frame) % 95);
                uint32_t fg = 0xFFFFFF;
                uint32_t bg = 0x101010;
                goot_render_cell_ext(px, py, cw, ch, cp, fg, bg, 0, 1, 0, 0);
            }
        }
        goot_render_end(false);
    }
    int64_t t3 = now_ns();
    int64_t render_100_frames_ms = (t3 - t2) / 1000000;

    printf("BASELINE_RENDER_RESULTS:\n");
    printf("1M glyph lookups:      %4ld ms (%ld lookups/s)\n", (long)font_lookup_ms, (long)(1000000000LL / (font_lookup_ms > 0 ? font_lookup_ms : 1)));
    printf("Render 100 frames:     %4ld ms (%4.1f fps, %ld cells/s)\n",
           (long)render_100_frames_ms,
           (100.0 * 1000.0) / (double)render_100_frames_ms,
           (long)(480000LL * 1000LL / render_100_frames_ms));

    free(g_mock_buf.pixels);
    goot_font_close();
    return 0;
}
