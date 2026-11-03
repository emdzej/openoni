/* SNDD, a sound's data (the sound system's SStSoundData): a WAVEFORMATEX and the samples, which are in
   the .raw. On the CD every one is Microsoft ADPCM, which the original hands to the Windows ACM
   (msacm32); the port decodes it itself, from the published format. Format: docs/formats/sndd.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { SNDD_TAG = 0x534e4444 };

/* Fields, from the instance data (tm.h); the template's data is 0x40 bytes (SS2rRegisterTemplates
   0x487080). SNDD_DATA is a .raw offset (SSiSoundData_ProcHandler 0x4834e0 turns it into a pointer). */
enum {
    SNDD_FLAGS = 0x00,          /* SNDD_FLAG_*: how the samples are stored */
    SNDD_FORMAT = 0x04,         /* WAVEFORMATEX, with the ADPCM extension: 0x32 bytes */
    SNDD_FORMAT_TAG = 0x04,     /* 2, MS ADPCM, on the CD */
    SNDD_CHANNELS = 0x06,
    SNDD_RATE = 0x08,           /* samples per second */
    SNDD_AVG_BYTES = 0x0c,
    SNDD_BLOCK_ALIGN = 0x10,    /* bytes per ADPCM block (IMA: the packet count, 0x4841d0) */
    SNDD_BITS = 0x12,           /* bits per sample: 4 for ADPCM */
    SNDD_EXTRA = 0x14,          /* cbSize: 32 for ADPCM */
    SNDD_SAMPLES_PER_BLOCK = 0x16,
    SNDD_COEF_COUNT = 0x18,     /* 7 on the CD */
    SNDD_COEFS = 0x1a,          /* s16 pairs, as many as the count: the extension holds room for 7 */
    SNDD_DURATION = 0x36,       /* u16 ticks (1/60 s); 0 = compute it (SSrSoundData_GetDuration 0x47f790) */
    SNDD_SIZE = 0x38,           /* bytes of sample data */
    SNDD_DATA = 0x3c,           /* .raw offset of the data */
};

enum {
    SNDD_FLAG_PCM = 0x3,        /* either bit: uncompressed (0x47f790 divides the size by the sample size) */
    SNDD_FLAG_IMA = 0x4,        /* IMA4 packets of 64 samples (0x4841d0); not on the CD */
    SNDD_FLAG_MSADPCM = 0x8,    /* the ACM decodes it (SSrMSADPCM_CalculateNumSamples 0x4841f0) */
};

enum { SNDD_MAX_COEFS = 7 };

typedef struct {
    uint32_t flags;
    uint16_t format, channels, block_align, bits, samples_per_block, coef_count;
    uint32_t rate;
    uint32_t size, data;        /* bytes of sample data and its .raw offset */
    uint32_t frames;            /* samples per channel the data decodes to; 0 if the port can't decode it */
    uint16_t duration_field;    /* SNDD_DURATION as stored */
    uint32_t duration;          /* ticks: SSrSoundData_GetDuration 0x47f790 */
} SnddInfo;

void sndd_info(const void *sndd, SnddInfo *out);

/* The samples as s16, channels interleaved: up to `frames` frames into pcm. With pcm NULL, the frame
   count only. Returns the frames written (or the count), -1 if the data can't be read or decoded (IMA,
   a bad predictor index). */
int64_t sndd_decode(const void *sndd, int16_t *pcm, uint32_t frames);
