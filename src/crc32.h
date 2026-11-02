/* CRC-32 (IEEE 802.3, reflected, as zlib's crc32): the installer archive's file checksums. */
#pragma once
#include <stddef.h>
#include <stdint.h>

uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
static inline uint32_t crc32(const void *data, size_t len) { return crc32_update(0, data, len); }
