/* Predicting an attack: whether the attacker's animation, played from its frame, would reach the target
   as the target goes on with its own (TRrIntersectAnimations 0x42a250, TRrCheckAnimationBounds
   0x429940), from a context of the two characters (ONrSetupAnimIntersectionContext 0x4eea60). The AI's
   melee weighs its techniques with it. Notes: docs/re/ai.md, "Predicting an attack".

   The context works in the attacker's space as (sideways, forward, height): a 3x4 matrix (column-major,
   the translation last) takes the target's space to it. */
#pragma once
#include "character.h"

#include <stdbool.h>
#include <stdint.h>

/* One character's half (0x94 bytes in the original, at +0x04 and +0x98 of the context). */
typedef struct {
    const void *anim;           /* +0x00: A+0x1af8 */
    uint16_t frame;             /* +0x04: A+0x1c88 */
    const Character *c;         /* +0x08 */
    bool modified;              /* +0x0c: AI2iMelee_SetupAttackerState changed it */
    struct ChrIntersectMotion {
        float pos[3], facing;   /* +0x10: C+0x148, C+0x16c */
        bool air;               /* +0x20: A+0xf6 != 0 */
        bool jumped;            /* +0x21: A+0xf4 & 1 */
        bool jump_held;         /* +0x22: the jump key held (A+0x2168 & 0x8000000) */
        uint16_t air_ticks;     /* +0x24: A+0xf6 */
        uint16_t air_frames;    /* +0x26: the frames of air to come (SetupAttackerState) */
        float air_velocity[3];  /* +0x28: A+0xf8 */
    } now, saved;               /* +0x10, and its copy at +0x34 */
    float radius;               /* +0x58: 3 for an AI, 4 for the player */
    float gravity, fall_gravity;/* +0x5c, +0x60: ONCC +0x00, +0x04 */
    float oncc_08, oncc_0c;     /* +0x64, +0x68 */
    float lift;                 /* +0x6c: ONCC +0x10, while the jump is held */
    uint16_t fall_after;        /* +0x70: ONCC +0x14 */
    uint16_t lift_ticks;        /* +0x72: ONCC +0x16 */
    /* the run (TRrIntersectAnimations) */
    float s_pos[3];             /* +0x74 */
    uint16_t s_frame;           /* +0x80 */
    uint16_t s_air_ticks;       /* +0x82 */
    float s_velocity[3];        /* +0x84 */
    bool s_falling;             /* +0x90 */
} ChrIntersectChar;

/* Why no hit (+0x1a8). */
enum {
    CHR_ISECT_NO_POINTS = 0,    /* no attack point at or after the frame */
    CHR_ISECT_TOO_FAR = 1,      /* the line reached the target's circle beyond the point's reach */
    CHR_ISECT_MISSED = 2,       /* the line passed the target's circle */
    CHR_ISECT_TOO_HIGH = 3,     /* the point's bottom over the target's top */
    CHR_ISECT_TOO_LOW = 4,      /* the point's top under the target's bottom */
    CHR_ISECT_AWAY = 5,         /* the target behind the line */
};

typedef struct {
    bool melee;                 /* +0x000: built for the melee (the parts' tighter set, extents less 3) */
    ChrIntersectChar a, t;      /* +0x004: the attacker, +0x098: the target */
    bool m_modified;            /* +0x12c */
    float m0[12], m[12];        /* +0x130, +0x160: the target's space to the attacker's */
    float t_velocity[3];        /* +0x190: the target's motion per frame, in its space */
    float t_bottom, t_top;      /* +0x19c, +0x1a0: its parts' heights over its feet */
    float t_radius;             /* +0x1a4 */
    int result;                 /* +0x1a8: CHR_ISECT_* */
    uint32_t frames;            /* +0x1ac: the frames to the hit */
} ChrIntersect;

/* ONrSetupAnimIntersectionContext 0x4eea60 */
void chr_intersect_setup(const Character *a, const Character *t, bool melee, ChrIntersect *x);
/* 0x4eefc0: undoes AI2iMelee_SetupAttackerState's changes. */
void chr_intersect_restore(ChrIntersect *x);
/* TRrCheckAnimationBounds 0x429940: whether the attack could reach at all (a quick test). */
bool chr_intersect_bounds(ChrIntersect *x);
/* TRrIntersectAnimations 0x42a250: whether it hits; x->frames then holds when. */
bool chr_intersect(ChrIntersect *x);
/* 3x4 matrix helpers (MUrMatrix3x3_MultiplyPoint 0x433a40). */
void chr_matrix3_apply(const float m[12], const float in[3], float out[3]);
