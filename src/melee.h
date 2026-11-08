/* The AI's melee (AI2rMelee): the level's melee profiles (OBJ's "Melee Profile" type, OBJiMelee_Read
   0x4c6c10), each a list of techniques (attacks, evades, maneuvers) made of moves; and the combat
   state's melee update (AI2rMelee_Update 0x4a01f0), which picks a technique by weight and runs its
   moves: positions (AI2iMelee_Position_Update 0x4a6040), maneuvers (AI2iMelee_Maneuver_Update
   0x4a7060) and attacks, which press the attack's keys through the executor's override
   (AI2rExecutor_AttackOverride 0x498bb0) as a player would. Notes: docs/re/ai.md, "Melee".

   Techniques are weighed by AI2iMelee_WeightTechnique 0x4a2580 (the attack's timing and turn
   predicted with TRrCheckAnimationBounds); the positions are moved by AI2iMelee_Position_Update
   0x4a6040, which asks the combat behaviour whether to wait for a shield charge. */
#pragma once
#include "ai.h"

#include <stdbool.h>
#include <stdint.h>

/* The categories of move (the type's top four bits): the table at 0x52a9e8. */
enum { MELEE_ATTACK = 0, MELEE_POSITION = 1, MELEE_MANEUVER = 2, MELEE_EVADE = 3, MELEE_THROW = 4 };

typedef struct MeleeMove {
    uint32_t type;          /* category << 28 | the move */
    float p[3];
    uint16_t state;         /* +0x10: the melee state it is done from */
    const void *anim;       /* +0x14: its animation */
    float motion;           /* +0x18: the animation's motion along the technique's direction */
    const void *anim2;      /* +0x1c: a throw's victim's animation */
    float motion2;          /* +0x20 */
    bool invalid;           /* +0x24 */
} MeleeMove;

/* A transition between melee states the class can make (built by AI2rMelee_PrepareForUse from the
   exe's table at 0x541d68): the direction, the state it starts from, its steps (melee states), its
   duration, a float of the row's, the keys held and pressed, its animation, the row's animation type
   and the state the animation goes to, its motion along the direction and across. */
typedef struct MeleeTransition {
    uint8_t dir, from, nsteps, duration;    /* +0x00..+0x03 */
    uint8_t steps[4];                       /* +0x04 */
    float f08;                              /* +0x08 */
    uint32_t held, down;                    /* +0x10, +0x14 */
    const void *anim;                       /* +0x18 */
    uint16_t type, to;                      /* +0x1c, +0x1e */
    float along, across;                    /* +0x20, +0x24 */
} MeleeTransition;

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
    uint32_t damage;        /* +0x60: its attacks' most damage, and its throws' */
    float reach, near;      /* +0x64, +0x68: its last attack's reach, and that less half its +0x11c */
    uint16_t attack_frames; /* +0x6c: that attack's TRAM +0xec */
    const void *attack;     /* +0x70: its first attack's animation */
    int32_t attack_move;    /* +0x74: that attack's move in the technique, -1 none */
    float throw_dist;       /* +0x78 */
} MeleeTech;

enum { MELEE_TRANSITIONS = 55 };

enum { MELEE_TECH_MAX = 64, MELEE_MOVE_MAX = 256 };

typedef struct MeleeProfile {
    uint32_t id;
    char name[64], cls[64];
    uint32_t dodge[4];      /* +0x84..+0x90: the dodge chances (with the evades, not yet ported) */
    uint32_t block_single;  /* +0x94: the chance (%) to block an attack, fighting alone */
    uint32_t block_group;   /* +0x98: in a group of four or more (AI2rBlockFunction) */
    float factor[6];        /* +0x9c..+0xb0: the weighting's factors for an attack the target couldn't
                               block, blocks at the other height, unblockable, stagger, block stun,
                               blocked */
    float throw_danger;     /* +0xb4: the factor for a throw whose victim lands toward a danger */
    uint32_t extra;         /* +0xb8 */
    uint32_t nattack, nevade, nmaneuver, nmoves;
    MeleeTech tech[MELEE_TECH_MAX];
    MeleeMove move[MELEE_MOVE_MAX];
    const void *oncc;       /* +0xd4: the class it is prepared for */
    uint32_t ntransitions;  /* +0xdc */
    MeleeTransition transition[MELEE_TRANSITIONS];  /* +0xe0 */
    float jump_time, long_jump_time;    /* +0x978, +0x97c */
    float dist[7];          /* +0x980: [3..6] the steps forward, back, left and right; [2..6] at
                               least half the class's +0x08 */
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
/* AI2rMelee_TestLocalEnvironment 0x49fe60: the way toward the target (to 50 past it) and away from it
   (50) over the grids. */
void melee_test_local_environment(MeleeState *m, const Character *c, const Character *target);
/* AI2rStopMeleeTechnique 0x48b6d0, and when the character is hit (AI2rMelee_NotifyAnimation 0x49fd10). */
void melee_stop(MeleeState *m);
/* AI2rBlockFunction 0x48b870: whether the AI blocks an attack it could block. Draws a random number. */
bool melee_block(const AiState *a);
/* AI2rMelee_NotifyAnimation: a new animation of type for the character, after one of type left. */
void melee_notify_animation(MeleeState *m, const void *anim, uint16_t type, uint16_t left, bool victim);
