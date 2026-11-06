/* The characters' physics: the body box and sphere tree (0x4e5ff0, 0x4dc160), the move slid along the
   environment's walls (OBiPhysics_Update 0x43af50 with the character's callbacks, 0x4ebd70), and the
   floor (FindGroundCollision 0x4ec040: standing on it, the landing and its damage). Notes:
   docs/re/physics.md.

   chars_update runs every character's physics after all the frames and contacts, as the original
   (chr_physics_later). Its context meets the others' (physctx.h). Stand-in until its port: the AI's
   wall notice. */
#pragma once
#include "character.h"
#include "physctx.h"

/* The character's context as the others' searches see it (false: none, no physics). */
bool physics_ctx(Character *c, PhysCtx *out);
/* The context for a character made active (FUN_004dc2b0): at its position, its tree built. */
void physics_start(Character *c);
/* 0x4e9210: the character put at pos, the physics' position and the old one with it. */
void physics_place(Character *c, const float pos[3]);
/* 0x4e5ff0 and 0x4dc160, step 19 of DoCharacterFrame: the body box (C+0x190, C+0x1658) from the pose,
   then the sphere tree. */
void physics_body(Character *c);
/* Step 12 of DoCharacterFrame: the position from the physics, the tick's move w (in the world) split
   into the move and the drop. */
void physics_take(Character *c, const float w[3]);
/* UpdateCharacters' step 10 and OBiPhysics_Update for the character's context. */
void physics_update(Character *c, const void *env);
/* PHrCollision_Quad_SphereTreeVector 0x43bfa0: a sphere tree (the root, 4 leaves, 2 middles) moving by mv
   against the quad, down to its leaves. */
bool physics_tree_hits(const float spheres[7][4], const void *env, uint32_t quad, const float mv[3]);

/* A debug log of the player's blocked moves (--param physlog=1); NULL off. */
extern void (*physics_debug_log)(const char *msg);
