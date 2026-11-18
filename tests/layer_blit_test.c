/* Host validation of the layer present path: scaling, channel order and the
 * fail-closed bounds rules. Compiled with -Wall -Wextra -Werror and run under
 * ASan/UBSan before anything is pushed to the build workflow. */
#include "B2LBlit.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static uint8_t *alloc(size_t n) { uint8_t *p = malloc(n); assert(p); memset(p, 0xcc, n); return p; }

/* 1:1 copy, unpadded source, row padding in the destination. */
static void test_identity(void)
{
    const uint32_t sw = 4, sh = 3, dw = 6, dh = 5;
    uint32_t src[12]; for (uint32_t i = 0; i < 12; ++i) src[i] = 0x11223300u + i;
    uint8_t *dst = alloc((size_t)dw * dh * 4 + 64);
    assert(b2l_blit_layer(dst, (uint64_t)dw*dh*4+64, dw, dh, dw*4,
                          src, sizeof(src), sw, sh, sw*4, 0,
                          1, 1, 0, 0, 2, 1, 2, 2, 0));
    /* Destination pixel (2,1) came from source (1,1) = src[5]. */
    uint32_t v; memcpy(&v, dst + (1u*dw + 2)*4, 4); assert(v == src[5]);
    /* Untouched destination pixels keep the guard pattern. */
    memcpy(&v, dst, 4); assert(v == 0xccccccccu);
    free(dst);
}

/* Source rectangle smaller than the destination: nearest-neighbour upscale. */
static void test_scale(void)
{
    const uint32_t dw = 4, dh = 4;
    uint32_t src[4] = { 10, 20, 30, 40 };           /* 2x2 */
    uint8_t *dst = alloc((size_t)dw * dh * 4);
    assert(b2l_blit_layer(dst, (uint64_t)dw*dh*4, dw, dh, dw*4,
                          src, sizeof(src), 2, 2, 2*4, 0,
                          0, 0, 2, 2, 0, 0, 4, 4, 0));
    const uint32_t want[16] = {
        10,10,20,20, 10,10,20,20,
        30,30,40,40, 30,30,40,40,
    };
    for (unsigned i = 0; i < 16; ++i) {
        uint32_t v; memcpy(&v, dst + i*4, 4);
        if (v != want[i]) { fprintf(stderr, "scale[%u]=%u want %u\n", i, v, want[i]); abort(); }
    }
    free(dst);
}

/* Downscale must not read past the source rectangle. */
static void test_downscale(void)
{
    const uint32_t sw = 8, sh = 8, dw = 3, dh = 3;
    uint32_t *src = (uint32_t*)alloc((size_t)sw*sh*4);
    for (uint32_t i = 0; i < sw*sh; ++i) src[i] = i;
    uint8_t *dst = alloc((size_t)dw * dh * 4);
    assert(b2l_blit_layer(dst, (uint64_t)dw*dh*4, dw, dh, dw*4,
                          src, (uint64_t)sw*sh*4, sw, sh, sw*4, 0,
                          0, 0, 8, 8, 0, 0, 3, 3, 0));
    /* Top-left is the first source pixel, bottom-right the last sampled one. */
    uint32_t v; memcpy(&v, dst, 4); assert(v == 0);
    /* Nearest-neighbour: index = floor(i * src / dst) = 5 for the last column. */
    memcpy(&v, dst + 8*4, 4); assert(v == 5u*8u + 5u);
    free(src); free(dst);
}

/* Channel order: ARGB in memory is A,R,G,B and must land as B,G,R,A. */
static void test_channel_order(void)
{
    struct { uint32_t fmt; int swz; uint8_t in[4]; uint8_t want[4]; } cases[] = {
        { B2L_BLIT_FMT_BGRA, 0, { 0x11,0x22,0x33,0x44 }, { 0x11,0x22,0x33,0x44 } },
        { B2L_BLIT_FMT_ARGB, 1, { 0x11,0x22,0x33,0x44 }, { 0x44,0x33,0x22,0x11 } },
        { B2L_BLIT_FMT_ABGR, 2, { 0x11,0x22,0x33,0x44 }, { 0x22,0x33,0x44,0x11 } },
        { B2L_BLIT_FMT_RGBA, 3, { 0x11,0x22,0x33,0x44 }, { 0x33,0x22,0x11,0x44 } },
    };
    for (unsigned c = 0; c < sizeof(cases)/sizeof(cases[0]); ++c) {
        int swz = b2l_blit_swizzle(cases[c].fmt);
        if (swz != cases[c].swz) { fprintf(stderr, "swizzle(%08x)=%d\n", cases[c].fmt, swz); abort(); }
        uint8_t dst[4] = { 0 };
        assert(b2l_blit_layer(dst, 4, 1, 1, 4, cases[c].in, 4, 1, 1, 4, 0,
                              0, 0, 1, 1, 0, 0, 1, 1, swz));
        if (memcmp(dst, cases[c].want, 4)) {
            fprintf(stderr, "fmt %08x: %02x%02x%02x%02x\n", cases[c].fmt,
                    dst[0], dst[1], dst[2], dst[3]);
            abort();
        }
    }
    assert(b2l_blit_swizzle(0x4c380000u /* 'L8  ' */) == -1);
}

