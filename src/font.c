/* TS fonts: see font.h. */
#include "font.h"

#include "tm.h"
#include "exe.h"

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

/* ---- TSrContext_FormatString 0x42e210 ---- */

enum { MAX_RUNS = FONT_RUNS, MAX_RUN = 255 };
typedef FontRun Run;

static uint16_t next_char(const char **p) { return font_next_char(p); }

static int16_t char_width(const void *tsft, uint16_t ch)
{
    FontGlyph g;
    return tsft && font_glyph(tsft, ch, &g) ? g.advance : 0;
}

static bool is_breaking(const void *family, uint16_t ch)
{
    const char *b = family ? font_family_breaking(family) : NULL;
    if (!b) return false;
    uint16_t c;
    while ((c = next_char(&b)))
        if (c == ch) return true;
    return false;
}

/* The colour code letter's colour (the table at 0x533da0: 8 bytes an entry, the letter and the ARGB; 0
   ends it). */
static bool code_colour(uint16_t ch, uint32_t *argb)
{
    for (uint32_t va = 0x533da0;; va += 8) {
        const uint8_t *e = exe_data(va, 8);
        if (!e || !rd16(e)) return false;
        if (rd16(e) == ch) {
            *argb = rd32(e + 4);
            return true;
        }
    }
}

/* The string into runs and lines within the rectangle from the pen: the runs between the colour codes
   ("[" a letter of the table sets the colour, "." ends the code, "]" puts the colour back), the lines
   broken where a run would pass the rectangle's width, at the last of the family's breaking characters
   in the run (else the run starts the next line, unless it is the line's first), at most 24 runs;
   each line aligned by the format (2 centred, 4 right, else at the pen), the lines from the pen's
   baseline down (format 8: centred in the rectangle's height). Format 0x30 is one line, as it is. */
int font_format_text(const void *family, const void *tsft, uint32_t shade, uint16_t format, const char *s,
                     const int16_t rect[4], const int16_t pen[2], FontRun *runs, int16_t *line_h)
{
    const void *f = tsft;
    FontInfo fi = { 0 };
    if (f) font_info(f, &fi);
    *line_h = (int16_t)(fi.ascending + fi.descending + fi.leading);
    int16_t width = (int16_t)(rect[2] - rect[0]);
    bool one_line = (format & 0x30) != 0;
    uint32_t colour = shade;
    int nruns = 0, lines = 0, line_first = 0;
    int16_t line_w = 0;
    bool code = false, done = false;
    const char *p = s;
    while (!done && nruns + 1 < MAX_RUNS) {
        const char *start = p, *after_break = NULL;
        uint16_t n = 0, n_break = 0;
        int16_t w = 0, w_break = 0;
        bool reset = false, ended_by_code = false, overflow = false;
        for (;;) {
            const char *q = p;
            uint16_t ch = next_char(&q);
            if (!ch) { done = true; break; }
            if (ch == '[') { code = true; p = q; ended_by_code = true; break; }
            if (ch == ']') { code = false; p = q; reset = true; ended_by_code = true; break; }
            if (code) {
                uint32_t c;
                if (ch == '.') { code = false; p = q; start = p; continue; }
                if (code_colour(ch, &c)) { colour = c; p = q; start = p; continue; }
                code = false;
            }
            int16_t cw = char_width(f, ch);
            if (!one_line && (w + cw + line_w > width || n + 1 > MAX_RUN)) { overflow = true; break; }
            w = (int16_t)(w + cw), n++, p = q;
            if (is_breaking(family, ch)) n_break = n, w_break = w, after_break = p;
        }
        if (overflow) {
            if (n_break) n = n_break, w = w_break, p = after_break;
            else if (nruns > line_first) n = 0, p = start;     /* the run goes to the next line */
            /* else: the line's first run, kept whole */
            else if (!n) { /* a character wider than the line */ uint16_t ch = next_char(&p); (void)ch; n = 1, w = char_width(f, ch); }
        }
        if (n) {
            runs[nruns] = (Run){ start, n, 0, w, colour, (uint16_t)lines };
            line_w = (int16_t)(line_w + w);
            nruns++;
        }
        if (reset) colour = shade;
        if (!ended_by_code || done) {
            /* the line ends: its runs placed */
            int16_t x = (format & 2) ? (int16_t)(rect[0] + ((width - line_w) >> 1))
                      : (format & 4) ? (int16_t)(rect[2] - line_w) : pen[0];
            for (int i = line_first; i < nruns; i++) runs[i].x = x, x = (int16_t)(x + runs[i].w);
            lines++, line_first = nruns, line_w = 0;
        }
        if (one_line && done) break;
    }
    return nruns;
}
