#include "crc32.h"

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    const uint8_t *p = data;
    crc = ~crc;
    while (len--) crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}
