/* TS fonts: see font.h. */
#include "font.h"

#include "tm.h"

#include <stddef.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

const void *font_family_get(const char *name)
{
    return tm_instance_get(TSFF_TAG, name);
}

int font_family_count(const void *family)
{
    return (int)rd32((const uint8_t *)family + TSFF_FONT_COUNT);
}

const void *font_family_at(const void *family, int i)
{
    if (i < 0 || i >= font_family_count(family)) return NULL;
    return tm_link((const uint8_t *)family + TSFF_FONTS + 4 * i);
}

/* 0x42ee80: the first font in the family's list with both; an empty link is passed over. */
const void *font_family_font(const void *family, uint16_t size, uint32_t style)
{
    int n = font_family_count(family);
    for (int i = 0; i < n; i++) {
        const uint8_t *f = font_family_at(family, i);
        if (f && rd16(f + TSFT_SIZE) == size && rd32(f + TSFT_STYLE) == style) return f;
    }
    return NULL;
}

/* 0x42ebd0. The walk down goes first; once it reaches 1 the walk up starts from the asked size again
   (which it tries a second time) and gives up past 18. A size above 17 that isn't found going down is
   not tried going up. Falling back to the plain style sticks, as the context keeps it. */
const void *font_family_best_font(const void *family, uint16_t *size, uint32_t *style)
{
    uint16_t s = *size;
    bool up = false;
    for (;;) {
        const void *f = font_family_font(family, s, *style);
        if (!f && (f = font_family_font(family, s, FONT_STYLE_PLAIN))) *style = FONT_STYLE_PLAIN;
        if (f) {
            *size = s;
            return f;
        }
        if (!up) {
            if (s > 1) {
                s--;
                continue;
            }
            up = true;
            s = *size;
        }
        if (s > 17) return NULL;
        s++;
    }
}

const char *font_family_breaking(const void *family)
{
    const uint8_t *l = tm_link((const uint8_t *)family + TSFF_LANGUAGE);
    return l ? (const char *)l + TSFL_STRING_SIZE : NULL;
}

void font_info(const void *font, FontInfo *o)
{
    const uint8_t *f = font;
    o->size = rd16(f + TSFT_SIZE);
    o->style = rd32(f + TSFT_STYLE);
    o->ascending = rd16(f + TSFT_ASCENDING);
    o->descending = rd16(f + TSFT_DESCENDING);
    o->leading = rd16(f + TSFT_LEADING);
    o->unknown_12 = rd16(f + TSFT_UNKNOWN_12);
    o->words = rd32(f + TSFT_WORD_COUNT);
}

uint16_t font_line_height(const void *font)
{
    const uint8_t *f = font;
    return (uint16_t)(rd16(f + TSFT_ASCENDING) + rd16(f + TSFT_DESCENDING) + rd16(f + TSFT_LEADING));
}

/* 0x42f230 (TSiCharacter_IsDoubleByte) on the first byte: bit 7 set and not 0xff. The exe has no switch
   for it, so on the US CD, whose fonts have only the glyph array of high byte 0, a byte 0x80..0xfe takes
   the next byte with it and draws nothing: the glyphs 0x80..0xfe of that array are never reached. */
uint16_t font_next_char(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    if (!(p[0] & 0x80) || p[0] == 0xff) {
        if (p[0]) (*s)++;
        return p[0];
    }
    /* The original steps over both bytes even when the second is the terminator, and reads on past the
       string; here the walk stops at it. */
    *s += p[1] ? 2 : 1;
    return (uint16_t)(p[0] << 8 | p[1]);
}

uint16_t font_string_length(const char *s)
{
    uint16_t n = 0;
    while (font_next_char(&s)) n++;
    return n;
}

bool font_glyph(const void *font, uint16_t ch, FontGlyph *o)
{
    const uint8_t *a = tm_link((const uint8_t *)font + TSFT_GLYPH_ARRAYS + 4 * (ch >> 8));
    if (!a) return false;
    const uint8_t *g = a + TSGA_GLYPH_SIZE * (ch & 0xff);
    o->code = rd16(g);
    o->advance = (int16_t)rd16(g + 2);
    o->width = rd16(g + 4);
    o->height = rd16(g + 6);
    o->x = (int16_t)rd16(g + 8);
    o->y = (int16_t)rd16(g + 10);
    o->bitmap = rd32(g + 12);
    return true;
}

/* TSiGlyph_Draw 0x42cc40 reads the bitmap at the font + 0x418 + 4 * the glyph's word, stride the width. */
const uint8_t *font_glyph_bitmap(const void *font, const FontGlyph *g)
{
    const uint8_t *f = font;
    uint64_t end = 4ull * g->bitmap + (uint64_t)g->width * g->height;
    if (end > 4ull * rd32(f + TSFT_WORD_COUNT)) return NULL;
    return f + TSFT_BITMAPS + 4 * (size_t)g->bitmap;
}

bool font_glyph_alpha(const void *font, const FontGlyph *g, uint8_t *dst)
{
    const uint8_t *b = font_glyph_bitmap(font, g);
    if (!b) return false;
    for (size_t i = 0, n = (size_t)g->width * g->height; i < n; i++) dst[i] = (uint8_t)(255 - b[i]);
    return true;
}

/* 0x42e920 counts the characters first, then walks that many: the same as walking to the 0. */
int16_t font_string_width(const void *font, const char *s)
{
    int16_t w = 0;
    uint16_t ch;
    FontGlyph g;
    while ((ch = font_next_char(&s)))
        if (font_glyph(font, ch, &g)) w = (int16_t)(w + g.advance);
    return w;
}
