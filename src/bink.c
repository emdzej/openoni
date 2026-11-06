/* Bink movies: see bink.h and docs/formats/bink.md. */
#include "bink.h"

#include "exe.h"
#include "vfs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The DLL's tables (docs/formats/bink.md). */
enum {
    DLL_TREE_TABLES = 0x1003a030,   /* 16 pointers: a byte per code prefix, length << 4 | index */
    DLL_TREE_MAXLEN = 0x100379f0,   /* 16 bytes */
    DLL_RLE = 0x10037a00,           /* the block types' repeats for symbols 12..15 */
    DLL_PATTERNS = 0x10037210,      /* the run blocks' 16 scan patterns, 64 bytes each */
    DLL_SCAN = 0x1003e7dc,          /* the coefficients' order */
    DLL_QUANT_INTRA = 0x10033208,   /* 16 × 64 ints */
    DLL_QUANT_INTER = 0x10035208,
};

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- the bits: 32-bit words, the low bit first ---- */

typedef struct {
    const uint8_t *p, *end;     /* the next word */
    uint32_t buf;               /* the bits left of the last word, the next lowest */
    unsigned left;
} Bits;

static uint32_t next_word(Bits *b)
{
    uint32_t w = b->p + 4 <= b->end ? rd32(b->p) : 0;
    b->p += 4;
    return w;
}

static uint32_t get_bits(Bits *b, unsigned n)
{
    if (!n) return 0;
    uint32_t mask = 0xffffffffu >> (32 - n), v;
    if (b->left < n) {
        uint32_t w = next_word(b);
        v = (b->buf | w << b->left) & mask;
        b->buf = w >> (n - b->left);
        b->left += 32 - n;
    } else {
        v = b->buf & mask;
        b->buf = n == 32 ? 0 : b->buf >> n;
        b->left -= n;
    }
    return v;
}

static uint32_t get_bit(Bits *b)
{
    if (!b->left) {
        uint32_t w = next_word(b);
        b->buf = w >> 1;
        b->left = 31;
        return w & 1;
    }
    b->left--;
    uint32_t v = b->buf & 1;
    b->buf >>= 1;
    return v;
}

/* ---- the Huffman trees (0x10003f00) ---- */

typedef struct {
    const uint8_t *table;
    unsigned maxlen;
    uint8_t syms[16];
} Tree;

/* 0x10004380: n from a and n from b, a bit picking which comes next until one runs out. */
static void merge(Bits *b, uint8_t *dst, const uint8_t *a, const uint8_t *c, int n)
{
    int na = n, nc = n;
    while (na && nc) {
        if (get_bit(b)) *dst++ = *c++, nc--;
        else *dst++ = *a++, na--;
    }
    while (na--) *dst++ = *a++;
    while (nc--) *dst++ = *c++;
}

static void read_tree(Bits *b, Tree *t)
{
    unsigned idx = get_bits(b, 4);
    t->table = dll_data(dll_u32(DLL_TREE_TABLES + 4 * idx), 1);
    const uint8_t *ml = dll_data(DLL_TREE_MAXLEN + idx, 1);
    t->maxlen = ml ? *ml : 4;
    if (!idx) {
        for (int i = 0; i < 16; i++) t->syms[i] = (uint8_t)i;
        return;
    }
    if (get_bit(b)) {
        /* a count, those symbols, then the others in order */
        unsigned n = get_bits(b, 3), unused = 0xffff;
        for (unsigned i = 0; i <= n; i++) {
            unsigned s = get_bits(b, 4);
            unused &= ~(1u << s);
            t->syms[i] = (uint8_t)s;
        }
        unsigned pos = n;
        for (unsigned s = 0; unused; s++, unused >>= 1)
            if (unused & 1) t->syms[++pos & 15] = (uint8_t)s;
        return;
    }
    unsigned depth = get_bits(b, 2);
    uint8_t pairs[16], q[4][4];
    uint8_t *out = depth ? pairs : t->syms;
    for (int i = 0; i < 16; i += 2) {
        bool swap = get_bit(b);
        out[i] = (uint8_t)(i + swap), out[i + 1] = (uint8_t)(i + !swap);
    }
    if (!depth) return;
    if (depth == 1) {
        for (int i = 0; i < 16; i += 4) merge(b, t->syms + i, pairs + i, pairs + i + 2, 2);
        return;
    }
    for (int i = 0; i < 4; i++) merge(b, q[i], pairs + 4 * i, pairs + 4 * i + 2, 2);
    if (depth == 2) {
        merge(b, t->syms, q[0], q[1], 4);
        merge(b, t->syms + 8, q[2], q[3], 4);
        return;
    }
    uint8_t e[16];
    merge(b, e, q[0], q[1], 4);
    merge(b, e + 8, q[2], q[3], 4);
    merge(b, t->syms, e, e + 8, 8);
}

static unsigned get_sym(Bits *b, const Tree *t)
{
    Bits peek = *b;
    uint8_t e = t->table ? t->table[get_bits(&peek, t->maxlen)] : 0;
    get_bits(b, e >> 4);
    return t->syms[e & 15];
}

/* ---- the bundles (0x10003e30) ---- */

enum { B_TYPES, B_SUBTYPES, B_COLOURS, B_PATTERNS, B_XOFF, B_YOFF, B_INTRA_DC, B_INTER_DC, B_RUNS, NBUNDLES };

typedef struct {
    unsigned len_bits, val_bits;
    bool sign;
    Tree tree;
    uint8_t *data, *cur, *end;
    size_t cap;
} Bundle;

/* The colours' high nibbles (0x10004440: 16 trees, and the last nibble read, from 0 each plane). */
typedef struct {
    Tree hi[16];
    unsigned last;
} ColourHigh;

static unsigned bit_length(uint32_t v)
{
    unsigned n = 0;
    while (v) n++, v >>= 1;
    return n;
}

