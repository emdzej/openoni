/* The physics objects other than the characters: the contexts of type 2 (PHrPhysicsContext, 0x150 bytes
   in the array at *0x5cb520) the weapons and the powerups make. A sphere moved by its velocity under
   gravity, slid along the environment's walls and slowed while touching them (OBiPhysics_Update 0x43af50
   with the generic callbacks). Notes: docs/re/physics.md "The other objects".

   The other contexts (physctx.h) through the callback +8 (PHrPhysics_Callback_FindPhyCollisions
   0x43be20), those the callback +0xc skips left out. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "physctx.h"


typedef struct PhysObject {
    float sphere[4];        /* +0x00's sphere (0x43b310): centre, radius */
    float vel[3];           /* +0x68 */
    float pos[3];           /* +0x84 */
    float m[12];            /* +0xa4: the place times the turn (+0x90, kept the identity) */
    float gravity;          /* +0x108 */
    float friction_sub;     /* +0x114 */
    float friction_mul;     /* +0x118 */
    float accel[3];         /* +0x11c */
    uint32_t flags;         /* +0x128: PHYS_* */
    /* the callbacks' +0x18 (after the collisions, with all the hits) */
    void (*post_collision)(struct PhysObject *o, const PhysHit *hits, unsigned n);
    void *owner;            /* +0x14c */
    int kind;               /* the callbacks' +0 (PHYS_KIND_*) */
    /* +0xc: another context left out of the search (NULL none) */
    bool (*skip)(const PhysCtx *self, const PhysCtx *other);
    /* +0x20: pushed by another's move (NULL none) */
    void (*receive_force)(const PhysCtx *self, const PhysCtx *from, const float force[3]);
} PhysObject;

enum {
    PHYS_ACTIVE = 0x2,
    PHYS_NO_COLLISION = 0x4,
    PHYS_NO_GRAVITY = 0x40,
    PHYS_NO_RESPOND = 0x800,        /* the collisions found but not slid along */
    PHYS_FRICTION_ON_CONTACT = 0x1000,
    PHYS_TOUCHED = 0x2000,          /* a collision took some of the move this tick */
    PHYS_ENV_SKIP = 0x4000,         /* the walls with flag 0x4000 also skipped */
    PHYS_TWICE = 0x8000,
};

/* The context as the searches see it (type 2, the sphere its tree). */
void physobj_ctx(PhysObject *o, PhysCtx *out);
/* 0x43beb0, the generic receive force: into the acceleration. */
void physobj_push(PhysObject *o, const float force[3]);
/* PHrPhysics_Init 0x43b5e0: zeroed, gravity 0.294, active; the matrix the identity. */
void physobj_init(PhysObject *o);
/* PHrPhysics_MaintainMatrix 0x43b410 for type 2: the place, the identity turn, the scale 1. */
void physobj_matrix(PhysObject *o);
/* One tick of OBiPhysics_Update 0x43af50 for the context: gravity into the acceleration, that into the
   velocity, the move slid along the walls (PHiPhysics_FindCollisions 0x43a710, then
   PHiPhysics_Respond 0x43aac0), the place moved, the friction (PHrPhysics_Callback_Friction 0x43bee0)
   when touching, the matrix. */
void physobj_update(PhysObject *o, const void *env);
