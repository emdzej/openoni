/* IM: images and pixel types (IMrInitialize 0x447f20). The game's texel types, their sizes, and the
   conversion to RGBA bytes the port's renderer and exporters use, ported from the original's converters
   (IMrImage_ConvertPixelType 0x44ac20), quirks included. The pixel type table is read from the exe. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* IMtPixelType: the types the CD's textures use (the table has 15). */
enum {
    IM_ARGB4444 = 0, IM_RGB555 = 1, IM_ARGB1555 = 2, IM_I8 = 3, IM_I1 = 4, IM_A8 = 5, IM_A4I4 = 6,
    IM_ARGB8888 = 7, IM_XRGB8888 = 8, IM_DXT1 = 9, IM_RGB_BYTES = 10, IM_RGBA_BYTES = 11,
    IM_RGBA5551 = 12, IM_RGBA4444 = 13, IM_RGB565 = 14, IM_TYPE_COUNT = 15,
};

/* Reads the pixel type table from the exe (needs exe_init). */
bool im_initialize(char *err, size_t errcap);
/* IMrImage_ComputeSize 0x4478d0: bytes of a w x h image, with its mip chain down to 1x1 if mipmaps. */
uint32_t im_image_size(int type, bool mipmaps, unsigned w, unsigned h);
/* The next level of a mip chain: halve, clamp to 1; false after 1x1. */
bool im_mip_next(unsigned *w, unsigned *h);
/* One level to RGBA bytes (w * h * 4 at dst). False for a type the CD doesn't use. */
bool im_to_rgba(int type, unsigned w, unsigned h, const uint8_t *src, uint8_t *dst);