static void bundle_init(Bundle *bu, unsigned width, unsigned val_bits, unsigned per, bool sign)
{
    bu->len_bits = bit_length(0x200 + (width >> 3) * per - 1);
    bu->val_bits = val_bits;
    bu->sign = sign;
    bu->cur = bu->end = NULL;
}

/* An empty batch (a count of 0): the pointer past the end, so the bundle isn't read again in the plane. */
static bool batch(Bundle *bu, Bits *b, uint32_t *n)
{
    if (bu->cur != bu->end) return false;
    *n = get_bits(b, bu->len_bits);
    if (!*n) {
        bu->cur = bu->data + 4;
        bu->end = bu->data;
        return false;
    }
    bu->cur = bu->data;
    bu->end = bu->data + (bu->val_bits > 8 ? 2 * *n : *n);
    return true;
}

static void fill(uint8_t *p, uint8_t v, uint32_t n, const Bundle *bu)
{
    size_t room = bu->cap - (size_t)(p - bu->data);
    memset(p, v, n < room ? n : room);
}

/* 0x100044a0 (the block types: symbols 12..15 repeat the last) and 0x10004d10 (the runs). */
static void read_types(Bundle *bu, Bits *b, bool rle)
{
    uint32_t n;
    if (!batch(bu, b, &n)) return;
    uint8_t *p = bu->data;
    if (get_bit(b)) {
        fill(p, (uint8_t)get_bits(b, 4), n, bu);
        return;
    }
    const uint8_t *rl = dll_data(DLL_RLE, 4);
    uint8_t last = 0;
    while (n) {     /* unsigned, as the original's: a repeat past the count wraps it */
        unsigned v = get_sym(b, &bu->tree);
        if (!rle || v < 12) {
            if ((size_t)(p - bu->data) < bu->cap) *p = (uint8_t)v;
            p++, n--, last = (uint8_t)v;
        } else {
            unsigned r = rl ? rl[v - 12] : 4;
            if ((size_t)(p - bu->data) >= bu->cap) break;
            fill(p, last, r, bu);
            p += r, n -= r;
        }
    }
}

/* 0x10004a30 */
static void read_colours(Bundle *bu, ColourHigh *ch, Bits *b)
{
    uint32_t n;
    if (!batch(bu, b, &n)) return;
    bool one = get_bit(b);
    uint32_t count = one ? 1 : n;
    for (uint32_t i = 0; i < count && i < bu->cap; i++) {
        unsigned h = get_sym(b, &ch->hi[ch->last]);
        ch->last = h;
        unsigned l = get_sym(b, &bu->tree);
        bu->data[i] = (uint8_t)(h << 4 | l);
    }
    if (one && n > 1) fill(bu->data, bu->data[0], n, bu);
}

/* 0x10004f10: two symbols a byte, the low nibble first */
static void read_patterns(Bundle *bu, Bits *b)
{
    uint32_t n;
    if (!batch(bu, b, &n)) return;
    for (uint32_t i = 0; i < n && i < bu->cap; i++) {
        unsigned lo = get_sym(b, &bu->tree);
        unsigned hi = get_sym(b, &bu->tree);
        bu->data[i] = (uint8_t)(hi << 4 | lo);
    }
}

/* 0x10005100 */
static void read_motion(Bundle *bu, Bits *b)
{
    uint32_t n;
    if (!batch(bu, b, &n)) return;
    if (get_bit(b)) {
        int v = (int)get_bits(b, 4);
        if (v && get_bit(b)) v = -v;
        fill(bu->data, (uint8_t)v, n, bu);
        return;
    }
    for (uint32_t i = 0; i < n && i < bu->cap; i++) {
        int v = (int)get_sym(b, &bu->tree);
        if (v && get_bit(b)) v = -v;
        bu->data[i] = (uint8_t)v;
    }
}

/* 0x10005360: the first value whole, then groups of up to 8 deltas */
static void read_dc(Bundle *bu, Bits *b)
{
    uint32_t n;
    if (!batch(bu, b, &n)) return;
    int16_t *out = (int16_t *)bu->data;
    size_t cap = bu->cap / 2;
    int32_t v;
    if (!bu->sign) v = (int32_t)get_bits(b, bu->val_bits);
    else {
        v = (int32_t)get_bits(b, bu->val_bits - 1);
        if (v && get_bit(b)) v = -v;
    }
    size_t k = 0;
    out[k++] = (int16_t)v;
    for (uint32_t left = n - 1; left;) {
        uint32_t g = left < 8 ? left : 8;
        unsigned w = get_bits(b, 4);
        for (uint32_t i = 0; i < g; i++) {
            if (w) {
                int32_t d = (int32_t)get_bits(b, w);
                if (d && get_bit(b)) d = -d;
                v += d;
            }
            if (k < cap) out[k] = (int16_t)v;
            k++;
        }
        left -= g;
    }
}

static uint8_t take8(Bundle *bu)
{
    uint8_t v = (size_t)(bu->cur - bu->data) < bu->cap ? *bu->cur : 0;
    bu->cur++;
    return v;
}

static int16_t take16(Bundle *bu)
{
    int16_t v = 0;
    if ((size_t)(bu->cur - bu->data) + 2 <= bu->cap) memcpy(&v, bu->cur, 2);
    bu->cur += 2;
    return v;
}

/* ---- the coefficients (0x10029640) and the residue (0x1002a010) ---- */

/* The list: an entry is its coefficient << 2 | its mode; 0 is none. */
typedef struct {
    uint8_t e[128];
    int start, end;
} CoefList;

static void list_init(CoefList *l, const uint8_t *first, int n)
{
    l->start = l->end = 64;
    for (int i = 0; i < n; i++) l->e[l->end++] = first[i];
}

/* A coefficient read at a bit plane: the plane's bits with its top bit set (just 1 on the last), and a
   sign. */
static int16_t coef_value(Bits *b, unsigned bits, int16_t top)
{
    int16_t v = bits ? (int16_t)(get_bits(b, bits) | (uint16_t)top) : 1;
    if (get_bit(b)) v = (int16_t)-v;
    return v;
}

