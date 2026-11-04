/* The AI's targeting (AI2rTargeting, its +0x10 the owner's callbacks): where the target will be, the
   aim toward it from the weapon (or a turret's barrel), and when to fire. The combat state's
   (combat.c) and the turrets' (turret.c). */
#pragma once
#include "character.h"

#include <stdbool.h>
#include <stdint.h>

/* The owner's callbacks (the AI's 0x53ebf8, a turret's 0x54c348); NULL ones are skipped. */
typedef struct {
    void (*fire)(void *owner, bool on);                         /* +0x00 */
    void (*aiming_speed)(void *owner, float speed);             /* +0x04 */
    void (*stop_aiming)(void *owner);                           /* +0x0c */
    void (*place)(void *owner, float out[3]);                   /* +0x10: the shooter's place */
    void (*aim_vector)(void *owner, const float v[3]);          /* +0x14: along the vector from the muzzle */
    void (*look_point)(void *owner, const float p[3]);          /* +0x18 */
    void (*look_char)(void *owner, const Character *c);         /* +0x1c */
} TargetingOps;

enum { TARGETING_MEMORY = 64 };
typedef struct {
    int axis;                   /* +0x00: the barrel's axis (0 x, 1 z) */
    void *owner;                /* +0x04 */
    const Character *target;    /* +0x08 */
    const uint8_t *cls;         /* +0x0c: the class's AI block (ONCC+0x138, TURR+0x5cc) */
    const TargetingOps *ops;    /* +0x10 */
    bool active;                /* +0x14: aiming a weapon */
    float speed;                /* +0x18: the target's */
    float predict;              /* +0x1c: how much of its motion is led */
    unsigned head, count;       /* +0x20, +0x24: the target's places, a ring of cls+0x18 */
    float ring[TARGETING_MEMORY][3];    /* +0x28 */
    float vel_short[3], vel_long[3];    /* +0x2c, +0x38 */
    float point[3];             /* +0x44: where it is aimed at (the target, led) */
    bool has_point;             /* +0x50 */
    bool inaccurate;            /* +0x51: the first shots' error on */
    float inaccuracy;           /* +0x54 */
    float grouping;             /* +0x58 */
    float offset[3];            /* +0x5c */
    bool first;                 /* +0x68: not on target since the last delay */
    uint32_t off_ticks;         /* +0x6c */
    uint32_t delay;             /* +0x70: ticks before it may fire */
    uint32_t close_ticks;       /* +0x74 */
    int mode;                   /* +0x78: 1 aiming the weapon, 2 looking */
    float aim[3];               /* +0x7c */
    float from[3];              /* +0x88: the shooter's place */
    float muzzle[3];            /* +0x94 */
    float barrel[3];            /* +0xa0 */
    float vector[3];            /* +0xac: from the muzzle to the aim */
    float dist, aim_dist;       /* +0xb8, +0xbc */
    float close;                /* +0xc0: 1 within 10, 0 beyond 25 */
    bool stopped, on_target, fire;      /* +0xc4, +0xc5, +0xc6 */
    float error, tolerance;     /* +0xc8, +0xcc */
    const uint8_t *params;      /* +0xd0: the AI firing mode (ONWC+0xd0, +0x150 the alternate; TURR+0x54c) */
    const uint8_t *skill;       /* +0xd4: the class's shooting skill for it */
    const uint8_t *skills;      /* +0xd8: the class's skills (0x18 bytes each) */
    float matrix[12];           /* +0xdc: the weapon's (the owner keeps it up to date) */
    bool ballistic;             /* +0xe0: a ballistic solution (or none needed) */
    float bal_h, bal_v, bal_angle;      /* +0xe4, +0xe8, +0xec */
} AiTargeting;

/* 0x4ba6d0: the owner, its callbacks, the barrel's axis, the class's block and skills, the firing mode. */
void targeting_init(AiTargeting *t, int axis, const TargetingOps *ops, void *owner, const uint8_t *params,
                    const uint8_t *cls, const uint8_t *skills);
/* 0x4bb700: the firing mode (NULL none); the ring of the target's places starts again. */
void targeting_set_weapon(AiTargeting *t, const uint8_t *params);
/* AI2rTargeting_SetupNewTarget 0x4babb0. */
void targeting_new_target(AiTargeting *t, const Character *target, bool inaccurate);
/* AI2rTargeting_Update 0x4ba720. *close: how close the target is within the weapon's closest (0 none). */
void targeting_update(AiTargeting *t, bool has_target, bool aim, bool fire, float *close);
/* 0x4ba830: whether to fire (the callback +0 told). */
void targeting_fire_decision(AiTargeting *t, bool aim, bool want, bool lof);
/* 0x4bab70: the aiming speed back to 1, the aim stopped, the memory freed. */
void targeting_stop(AiTargeting *t);
