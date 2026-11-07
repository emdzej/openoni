/* The level's turrets (OT_Turret.c, the "Turret" mechanics type, 0x3f8 bytes each): a head turned about
   its vertical axis and a barrel about its horizontal one (the TURR class's limits and speeds), aimed by
   the AI's targeting (targeting.h) at the nearest character of its target teams, its shooters'
   particles fired as the targeting says. Notes: docs/re/turret.md. */
#pragma once
#include "akev_vis.h"
#include "targeting.h"

#include <stdbool.h>
#include <stdint.h>

/* The flags (turret+0x2e). */
enum { TURRET_PARTICLES = 0x01, TURRET_FIRING = 0x04, TURRET_SOUND = 0x08, TURRET_DRAWN = 0x10, TURRET_SHOWN = 0x20 };
enum { TURRET_SHOOTERS = 16 };

typedef struct {
    float pos[3], rot[3];       /* +0x0c, +0x18 (degrees) */
    uint16_t id;                /* +0x2c */
    uint16_t flags;             /* +0x2e */
    int state;                  /* +0x30: 1 active */
    char name[64];              /* +0x32: the class's */
    const uint8_t *cls;         /* +0x74: the TURR */
    uint32_t teams;             /* +0x78: the target teams (bit per team) */
    float pitch, yaw;           /* +0x80, +0x84: the barrel's and the head's */
    float want_yaw, want_pitch; /* +0x88, +0x8c */
    bool fire;                  /* +0x90: the targeting's trigger */
    float base[12];             /* +0x94: the object's (its place and rotation, 0x35c) */
    float head[12], barrel[12]; /* +0xc4, +0xf4: the head's and the barrel's, local */
    float world[12];            /* +0x124: the barrel's in the world (the shooters hang from it) */
    bool los;                   /* +0x154 */
    uint16_t cooldown;          /* +0x156: ticks before the next shot */
    uint16_t retarget;          /* +0x158 */
    uint32_t active_ticks;      /* +0x15c */
    uint32_t hidden_ticks;      /* +0x160 */
    uint32_t sound;             /* +0x3a0: the active sound's (the OS layer's ambient id), UINT32_MAX none */
    VisList leaves;             /* +0x3a4: the leaves round its head, found at its first draw */
    uint32_t particle[TURRET_SHOOTERS];     /* +0x168 (their handles, +0x1a8) */
    AiTargeting t;              /* +0x268 (its +8, +0x270, the target) */
} Turret;

/* Reads the level's Turret objects (OBJiTurret_SetOSD 0x4ceb80): their classes, inactive. */
int turret_load(void);
void turret_unload(void);
int turret_count(void);
Turret *turret_get(int i);
/* One tick of each (OBJiTurretCombat_Update 0x4cf140). env: the AKEV for the line of sight. */
void turret_update(const void *env);
/* 0x4cd990 / 0x4cd9c0 (the event lists' "activate turret", turret_activate): the turrets of the id
   (0xffff every one) start; 0x4cda80 / 0x4cdab0 (turret_deactivate) stop. */
void turret_activate(uint16_t id);
void turret_deactivate(uint16_t id);
/* The inaccuracy of the turret with the id (its class's shooting skill), false for none. */
bool turret_inaccuracy(uint16_t id, float *out);
/* The drawn turret's geometries (the draw 0x4ce300: the head under base x head, the barrel under base x
   head x barrel): k 0 the head, 1 the barrel; NULL none. Marks it drawn (TURRET_DRAWN). */
const void *turret_geometry(int i, int k, float m[12]);
void turret_register_commands(void);