/* coef[1..63] in list order (coef[0], the DC, the caller's) */
static void read_coefs(Bits *b, int16_t coef[64])
{
    static const uint8_t first[6] = { 4 << 2, 24 << 2, 44 << 2, 1 << 2 | 3, 2 << 2 | 3, 3 << 2 | 3 };
    int16_t c[64] = { 0 };
    CoefList l;
    list_init(&l, first, 6);
    unsigned planes = get_bits(b, 4);
    int16_t top = (int16_t)(1 << ((planes - 1) & 31));
    for (int p = (int)planes - 1; p >= 0; p--, top = (int16_t)(top >> 1)) {
        for (int i = l.start; i < l.end;) {
            uint8_t e = l.e[i];
            if (!e || !get_bit(b)) { i++; continue; }
            unsigned k = e >> 2;
            switch (e & 3) {
            case 1:
                l.e[i] = (uint8_t)(k << 2 | 2);
                for (int j = 1; j <= 3 && l.end < 128; j++) l.e[l.end++] = (uint8_t)((k + 4 * j) << 2 | 2);
                break;
            case 3:
                c[k & 63] = coef_value(b, (unsigned)p, top);
                l.e[i++] = 0;
                break;
            default:
                if ((e & 3) == 0) l.e[i] = (uint8_t)((k + 4) << 2 | 1);
                else l.e[i++] = 0;
                for (int j = 0; j < 4; j++, k++) {
                    if (get_bit(b)) {
                        if (l.start > 0) l.e[--l.start] = (uint8_t)(k << 2 | 3);
                    } else
                        c[k & 63] = coef_value(b, (unsigned)p, top);
                }
                break;
            }
        }
    }
    for (int i = 1; i < 64; i++) coef[i] = c[i];
}

/* r[64] in list order; masks: the changes allowed (one more than it) */
static void read_residue(Bits *b, int8_t r[64], unsigned masks)
{
    static const uint8_t first[4] = { 4 << 2, 24 << 2, 44 << 2, 0 << 2 | 2 };
    memset(r, 0, 64);
    CoefList l;
    list_init(&l, first, 4);
    uint8_t nz[64];
    int nnz = 0;
    unsigned done = 0;
    int8_t mask = (int8_t)(1 << get_bits(b, 3));
    for (; mask; mask = (int8_t)((uint8_t)mask >> 1)) {
        for (int i = 0; i < nnz; i++) {
            if (!get_bit(b)) continue;
            int8_t *v = &r[nz[i]];
            *v = (int8_t)(*v + (*v < 0 ? -mask : mask));
            if (done++ == masks) return;
        }
        for (int i = l.start; i < l.end;) {
            uint8_t e = l.e[i];
            if (!e || !get_bit(b)) { i++; continue; }
            unsigned k = e >> 2;
            switch (e & 3) {
            case 1:
                l.e[i] = (uint8_t)(k << 2 | 2);
                for (int j = 1; j <= 3 && l.end < 128; j++) l.e[l.end++] = (uint8_t)((k + 4 * j) << 2 | 2);
                break;
            case 3:
                if (nnz < 64) nz[nnz++] = (uint8_t)(k & 63);
                r[k & 63] = get_bit(b) ? (int8_t)-mask : mask;
                l.e[i++] = 0;
                if (done++ == masks) return;
                break;
            default:
                if ((e & 3) == 0) l.e[i] = (uint8_t)((k + 4) << 2 | 1);
                else l.e[i++] = 0;
                for (int j = 0; j < 4; j++, k++) {
                    if (get_bit(b)) {
                        if (l.start > 0) l.e[--l.start] = (uint8_t)(k << 2 | 3);
                    } else {
                        if (nnz < 64) nz[nnz++] = (uint8_t)(k & 63);
                        r[k & 63] = get_bit(b) ? (int8_t)-mask : mask;
                        if (done++ == masks) return;
                    }
                }
                break;
            }
        }
    }
}

/* ---- the IDCT (0x10001930; inter 0x100021c0, scaled 0x10001d50) ---- */

