#include "persist.h"

static int difficulty = 1;
static uint32_t weapons_seen, items_seen;       /* 0x5ed36c, 0x5ed370 */
static uint32_t diary_level, diary_page;        /* 0x5ed374, 0x5ed378 */

/* ONrPersist_GetDifficulty 0x50f240 */
int persist_difficulty(void)
{
    return difficulty < 0 ? 0 : difficulty > 2 ? 2 : difficulty;
}

/* ONrPersist_SetDifficulty 0x50f260 */
void persist_set_difficulty(int d)
{
    difficulty = d < 0 ? 0 : d > 2 ? 2 : d;
}

/* 0x50f4e0 */
bool persist_weapon_seen(uint16_t index) { return (weapons_seen >> (index & 0x1f)) & 1; }

/* 0x50f500 (then the block saved) */
void persist_set_weapon_seen(uint16_t index) { weapons_seen |= 1u << (index & 0x1f); }

/* 0x50f560 */
bool persist_item_seen(int type) { return (items_seen >> (type & 0x1f)) & 1; }

/* 0x50f580 (then the block saved) */
void persist_set_item_seen(int type) { items_seen |= 1u << (type & 0x1f); }

/* 0x50f5d0: a later level, or a later page of the same one (then the block saved) */
void persist_diary_read(uint32_t level, uint32_t page)
{
    if (level < diary_level || (level == diary_level && page <= diary_page)) return;
    diary_level = level;
    diary_page = page;
}
