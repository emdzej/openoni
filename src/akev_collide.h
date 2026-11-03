/* The environment's collisions: a sphere moving through the level's quads (AKrCollision_Sphere 0x441cb0,
   AKrGQ_SphereVector 0x4425e0), found through the octree. Notes: docs/re/physics.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A hit (0x48 bytes in the original's list at 0x6a9080). */
typedef struct {
    uint32_t idist;         /* +0x00: the distance rounded, the sort's key */
    float plane[4];         /* +0x0c: the quad's plane, turned by its index's sign */
    float point[3];         /* +0x1c: where the sphere touches it */
    uint32_t quad;          /* +0x28 */
    float dist;             /* +0x44: from the sphere's centre */
} AkevHit;

enum { AKEV_HITS_MAX = 255 };

/* AKrCollision_Sphere 0x441cb0: the sphere (centre, radius) moving by move against the quads whose flags
   don't meet skip. limit 0 stops at the first hit (no list); 1 keeps the nearest; else all, nearest first.
   Returns the hits' count (at most AKEV_HITS_MAX), filling hits (which holds AKEV_HITS_MAX). */
unsigned akev_collide_sphere(const void *env, const float sphere[4], const float move[3], uint32_t skip, unsigned limit,
                             AkevHit *hits);
/* AKrCollision_Point 0x4412d0 (through 0x441c80): the segment from p along v against the quads, through the octree; limit
   as for the sphere's, the distance the L1 one from p. */
unsigned akev_collide_point(const void *env, const float p[3], const float v[3], uint32_t skip, unsigned limit,
                            AkevHit *hits);
/* AKrGQ_SphereVector 0x4425e0: the sphere moving by move against one quad; the touch point to at. */
bool akev_quad_sphere_vector(const void *env, uint32_t quad, const float sphere[4], const float move[3], float at[3]);
/* 0x43e3b0: whether p, on the quad's plane, is inside the quad (axis: its projection, 0 to work it out). */
bool akev_point_in_quad(const void *env, uint32_t quad, int axis, const float p[3]);
/* 0x43ddd0: the quad's projection axis: 1 drops z, 2 drops y, 3 drops x, 0 none. */
int akev_quad_axis(const void *env, uint32_t quad);
