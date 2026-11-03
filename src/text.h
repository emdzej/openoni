/* The game's text instances. Format: docs/formats/text.md.

   - SUBT, subtitle arrays (SS2rRegisterTemplates 0x487080): name and text pairs in the .raw. Level 0
     has two, "subtitles" (the dialogue: SSrSubtitle_Find 0x47f460) and "messages" (the training and
     door messages: SSrMessage_Find 0x47f550).
   - IGSt, an in-game UI string (registered by 0x503600): a font family, style, colour and size, which
     of those it sets, and up to 384 bytes of text. IGSA, an array of them.
   - IGPG, an in-game UI page (objectives, items, weapons, help, diaries): the same font header, an
     image, and two IGSAs (ONiIGUI_Page_Paint 0x5029a0). IGPA, an array of pages (a level's text
     consoles, TxtC, link one).
   - IGHH, the HUD help overlay (ONiInGameUI_DisplayHelp 0x501440): two textures and their labels.
   - StNA, string arrays of TStr (registered by 0x516c00): on the CD, the animation type, state and
     flag names. The v1.0 exe reads neither but through the registration.

   Texts are the bytes of the files, 0-terminated, drawn through the fonts (font.h). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    SUBT_TAG = 0x53554254, IGST_TAG = 0x49475374, IGSA_TAG = 0x49475341, IGPG_TAG = 0x49475047,
    IGPA_TAG = 0x49475041, IGHH_TAG = 0x49474848, STNA_TAG = 0x53744e41, TSTR_TAG = 0x54537472,
};

/* Fields, from the instance data (tm.h). */
enum {
    SUBT_RAW = 0x10,            /* u32: the block's offset in the .raw (not a raw field: not converted) */
    SUBT_COUNT = 0x14,          /* u32, then a u32 per entry: its offset in the block */
    SUBT_ENTRIES = 0x18,

    IGST_FAMILY = 0x00,         /* link to the TSFF */
    IGST_STYLE = 0x04,          /* u32 */
    IGST_COLOR = 0x08,          /* u32, the shade */
    IGST_SIZE = 0x0c,           /* u16 */
    IGST_FLAGS = 0x0e,          /* u16, IGST_SETS_* */
    IGST_TEXT = 0x10,           /* 384 bytes */
    IGST_TEXT_SIZE = 384,

    IGSA_COUNT = 0x14,          /* u32, then the IGSt links (IGPA alike, with IGPG links) */
    IGSA_LINKS = 0x18,

    IGPG_IMAGE = 0x10,          /* link to a TXMP or a PSpc (or none) */
    IGPG_TEXT = 0x14,           /* IGSA, drawn for the page's first part (0x6e) */
    IGPG_HINT = 0x18,           /* IGSA, for the second (0x6f) */

    IGHH_LEFT_TEXTURE = 0x1c,   /* links to TXMP */
    IGHH_RIGHT_TEXTURE = 0x20,
    IGHH_LEFT_X = 0x24,         /* s16 each: added to points of the HUD */
    IGHH_LEFT_Y = 0x26,
    IGHH_RIGHT_X = 0x28,
    IGHH_RIGHT_Y = 0x2a,
    IGHH_LEFT_COUNT = 0x2c,     /* u32: the first this many labels go with the left texture */
    IGHH_RIGHT_COUNT = 0x30,    /* u32: the rest (inferred: the draw doesn't read it) */
    IGHH_COUNT = 0x34,          /* u32, then 0x44-byte labels: 64 bytes of text, s16 x, s16 y */
    IGHH_LABELS = 0x38,
    IGHH_LABEL_SIZE = 0x44,

    STNA_COUNT = 0x16,          /* u16, then the TStr links */
    STNA_LINKS = 0x18,
    TSTR_TEXT_SIZE = 128,       /* TStr is just the text */
};

/* What an IGSt or IGPG header sets over the text state it is drawn in (0x5028d0). */
enum { IGST_SETS_FAMILY = 1, IGST_SETS_STYLE = 2, IGST_SETS_COLOR = 4, IGST_SETS_SIZE = 8 };

/* SSrSubtitle_Find 0x47f460: the dialogue line for a sound. An entry matches if the name starts with
   it, case aside; an entry starting with a digit is matched against the name past its first character
   ("01_01_01" is c01_01_01's). NULL if none (or no "subtitles" array is loaded). */
const char *text_subtitle(const char *name);
/* SSrMessage_Find 0x47f550: the message of exactly that name, case aside, from "messages". */
const char *text_message(const char *name);
/* A SUBT's entries; false if the entry is out of range or runs past the .raw. */
uint32_t text_subt_count(const void *subt);
bool text_subt_entry(const void *subt, uint32_t i, const char **name, const char **text);

typedef struct {
    const void *family;         /* TSFF data or NULL */
    uint32_t style, color;
    uint16_t size, flags;
    const char *text;           /* IGST_TEXT, not past IGST_TEXT_SIZE */
    size_t length;
} TextIgst;
void text_igst(const void *igst, TextIgst *out);

/* IGSA, IGPA and StNA: the count, and the linked instance's data (NULL for an empty link). */
uint32_t text_array_count(const void *array, uint32_t tag);
const void *text_array_at(const void *array, uint32_t tag, uint32_t i);

typedef struct {
    TextIgst font;              /* text, length: none */
    const void *image;          /* TXMP, PSpc or NULL */
    const void *text, *hint;    /* IGSA or NULL */
} TextIgpg;
void text_igpg(const void *igpg, TextIgpg *out);

typedef struct {
    const void *texture[2];     /* left, right */
    int16_t x[2], y[2];
    uint32_t left_count, right_count, count;
} TextIghh;
void text_ighh(const void *ighh, TextIghh *out);
/* A label: its text (not past 64 bytes; *length) and position. False out of range. */
bool text_ighh_label(const void *ighh, uint32_t i, const char **text, size_t *length, int16_t *x, int16_t *y);

/* A TStr's text and its length (not past 128 bytes). */
const char *text_tstr(const void *tstr, size_t *length);
/* A string array by name ("anim_types") and a string of it; NULL if none. */
const void *text_string_array_get(const char *name);
const char *text_string_array_at(const void *stna, uint32_t i, size_t *length);