/* Everything outside the mapped extents must be refused, never clipped. */
static void test_bounds(void)
{
    uint32_t src[16]; uint8_t dst[16*4]; uint32_t v;
    for (unsigned i = 0; i < 16; ++i) src[i] = i;
    const uint32_t dw = 4, dh = 4;

    assert(b2l_blit_layer(dst, 64, dw, dh, 16, src, 64, 4, 4, 16, 0,
                          0, 0, 4, 4, 0, 0, 4, 4, 0));           /* exact fit works */
    assert(b2l_blit_layer(dst, 64, dw, dh, 16, src, 64, 4, 4, 16, 0,
                          0, 0, 0, 0, 0, 0, 4, 4, 0));            /* no rect = full */
    assert(!b2l_blit_layer(dst, 64, dw, dh, 16, src, 64, 4, 4, 16, 0,
                           1, 0, 4, 4, 0, 0, 4, 4, 0));           /* src overruns width */
    assert(!b2l_blit_layer(dst, 64, dw, dh, 16, src, 64, 4, 4, 16, 0,
                           0, 1, 4, 4, 0, 0, 4, 4, 0));           /* src overruns height */
    /* A destination rectangle crossing the panel edge is clipped, never rejected. */
    memset(dst, 0xcc, sizeof(dst));
    assert(b2l_blit_layer(dst, 64, dw, dh, 16, src, 64, 4, 4, 16, 0,
                          0, 0, 4, 4, 1, 1, 4, 4, 0));
    memcpy(&v, dst, 4); assert(v == 0xccccccccu);
    /* index 2 of a 4-wide dest sampling a 4-wide source */
    memcpy(&v, dst + (3u*dw + 3)*4, 4); assert(v == 10u);
    assert(!b2l_blit_layer(dst, 63, dw, dh, 16, src, 64, 4, 4, 16, 0,
                           0, 0, 4, 4, 0, 0, 4, 4, 0));           /* short dst mapping */
    assert(!b2l_blit_layer(dst, 64, dw, dh, 16, src, 63, 4, 4, 16, 0,
                           0, 0, 4, 4, 0, 0, 4, 4, 0));           /* short src mapping */
    assert(!b2l_blit_layer(dst, 64, dw, dh, 15, src, 64, 4, 4, 16, 0,
                           0, 0, 4, 4, 0, 0, 4, 4, 0));           /* stride < width*4 */
    assert(!b2l_blit_layer(NULL, 64, dw, dh, 16, src, 64, 4, 4, 16, 0,
                           0, 0, 4, 4, 0, 0, 4, 4, 0));
    assert(!b2l_blit_layer(dst, 64, dw, dh, 16, NULL, 64, 4, 4, 16, 0,
                           0, 0, 4, 4, 0, 0, 4, 4, 0));
    assert(!b2l_blit_layer(dst, 64, dw, dh, 16, src, 64, 4, 4, 16, 0,
                           0, 0, -2, 4, 0, 0, 4, 4, 0));          /* invalid rect */
    assert(!b2l_blit_layer(dst, 64, dw, dh, 16, src, 64, 4, 4, 16, 0,
                           0, 0, 4, 4, 0, 0, 4, 4, 4));           /* unknown swizzle */
}

/* The legacy entry point must keep its exact contract. */
static void test_legacy(void)
{
    uint32_t src[16]; uint8_t dst[16*4];
    for (unsigned i = 0; i < 16; ++i) src[i] = i;
    assert(b2l_blit(dst, 64, 4, 4, 16, src, 64, 4, 4, 16, 0, 0, 0, 0, 0, 4, 4));
    uint32_t v; memcpy(&v, dst + 5*4, 4); assert(v == 5);
    assert(!b2l_blit(dst, 64, 4, 4, 16, src, 64, 4, 4, 16, 0, 4, 0, 0, 0, 4, 4));
    assert(b2l_blit(dst, 64, 4, 4, 16, src, 64, 4, 4, 16, 0, 1, 0, 0, 0, 4, 4));   /* legacy clips */
}

int main(void)
{
    test_identity();
    test_scale();
    test_downscale();
    test_channel_order();
    test_bounds();
    test_legacy();
    puts("blit: all layer-present checks passed");
    return 0;
}
