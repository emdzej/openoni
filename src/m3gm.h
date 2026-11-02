/* M3GM, geometry (the Motoko draw engine's M3tGeometry): points, normals, texture coordinates, triangle
   strips and a texture, each a separate instance the geometry links to. Format: docs/formats/m3gm.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { M3GM_TAG = 0x4d33474d };

/* The fields, from the instance data (tm.h): all links except the flags. */
enum {
    M3GM_FLAGS = 0x00,
    M3GM_POINTS = 0x04,          /* PNTA */
    M3GM_VERTEX_NORMALS = 0x08,  /* VCRA, one per point */
    M3GM_FACE_NORMALS = 0x0c,    /* VCRA */
    M3GM_TEXCOORDS = 0x10,       /* TXCA, one per point */
    M3GM_STRIPS = 0x14,          /* IDXA */
    M3GM_FACE_NORMAL_INDEX = 0x18, /* IDXA, one per triangle */
    M3GM_TEXTURE = 0x1c,         /* TXMP; when 0, M3rGeometry_Draw 0x41a750 looks one up */
};

/* The arrays' elements (little-endian, as stored): count and the first element, NULL if no link. */
const uint8_t *m3gm_points(const void *geom, uint32_t *count);        /* 3 floats */
const uint8_t *m3gm_vertex_normals(const void *geom, uint32_t *count); /* 3 floats */
const uint8_t *m3gm_face_normals(const void *geom, uint32_t *count);   /* 3 floats */
const uint8_t *m3gm_texcoords(const void *geom, uint32_t *count);      /* 2 floats */
/* PNTA's bounds: min xyz, max xyz, then the bounding sphere's centre xyz and radius. */
bool m3gm_bounds(const void *geom, float box[6], float sphere[4]);

/* The triangles of the strips, 3 point indices each, in the order the draw takes them, and each one's
   face normal index. tris and normals may be NULL to count. Returns the triangle count, or -1 if the
   strips are inconsistent (an index past the points, a count that differs from the face normals). */
int32_t m3gm_triangles(const void *geom, uint32_t *tris, uint32_t *normals, int32_t cap);
