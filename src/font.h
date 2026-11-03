/* TS, the text system's fonts (BFW_TextSystem; TSrRegisterTemplates 0x42f250). A font family (TSFF) lists
   its fonts (TSFT), one per size and style; a font holds 256 links to glyph arrays (TSGA), indexed by a
   character's high byte, and the glyphs' bitmaps; a glyph array holds 256 glyphs, indexed by the low
   byte. A bitmap is one byte per pixel, 0 where the glyph is inked, 255 where it isn't. The family also
   links a TSFL, the language's line breaking characters. Format: docs/formats/font.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { TSFF_TAG = 0x54534646, TSFT_TAG = 0x54534654, TSGA_TAG = 0x54534741, TSFL_TAG = 0x5453464c };

/* Fields, from the instance data (tm.h). */
enum {
    TSFF_LANGUAGE = 0x10,       /* link to the TSFL */
    TSFF_FONT_COUNT = 0x14,     /* u32, then the TSFT links */
    TSFF_FONTS = 0x18,

    TSFT_SIZE = 0x06,           /* u16, in points */
    TSFT_STYLE = 0x08,          /* u32, FONT_STYLE_* */
    TSFT_ASCENDING = 0x0c,      /* u16 each: TSrFont_GetAscendingHeight 0x42eec0 */
    TSFT_DESCENDING = 0x0e,     /* TSrFont_GetDescendingHeight 0x42ef60 */
    TSFT_LEADING = 0x10,        /* TSrFont_GetLeadingHeight 0x42ef30 */
    TSFT_UNKNOWN_12 = 0x12,     /* u16, equal to the descending height on the CD; no reader found */
    TSFT_GLYPH_ARRAYS = 0x14,   /* 256 links to TSGA, by the character's high byte */
    TSFT_WORD_COUNT = 0x414,    /* u32: the bitmaps' size in 4-byte words, then the bitmaps */
    TSFT_BITMAPS = 0x418,

    TSGA_GLYPH_SIZE = 0x14,     /* 256 glyphs from the start of the data */
    TSFL_STRING_SIZE = 0x40,    /* five strings of 64 bytes (docs/formats/font.md) */
};

/* The styles of the CD's fonts (inferred from the glyphs: 1 is wider, 2 slanted). TSrContext_GetFont
   0x42e8c0 takes 3 for the context's own font. */
enum { FONT_STYLE_PLAIN = 0, FONT_STYLE_BOLD = 1, FONT_STYLE_ITALIC = 2, FONT_STYLE_CURRENT = 3 };

typedef struct {
    uint16_t size, ascending, descending, leading, unknown_12;
    uint32_t style;
    uint32_t words;             /* TSFT_WORD_COUNT */
} FontInfo;

/* A glyph, 0x14 bytes in its TSGA. +0x10 is the run time's texture cache cell (0 in the files). */
typedef struct {
    uint16_t code;              /* +0x00: the character (high byte << 8 | low), 0 for an empty slot */
    int16_t advance;            /* +0x02: the pen moves by this (TSrContext_GetStringRect 0x42e920) */
    uint16_t width, height;     /* +0x04, +0x06: the bitmap (TSrFont_GetCharacterSize 0x42eed0) */
    int16_t x, y;               /* +0x08, +0x0a: the bitmap's top left is at pen x - x, pen y - y */
    uint32_t bitmap;            /* +0x0c: in 4-byte words from TSFT_BITMAPS */
} FontGlyph;

/* TSrFontFamily_Get 0x42ee60: the family by name ("Tahoma", without the tag); NULL if none. */
const void *font_family_get(const char *name);
int font_family_count(const void *family);
const void *font_family_at(const void *family, int i);      /* NULL for an empty link */
/* TSrFontFamily_GetFont 0x42ee80: the family's font of exactly that size and style, else NULL. */
const void *font_family_font(const void *family, uint16_t size, uint32_t style);
/* TSiContext_GetBestFont 0x42ebd0, what a text context does when its size or style changes: the size
   and style, else the size in plain style, else the same from smaller sizes down to 1, then from larger
   ones up to 18. Updates size and style to the font found (unchanged if none: NULL). */
const void *font_family_best_font(const void *family, uint16_t *size, uint32_t *style);
/* The TSFL's breaking characters (the second string, what TSrContext_FormatString 0x42e210 wraps at). */
const char *font_family_breaking(const void *family);

void font_info(const void *font, FontInfo *out);
/* TSrFont_GetLineHeight 0x42ef40: ascending + descending + leading. */
uint16_t font_line_height(const void *font);

/* TSiString_GetNextCharacter (inlined in every TS reader, e.g. 0x42e920): a byte with bit 7 set, other
   than 0xff, starts a two-byte character (that byte high); the others are characters themselves.
   Returns the character at *s (0 at the end) and moves *s past it. */
uint16_t font_next_char(const char **s);
/* TSrString_GetLength 0x42f090: characters before the terminating 0. */
uint16_t font_string_length(const char *s);

/* TSiFont_GetGlyph (inlined, e.g. 0x42e920): the glyph of a character, false if the font has no glyph
   array for its high byte. A slot can be empty (all 0). */
bool font_glyph(const void *font, uint16_t ch, FontGlyph *out);
/* The glyph's bitmap, width * height bytes row by row (0 inked, 255 clear: TSiGlyph_Draw 0x42cc40 keeps
   that much of what is under it and puts the colour in for the rest), NULL if it runs past the font's
   bitmaps. */
const uint8_t *font_glyph_bitmap(const void *font, const FontGlyph *g);
/* The bitmap as coverage (255 - each byte) into dst, width * height bytes. False as above. */
bool font_glyph_alpha(const void *font, const FontGlyph *g, uint8_t *dst);

/* TSrContext_GetStringRect 0x42e920: the string's width, its glyphs' advances added up in 16 bits (a
   character without a glyph array adds nothing; there is no kerning). Its height is the line height. */
int16_t font_string_width(const void *font, const char *s);