/* out: 64 values, each the low byte of (x + 0x7f) >> 8 (unclamped) */
static void idct(const int16_t in[64], const uint8_t *scan, const int32_t *quant, uint8_t out[64])
{
    int32_t t[64], c[64];
    /* the coefficients from list order to their places, dequantized as the columns read them */
    int16_t blk[64] = { 0 };
    for (int k = 0; k < 64; k++) blk[scan[k] & 63] = in[k];
    for (int i = 0; i < 64; i++) c[i] = blk[i];
    for (int x = 0; x < 8; x++) {
        const int32_t *s = c + x;
        const int32_t *q = quant + x;
        int32_t *d = t + x;
        if (!s[8] && !s[16] && !s[24] && !s[32] && !s[40] && !s[48] && !s[56]) {
            int32_t v = s[0] * q[0] >> 11;
            for (int y = 0; y < 8; y++) d[8 * y] = v;
            continue;
        }
        int32_t a0 = s[0] * q[0] >> 11, a4 = s[32] * q[32] >> 11;
        int32_t a2 = s[16] * q[16] >> 11, a6 = s[48] * q[48] >> 11;
        int32_t e0 = a0 + a4, e1 = a0 - a4, e2 = a2 + a6;
        int32_t s0 = e2 + e0, s3 = e0 - e2;
        int32_t e3 = ((a2 - a6) * 0xb50 >> 11) - e2;
        int32_t s1 = e3 + e1, s2 = e1 - e3;
        int32_t a3 = s[24] * q[24] >> 11, a5 = s[40] * q[40] >> 11;
        int32_t a7 = s[56] * q[56] >> 11, a1 = s[8] * q[8] >> 11;
        int32_t o0 = a3 + a5, o1 = a5 - a3, o2 = a7 + a1, o3 = a1 - a7;
        int32_t f0 = o2 + o0;
        int32_t f1 = (o3 + o1) * 0xec8 >> 11;
        int32_t f2 = ((o1 * -0x14e8 >> 11) - f0) + f1;
        int32_t f3 = ((o2 - o0) * 0xb50 >> 11) - f2;
        int32_t f4 = ((o3 * 0x8a9 >> 11) - f1) + f3;
        d[0] = s0 + f0, d[56] = s0 - f0;
        d[8] = f2 + s1, d[48] = s1 - f2;
        d[16] = f3 + s2, d[40] = s2 - f3;
        d[32] = f4 + s3, d[24] = s3 - f4;
    }
    for (int y = 0; y < 8; y++) {
        const int32_t *r = t + 8 * y;
        uint8_t *o = out + 8 * y;
        int32_t e0 = r[0] + r[4], e1 = r[0] - r[4], e2 = r[2] + r[6];
        int32_t s0 = e2 + e0, s3 = e0 - e2;
        int32_t e3 = ((r[2] - r[6]) * 0xb50 >> 11) - e2;
        int32_t s1 = e3 + e1, s2 = e1 - e3;
        int32_t o0 = r[3] + r[5], o1 = r[5] - r[3], o2 = r[7] + r[1], o3 = r[1] - r[7];
        int32_t f0 = o2 + o0;
        int32_t f1 = (o3 + o1) * 0xec8 >> 11;
        int32_t f2 = ((o1 * -0x14e8 >> 11) - f0) + f1;
        int32_t f3 = ((o2 - o0) * 0xb50 >> 11) - f2;
        int32_t f4 = ((o3 * 0x8a9 >> 11) - f1) + f3;
        o[0] = (uint8_t)((uint32_t)(f0 + 0x7f + s0) >> 8);
        o[7] = (uint8_t)((uint32_t)((s0 - f0) + 0x7f) >> 8);
        o[1] = (uint8_t)((uint32_t)(f2 + 0x7f + s1) >> 8);
        o[6] = (uint8_t)((uint32_t)((s1 - f2) + 0x7f) >> 8);
        o[2] = (uint8_t)((uint32_t)(f3 + 0x7f + s2) >> 8);
        o[5] = (uint8_t)((uint32_t)((s2 - f3) + 0x7f) >> 8);
        o[3] = (uint8_t)((uint32_t)(s3 + 0x7f + f4) >> 8);
        o[4] = (uint8_t)((uint32_t)((s3 - f4) + 0x7f) >> 8);
    }
}

/* ---- the movie ---- */

/* A track's decoder (0x10028890) */
typedef struct {
    uint32_t rate, flags, channels;
    uint32_t len, overlap, nbands;
    uint32_t bands[27];
    float root;
    bool first;
    int16_t prev[1024], block[8192];
    int16_t *out;               /* the frame's samples */
    uint32_t count, cap;
} Track;

struct BinkMovie {
    VfsFile *f;
    uint32_t version, frames, width, height, num, den, flags, ntracks;
    uint32_t *offsets;      /* frames + 1, bit 0 the key frame */
    uint32_t frame;
    uint8_t *packet;
    size_t packet_cap;
    /* two frame buffers (luma, then the two chroma planes), the current shown */
    uint8_t *buf[2];
    int cur;
    uint32_t stride, ph, cw, ch;    /* the luma plane's stride and rows, a chroma plane's */
    Bundle bundles[NBUNDLES];
    Track track;
};

uint32_t bink_frames(const BinkMovie *m) { return m->frames; }
uint32_t bink_width(const BinkMovie *m) { return m->width; }
uint32_t bink_height(const BinkMovie *m) { return m->height; }
uint32_t bink_frame(const BinkMovie *m) { return m->frame; }
void bink_rate(const BinkMovie *m, uint32_t *num, uint32_t *den) { *num = m->num, *den = m->den; }
uint32_t bink_audio_rate(const BinkMovie *m) { return m->ntracks ? m->track.rate : 0; }
uint32_t bink_audio_channels(const BinkMovie *m) { return m->track.channels; }
void bink_next_frame(BinkMovie *m) { if (m->frame + 1 < m->frames) m->frame++; }

const int16_t *bink_audio(const BinkMovie *m, uint32_t *count)
{
    *count = m->track.count;
    return m->track.out;
}

const uint8_t *bink_plane(const BinkMovie *m, int plane, uint32_t *stride)
{
    const uint8_t *y = m->buf[m->cur];
    *stride = plane ? m->stride / 2 : m->stride;
    if (!plane) return y;
    /* the buffer holds Cr first, then Cb (the blit's reading, docs/formats/bink.md) */
    return y + (size_t)m->stride * m->ph + (plane == 1 ? (size_t)(m->stride / 2) * (m->ph / 2) : 0);
}

/* ---- a plane (0x10002860) ---- */

typedef struct {
    uint8_t *base;              /* the buffer (for the motion's reads, kept inside it) */
    size_t size;
    const uint8_t *scan, *patterns;
    const int32_t *qintra, *qinter;
} Ctx;

/* 8×8 from the reference at p (clamped to the buffer) */
static void block_from(const Ctx *x, const uint8_t *ref, ptrdiff_t off, uint32_t stride, uint8_t out[64])
{
    for (int y = 0; y < 8; y++)
        for (int i = 0; i < 8; i++) {
            ptrdiff_t at = off + (ptrdiff_t)y * stride + i;
            out[8 * y + i] = at >= 0 && (size_t)at < x->size ? ref[at] : 0;
        }
}

static void put8(uint8_t *dst, uint32_t stride, const uint8_t b[64])
{
    for (int y = 0; y < 8; y++) memcpy(dst + (size_t)y * stride, b + 8 * y, 8);
}

/* 8×8 doubled to 16×16 */
static void put16(uint8_t *dst, uint32_t stride, const uint8_t b[64])
{
    for (int y = 0; y < 16; y++)
        for (int i = 0; i < 16; i++) dst[(size_t)y * stride + i] = b[8 * (y >> 1) + (i >> 1)];
}

