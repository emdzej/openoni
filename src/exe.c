#include "exe.h"
#include "crc32.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PE layout: e_lfanew at 0x3c -> "PE\0\0", COFF header (section count at +6, optional header size at
   +20), optional header (image base at +28), then 40-byte section headers: name[8], virtual size,
   virtual address, raw size, raw offset. */
enum { MAX_SECTIONS = 8 };

static uint8_t *image;
static size_t image_size;
static uint32_t image_base;
static struct { uint32_t va, vsize, raw, rsize; } sec[MAX_SECTIONS];
static int nsec;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static bool fail(char *err, size_t cap, const char *msg)
{
    if (err) snprintf(err, cap, "%s: %s", EXE_PATH, msg);
    exe_free();
    return false;
}

bool exe_init(char *err, size_t errcap)
{
    exe_free();
    image = vfs_read_all(EXE_PATH, &image_size);
    if (!image) return fail(err, errcap, "not found (the game program is needed for its tables)");
    if (image_size != EXE_SIZE || crc32(image, image_size) != EXE_CRC32)
        return fail(err, errcap, "unsupported version (expected Windows v1.0: 1,527,808 bytes, CRC-32 a8223704)");
    uint32_t pe = rd32(image + 0x3c);
    nsec = rd16(image + pe + 6);
    uint32_t opt = rd16(image + pe + 20);
    image_base = rd32(image + pe + 24 + 28);
    if (nsec > MAX_SECTIONS) return fail(err, errcap, "too many sections");
    for (int i = 0; i < nsec; i++) {
        const uint8_t *s = image + pe + 24 + opt + i * 40;
        sec[i].vsize = rd32(s + 8);
        sec[i].va = image_base + rd32(s + 12);
        sec[i].rsize = rd32(s + 16);
        sec[i].raw = rd32(s + 20);
    }
    return true;
}

bool exe_loaded(void) { return image != NULL; }

void exe_free(void)
{
    free(image);
    image = NULL;
    image_size = 0;
    nsec = 0;
}

const uint8_t *exe_data(uint32_t va, size_t len)
{
    for (int i = 0; i < nsec; i++) {
        uint32_t n = sec[i].rsize < sec[i].vsize ? sec[i].rsize : sec[i].vsize;
        if (va >= sec[i].va && (uint64_t)va - sec[i].va + len <= n) return image + sec[i].raw + (va - sec[i].va);
    }
    return NULL;
}

uint32_t exe_u32(uint32_t va)
{
    const uint8_t *p = exe_data(va, 4);
    return p ? rd32(p) : 0;
}

uint16_t exe_u16(uint32_t va)
{
    const uint8_t *p = exe_data(va, 2);
    return p ? rd16(p) : 0;
}

const char *exe_str(uint32_t va)
{
    for (int i = 0; i < nsec; i++) {
        uint32_t n = sec[i].rsize < sec[i].vsize ? sec[i].rsize : sec[i].vsize;
        if (va < sec[i].va || va >= sec[i].va + n) continue;
        const char *s = (const char *)image + sec[i].raw + (va - sec[i].va);
        return memchr(s, 0, sec[i].va + n - va) ? s : "";
    }
    return "";
}
