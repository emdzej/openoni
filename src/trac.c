/* TRAC animation collections: see trac.h. */
#include "trac.h"

#include "tm.h"
#include "tram.h"

#include <stdlib.h>
#include <string.h>

/* An entry, 12 bytes from 0x18: weight (u16), the state it starts from (u16), the sort key (u32: the
   animation's type << 16 | the state; the original fills it at load), the TRAM link. */
enum { E_WEIGHT = 0, E_FROM = 2, E_KEY = 4, E_TRAM = 8, ENTRY = 12, PARENT = 0x10, ALLOCATED = 0x14, COUNT = 0x16, ENTRIES = 0x18 };

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

/* UUrRandom 0x426e10: the LCG of the game's random numbers (the seed at 0x5326d4 starts at 0xfded). */
static uint32_t seed = 0xfded;
uint16_t trac_random(void)
{
    seed = seed * 0x19660du + 0x3c6ef35fu;
    return (uint16_t)(seed >> 16);
}
void trac_random_seed(uint32_t s) { seed = s; }

/* UUrLocalRandom 0x426e30: the second LCG (the seed at 0x5326d8 starts at 0xfded): the sound's permutations,
   the particles. */
static uint32_t local_seed = 0xfded;
uint16_t trac_local_random(void)
{
    local_seed = local_seed * 0x19660du + 0x3c6ef35fu;
    return (uint16_t)(local_seed >> 16);
}

static uint16_t level = 0xffff;
void trac_set_level(uint16_t l) { level = l; }

static uint8_t *entry(uint8_t *c, uint32_t i) { return c + ENTRIES + ENTRY * i; }
static const uint8_t *tram_of(const uint8_t *e) { return tm_link(e + E_TRAM); }

/* Collections the load makes for shortcuts aren't instances, so the links to and from them can't be TM
   links: they are kept here. The original puts each in as its owner's parent, with the old parent as
   its own (ALLOCATED marks it). */
typedef struct Made { struct Made *next; const uint8_t *owner; uint8_t *c; const uint8_t *parent; } Made;
static Made *made;

/* The collection the lookup falls back to. */
static const uint8_t *fallback(const uint8_t *c)
{
    for (Made *m = made; m; m = m->next) {
        if (m->owner == c) return m->c;
        if (m->c == c) return m->parent;
    }
    return tm_link(c + PARENT);
}

/* FUN_00428560, the sort's order: key ascending, then the variant descending. */
static int compare(const void *a, const void *b)
{
    const uint8_t *x = a, *y = b;
    uint32_t kx = rd32(x + E_KEY), ky = rd32(y + E_KEY);
    if (kx != ky) return kx < ky ? -1 : 1;
    uint16_t vx = rd16(tram_of(x) + TRAM_VARIENT), vy = rd16(tram_of(y) + TRAM_VARIENT);
    return vx > vy ? -1 : vx < vy;
}

/* FUN_00428430: the entries sorted, then each run of equal (key, variant) gets its weights as the
   running sums before each entry, the run's first entry holding the run's total. */
static void sort_and_weigh(uint8_t *c)
{
    uint16_t n = rd16(c + COUNT);
    qsort(c + ENTRIES, n, ENTRY, compare);
    uint32_t start = 0;
    for (uint32_t i = 0; i < n; i++) {
        bool last = i + 1 >= n;
        if (!last) {
            const uint8_t *a = entry(c, i), *b = entry(c, i + 1);
            last = rd32(a + E_KEY) != rd32(b + E_KEY) ||
                   rd16(tram_of(a) + TRAM_VARIENT) != rd16(tram_of(b) + TRAM_VARIENT);
        }
        if (!last) continue;
        uint16_t sum = 0;
        for (uint32_t k = start; k <= i; k++) {
            uint16_t w = rd16(entry(c, k) + E_WEIGHT);
            wr16(entry(c, k) + E_WEIGHT, sum);
            sum = (uint16_t)(sum + w);
        }
        wr16(entry(c, start) + E_WEIGHT, sum);
        start = i + 1;
    }
}

