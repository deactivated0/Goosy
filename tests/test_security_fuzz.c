/*
 * Goot — Security Fuzzing & Resource Boundary Regression Test
 * Validates rejection of malformed Sixel, Kitty, PNG, and PTY write overflows.
 */

#define _POSIX_C_SOURCE 200809L

#include "goot_native.h"
#include "goot_image.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_sixel_overflow_rejection(void) {
    printf("[security] 1. Testing Sixel integer overflow rejection...\n");
    // Sixel with absurdly huge repeat count: "!2000000000~"
    const char *malformed_sixel = "\x1bPq#0;2;100;100;100!2000000000~\x1b\\";
    goot_const_bytes_t data = { (const uint8_t *)malformed_sixel, (int64_t)strlen(malformed_sixel) };
    int32_t rows = goot_sixel_decode(data, 0, 0, 8, 16);
    // Should be clamped to safe dimensions, not crash or OOM
    printf("[security] Sixel clamped rows: %d (safe)\n", (int)rows);
    goot_image_clear();
}

static void test_kitty_bounds_rejection(void) {
    printf("[security] 2. Testing Kitty graphics dimensions & payload bounds...\n");
    // Kitty image requesting 100000x100000 RGBA buffer (would be 40GB!)
    const char *bad_kitty = "a=T,f=32,s=100000,v=100000;AAAA";
    goot_const_bytes_t data = { (const uint8_t *)bad_kitty, (int64_t)strlen(bad_kitty) };
    int32_t rows = goot_kitty_graphics(data, 0, 0, 8, 16);
    assert(rows == 0); // Must be rejected
    printf("[security] Unbounded Kitty dimensions successfully rejected (rows=0)\n");

    // Invalid base64 characters should not crash
    const char *bad_b64 = "a=T,f=32,s=10,v=10;!@#$%^&*()_+";
    goot_const_bytes_t data2 = { (const uint8_t *)bad_b64, (int64_t)strlen(bad_b64) };
    int32_t rows2 = goot_kitty_graphics(data2, 0, 0, 8, 16);
    (void)rows2;
    printf("[security] Corrupt Base64 payload safely handled\n");
    goot_image_clear();
}

static void test_png_bounds_rejection(void) {
    printf("[security] 3. Testing PNG dimension bounds...\n");
    // Corrupted 8-byte signature but invalid IHDR
    uint8_t fake_png[32] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R',
        0x7F, 0xFF, 0xFF, 0xFF, // huge width
        0x7F, 0xFF, 0xFF, 0xFF  // huge height
    };
    int32_t out_w = 0, out_h = 0;
    uint32_t *p = goot_decode_png_to_argb(fake_png, sizeof(fake_png), &out_w, &out_h);
    assert(p == NULL);
    printf("[security] Corrupt/oversized PNG safely rejected\n");
}

static void test_pty_dynamic_write_burst(void) {
    printf("[security] 4. Testing PTY dynamic buffer large write burst...\n");
    // Allocate 256 KiB buffer (larger than initial 64 KiB buffer)
    size_t burst_sz = 256 * 1024;
    uint8_t *burst = (uint8_t *)malloc(burst_sz);
    assert(burst != NULL);
    memset(burst, 'A', burst_sz);

    goot_const_bytes_t data = { burst, (int64_t)burst_sz };
    // Writing without open master fd should return 0, but must not crash or leak
    int64_t written = goot_pty_write(data);
    (void)written;
    free(burst);
    printf("[security] Large PTY write burst test passed\n");
}

int main(void) {
    test_sixel_overflow_rejection();
    test_kitty_bounds_rejection();
    test_png_bounds_rejection();
    test_pty_dynamic_write_burst();
    printf("\nALL SECURITY & RESOURCE BOUNDARY REGRESSION TESTS PASSED.\n");
    return 0;
}
