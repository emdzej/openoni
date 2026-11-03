/* Text instances: see text.h. */
#include "text.h"

#include "tm.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* UUrString_Compare_NoCase 0x4266d0 and UUrString_CompareLen_NoCase 0x426700: only a..z fold (to
   upper case), on signed chars; the walk stops at a difference, a 0, or after n characters. */
static int fold(char c) { return c > '`' && c < '{' ? c - 0x20 : c; }

static int compare_nocase(const char *a, const char *b, size_t n)
{
    int x, y;
    do {
        x = fold(*a++);
        y = fold(*b++);
    } while (x == y && x && --n);
    return x - y;
}

uint32_t text_subt_count(const void *subt)
{
    return rd32((const uint8_t *)subt + SUBT_COUNT);
}

/* An entry is its name and its text, each 0-terminated, at the block (the .raw base, from
   TMrInstance_GetRawOffset 0x423e10, + SUBT_RAW) + its offset. */
bool text_subt_entry(const void *subt, uint32_t i, const char **name, const char **text)
{
    const uint8_t *s = subt;
    size_t size;
    const uint8_t *raw = tm_raw(subt, &size);
    if (!raw || i >= text_subt_count(subt)) return false;
    uint64_t at = (uint64_t)rd32(s + SUBT_RAW) + rd32(s + SUBT_ENTRIES + 4 * i);
    if (at >= size) return false;
    const uint8_t *n = raw + at, *z = memchr(n, 0, size - at);
    if (!z || z + 1 >= raw + size) return false;
    const uint8_t *t = z + 1;
    if (!memchr(t, 0, (size_t)(raw + size - t))) return false;
    *name = (const char *)n;
    *text = (const char *)t;
    return true;
}

/* 0x47f460. The digit test is the C runtime's isdigit on the entry's first byte. */
const char *text_subtitle(const char *name)
{
    const void *s = tm_instance_get(SUBT_TAG, "subtitles");
    if (!s) return NULL;
    uint32_t n = text_subt_count(s);
    for (uint32_t i = 0; i < n; i++) {
        const char *e, *t;
        if (!text_subt_entry(s, i, &e, &t)) continue;
        const char *key = e[0] >= '0' && e[0] <= '9' ? name + 1 : name;
        if (!compare_nocase(e, key, strlen(e))) return t;
    }
    return NULL;
}

/* 0x47f550 */
const char *text_message(const char *name)
{
    const void *s = tm_instance_get(SUBT_TAG, "messages");
    if (!s) return NULL;
    uint32_t n = text_subt_count(s);
    for (uint32_t i = 0; i < n; i++) {
        const char *e, *t;
        if (text_subt_entry(s, i, &e, &t) && !compare_nocase(e, name, (size_t)-1)) return t;
    }
    return NULL;
}

static void font_header(const uint8_t *p, TextIgst *o)
{
    o->family = tm_link(p + IGST_FAMILY);
    o->style = rd32(p + IGST_STYLE);
    o->color = rd32(p + IGST_COLOR);
    o->size = rd16(p + IGST_SIZE);
    o->flags = rd16(p + IGST_FLAGS);
    o->text = NULL;
    o->length = 0;
}

void text_igst(const void *igst, TextIgst *o)
{
    const uint8_t *p = igst;
    font_header(p, o);
    o->text = (const char *)p + IGST_TEXT;
    o->length = strnlen(o->text, IGST_TEXT_SIZE);
}

/* IGSA and IGPA: a u32 count at 0x14; StNA: a u16 at 0x16. The links follow at 0x18. */
uint32_t text_array_count(const void *array, uint32_t tag)
{
    const uint8_t *p = array;
    return tag == STNA_TAG ? rd16(p + STNA_COUNT) : rd32(p + IGSA_COUNT);
}

const void *text_array_at(const void *array, uint32_t tag, uint32_t i)
{
    if (i >= text_array_count(array, tag)) return NULL;
    return tm_link((const uint8_t *)array + IGSA_LINKS + 4 * i);
}

void text_igpg(const void *igpg, TextIgpg *o)
{
    const uint8_t *p = igpg;
    font_header(p, &o->font);
    o->image = tm_link(p + IGPG_IMAGE);
    o->text = tm_link(p + IGPG_TEXT);
    o->hint = tm_link(p + IGPG_HINT);
}

void text_ighh(const void *ighh, TextIghh *o)
{
    const uint8_t *p = ighh;
    o->texture[0] = tm_link(p + IGHH_LEFT_TEXTURE);
    o->texture[1] = tm_link(p + IGHH_RIGHT_TEXTURE);
    o->x[0] = (int16_t)rd16(p + IGHH_LEFT_X);
    o->y[0] = (int16_t)rd16(p + IGHH_LEFT_Y);
    o->x[1] = (int16_t)rd16(p + IGHH_RIGHT_X);
    o->y[1] = (int16_t)rd16(p + IGHH_RIGHT_Y);
    o->left_count = rd32(p + IGHH_LEFT_COUNT);
    o->right_count = rd32(p + IGHH_RIGHT_COUNT);
    o->count = rd32(p + IGHH_COUNT);
}

bool text_ighh_label(const void *ighh, uint32_t i, const char **text, size_t *length, int16_t *x, int16_t *y)
{
    const uint8_t *p = ighh;
    if (i >= rd32(p + IGHH_COUNT)) return false;
    const uint8_t *l = p + IGHH_LABELS + IGHH_LABEL_SIZE * i;
    *text = (const char *)l;
    *length = strnlen(*text, 64);
    *x = (int16_t)rd16(l + 64);
    *y = (int16_t)rd16(l + 66);
    return true;
}

const char *text_tstr(const void *tstr, size_t *length)
{
    if (length) *length = strnlen(tstr, TSTR_TEXT_SIZE);
    return tstr;
}

const void *text_string_array_get(const char *name)
{
    return tm_instance_get(STNA_TAG, name);
}

const char *text_string_array_at(const void *stna, uint32_t i, size_t *length)
{
    const void *t = text_array_at(stna, STNA_TAG, i);
    return t ? text_tstr(t, length) : NULL;
}
