/* The decals (P3rCreateDecal 0x44f070 and its clipping, P3iDecal_ProcessAdjacency 0x450f50, the lists
   and their display, P3iDisplayDecals 0x453760): a quad of texture laid on the walls around a point, cut
   to the quads it covers and folded over their edges. Notes: docs/re/decal.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The flags of P3rCreateDecal. */
enum {
    DECAL_STATIC = 0x01,        /* in the static list, its own memory (else the dynamic ring) */
    DECAL_INTO = 0x02,          /* into the caller's buffer (not used by the port) */
    DECAL_CULL = 0x04,          /* only the quads facing the decal (flags2 0x400000) */
    DECAL_PLANAR = 0x08,        /* cut to the quads the sphere meets, no folding (the characters') */
    DECAL_UNSHADED = 0x10,      /* the colours not taken from the quads' shading (flags2 0x200000) */
};

/* The level's environment (P3rDecal_LevelBegin): the decals' quads. NULL at the level's end. */
void decal_set_environment(const void *env);

/* P3rCreateDecal 0x44f070: the decal of the texture on quad (or, -1, the one a ray from pos against
   the normal meets), its x axis a and y axis b (unit vectors on the wall) scaled by xs and ys, pressed
   along dir (NULL: against the normal), folded over edges no sharper than wrap (radians), in the
   alpha (0..255) and tint. Its id (what the particle's decal block keeps; the original keeps a pointer),
   0 if none could be made. */
uint32_t decal_create(const void *texture, int32_t quad, unsigned flags, float wrap, const float *dir, const float pos[3],
                      const float normal[3], const float b[3], const float a[3], float xs, float ys, uint16_t alpha,
                      uint32_t tint);
/* The particle the decal belongs to (the decal block's +4): killed when the ring takes its memory. */
void decal_set_owner(uint32_t id, uint32_t particle);
/* Whether the decal still has its triangles (the ring hasn't taken them). */
bool decal_live(uint32_t id);
/* P3rDeleteDecal 0x453630: out of its list and gone. */
void decal_delete(uint32_t id);
/* The decal's alpha (0..255), as a decal particle fades. */
void decal_set_alpha(uint32_t id, uint16_t alpha);
/* At the level's end (P3rDecalDeleteAllDynamic and the static ones). */
void decal_delete_all(void);

/* The decals' triangles for the renderer, the static list then the dynamic one (P3rDisplayStaticDecals,
   P3rDisplayDynamicDecals): each decal's texture, quad (-1 none), tint, alpha, and its vertices (place,
   u, v, a shade each) and triangles. */
typedef struct {
    const void *texture;
    int32_t quad;
    uint32_t tint;
    uint16_t alpha;
    uint32_t nverts, ntris;
    const float *pos;           /* 3 a vertex */
    const float *uv;            /* 2 a vertex */
    const uint32_t *shade;      /* ARGB a vertex (0xffffffff unshaded) */
    const uint16_t *tris;       /* 3 a triangle */
} DecalDraw;
typedef void (*DecalFn)(const DecalDraw *d, void *user);
void decal_each(DecalFn fn, void *user);
int decal_count(void);
