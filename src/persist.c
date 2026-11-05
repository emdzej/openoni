#include "persist.h"

#include "exe.h"

#include <stdlib.h>
#include <string.h>

enum {
    SIZE = 0x326a0, VERSION = 0xf, MARK = 0xd0d00b0eu,
    LEVELS_BITS = 0x08, GRIFFIN = 0x28, WEAPONS = 0x2c, ITEMS = 0x30, DIARY_LEVEL = 0x34, DIARY_PAGE = 0x38, QUALITY = 0x3c,
    VOLUME = 0x40, FLAGS = 0x44, DIFFICULTY = 0x48, RESOLUTION = 0x4c, DEPTH = 0x50, GAMMA = 0x54,
    PLACE = 0x58, CONTINUES = 0x60,
};

static uint8_t block[SIZE];     /* 0x5ed340 */
static const char FILE_NAME[] = "persist.dat";      /* 0x557194 */

static uint32_t get32(unsigned off) { const uint8_t *p = block + off; return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(unsigned off, uint32_t v) { uint8_t *p = block + off; p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static void putf(unsigned off, float f) { uint32_t v; memcpy(&v, &f, 4); put32(off, v); }
static float getf(unsigned off) { uint32_t v = get32(off); float f; memcpy(&f, &v, 4); return f; }

static uint8_t *(*io_load)(const char *name, size_t *size);
static bool (*io_save)(const char *name, const void *data, size_t size);

/* BFrFile_Open(persist.dat, "w") and the block written (0x4029d0) */
static void save(void) { if (io_save) io_save(FILE_NAME, block, sizeof block); }

/* The block made anew (ONrPersistance_Initialize's defaults). */
static void defaults(void)
{
    memset(block, 0, sizeof block);
    put32(0, VERSION);
    put32(4, MARK);
    put32(QUALITY, 2);          /* by the memory: 2 from 90 MB (GlobalMemoryStatus) */
    putf(VOLUME, 1.0f);
    put32(FLAGS, 2);            /* | 1 without sound (0x483530): the port has it */
    put32(DIFFICULTY, 1);
    put32(RESOLUTION, 0x1e00280);   /* 640 by 480 */
    put32(DEPTH, 0x20);
    putf(GAMMA, 0.5f);
    /* level 1's second continue, "Syndicate Warehouse" (0x554988), flags 3: one, to be ignored */
    uint8_t *c = block + CONTINUES + (1 * PERSIST_CONTINUES + 1) * PERSIST_CONTINUE_SIZE;
    const char *name = exe_str(0x554988);
    if (name) memcpy(c, name, strlen(name) < 0x3f ? strlen(name) : 0x3f);
    c[0x40] = 3;
}

/* Before persist_initialize (the tests): the defaults, kept in memory. */
static void ensure(void) { if (get32(0) != VERSION) defaults(); }

void persist_initialize(uint8_t *(*load)(const char *name, size_t *size), bool (*store)(const char *name, const void *data, size_t size))
{
    io_load = load, io_save = store;
    size_t n = 0;
    uint8_t *f = io_load ? io_load(FILE_NAME, &n) : NULL;
    if (f && n >= sizeof block) {
        memcpy(block, f, sizeof block);
        free(f);
        if (get32(0) == VERSION && get32(4) == MARK) return;
    } else
        free(f);
    defaults();
}

int persist_difficulty(void)
{
    ensure();
    int d = (int)get32(DIFFICULTY);
    return d < 0 ? 0 : d > 2 ? 2 : d;
}

void persist_set_difficulty(int d)
{
    ensure();
    put32(DIFFICULTY, (uint32_t)(d < 0 ? 0 : d > 2 ? 2 : d));
    save();
}

int persist_quality(void) { ensure(); return (int)get32(QUALITY); }
void persist_set_quality(int q) { ensure(); put32(QUALITY, (uint32_t)q); save(); }
float persist_volume(void) { ensure(); return getf(VOLUME); }
void persist_set_volume(float v) { ensure(); putf(VOLUME, v); save(); }
bool persist_subtitles(void) { ensure(); return get32(FLAGS) & 1; }
bool persist_invert_mouse(void) { ensure(); return (get32(FLAGS) >> 1) & 1; }

static void set_flag(uint32_t bit, bool on)
{
    ensure();
    put32(FLAGS, on ? get32(FLAGS) | bit : get32(FLAGS) & ~bit);
    save();
}
void persist_set_subtitles(bool on) { set_flag(1, on); }
void persist_set_invert_mouse(bool on) { set_flag(2, on); }
bool persist_won_game(void) { ensure(); return (get32(FLAGS) >> 2) & 1; }
void persist_set_won_game(void) { set_flag(4, true); }

float persist_gamma(void)
{
    ensure();
    float g = getf(GAMMA);
    return g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
}

void persist_set_gamma(float g)
{
    ensure();
    if (!(g >= 0.0f && g <= 1.0f)) return;
    putf(GAMMA, g);
    save();
}

void persist_resolution(int *w, int *h, int *depth)
{
    ensure();
    uint32_t r = get32(RESOLUTION);
    *w = (int)(r & 0xffff), *h = (int)(r >> 16), *depth = (int)get32(DEPTH);
}

bool persist_killed_griffin(void) { ensure(); return get32(GRIFFIN) != 0; }   /* 0x50f0c0 */

void persist_set_killed_griffin(bool killed)
{
    ensure();
    put32(GRIFFIN, killed);
    save();
}

bool persist_weapon_seen(uint16_t index) { ensure(); return (get32(WEAPONS) >> (index & 0x1f)) & 1; }

void persist_set_weapon_seen(uint16_t index)
{
    ensure();
    uint32_t bit = 1u << (index & 0x1f);
    if (get32(WEAPONS) & bit) return;
    put32(WEAPONS, get32(WEAPONS) | bit);
    save();
}

bool persist_item_seen(int type) { ensure(); return (get32(ITEMS) >> (type & 0x1f)) & 1; }

void persist_set_item_seen(int type)
{
    ensure();
    uint32_t bit = 1u << (type & 0x1f);
    if (get32(ITEMS) & bit) return;
    put32(ITEMS, get32(ITEMS) | bit);
    save();
}

void persist_diary_read(uint32_t level, uint32_t page)
{
    ensure();
    uint32_t l = get32(DIARY_LEVEL), p = get32(DIARY_PAGE);
    if (level < l || (level == l && page <= p)) return;
    put32(DIARY_LEVEL, level);
    put32(DIARY_PAGE, page);
    save();
}

bool persist_level_reached(int level)
{
    ensure();
    if (level == 1) return true;
    if (level < 0 || level > PERSIST_LEVELS) return false;
    return (block[LEVELS_BITS + (level >> 3)] >> (level & 7)) & 1;
}

void persist_set_level_reached(int level)
{
    ensure();
    if (level <= 1 || level > PERSIST_LEVELS) return;
    uint8_t *b = &block[LEVELS_BITS + (level >> 3)], bit = (uint8_t)(1 << (level & 7));
    if (*b & bit) return;
    *b |= bit;
    save();
}

const uint8_t *persist_continue(int level, int slot)
{
    ensure();
    if (level < 0 || level >= PERSIST_LEVELS || slot < 0 || slot >= PERSIST_CONTINUES) return NULL;
    const uint8_t *c = block + CONTINUES + (unsigned)(slot + level * PERSIST_CONTINUES) * PERSIST_CONTINUE_SIZE;
    return (c[0x40] & 1) ? c : NULL;
}

void persist_set_continue(int level, int slot, const uint8_t rec[PERSIST_CONTINUE_SIZE])
{
    ensure();
    if (level < 0 || level >= PERSIST_LEVELS || slot < 0 || slot >= PERSIST_CONTINUES) return;
    memcpy(block + CONTINUES + (unsigned)(slot + level * PERSIST_CONTINUES) * PERSIST_CONTINUE_SIZE, rec, PERSIST_CONTINUE_SIZE);
    save();
}

void persist_place(int *level, int *slot)
{
    ensure();
    *level = (int)get32(PLACE);
    *slot = (int)get32(PLACE + 4);
}

void persist_set_place(int level, int slot)
{
    ensure();
    put32(PLACE, (uint32_t)level);
    put32(PLACE + 4, (uint32_t)slot);
    save();
}
