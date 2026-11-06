/* Bink movies (BIK version 'i', the intro and outro), decoded as RAD's binkw32.dll does: the container
   (BinkOpen 0x10007f00, BinkDoFrame 0x10009bc0), the video (ExpandBink 0x100026e0) and the audio
   (0x100289b0). The DLL's tables come from the user's copy (dll_init). docs/formats/bink.md. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct BinkMovie BinkMovie;

/* Opens a movie through the file layer (needs dll_init). NULL (err says why) if it can't. */
BinkMovie *bink_open(const char *rel, char *err, size_t errcap);
void bink_close(BinkMovie *m);

uint32_t bink_frames(const BinkMovie *m);
uint32_t bink_width(const BinkMovie *m);
uint32_t bink_height(const BinkMovie *m);
void bink_rate(const BinkMovie *m, uint32_t *num, uint32_t *den);
/* The frame bink_do_frame decodes next (from 0). */
uint32_t bink_frame(const BinkMovie *m);

/* BinkDoFrame: decodes the current frame (its audio and video); false on a read error. */
bool bink_do_frame(BinkMovie *m);
/* BinkNextFrame: the next frame becomes current. */
void bink_next_frame(BinkMovie *m);

/* The last decoded picture's planes: 0 luma (Y), 1 Cb (U) and 2 Cr (V) at half size; stride in bytes. */
const uint8_t *bink_plane(const BinkMovie *m, int plane, uint32_t *stride);

/* BinkCopyToBuffer: the last picture as RGBA (width × height × 4 bytes), the DLL's conversion. */
void bink_copy_rgba(const BinkMovie *m, uint8_t *rgba);

/* The audio track (the first): 0 rate without one. */
uint32_t bink_audio_rate(const BinkMovie *m);
uint32_t bink_audio_channels(const BinkMovie *m);
/* The samples (interleaved, 16-bit) the last bink_do_frame decoded, and their count (all channels). */
const int16_t *bink_audio(const BinkMovie *m, uint32_t *count);