/* the run block's 64 pixels into tmp: up to 63 by runs, the last alone */
static void run_block(Bits *b, Bundle *bu, const uint8_t *patterns, uint8_t tmp[64])
{
    const uint8_t *pat = patterns + 64 * get_bits(b, 4);
    int i = 0;
    do {
        bool same = get_bit(b);
        uint8_t c = same ? take8(&bu[B_COLOURS]) : 0;
        int r = take8(&bu[B_RUNS]) + 1;
        for (; r; r--, i++) {
            uint8_t at = i < 128 ? pat[i] : 0;    /* past the pattern: the next one's bytes, as the original */
            uint8_t v = same ? c : take8(&bu[B_COLOURS]);
            if (at < 64) tmp[at] = v;
        }
    } while (i < 63);
    if (i == 63) tmp[pat[63] & 63] = take8(&bu[B_COLOURS]);
}

static void pattern_block(Bundle *bu, uint8_t tmp[64])
{
    uint8_t c0 = take8(&bu[B_COLOURS]), c1 = take8(&bu[B_COLOURS]);
    for (int y = 0; y < 8; y++) {
        uint8_t p = take8(&bu[B_PATTERNS]);
        for (int i = 0; i < 8; i++) tmp[8 * y + i] = (p >> i) & 1 ? c1 : c0;
    }
}

static uint8_t *plane(const Ctx *x, Bits *b, Bundle *bu, ColourHigh *ch, uint8_t *dst, const uint8_t *ref, uint32_t w,
                      uint32_t h, uint32_t stride)
{
    bundle_init(&bu[B_TYPES], w, 4, 1, false);
    bundle_init(&bu[B_SUBTYPES], w >> 1, 4, 1, false);
    bundle_init(&bu[B_COLOURS], w, 8, 64, false);
    bundle_init(&bu[B_PATTERNS], w, 8, 8, false);
    bundle_init(&bu[B_XOFF], w, 5, 1, true);
    bundle_init(&bu[B_YOFF], w, 5, 1, true);
    bundle_init(&bu[B_INTRA_DC], w, 11, 1, false);
    bundle_init(&bu[B_INTER_DC], w, 11, 1, true);
    bundle_init(&bu[B_RUNS], w, 4, 0x30, false);
    read_tree(b, &bu[B_TYPES].tree);
    read_tree(b, &bu[B_SUBTYPES].tree);
    for (int i = 0; i < 16; i++) read_tree(b, &ch->hi[i]);
    read_tree(b, &bu[B_COLOURS].tree);
    ch->last = 0;
    read_tree(b, &bu[B_PATTERNS].tree);
    read_tree(b, &bu[B_XOFF].tree);
    read_tree(b, &bu[B_YOFF].tree);
    read_tree(b, &bu[B_RUNS].tree);
    uint8_t tmp[64] = { 0 }, mv[64];
    int16_t coef[64];
    int8_t res[64];
    for (uint32_t by = 0; by < h; by += 8) {
        read_types(&bu[B_TYPES], b, true);
        read_types(&bu[B_SUBTYPES], b, true);
        read_colours(&bu[B_COLOURS], ch, b);
        read_patterns(&bu[B_PATTERNS], b);
        read_motion(&bu[B_XOFF], b);
        read_motion(&bu[B_YOFF], b);
        read_dc(&bu[B_INTRA_DC], b);
        read_dc(&bu[B_INTER_DC], b);
        read_types(&bu[B_RUNS], b, false);
        for (uint32_t bx = 0; bx < w; bx += 8) {
            size_t at = (size_t)by * stride + bx;
            uint8_t *d = dst + at;
            const uint8_t *r = ref + at;
            ptrdiff_t ref_at = (ptrdiff_t)(r - x->base);
            unsigned type = take8(&bu[B_TYPES]);
            switch (type) {
            case 0:     /* skip */
                for (int y = 0; y < 8; y++) memcpy(d + (size_t)y * stride, r + (size_t)y * stride, 8);
                break;
            case 1:     /* scaled: on even rows a 16×16 block, on odd ones nothing; two blocks passed */
                if (!(by & 8)) {
                    switch (take8(&bu[B_SUBTYPES])) {
                    case 3:
                        run_block(b, bu, x->patterns, tmp);
                        put16(d, stride, tmp);
                        break;
                    case 5:
                        coef[0] = take16(&bu[B_INTRA_DC]);
                        read_coefs(b, coef);
                        idct(coef, x->scan, x->qintra + 64 * get_bits(b, 4), mv);
                        put16(d, stride, mv);
                        break;
                    case 6: {
                        uint8_t c = take8(&bu[B_COLOURS]);
                        for (int y = 0; y < 16; y++) memset(d + (size_t)y * stride, c, 16);
                        break;
                    }
                    case 8:
                        pattern_block(bu, mv);
                        put16(d, stride, mv);
                        break;
                    case 9:
                        for (int i = 0; i < 64; i++) mv[i] = take8(&bu[B_COLOURS]);
                        put16(d, stride, mv);
                        break;
                    }
                }
                bx += 8;
                break;
            case 2: {   /* motion */
                int dx = (int8_t)take8(&bu[B_XOFF]), dy = (int8_t)take8(&bu[B_YOFF]);
                block_from(x, x->base, ref_at + dx + (ptrdiff_t)dy * stride, stride, mv);
                put8(d, stride, mv);
                break;
            }
            case 3:     /* run */
                run_block(b, bu, x->patterns, tmp);
                put8(d, stride, tmp);
                break;
            case 4: {   /* residue */
                int dx = (int8_t)take8(&bu[B_XOFF]), dy = (int8_t)take8(&bu[B_YOFF]);
                block_from(x, x->base, ref_at + dx + (ptrdiff_t)dy * stride, stride, mv);
                read_residue(b, res, get_bits(b, 7));
                for (int k = 0; k < 64; k++) mv[x->scan[k] & 63] = (uint8_t)(mv[x->scan[k] & 63] + res[k]);
                put8(d, stride, mv);
                break;
            }
            case 5:     /* intra */
                coef[0] = take16(&bu[B_INTRA_DC]);
                read_coefs(b, coef);
                idct(coef, x->scan, x->qintra + 64 * get_bits(b, 4), mv);
                put8(d, stride, mv);
                break;
            case 6: {   /* fill */
                uint8_t c = take8(&bu[B_COLOURS]);
                for (int y = 0; y < 8; y++) memset(d + (size_t)y * stride, c, 8);
                break;
            }
            case 7: {   /* inter */
                int dx = (int8_t)take8(&bu[B_XOFF]), dy = (int8_t)take8(&bu[B_YOFF]);
                block_from(x, x->base, ref_at + dx + (ptrdiff_t)dy * stride, stride, mv);
                coef[0] = take16(&bu[B_INTER_DC]);
                read_coefs(b, coef);
                uint8_t add[64];
                idct(coef, x->scan, x->qinter + 64 * get_bits(b, 4), add);
                for (int i = 0; i < 64; i++) mv[i] = (uint8_t)(mv[i] + add[i]);
                put8(d, stride, mv);
                break;
            }
            case 8:     /* pattern */
                pattern_block(bu, mv);
                put8(d, stride, mv);
                break;
            case 9:     /* raw */
                for (int i = 0; i < 64; i++) mv[i] = take8(&bu[B_COLOURS]);
                put8(d, stride, mv);
                break;
            }
        }
    }
    return (uint8_t *)b->p;
}