/* iAnimationCollection_DiskToMemory 0x428250 (load). */
bool trac_prepare(void *trac)
{
    uint8_t *c = trac;
    uint16_t n = rd16(c + COUNT);
    for (uint16_t i = 0; i < n;) {          /* entries without an animation go: the last moves in */
        if (!rd32(entry(c, i) + E_TRAM)) {
            n--;
            memmove(entry(c, i), entry(c, n), ENTRY);
            wr16(c + COUNT, n);
        } else
            i++;
    }
    uint32_t shortcuts = 0;
    for (uint16_t i = 0; i < n; i++) {
        uint8_t *e = entry(c, i);
        const uint8_t *t = tram_of(e);
        uint16_t from = rd16(t + TRAM_FROM_STATE);
        wr16(e + E_FROM, from);
        wr32(e + E_KEY, (uint32_t)rd16(t + TRAM_TYPE) << 16 | from);
        shortcuts += t[TRAM_SHORTCUT_COUNT];
    }
    sort_and_weigh(c);
    if (!shortcuts) return true;
    /* Every shortcut of every animation becomes an entry of a new collection, weight 100, from the
       shortcut's state; it is put in as the collection's parent, before the old one. */
    uint8_t *s = calloc(1, ENTRIES + ENTRY * shortcuts);
    Made *m = malloc(sizeof *m);
    if (!s || !m) return free(s), free(m), false;
    uint32_t k = 0;
    size_t rs;
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t *t = tram_of(entry(c, i));
        const uint8_t *raw = tm_raw(t, &rs);
        uint32_t off = rd32(t + TRAM_SHORTCUTS);
        for (uint8_t j = 0; raw && off && j < t[TRAM_SHORTCUT_COUNT]; j++) {
            if ((size_t)off + 8u * j + 2 > rs) break;
            uint8_t *e = entry(s, k++);
            uint16_t from = rd16(raw + off + 8u * j);
            wr16(e + E_WEIGHT, 100);
            wr16(e + E_FROM, from);
            wr32(e + E_KEY, (uint32_t)rd16(t + TRAM_TYPE) << 16 | from);
            wr32(e + E_TRAM, tm_handle_of(t));
        }
    }
    wr16(s + COUNT, (uint16_t)k);
    wr16(s + ALLOCATED, 1);
    m->parent = fallback(c);
    m->owner = c, m->c = s, m->next = made, made = m;
    sort_and_weigh(s);
    return true;
}

void trac_forget(void)
{
    while (made) { Made *n = made->next; free(made->c); free(made); made = n; }
}

/* FUN_00427e60: the first entry with the key (binary search, then back to the run's start); -1 if none. */
static int find(const uint8_t *c, uint16_t type, uint16_t state)
{
    uint32_t key = (uint32_t)type << 16 | state;
    int lo = 0, hi = rd16(c + COUNT) - 1, mid = -1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        uint32_t k = rd32(c + ENTRIES + ENTRY * m + E_KEY);
        if (k == key) { mid = m; break; }
        if (key < k) hi = m - 1;
        else lo = m + 1;
    }
    if (mid < 0) return -1;
    while (mid > 0 && rd32(c + ENTRIES + ENTRY * (mid - 1) + E_KEY) == key) mid--;
    return mid;
}

/* FUN_00427fd0. With a type, the search starts at the type and state's run; without (0), it scans
   everything for the state. Of the matching entries whose variant is within the variant asked for and
   that the level allows, it takes the first run's pick (by the random weight). If that isn't the exact
   variant, the fallback collection may have a better one. */
static const void *search(const uint8_t *c, uint16_t type, uint16_t state, uint16_t varient, int depth)
{
    const uint8_t *pick = NULL;
    if (depth > 16) return NULL;
    int i = 0;
    uint16_t n = rd16(c + COUNT);
    if (type) {
        i = find(c, type, state);
        if (i < 0) i = n;   /* 0xffff in the original: past the end */
    }
    bool found = false;
    uint32_t r = 0;
    uint16_t fvar = 0, ftype = 0;
    for (; i < n; i++) {
        const uint8_t *e = c + ENTRIES + ENTRY * i, *t = tram_of(e);
        if (rd16(e + E_FROM) == state) {
            if (type && rd16(t + TRAM_TYPE) != type) break;
            uint16_t v = rd16(t + TRAM_VARIENT);
            if ((v & ~varient) || rd16(t + TRAM_LEVEL) > level) continue;
            uint16_t w = rd16(e + E_WEIGHT);
            if (found && ((int)r < (int)w || fvar != v || ftype != rd16(t + TRAM_TYPE))) break;
            pick = t;
            if (!w) break;
            if (!found) {
                r = w ? (uint32_t)((trac_random() * (uint32_t)w) >> 16) : 0;
                fvar = v;
                ftype = rd16(t + TRAM_TYPE);
                found = true;
            }
        } else if (type)
            break;
    }
    if (pick && rd16(pick + TRAM_VARIENT) == varient) return pick;
    const uint8_t *p = fallback(c);
    if (p) {
        const void *q = search(p, type, state, varient, depth + 1);
        if (!q) q = search(p, type, TRAC_ANY_STATE, varient, depth + 1);
        if (q && (!pick || rd16(pick + TRAM_VARIENT) < rd16((const uint8_t *)q + TRAM_VARIENT))) return q;
    }
    return pick;
}

int trac_list(const void *trac, uint16_t type, uint16_t state, const void **out, int max)
{
    int n = 0;
    for (const uint8_t *c = trac; c && n < max; c = fallback(c)) {
        int i = find(c, type, state);   /* FUN_00427dd0: the run of the key */
        if (i < 0) continue;
        for (uint16_t cnt = rd16(c + COUNT); i < cnt && n < max; i++) {
            const uint8_t *e = c + ENTRIES + ENTRY * i;
            if (rd32(e + E_KEY) != ((uint32_t)type << 16 | state)) break;
            out[n++] = tram_of(e);
        }
    }
    return n;
}

/* TRrCollection_Lookup 0x427f00: the state asked for, else from any state (1). */
const void *trac_lookup(const void *trac, uint16_t type, uint16_t state, uint16_t varient)
{
    const void *t = search(trac, type, state, varient, 0);
    return t ? t : search(trac, type, TRAC_ANY_STATE, varient, 0);
}
