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

typedef struct {
    const char *path;
    uint32_t size, crc;
    const char *wrong;      /* the message for another build */
    uint8_t *image;
    size_t image_size;
    struct { uint32_t va, vsize, raw, rsize; } sec[MAX_SECTIONS];
    int nsec;
} Pe;

static Pe exe = { .path = EXE_PATH, .size = EXE_SIZE, .crc = EXE_CRC32, .wrong = "unsupported version (expected Windows v1.0: 1,527,808 bytes, CRC-32 a8223704)" };
static Pe dll = { .path = DLL_PATH, .size = DLL_SIZE, .crc = DLL_CRC32, .wrong = "unsupported version (expected the one Oni 1.0 ships: 286,208 bytes, CRC-32 6696be85)" };

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void pe_free(Pe *pe)
{
    free(pe->image);
    pe->image = NULL;
    pe->image_size = 0;
    pe->nsec = 0;
}

static bool fail(Pe *pe, char *err, size_t cap, const char *msg)
{
    if (err) snprintf(err, cap, "%s: %s", pe->path, msg);
    pe_free(pe);
    return false;
}

static bool pe_load(Pe *pe, const char *missing, char *err, size_t errcap)
{
    pe_free(pe);
    pe->image = vfs_read_all(pe->path, &pe->image_size);
    if (!pe->image) return fail(pe, err, errcap, missing);
    if (pe->image_size != pe->size || crc32(pe->image, pe->image_size) != pe->crc) return fail(pe, err, errcap, pe->wrong);
    uint32_t h = rd32(pe->image + 0x3c);
    pe->nsec = rd16(pe->image + h + 6);
    uint32_t opt = rd16(pe->image + h + 20);
    uint32_t base = rd32(pe->image + h + 24 + 28);
    if (pe->nsec > MAX_SECTIONS) return fail(pe, err, errcap, "too many sections");
    for (int i = 0; i < pe->nsec; i++) {
        const uint8_t *s = pe->image + h + 24 + opt + i * 40;
        pe->sec[i].vsize = rd32(s + 8);
        pe->sec[i].va = base + rd32(s + 12);
        pe->sec[i].rsize = rd32(s + 16);
        pe->sec[i].raw = rd32(s + 20);
    }
    return true;
}

static const uint8_t *pe_data(const Pe *pe, uint32_t va, size_t len)
{
    for (int i = 0; i < pe->nsec; i++) {
        uint32_t n = pe->sec[i].rsize < pe->sec[i].vsize ? pe->sec[i].rsize : pe->sec[i].vsize;
        if (va >= pe->sec[i].va && (uint64_t)va - pe->sec[i].va + len <= n) return pe->image + pe->sec[i].raw + (va - pe->sec[i].va);
    }
    return NULL;
}

bool exe_init(char *err, size_t errcap) { return pe_load(&exe, "not found (the game program is needed for its tables)", err, errcap); }
bool exe_loaded(void) { return exe.image != NULL; }
void exe_free(void) { pe_free(&exe); }
const uint8_t *exe_data(uint32_t va, size_t len) { return pe_data(&exe, va, len); }

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
    for (int i = 0; i < exe.nsec; i++) {
        uint32_t n = exe.sec[i].rsize < exe.sec[i].vsize ? exe.sec[i].rsize : exe.sec[i].vsize;
        if (va < exe.sec[i].va || va >= exe.sec[i].va + n) continue;
        const char *s = (const char *)exe.image + exe.sec[i].raw + (va - exe.sec[i].va);
        return memchr(s, 0, exe.sec[i].va + n - va) ? s : "";
    }
    return "";
}

bool dll_init(char *err, size_t errcap) { return pe_load(&dll, "not found (the movies' decoder needs its tables)", err, errcap); }
bool dll_loaded(void) { return dll.image != NULL; }
void dll_free(void) { pe_free(&dll); }
const uint8_t *dll_data(uint32_t va, size_t len) { return pe_data(&dll, va, len); }

uint32_t dll_u32(uint32_t va)
{
    const uint8_t *p = dll_data(va, 4);
    return p ? rd32(p) : 0;
}
