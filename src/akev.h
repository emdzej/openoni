/* AKEV, the environment (Akira, the level engine's AKtEnvironment): the level's geometry as "gunk quads"
   (quads and triangles over one shared point and texture coordinate array), with their textures, planes,
   the BSP and octree used for collision and visibility, and the AI's pathfinding nodes. One per level.
   Format: docs/formats/akev.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { AKEV_TAG = 0x414b4556 };

/* The fields, from the instance data (tm.h): seventeen links, then the bounding box. */
enum {
    AKEV_POINTS = 0x00,          /* PNTA */
    AKEV_PLANES = 0x04,          /* PLEA */
    AKEV_TEXCOORDS = 0x08,       /* TXCA */
    AKEV_QUADS = 0x0c,           /* AGQG: corners, texture coordinates, shades, flags */
    AKEV_QUAD_RENDER = 0x10,     /* AGQR: each quad's texture, an index into TXMA */
    AKEV_QUAD_COLLISION = 0x14,  /* AGQC: each quad's plane and bounding box */
    AKEV_QUAD_DEBUG = 0x18,      /* AGDB: raw offsets of names, for the editor */
    AKEV_TEXTURES = 0x1c,        /* TXMA */
    AKEV_BNV_NODES = 0x20,       /* AKVA */
    AKEV_SIDES = 0x24,           /* AKBA */
    AKEV_SCRIPT_QUADS = 0x28,    /* IDXA: quads scripts refer to... */
    AKEV_SCRIPT_IDS = 0x2c,      /* IDXA: ...and their ids (AIiScript_EnvTexSwap 0x4bcdc0) */
    AKEV_BSP_NODES = 0x30,       /* AKBP */
    AKEV_BNV_BSP = 0x34,         /* ABNA */
    AKEV_OCTREE = 0x38,          /* AKOT */
    AKEV_ADJACENCY = 0x3c,       /* AKAA */
    AKEV_DOOR_FRAMES = 0x40,     /* AKDA */
    AKEV_BOUNDS = 0x44,          /* 6 floats: min xyz, max xyz */
};

/* Quad flags (AGQG +0x30) the port reads; docs/formats/akev.md lists them all. */
enum {
    AKEV_QUAD_DOOR_FRAME = 0x01,
    AKEV_QUAD_GHOST = 0x02,          /* not in the octree: never drawn or collided */
    AKEV_QUAD_STAIRS_UP = 0x04,
    AKEV_QUAD_STAIRS_DOWN = 0x08,
    AKEV_QUAD_JELLO = 0x20,          /* run time: see-through, set when it hides the camera's target */
    AKEV_QUAD_TRIANGLE = 0x40,       /* three corners: the fourth repeats the third */
    AKEV_QUAD_TRANSPARENT = 0x80,    /* drawn with the alpha pass */
    AKEV_QUAD_NO_DRAW = 0x42000,     /* AKiEnvironment_ComputeVis 0x444030 leaves these out */
};

typedef struct {
    uint32_t corners;       /* 3 or 4 */
    uint32_t point[4];      /* into the points */
    uint32_t texcoord[4];   /* into the texture coordinates */
    uint32_t shade[4];      /* ARGB, the baked light of each corner */
    uint32_t flags;         /* AKEV_QUAD_* */
    uint32_t object;        /* the object id the editor gave it, 0xffffffff for none */
    uint32_t texture;       /* into the texture array (AGQR) */
} AkevQuad;

/* The arrays' elements (little-endian, as stored): count and the first element, NULL if no link. */
const uint8_t *akev_points(const void *env, uint32_t *count);      /* 3 floats */
const uint8_t *akev_texcoords(const void *env, uint32_t *count);   /* 2 floats */
const uint8_t *akev_planes(const void *env, uint32_t *count);      /* a, b, c, d */
void akev_bounds(const void *env, float box[6]);
uint32_t akev_quad_count(const void *env);
/* Quad i; false if i is out of range or the quad's indices are past their arrays. */
bool akev_quad(const void *env, uint32_t i, AkevQuad *q);
/* The texture array's entry i: the TXMP's data (tm.h), NULL if none or unresolved. */
uint32_t akev_texture_count(const void *env);
void *akev_texture(const void *env, uint32_t i);
/* The highest floor under (x, z) between y + up and y - down: of the quads facing up (collision plane
   normal y > 0.5) that aren't ghosts, whose corners enclose the point seen from above. False if none.
   A stand-in for ONrCharacter_FindFloorWithRay 0x4daa10 (which casts through the octree) until the
   physics port. */
bool akev_floor(const void *env, float x, float y, float z, float up, float down, float *floor_y);

/* Whether the segment from p along v (p + v its end) meets a quad, of those without any of the skip
   flags (and not ghosts): AKrCollision_Point 0x4412d0 asked for the first hit only (0x441c80). A
   stand-in for its walk of the octree: every quad's box, then its plane and corners. */
bool akev_segment_hits(const void *env, const float p[3], const float v[3], uint32_t skip);

/* The script ids (IDXA pairs at AKEV_SCRIPT_QUADS / AKEV_SCRIPT_IDS: entry i gives quad i's id). */
uint32_t akev_script_count(const void *env);
bool akev_script_entry(const void *env, uint32_t i, uint32_t *quad, uint32_t *id);
/* The quad's collision plane (a, b, c, d, its index's sign applied) and its corner k's place. */
bool akev_quad_plane(const void *env, uint32_t quad, float plane[4]);
bool akev_quad_corner(const void *env, uint32_t quad, unsigned k, float out[3]);
/* Changes a quad's flags in the loaded data, as the original does at run time (env_show, the jello);
   every change bumps akev_flags_serial so the draw can rebuild. */
void akev_quad_set_flags(void *env, uint32_t quad, uint32_t set, uint32_t clear);
uint32_t akev_flags_serial(void);
/* Sets the quad's corner shades (three for a triangle) to one ARGB, as the doors do (0x5071e0). */
void akev_quad_set_shade(void *env, uint32_t quad, uint32_t argb);

/* Whether the normal draw (AKiEnvironment_ComputeVis 0x444030) takes a quad with these flags, when the
   visibility marks it. */
static inline bool akev_quad_drawn(uint32_t flags) { return (flags & 0xf) == 0 && (flags & AKEV_QUAD_NO_DRAW) == 0; }