/* ExpandBink 0x100026e0 */
static void expand(BinkMovie *m, const uint8_t *data, size_t len)
{
    uint8_t *dst = m->buf[m->cur ^ 1], *ref = m->buf[m->cur];
    Ctx x = { 0 };
    /* the reference's buffer for the motion (both buffers are as big) */
    x.base = ref;
    x.size = (size_t)m->stride * m->ph + 2 * (size_t)m->cw * m->ch;
    x.scan = dll_data(DLL_SCAN, 64);
    x.patterns = dll_data(DLL_PATTERNS, 16 * 64 + 64);
    x.qintra = (const int32_t *)dll_data(DLL_QUANT_INTRA, 16 * 64 * 4);
    x.qinter = (const int32_t *)dll_data(DLL_QUANT_INTER, 16 * 64 * 4);
    if (!x.scan || !x.patterns || !x.qintra || !x.qinter) return;
    static ColourHigh ch;
    const uint8_t *end = data + len, *p = data;
    if (m->flags & 0x100000) {
        if (p + 4 > end) return;
        p += rd32(p);   /* the alpha plane: not in Oni's movies */
    }
    const uint8_t *chroma = NULL;
    if (!(m->flags & 0x8000)) {
        if (p + 4 > end) return;
        chroma = p + rd32(p);
        p += 4;
    }
    uint32_t w = (m->width + 7) & ~7u, h = (m->height + 7) & ~7u;
    Bits b = { p, end, 0, 0 };
    const uint8_t *next = plane(&x, &b, m->bundles, &ch, dst, ref, w, h, m->stride);
    if (m->flags & 0x20000) return;
    if (chroma) next = chroma;
    size_t ysize = (size_t)m->stride * m->ph;
    uint32_t cw = (((m->width + 1) >> 1) + 7) & ~7u, chh = (((m->height + 1) >> 1) + 7) & ~7u;
    uint32_t cs = m->stride / 2;
    for (int k = 0; k < 2; k++) {
        size_t off = ysize + (size_t)k * cs * (m->ph / 2);
        Bits cb = { next < end ? next : end, end, 0, 0 };
        next = plane(&x, &cb, m->bundles, &ch, dst + off, ref + off, cw, chh, cs);
    }
}

/* ---- BinkCopyToBuffer 0x10009040, 32 bits (_YUV_blit_32bpp 0x1000d180): YUV_init 0x1000b4e0's tables
   (its C path; the MMX blitters it may pick instead round their own way). Luma clamped to 16..235,
   then × 0x950a >> 15; each chroma term (c − 128) × its factor >> 15 (toward zero): R from Cr × 0xcc4b,
   G from Cb × 0x3225 and Cr × 0x680f (subtracted), B from Cb × 0x10235; the sums clamped to 0..255.
   A chroma sample covers 2×2 pixels. ---- */

static int32_t div15(int32_t v) { return v / 32768; }   /* (v + (v >> 31 & 0x7fff)) >> 15 */

void bink_copy_rgba(const BinkMovie *m, uint8_t *rgba)
{
    static int32_t ty[256], tr[256], tgu[256], tgv[256], tb[256];
    static bool made;
    if (!made) {
        for (int i = 0; i < 256; i++) {
            int y = i < 16 ? 16 : i > 235 ? 235 : i;
            ty[i] = ((y - 16) * 0x950a) >> 15;
            tr[i] = div15(-0x662580 + i * 0xcc4b);
            tgu[i] = div15(0x191280 - i * 0x3225);
            tgv[i] = div15(0x340780 - i * 0x680f);
            tb[i] = div15(-0x811a80 + i * 0x10235);
        }
        made = true;
    }
    uint32_t ys, us, vs;
    const uint8_t *y = bink_plane(m, 0, &ys), *u = bink_plane(m, 1, &us), *v = bink_plane(m, 2, &vs);
    for (uint32_t j = 0; j < m->height; j++)
        for (uint32_t i = 0; i < m->width; i++) {
            int32_t l = ty[y[(size_t)j * ys + i]];
            uint8_t cb = u[(size_t)(j >> 1) * us + (i >> 1)], cr = v[(size_t)(j >> 1) * vs + (i >> 1)];
            int32_t c[3] = { l + tr[cr], l + tgu[cb] + tgv[cr], l + tb[cb] };
            uint8_t *o = rgba + 4 * ((size_t)j * m->width + i);
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)(c[k] < 0 ? 0 : c[k] > 255 ? 255 : c[k]);
            o[3] = 255;
        }
}

