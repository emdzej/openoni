/* TXMP, texture maps (M3rTextureMap_New 0x41eb00; the pixels: M3iTextureMap_PrepareRaw 0x41e9d0). A
   TXMP instance describes one texture: size, texel type, flags, links to its animation and environment
   map, and where its pixels are in the level's .raw. Format: docs/formats/txmp.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { TXMP_TAG = 0x54584d50 };

/* The fields, from the instance data (tm.h: past the 8-byte header). */
enum {
    TXMP_NAME = 0x00,        /* char[128], the debug name (usually empty) */
    TXMP_FLAGS = 0x80,
    TXMP_WIDTH = 0x84,       /* u16 */
    TXMP_HEIGHT = 0x86,      /* u16 */
    TXMP_TYPE = 0x88,        /* IMtPixelType (im.h) */
    TXMP_ANIMATION = 0x8c,   /* link: TXAN */
    TXMP_ENVMAP = 0x90,      /* link: TXMP */
    TXMP_RAW = 0x94,         /* offset of the pixels in the .raw (the original: then a pointer) */
};

enum {
    TXMP_FLAG_MIPMAPS = 0x0001,
    TXMP_FLAG_NATIVE_ORDER = 0x1000,    /* already little-endian (all of the CD's) */
    TXMP_FLAG_RAW_RESOLVED = 0x2000,    /* set at run time once TXMP_RAW is a pointer */
};

typedef struct {
    uint32_t flags;
    unsigned width, height;
    int type;
    uint32_t raw;           /* offset in the .raw */
    uint32_t size;          /* bytes, every level */
} TxmpInfo;

void txmp_info(const void *txmp, TxmpInfo *out);
/* All levels' pixels as RGBA bytes, largest first, *size bytes; malloc'd, NULL on error. */
uint8_t *txmp_rgba(const void *txmp, uint32_t *size);

/* M3iResolveTexture 0x41e500: the frame of the texture's animation (TXAN, +0x8c) at time ticks (and the
   index, for textures that add it: flag 0x100): frame 0 is the texture itself. Flag 0x4000 holds the
   first frame, 0x40 runs forth and back, 0x80 picks a random frame, reseeding UUrLocalRandom as the
   original does (so drawing moves the game's second generator). */
const void *txmp_resolve(const void *txmp, uint32_t time, uint32_t index);
