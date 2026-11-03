/* TXMP texture maps: see txmp.h. */
#include "txmp.h"

#include "im.h"
#include "tm.h"
#include "trac.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

void txmp_info(const void *txmp, TxmpInfo *out)
{
    const uint8_t *t = txmp;
    out->flags = rd32(t + TXMP_FLAGS);
    out->width = rd16(t + TXMP_WIDTH);
    out->height = rd16(t + TXMP_HEIGHT);
    out->type = (int)rd32(t + TXMP_TYPE);
    out->raw = rd32(t + TXMP_RAW);
    /* M3iTextureMap_PrepareRaw 0x41e9d0 counts the levels when bit 0 is set (~flags & 1 = no mips) */
    out->size = im_image_size(out->type, out->flags & TXMP_FLAG_MIPMAPS, out->width, out->height);
}

/* M3iTextureMap_PrepareRaw 0x41e9d0 turns TXMP_RAW into a pointer into the loaded .raw
   (TMrInstance_GetRawOffset 0x423e10); the CD's textures are in native order (TXMP_FLAG_NATIVE_ORDER), so there is nothing to swap. Here the
   pixels are read from the .raw and converted to RGBA bytes level by level. */
uint8_t *txmp_rgba(const void *txmp, uint32_t *size)
{
    TxmpInfo in;
    txmp_info(txmp, &in);
    if (!in.size) return NULL;
    bool mips = in.flags & TXMP_FLAG_MIPMAPS;
    uint32_t total = 0;
    unsigned w = in.width, h = in.height;
    do total += w * h * 4;
    while (mips && im_mip_next(&w, &h));
    uint8_t *src = malloc(in.size), *dst = malloc(total);
    bool ok = src && dst && tm_raw_read(txmp, in.raw, src, in.size) == (int64_t)in.size;
    w = in.width;
    h = in.height;
    const uint8_t *s = src;
    uint8_t *d = dst;
    while (ok) {
        ok = im_to_rgba(in.type, w, h, s, d);
        s += im_image_size(in.type, false, w, h);
        d += w * h * 4;
        if (!mips || !im_mip_next(&w, &h)) break;
    }
    free(src);
    if (!ok) {
        free(dst);
        return NULL;
    }
    if (size) *size = total;
    return dst;
}

const void *txmp_resolve(const void *txmp, uint32_t time, uint32_t index)
{
    const uint8_t *t = txmp;
    const uint8_t *an = t ? tm_link(t + 0x8c) : NULL;
    if (!an) return txmp;
    uint32_t flags = (uint32_t)t[0x80] | (uint32_t)t[0x81] << 8 | (uint32_t)t[0x82] << 16 | (uint32_t)t[0x83] << 24;
    uint16_t speed = (uint16_t)(an[0xc] | an[0xd] << 8);
    uint32_t n = (uint32_t)an[0x14] | (uint32_t)an[0x15] << 8 | (uint32_t)an[0x16] << 16 | (uint32_t)an[0x17] << 24;
    if (!speed) return txmp;    /* the original divides by it */
    if (flags & 0x4000) time = 0;
    if (flags & 0x100) time += index;
    uint32_t f;
    if (flags & 0x80) {
        uint32_t k = time / speed;
        trac_local_seed((k >> 24 | (k >> 8 & 0xff00) | (k << 8 & 0xff0000) | k << 24) ^ 0xfdedfdedu);
        f = n ? (uint32_t)(trac_local_random() * n >> 16) : 0;
    } else if (flags & 0x40) {
        uint32_t m = n * 2 - 2;
        f = m ? time / speed % m : 0;
        if (f >= n) f = m - f;
    } else
        f = n ? time / speed % n : 0;
    if (!f) return txmp;
    const void *fr = tm_link(an + 0x18 + 4 * f);
    return fr ? fr : txmp;
}
