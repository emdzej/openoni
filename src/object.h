/* The level's objects (OB): the things of the level that move or can be moved as one piece, the
   forklifts, the truck, crates, from the level's starting object array (OBOA). Each is a geometry array
   placed by a matrix, or played by an object animation (oban.h). The environment scripts (env_setanim,
   env_anim) pick them by id. Format: docs/formats/oboa.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { OBOA_TAG = 0x4f424f41, M3GA_TAG = 0x4d334741 };

/* The OBOA element (the object setup 0x439910). */
enum {
    OBOA_COUNT = 0x16,          /* u16 (after 0xdead padding), then the elements */
    OBOA_ELEMENTS = 0x18,
    OBOA_SIZE = 0xf0,
    OBOA_GEOMETRY = 0x00,       /* M3GA */
    OBOA_ANIMATION = 0x04,      /* OBAN, or 0 */
    OBOA_PARTICLES = 0x08,      /* link (inferred: what 0x438d90 sets up) */
    OBOA_FLAGS = 0x0c,          /* OBOA_* below */
    OBOA_DOOR = 0x14,           /* u32: the door's id (OSD +0) for a door's panel; its second panel id | 0x1000 */
    OBOA_PHYSICS = 0x18,        /* u32: the physics type without an animation */
    OBOA_ID = 0x1c,             /* u32: what the scripts call it */
    OBOA_POSITION = 0x20,       /* 3 floats */
    OBOA_ROTATION = 0x2c,       /* quaternion */
    OBOA_SCALE = 0x3c,          /* float */
    OBOA_MATRIX = 0x40,         /* 3x4 by columns: the placement the doors' animations are under */
    OBOA_NAME = 0x70,           /* 64 bytes */
    OBOA_FILE = 0xb0,           /* 64 bytes: the file it was made from */
};

/* Flags of the element (+0x0c), as the setup reads them. */
enum {
    OBOA_FLAG_BOX_PLANES = 0x1000,  /* keeps its box's planes (0x1000 of the low word) */
    OBOA_FLAG_PHYS_4 = 0x0400,
    OBOA_FLAG_PHYS_40 = 0x0800,
};

/* Finds the level's array (returns its element count); there are no objects until obj_create makes them. */
int object_load(void);
void object_unload(void);
/* obj_create's work: the elements of ids from..to (either order) that allow it; how many were made. */
int object_create(uint32_t from, uint32_t to);
/* The objects there are. */
int object_count(void);
bool object_shown(int i);
uint32_t object_id(int i);
const char *object_name(int i);
/* The object's geometry array (M3GA: count at 0x14, M3GM links from 0x18). */
uint32_t object_geometry_count(int i);
const void *object_geometry(int i, uint32_t k);
/* The object's matrix now (PHrPhysics_MaintainMatrix 0x43b410), 3x4 by columns. */
void object_matrix(int i, float m[12]);
/* One tick: the playing animations step (PHrPhysics_Update_Animation). now: the game time, ticks. */
void object_update(uint32_t now);

/* obj_create, obj_kill, obj_show, obj_hide, env_setanim, env_setanim_block, env_anim, env_anim_block. */
void object_register_commands(void);