/* ---- the file ---- */

static bool fail(char *err, size_t cap, const char *rel, const char *msg)
{
    if (err) snprintf(err, cap, "%s: %s", rel, msg);
    return false;
}

void bink_close(BinkMovie *m)
{
    if (!m) return;
    if (m->f) vfs_close(m->f);
    free(m->offsets);
    free(m->packet);
    free(m->buf[0]);
    free(m->buf[1]);
    for (int i = 0; i < NBUNDLES; i++) free(m->bundles[i].data);
    free(m->track.out);
    free(m);
}

BinkMovie *bink_open(const char *rel, char *err, size_t errcap)
{
    if (!dll_loaded()) { fail(err, errcap, DLL_PATH, "not loaded"); return NULL; }
    BinkMovie *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->f = vfs_open(rel);
    uint8_t h[0x2c];
    if (!m->f || vfs_read_at(m->f, 0, h, sizeof h) != (int64_t)sizeof h) {
        fail(err, errcap, rel, "not found");
        goto bad;
    }
    /* BinkOpen 0x10007f00: "BIK" and f..i; only i is Oni's */
    if (memcmp(h, "BIKi", 4)) {
        fail(err, errcap, rel, "not a Bink file of version i");
        goto bad;
    }
    m->version = h[3];
    m->frames = rd32(h + 8);
    m->width = rd32(h + 0x14), m->height = rd32(h + 0x18);
    m->num = rd32(h + 0x1c), m->den = rd32(h + 0x20);
    m->flags = rd32(h + 0x24);
    m->ntracks = rd32(h + 0x28);
    if (!m->frames || !m->width || !m->height || m->width > 4096 || m->height > 4096 || m->ntracks > 16) {
        fail(err, errcap, rel, "bad header");
        goto bad;
    }
    size_t idx = 0x2c + 12 * (size_t)m->ntracks;
    uint8_t *t = malloc(12 * (size_t)m->ntracks + 4 * ((size_t)m->frames + 1));
    if (!t || vfs_read_at(m->f, 0x2c, t, 12 * (size_t)m->ntracks + 4 * ((size_t)m->frames + 1)) < 0) {
        free(t);
        fail(err, errcap, rel, "short file");
        goto bad;
    }
    if (m->ntracks) {
        uint32_t rf = rd32(t + 4 * m->ntracks);
        m->track.rate = rf & 0xffff;
        m->track.flags = rf >> 16;
        m->track.channels = (m->track.flags & 0x2000) ? 2 : 1;
    }
    m->offsets = malloc(4 * ((size_t)m->frames + 1));
    for (uint32_t i = 0; m->offsets && i <= m->frames; i++) m->offsets[i] = rd32(t + 12 * m->ntracks + 4 * i);
    free(t);
    (void)idx;
    if (!m->offsets) goto bad;
    /* the planes: luma rows and stride twice the chroma's, rounded to 8 */
    m->cw = (((m->width + 1) >> 1) + 7) & ~7u;
    m->ch = (((m->height + 1) >> 1) + 7) & ~7u;
    m->stride = 2 * m->cw;
    m->ph = 2 * m->ch;
    size_t fsize = (size_t)m->stride * m->ph + 2 * (size_t)m->cw * m->ch;
    m->buf[0] = calloc(1, fsize), m->buf[1] = calloc(1, fsize);
    for (int i = 0; i < NBUNDLES; i++) {
        m->bundles[i].cap = (size_t)2 * (0x200 + (m->stride >> 3) * 64) + 64;
        m->bundles[i].data = calloc(1, m->bundles[i].cap);
        if (!m->bundles[i].data) goto bad;
    }
    if (!m->buf[0] || !m->buf[1]) goto bad;
    return m;
bad:
    bink_close(m);
    return NULL;
}

static void decode_audio(Track *t, const uint8_t *p, size_t len);

bool bink_do_frame(BinkMovie *m)
{
    uint32_t a = m->offsets[m->frame] & ~1u, z = m->offsets[m->frame + 1] & ~1u;
    if (z < a) return false;
    size_t len = z - a;
    if (len + 64 > m->packet_cap) {
        free(m->packet);
        m->packet_cap = len + 64;
        m->packet = malloc(m->packet_cap);
        if (!m->packet) { m->packet_cap = 0; return false; }
    }
    if (vfs_read_at(m->f, a, m->packet, len) != (int64_t)len) return false;
    memset(m->packet + len, 0, 64);
    const uint8_t *p = m->packet, *end = m->packet + len;
    m->track.count = 0;
    for (uint32_t i = 0; i < m->ntracks && p + 4 <= end; i++) {
        uint32_t size = rd32(p);
        p += 4;
        if (size > (size_t)(end - p)) return false;
        if (i == 0 && size >= 4) decode_audio(&m->track, p, size);
        p += size;
    }
    expand(m, p, (size_t)(end - p));
    m->cur ^= 1;
    return true;
}

/* ---- the audio ---- */

enum {
    DLL_CRITICAL = 0x1003e574,      /* 25 frequencies */
    DLL_AUDIO_RLE = 0x1003e704,     /* 16 runs, × 8 */
    DLL_EXPONENTS = 0x1003e718,     /* 32 doubles */
};

/* 0x10028890: the frame (2,048 samples at 44,100 Hz and over, 1,024 from 22,050, else 512) times the
   channels, interleaved; the overlap a 16th; the bands from the critical frequencies below half the rate
   times the channels (the original's) */
static void track_init(Track *t)
{
    uint32_t half = (t->rate * t->channels + 1) >> 1;
    t->len = (t->rate >= 44100 ? 2048u : t->rate >= 22050 ? 1024u : 512u) * t->channels;
    if (t->len > 8192) t->len = 8192;
    t->overlap = t->len >> 4;
    t->root = (float)(2.0 / (double)sqrtf((float)t->len));     /* 0x10037d60 */
    t->nbands = 0;
    for (int i = 0; i < 25; i++) {
        uint32_t f = dll_u32(DLL_CRITICAL + 4 * (uint32_t)i);
        if (half <= f) break;
        t->bands[t->nbands++] = (uint32_t)((uint64_t)f * (t->len >> 1) / half);
    }
    t->bands[t->nbands] = t->len >> 1;
    t->first = true;
}

