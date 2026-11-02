/* DEFLATE (RFC 1951) decoding, from OpenCMR2, plus MindVision's Installer VISE variant
   (docs/formats/vise.md): plain deflate, read in 16-bit words whose two bytes are swapped. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Raw deflate stream in src -> dst. Returns the decoded size, or -1 on a malformed stream or if the output
   would exceed dstcap. *used (may be NULL) gets the number of input bytes consumed. */
int64_t inflate_raw(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap, size_t *used);

/* A VISE archive member: the bytes swapped in pairs, then inflate_raw. */
int64_t inflate_vise(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap);
