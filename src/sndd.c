/* SNDD sound data: see sndd.h. */
#include "sndd.h"

#include "tm.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* MS ADPCM (format tag 2, the published WAVE format): a block starts with a header per channel, the
   fields interleaved by channel: the predictor index (u8), the step (s16), the second sample, then the
   first (s16 each). The two header samples come out first, oldest first; then every byte is two
   nibbles, the high one first (in stereo: left, then right). */
enum { ADPCM_HEADER = 7, ADPCM_MIN_STEP = 16 };

/* x >> 8 rounding toward minus infinity, without relying on the compiler's signed shift */
static int asr8(int x) { return x >= 0 ? x / 256 : -((-x + 255) / 256); }

/* The step's adaptation per nibble, from the format's specification (not from the exe: the exe leaves
   decoding to the ACM). */
static const int16_t adapt[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };

/* Frames a block of len bytes decodes to: the two header samples and two per byte per channel; a full
   block gives its samples-per-block. The last block of a sound is short on the CD (the stored
   durations count its samples, so the ACM decodes it). */
static uint32_t adpcm_block_frames(const SnddInfo *in, uint32_t len)
{
    uint32_t head = ADPCM_HEADER * in->channels;
    if (len < head) return 0;
    uint32_t n = 2 + (len - head) * 2 / in->channels;
    return n < in->samples_per_block ? n : in->samples_per_block;
}

static uint32_t adpcm_frames(const SnddInfo *in)
{
    if (!in->block_align || !in->channels || in->channels > 2) return 0;
    uint32_t full = in->size / in->block_align, rest = in->size % in->block_align;
    return full * adpcm_block_frames(in, in->block_align) + adpcm_block_frames(in, rest);
}

void sndd_info(const void *sndd, SnddInfo *o)
{
    const uint8_t *s = sndd;
    memset(o, 0, sizeof *o);
    o->flags = rd32(s + SNDD_FLAGS);
    o->format = rd16(s + SNDD_FORMAT_TAG);
    o->channels = rd16(s + SNDD_CHANNELS);
    o->rate = rd32(s + SNDD_RATE);
    o->block_align = rd16(s + SNDD_BLOCK_ALIGN);
    o->bits = rd16(s + SNDD_BITS);
    o->size = rd32(s + SNDD_SIZE);
    o->data = rd32(s + SNDD_DATA);
    o->duration_field = rd16(s + SNDD_DURATION);
    if (o->flags & SNDD_FLAG_PCM) {
        if (o->channels && (o->bits == 8 || o->bits == 16)) o->frames = o->size / (o->channels * (o->bits / 8u));
    } else if (o->flags & SNDD_FLAG_MSADPCM) {
        o->samples_per_block = rd16(s + SNDD_SAMPLES_PER_BLOCK);
        o->coef_count = rd16(s + SNDD_COEF_COUNT);
        o->frames = adpcm_frames(o);
    }
    /* SSrSoundData_GetDuration 0x47f790: the stored ticks; else samples (of all channels) * 60 over
       channels * rate, at least 1. The original counts the ADPCM samples with acmStreamSize
       (0x4841f0); here they are the frames the decoder gives. */
    if (o->duration_field) o->duration = o->duration_field;
    else if (o->rate) {
        uint64_t t = (uint64_t)o->frames * 60 / o->rate;
        o->duration = t > 1 ? (uint32_t)t : 1;
    } else o->duration = 1;
}

/* One block: len bytes in, frames frames out (channels interleaved). False on a bad predictor index
   (the ACM would refuse the block). */
static bool adpcm_block(const SnddInfo *in, const int16_t *coefs, const uint8_t *b, uint32_t len,
                        int16_t *out, uint32_t frames)
{
    unsigned ch = in->channels;
    int c1[2], c2[2], step[2], s1[2], s2[2];
    for (unsigned c = 0; c < ch; c++) {
        unsigned p = b[c];
        if (p >= in->coef_count) return false;
        c1[c] = coefs[2 * p];
        c2[c] = coefs[2 * p + 1];
        step[c] = (int16_t)rd16(b + ch + 2 * c);
        s1[c] = (int16_t)rd16(b + 3 * ch + 2 * c);
        s2[c] = (int16_t)rd16(b + 5 * ch + 2 * c);
    }
    uint32_t f = 0;
    for (; f < 2 && f < frames; f++)
        for (unsigned c = 0; c < ch; c++) *out++ = (int16_t)(f == 0 ? s2[c] : s1[c]);
    const uint8_t *p = b + ADPCM_HEADER * ch, *end = b + len;
    unsigned c = 0, high = 1;
    for (uint32_t k = f * ch; k < frames * ch && p < end; k++) {
        int nib = high ? *p >> 4 : *p & 15;
        if (!high) p++;
        high ^= 1;
        /* the prediction from the last two samples, the coefficients in 8.8 fixed point, shifted down
           arithmetically (rounding toward minus infinity; a division would round toward 0 and drift) */
        int pred = asr8(s1[c] * c1[c] + s2[c] * c2[c]);
        int v = pred + (nib >= 8 ? nib - 16 : nib) * step[c];
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        *out++ = (int16_t)v;
        s2[c] = s1[c];
        s1[c] = v;
        step[c] = asr8(step[c] * adapt[nib]);
        if (step[c] < ADPCM_MIN_STEP) step[c] = ADPCM_MIN_STEP;
        if (++c == ch) c = 0;
    }
    return true;
}

int64_t sndd_decode(const void *sndd, int16_t *pcm, uint32_t frames)
{
    SnddInfo in;
    sndd_info(sndd, &in);
    if (!in.frames && in.size) return -1;
    if (!pcm) return in.frames;
    if (frames > in.frames) frames = in.frames;
    size_t size;
    const uint8_t *raw = tm_raw(sndd, &size);
    if (!raw || in.data > size || in.size > size - in.data) return -1;
    const uint8_t *d = raw + in.data;
    if (in.flags & SNDD_FLAG_PCM) {
        /* the channel fill (0x4851f0) copies the bytes as they are; 8-bit is unsigned, as in WAVE */
        uint32_t n = frames * in.channels;
        for (uint32_t i = 0; i < n; i++)
            pcm[i] = in.bits == 8 ? (int16_t)((d[i] - 128) << 8) : (int16_t)rd16(d + 2 * i);
        return frames;
    }
    if (!(in.flags & SNDD_FLAG_MSADPCM) || in.coef_count > SNDD_MAX_COEFS) return -1;
    int16_t coefs[2 * SNDD_MAX_COEFS];
    for (unsigned i = 0; i < 2u * in.coef_count; i++) coefs[i] = (int16_t)rd16((const uint8_t *)sndd + SNDD_COEFS + 2 * i);
    /* 0x484ef0 feeds the ACM a buffer's worth of blocks at a time; the blocks decode independently, so
       this goes block by block */
    uint32_t done = 0;
    for (uint32_t at = 0; at < in.size && done < frames; at += in.block_align) {
        uint32_t len = in.size - at < in.block_align ? in.size - at : in.block_align;
        uint32_t n = adpcm_block_frames(&in, len);
        if (n > frames - done) n = frames - done;
        if (n && !adpcm_block(&in, coefs, d + at, len, pcm + (size_t)done * in.channels, n)) return -1;
        done += n;
    }
    return done;
}
