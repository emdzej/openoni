/* DEFLATE decoder (see inflate.h). Huffman codes are decoded canonically: per code length, the number of
   codes and the symbols in code order, walked one bit at a time (fast enough for loading; the game reads
   each file once). */
#include "inflate.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum { MAXBITS = 15, MAXLIT = 288, MAXDIST = 30, MAXCODES = MAXLIT + MAXDIST };

typedef struct {
    const uint8_t *in;
    size_t inlen, pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t outcap, outpos;
    bool err;
} Inf;

typedef struct {
    uint16_t count[MAXBITS + 1];   /* codes per length */
    uint16_t symbol[MAXCODES];     /* symbols ordered by code */
} Huff;

static uint32_t bits(Inf *s, int n)
{
    while (s->bitcnt < n) {
        if (s->pos >= s->inlen) { s->err = true; return 0; }
        s->bitbuf |= (uint32_t)s->in[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    uint32_t v = s->bitbuf & ((1u << n) - 1);
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return v;
}

/* Builds the table from code lengths. False if the lengths over-subscribe the code space (an incomplete
   code is allowed: RFC 1951 permits one distance code of length 1). */
static bool build(Huff *h, const uint8_t *len, int n)
{
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int b = 1; b <= MAXBITS; b++) {
        left = (left << 1) - h->count[b];
        if (left < 0) return false;
    }
    uint16_t offs[MAXBITS + 2];
    offs[1] = 0;
    for (int b = 1; b <= MAXBITS; b++) offs[b + 1] = (uint16_t)(offs[b] + h->count[b]);
    for (int i = 0; i < n; i++)
        if (len[i]) h->symbol[offs[len[i]]++] = (uint16_t)i;
    return true;
}

static int decode(Inf *s, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int b = 1; b <= MAXBITS; b++) {
        code |= (int)bits(s, 1);
        if (s->err) return -1;
        int c = h->count[b];
        if (code - first < c) return h->symbol[index + code - first];
        index += c;
        first = (first + c) << 1;
        code <<= 1;
    }
    s->err = true;
    return -1;
}

static const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DEXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static bool put(Inf *s, uint8_t b)
{
    if (s->outpos >= s->outcap) { s->err = true; return false; }
    s->out[s->outpos++] = b;
    return true;
}

static bool codes(Inf *s, const Huff *lit, const Huff *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0) return false;
        if (sym < 256) {
            if (!put(s, (uint8_t)sym)) return false;
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = LBASE[sym] + bits(s, LEXTRA[sym]);
            int d = decode(s, dist);
            if (d < 0 || d >= 30) return false;
            size_t back = DBASE[d] + bits(s, DEXTRA[d]);
            if (s->err || back > s->outpos || len > s->outcap - s->outpos) { s->err = true; return false; }
            uint8_t *o = s->out + s->outpos;   /* byte by byte: the copy may overlap itself */
            for (size_t i = 0; i < len; i++) o[i] = o[(ptrdiff_t)i - (ptrdiff_t)back];
            s->outpos += len;
        }
    }
}

static bool stored(Inf *s)
{
    s->bitbuf = 0;   /* to the byte boundary */
    s->bitcnt = 0;
    if (s->pos + 4 > s->inlen) return false;
    unsigned len = s->in[s->pos] | s->in[s->pos + 1] << 8;
    unsigned nlen = s->in[s->pos + 2] | s->in[s->pos + 3] << 8;
    s->pos += 4;
    if (len != (~nlen & 0xffff) || len > s->inlen - s->pos || len > s->outcap - s->outpos) return false;
    memcpy(s->out + s->outpos, s->in + s->pos, len);
    s->pos += len;
    s->outpos += len;
    return true;
}

static bool fixed(Inf *s)
{
    static Huff lit, dist;
    static bool built;
    if (!built) {
        uint8_t l[MAXLIT];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < MAXLIT; i++) l[i] = 8;
        build(&lit, l, MAXLIT);
        for (i = 0; i < MAXDIST; i++) l[i] = 5;
        build(&dist, l, MAXDIST);
        built = true;
    }
    return codes(s, &lit, &dist);
}

static bool dynamic(Inf *s)
{
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int nlen = (int)bits(s, 5) + 257, ndist = (int)bits(s, 5) + 1, ncode = (int)bits(s, 4) + 4;
    if (s->err || nlen > 286 || ndist > MAXDIST) return false;
    uint8_t len[MAXCODES] = {0};
    for (int i = 0; i < ncode; i++) len[ORDER[i]] = (uint8_t)bits(s, 3);
    Huff lc, lit, dist;
    if (s->err || !build(&lc, len, 19)) return false;
    memset(len, 0, sizeof len);
    for (int i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lc);
        if (sym < 0) return false;
        if (sym < 16) { len[i++] = (uint8_t)sym; continue; }
        int rep;
        uint8_t v = 0;
        if (sym == 16) {
            if (!i) return false;
            v = len[i - 1];
            rep = 3 + (int)bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + (int)bits(s, 3);
        } else {
            rep = 11 + (int)bits(s, 7);
        }
        if (s->err || i + rep > nlen + ndist) return false;
        while (rep--) len[i++] = v;
    }
    if (!len[256]) return false;   /* no end-of-block code */
    if (!build(&lit, len, nlen) || !build(&dist, len + nlen, ndist)) return false;
    return codes(s, &lit, &dist);
}

int64_t inflate_raw(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap, size_t *used)
{
    Inf s = {src, srclen, 0, 0, 0, dst, dstcap, 0, false};
    bool last;
    do {
        last = bits(&s, 1);
        int type = (int)bits(&s, 2);
        if (s.err) return -1;
        bool ok = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : false;
        if (!ok || s.err) return -1;
    } while (!last);
    if (used) *used = s.pos;
    return (int64_t)s.outpos;
}

int64_t inflate_vise(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap)
{
    size_t n = srclen + (srclen & 1);
    uint8_t *sw = malloc(n ? n : 1);
    if (!sw) return -1;
    for (size_t i = 0; i < n; i += 2) {   /* each word's two bytes swapped (a short last word gets a 0) */
        sw[i] = i + 1 < srclen ? src[i + 1] : 0;
        sw[i + 1] = src[i];
    }
    int64_t r = inflate_raw(sw, n, dst, dstcap, NULL);
    free(sw);
    return r;
}
