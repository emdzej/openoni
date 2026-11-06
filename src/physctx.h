/* The physics contexts against each other (PHrPhysics_Callback_FindPhyCollisions 0x43be20 and the
   character's 0x4ecb40, through PHrPhysics_Single_PhyCollision 0x43ba70): a moving context's sphere
   tree swept against the other contexts' trees, or against an object's box (PHrCollision_Volume_
   SphereTree 0x43a2d0); the hits join the walls' in the mover's list and a hit that takes some of the
   move pushes the other (its receive-force callback, +0x20). Notes: docs/re/physics.md "The contexts'
   collisions".

   The port keeps each kind of context with its owner (the characters, the level's objects, the weapons,
   the powerups): each registers a source the search walks, in that order (the original walks its one
   array of 0x150-byte contexts in the order they were made). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The callback table's kinds (+0x148's +0). */
enum { PHYS_KIND_CHARACTER = 1, PHYS_KIND_OBJECT = 2, PHYS_KIND_WEAPON = 5, PHYS_KIND_POWERUP = 7 };

/* The context flags these tests read (+0x128; physobj.h has the others). */
enum { PHYS_INACTIVE_OTHER = 0x10, PHYS_POINT = 0x20, PHYS_VOLUME = 0x200, PHYS_FIRST_PASS = 0x10000 };

/* A sphere tree (the context's +0: a node is a sphere, its first child +0x10 and next sibling +0x14):
   the spheres and, for each, its first child and next sibling (-1 none). */
typedef struct {
    const float (*s)[4];
    const int8_t (*links)[2];
    int root;
} PhysTree;

/* An object's box (the context's +0x64, 0x174 bytes): its corners in the world (+0), its faces' corner
   indices (+0x60, the exe's table at 0x529660), the faces' normals as made (+0xc0), their planes in the
   world (+0x108) and the axis each drops for the inside test (+0x168). local: the context's +0x04, the
   corners before the matrix. */
typedef struct {
    float corners[8][3];
    int32_t faces[6][4];
    float normals[6][3];
    float planes[6][4];
    uint16_t axis[6];
    float local[8][3];
} PhysBox;

/* A context as the search sees it. */
typedef struct PhysCtx {
    int kind;               /* PHYS_KIND_* */
    int type;               /* +0x12c: 0 static .. 3 animated, 4 */
    uint32_t flags;         /* +0x128 */
    PhysTree tree;
    const PhysBox *box;     /* PHYS_VOLUME's */
    void *owner;            /* +0x14c */
    /* +0x20: pushed by from's move into it (the part the mover's slide took off) */
    void (*receive_force)(const struct PhysCtx *self, const struct PhysCtx *from, const float force[3]);
} PhysCtx;

/* A hit on the environment (kind 0: the quad) or on another context (kind 1: other), as the 0x28-byte
   records 0x43a710 sorts. */
typedef struct {
    int kind;               /* +0x00 */
    uint32_t quad;          /* +0x04 */
    PhysCtx other;          /* +0x04 for kind 1 (a copy of the view) */
    float plane[4];         /* +0x08: the normal first */
    float point[3];         /* +0x18 */
    float d2;               /* +0x24: from the sphere's centre, the sort's key */
} PhysHit;

/* A source of contexts: how many, and the i-th (false when it has none now). */
typedef struct {
    int (*count)(void);
    bool (*get)(int i, PhysCtx *out);
} PhysSource;
/* Registered once (each module's install); the same source twice is kept once. */
void physctx_add_source(PhysSource src);

/* The other contexts the mover meets moving by mv, appended to hits from n (the count returned).
   skip: the mover's callback +0xc (true: not that one; for the character's own search, 0x4ecb40, its
   rules), NULL none. */
unsigned physctx_find(const PhysCtx *mover, const float mv[3], bool (*skip)(const PhysCtx *mover, const PhysCtx *other),
                      PhysHit *hits, unsigned n, unsigned cap);

/* An object's box from its geometries' bounds (0x439910) and its bounding sphere's radius. */
void physctx_box_make(PhysBox *box, const void *const *geometries, int n, const float pos[3], float *radius);
/* PHrPhysics_MaintainMatrix 0x43b410's box part: the corners and the planes through the matrix. */
void physctx_box_place(PhysBox *box, const float m[12]);
