/* IM, pixel types and conversion: see im.h, docs/formats/txmp.md. */
#include "im.h"

#include "exe.h"

#include <stdio.h>
#include <string.h>

enum {
    PIXEL_TYPES_VA = 0x529e78,      /* IMtPixelTypeInfo[15]: id, flag, bytes per pixel, A R G B masks */
    PIXEL_TYPE_STRIDE = 0x18,
};

static uint8_t bytes_per_pixel[IM_TYPE_COUNT];
static bool ready;

bool im_initialize(char *err, size_t cap)
{
    for (int t = 0; t < IM_TYPE_COUNT; t++) {
        const uint8_t *e = exe_data(PIXEL_TYPES_VA + (uint32_t)t * PIXEL_TYPE_STRIDE, PIXEL_TYPE_STRIDE);
        if (!e || e[0] != t) {
            if (err && cap) snprintf(err, cap, "pixel type table: entry %d", t);
            return false;
        }
        bytes_per_pixel[t] = e[5];
    }
    ready = true;
    return true;
}

/* IMrImage_ComputeSize 0x4478d0 (without the recursion): one level. */
static uint32_t level_size(int type, unsigned w, unsigned h)
{
    if (!w || !h || type < 0 || type >= IM_TYPE_COUNT) return 0;
    switch (bytes_per_pixel[type]) {
    case 0:
        if (type == IM_I1) return ((w + 15) >> 3) * h;
        if (type == IM_DXT1) return ((w + 3) & ~3u) * ((h + 3) & ~3u) * 2 >> 2;
        return 0;
    case 1: return ((w + 1) & ~1u) * h;
    case 2: return w * h * 2;
    case 4: return w * h * 4;
    default: return 0;   /* 3 (RGB bytes) isn't sized by the original either */
    }
}

bool im_mip_next(unsigned *w, unsigned *h)
{
    *w >>= 1;
    *h >>= 1;
    if (!*w && !*h) return false;
    if (*w < 2) *w = 1;
    if (*h < 2) *h = 1;
    return true;
}

