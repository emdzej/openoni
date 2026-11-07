/* The corpses (GS+0x2f8, the level's CRSA: docs/formats/crsa.md): bodies drawn as they lie, without a
   character. The level's own come first (+0x0c of them: the dead set into its scenes); after them, as
   many as fit (+0x14 in all, 20), the AI characters' dead bodies, the oldest replaced
   (ONrGameState_UpdateCharacters 0x4dcf1c, 0x4ef340). Drawn by 0x4e1540 (render/char_draw.c). */
#pragma once
#include "akev_vis.h"
#include "character.h"

#include <stdbool.h>
#include <stdint.h>

enum { CORPSE_MAX = 64 };

typedef struct {
    char name[32];              /* +0x00: "dynamic_corpse" for a character's */
    const void *oncc;           /* +0xa0: none, an empty slot */
    float m[CHR_BONES][12];     /* +0xa4: the parts' matrices (A+0x2278's) */
    float box[6];               /* +0x434: min xyz, max xyz (C+0x1648) */
    VisList leaves;             /* +0x20: the leaves the box is in (0x440900; found at its first draw) */
    bool listed;
} Corpse;

/* 0x4d9d50 (ONrCharacter_LevelBegin 0x4d87b0): the level's corpses read, the characters' slots
   emptied. */
void corpse_level_begin(void);
/* 0x4ef340: the dead character's body as a corpse, in the next slot after the level's own. */
void corpse_add(const Character *c);
/* 0x4dcbd0's first part (trigvolume_corpse): the characters' corpses in the trigger volume emptied. */
void corpse_clear_volume(int volume);
int corpse_count(void);
const Corpse *corpse_get(int i);
/* 0x440b50 on the corpse's leaves: one of them visible. */
bool corpse_visible(int i);
