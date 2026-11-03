/* The AI's melee (AI2rMelee): the level's melee profiles (OBJ's "Melee Profile" type, OBJiMelee_Read
   0x4c6c10), each a list of techniques (attacks, evades, maneuvers) made of moves; and the combat
   state's melee update (AI2rMelee_Update 0x4a01f0), which picks a technique by weight and runs its
   moves: positions (AI2iMelee_Position_Update 0x4a6040), maneuvers (AI2iMelee_Maneuver_Update
   0x4a7060) and attacks, which press the attack's keys through the executor's override
   (AI2rExecutor_AttackOverride 0x498bb0) as a player would. Notes: docs/re/ai.md, "Melee".

   Stand-ins until their ports: the technique weighting's prediction of what an attack would do to
   the target (AI2iMelee_WeightTechnique 0x4a2580: the base weight stays), the positions' prediction
   of the attack reaching (TRrIntersectAnimations: the profile's distances and the attack's reach),
   evades and the dodge chance, throws, jumps and crouches, and the circling's check of the other
   attackers. */
#pragma once
#include "ai.h"

#include <stdbool.h>
#include <stdint.h>

/* The categories of move (the type's top four bits): the table at 0x52a9e8. */
enum { MELEE_ATTACK = 0, MELEE_POSITION = 1, MELEE_MANEUVER = 2, MELEE_EVADE = 3, MELEE_THROW = 4 };

typedef struct {
    uint32_t type;          /* category << 28 | the move */
    float p[3];
    bool invalid;           /* +0x24 */
} MeleeMove;

/* A technique: on disk a name (64), flags, weight, importance, the repeat delay, the move count and
   the first move; then what AI2rMelee_PrepareForUse 0x4a4650 works out. */
typedef struct MeleeTech {
    char name[64];
    uint32_t flags;         /* +0x40: 1 interruptible, 2 the wide facing */
    uint32_t weight;        /* +0x44 */
    uint32_t importance;    /* +0x48 */
    uint32_t delay;         /* +0x54: ticks before it may be done again */
    uint32_t count;         /* +0x4c */
    uint32_t first;         /* +0x50 */
    uint16_t index;         /* +0x58: within its kind */
    int16_t delay_slot;     /* +0x5a: -1 none */
    uint32_t computed;      /* +0x5c: the melee state it starts from (low byte), the direction
                               (bits 8..11), 0x1000 an attack, 0x4000 no moves, 0x20000 any state,
                               0x40000 attack, 0x80000 evade, 0x100000 maneuver */
    const void *attack;     /* +0x70: its first attack's animation */
    float reach, near;      /* +0x64, +0x68: that attack's reach */
} MeleeTech;

enum { MELEE_TECH_MAX = 64, MELEE_MOVE_MAX = 256 };

typedef struct MeleeProfile {
    uint32_t id;
    char name[64], cls[64];
    uint32_t dodge[4];      /* +0x84..+0x90: the dodge chances (with the evades, not yet ported) */
    uint32_t block_single;  /* +0x94: the chance (%) to block an attack, fighting alone */
    uint32_t block_group;   /* +0x98: in a group of four or more (AI2rBlockFunction) */
    uint32_t nattack, nevade, nmaneuver, nmoves;
    MeleeTech tech[MELEE_TECH_MAX];
    MeleeMove move[MELEE_MOVE_MAX];
    bool prepared;
} MeleeProfile;

/* AI2rMelee_Initialize 0x49fa50: the attack moves' keys from the attacks' table. Once. */
void melee_initialize(void);
/* Reads the level's melee profiles; returns how many. */
int melee_load(void);
const MeleeProfile *melee_find(uint32_t id);
/* A character's melee profile, ready for it (AI2rMelee_PrepareForUse with the character's class). */
void melee_start(MeleeState *m, const MeleeProfile *p, const void *oncc);
/* AI2rMelee_Update: one tick against the target (a Chr). Returns false if there was nothing to do
   (no technique). */
bool melee_update(AiState *a, Character *c, const Character *target);
/* AI2rStopMeleeTechnique 0x48b6d0, and when the character is hit (AI2rMelee_NotifyAnimation 0x49fd10). */
void melee_stop(MeleeState *m);
/* AI2rBlockFunction 0x48b870: whether the AI blocks an attack it could block. Draws a random number. */
bool melee_block(const AiState *a);
/* AI2rMelee_NotifyAnimation: a new animation of type for the character. */
void melee_notify_animation(MeleeState *m, uint16_t type, bool victim);