uint32_t im_image_size(int type, bool mipmaps, unsigned w, unsigned h)
{
    if (!ready || !w || !h) return 0;
    uint32_t n = 0;
    do n += level_size(type, w, h);
    while (mipmaps && im_mip_next(&w, &h));
    return n;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* DecodeDXT1Block_To_RGBABytes 0x44a8a0: the block's colours. The end points are widened by a shift
   only; green's thirds aren't rounded; with c0 <= c1 the fourth colour is opaque black. */
static void dxt1_palette(uint16_t c0, uint16_t c1, uint8_t pal[4][4])
{
    unsigned r0 = c0 >> 11, g0 = (c0 >> 5) & 63, b0 = c0 & 31;
    unsigned r1 = c1 >> 11, g1 = (c1 >> 5) & 63, b1 = c1 & 31;
    uint8_t p[4][4] = {
        { (uint8_t)(r0 << 3), (uint8_t)(g0 << 2), (uint8_t)(b0 << 3), 255 },
        { (uint8_t)(r1 << 3), (uint8_t)(g1 << 2), (uint8_t)(b1 << 3), 255 },
    };
    if (c0 > c1) {
        p[2][0] = (uint8_t)(((r1 + 1 + 2 * r0) / 3) << 3);
        p[2][1] = (uint8_t)(((g1 + 2 * g0) / 3) << 2);
        p[2][2] = (uint8_t)(((b1 + 1 + 2 * b0) / 3) << 3);
        p[3][0] = (uint8_t)(((r0 + 1 + 2 * r1) / 3) << 3);
        p[3][1] = (uint8_t)(((g0 + 2 * g1) / 3) << 2);
        p[3][2] = (uint8_t)(((b0 + 1 + 2 * b1) / 3) << 3);
    } else {
        p[2][0] = (uint8_t)(((r1 + 1 + r0) >> 1) << 3);
        p[2][1] = (uint8_t)(((g1 + g0) >> 1) << 2);
        p[2][2] = (uint8_t)(((b1 + 1 + b0) >> 1) << 3);
    }
    p[2][3] = p[3][3] = 255;
    memcpy(pal, p, sizeof p);
}

/* IMiConvert_DXT1_to_NByte 0x449650: 4x4 blocks, left to right, top to bottom; at the edges of a
   level smaller than a block, only the pixels inside are written. */
static void dxt1_to_rgba(unsigned w, unsigned h, const uint8_t *src, uint8_t *dst)
{
    for (unsigned by = 0; by < (h + 3) / 4; by++)
        for (unsigned bx = 0; bx < (w + 3) / 4; bx++, src += 8) {
            uint8_t pal[4][4];
            dxt1_palette(rd16(src), rd16(src + 2), pal);
            for (unsigned y = 0; y < 4 && by * 4 + y < h; y++) {
                uint8_t bits = src[4 + y];
                for (unsigned x = 0; x < 4 && bx * 4 + x < w; x++)
                    memcpy(dst + ((by * 4 + y) * w + bx * 4 + x) * 4, pal[(bits >> (2 * x)) & 3], 4);
            }
        }
}

bool im_to_rgba(int type, unsigned w, unsigned h, const uint8_t *src, uint8_t *dst)
{
    size_t n = (size_t)w * h;
    switch (type) {
    case IM_ARGB4444:   /* 0x44b020: every nibble repeated */
        for (size_t i = 0; i < n; i++, dst += 4) {
            unsigned p = rd16(src + 2 * i);
            dst[0] = (uint8_t)(((p >> 8) & 15) * 17);
            dst[1] = (uint8_t)(((p >> 4) & 15) * 17);
            dst[2] = (uint8_t)((p & 15) * 17);
            dst[3] = (uint8_t)((p >> 12) * 17);
        }
        return true;
    case IM_RGB555:     /* 0x44afa0: shifted, the low bits left 0 */
        for (size_t i = 0; i < n; i++, dst += 4) {
            unsigned p = rd16(src + 2 * i);
            dst[0] = (uint8_t)((p >> 7) & 0xf8);
            dst[1] = (uint8_t)((p >> 2) & 0xf8);
            dst[2] = (uint8_t)(p << 3);
            dst[3] = 255;
        }
        return true;
    case IM_ARGB1555:   /* 0x44b0b0: the top bits repeated below */
        for (size_t i = 0; i < n; i++, dst += 4) {
            unsigned p = rd16(src + 2 * i);
            unsigned r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
            dst[0] = (uint8_t)(r << 3 | r >> 2);
            dst[1] = (uint8_t)(g << 3 | g >> 2);
            dst[2] = (uint8_t)(b << 3 | b >> 2);
            dst[3] = p & 0x8000 ? 255 : 0;
        }
        return true;
    case IM_XRGB8888:   /* 0x44b250 (to RGB bytes), opaque */
        for (size_t i = 0; i < n; i++, dst += 4, src += 4) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = 255;
        }
        return true;
    case IM_DXT1:
        dxt1_to_rgba(w, h, src, dst);
        return true;
    default:
        return false;
    }
}

/* IMrShade_Interpolate 0x447c10 */
uint32_t im_shade_interpolate(uint32_t a, uint32_t b, float t)
{
    float tt = t * 256.0f + 8388608.0f;
    uint32_t bits;
    memcpy(&bits, &tt, 4);
    uint32_t i = bits & 0x7fffff, j = 0x100 - i;
    uint32_t ah = a >> 8, bh = b >> 8;
    return (((a & 0xff) * j + (b & 0xff) * i) >> 8 & 0xff) | (((bh & 0xff0000) * i + (ah & 0xff0000) * j) & 0xff000000) |
           (((bh & 0xff) * i + (ah & 0xff) * j) & 0xff00) | (((bh & 0xff00) * i + (ah & 0xff00) * j) & 0xff0000);
}