/* 0x10028e70: 23 bits of mantissa, 5 of exponent (the DLL's powers of two), a sign */
static float float29(uint32_t v)
{
    const uint8_t *e = dll_data(DLL_EXPONENTS + 8 * (v & 31), 8);
    double scale = 0.0;
    if (e) memcpy(&scale, e, 8);
    double f = (double)((v >> 5) & 0x7fffff) * scale;
    return (float)((v & 0x10000000) ? -f : f);
}

/* The inverse real transform (0x100241e0, Ooura's rdft with isgn −1, unscaled): a[0] is R[0], a[1]
   R[n/2], a[2j] and a[2j+1] R[j] and I[j]; out[k] = (R[0] + R[n/2] cos(pi k)) / 2 + the sum of
   R[j] cos(2 pi j k / n) + I[j] sin(2 pi j k / n). The port's own radix-2 FFT: the same transform, but
   the original's split-radix butterflies round differently (the lowest bits of a sample). */
static void irdft(float *a, uint32_t n)
{
    static float re[8192], im[8192];
    re[0] = a[0], im[0] = 0.0f;
    re[n / 2] = a[1], im[n / 2] = 0.0f;
    for (uint32_t j = 1; j < n / 2; j++) {
        re[j] = a[2 * j], im[j] = -a[2 * j + 1];
        re[n - j] = a[2 * j], im[n - j] = a[2 * j + 1];
    }
    /* bit reversal, then the butterflies of the inverse (e^{+i}) */
    for (uint32_t i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (uint32_t len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * 3.14159265358979323846 / (double)len;
        for (uint32_t k = 0; k < len / 2; k++) {
            float wr = (float)cos(ang * k), wi = (float)sin(ang * k);
            for (uint32_t i = k; i < n; i += len) {
                uint32_t j = i + len / 2;
                float xr = re[j] * wr - im[j] * wi, xi = re[j] * wi + im[j] * wr;
                re[j] = re[i] - xr, im[j] = im[i] - xi;
                re[i] += xr, im[i] += xi;
            }
        }
    }
    for (uint32_t k = 0; k < n; k++) a[k] = 0.5f * re[k];
}

/* 0x10028ab0: one block from the bits at p (word-aligned); returns the bytes it took */
static size_t audio_block(Track *t, const uint8_t *p, const uint8_t *end)
{
    static float coef[8192];
    static int16_t q16[8192];
    float quant[26];
    Bits b = { p, end, 0, 0 };
    uint32_t w0 = next_word(&b), w1 = next_word(&b);
    coef[0] = float29(w0 & 0x1fffffff);
    coef[1] = float29((w1 & 0x3ffffff) << 3 | w0 >> 29);
    b.buf = w1 >> 26, b.left = 6;
    for (uint32_t i = 0; i < t->nbands; i++)
        quant[i] = (float)pow(10.0, (double)get_bits(&b, 8) * (double)0.0664f);     /* 0x10037d34 */
    const uint8_t *rl = dll_data(DLL_AUDIO_RLE, 16);
    memset(q16, 0, sizeof q16);
    for (uint32_t k = 2; k < t->len;) {
        uint32_t stop = get_bit(&b) ? k + 8u * (rl ? rl[get_bits(&b, 4)] : 1) : k + 8;
        if (stop > t->len) stop = t->len;
        unsigned w = get_bits(&b, 4);
        for (; k < stop; k++) {
            if (!w) { q16[k] = 0; continue; }
            int v = (int)get_bits(&b, w);
            if (v && get_bit(&b)) v = -v;
            q16[k] = (int16_t)v;
        }
    }
    for (uint32_t i = 0; i < t->nbands; i++)
        for (uint32_t j = t->bands[i]; j < t->bands[i + 1]; j++)
            if (j) coef[2 * j] = (float)q16[2 * j] * quant[i], coef[2 * j + 1] = (float)q16[2 * j + 1] * quant[i];
    irdft(coef, t->len);
    /* 0x10028eb0: times the root, rounded (the FPU's nearest), clamped */
    for (uint32_t i = 0; i < t->len; i++) {
        long v = lrint((double)coef[i] * (double)t->root);
        t->block[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    return (size_t)(b.p - p);
}

/* BinkDoFrame's part, 0x100289b0 for each block: the packet's declared bytes of samples */
static void decode_audio(Track *t, const uint8_t *p, size_t len)
{
    if (!t->len) track_init(t);
    const uint8_t *end = p + len;
    uint32_t want = rd32(p) / 2;
    p += 4;
    if (want > t->cap) {
        free(t->out);
        t->cap = want;
        t->out = malloc(2 * (size_t)want);
        if (!t->out) { t->cap = 0; return; }
    }
    while (want && p < end) {
        p += audio_block(t, p, end);
        /* the start crossfaded with the last block's end: unsigned divisions of the signed products
           (the original's: a floor, not a truncation, for the negative ones) */
        if (!t->first)
            for (uint32_t i = 0; i < t->overlap; i++) {
                uint32_t a = (uint32_t)((int32_t)(t->overlap - i) * t->prev[i]) / t->overlap;
                uint32_t c = (uint32_t)((int32_t)t->block[i] * (int32_t)i) / t->overlap;
                t->block[i] = (int16_t)(uint16_t)(a + c);
            }
        t->first = false;
        memcpy(t->prev, t->block + t->len - t->overlap, 2 * (size_t)t->overlap);
        uint32_t n = t->len - t->overlap;
        if (n > want) n = want;
        memcpy(t->out + t->count, t->block, 2 * (size_t)n);
        t->count += n, want -= n;
    }
}
