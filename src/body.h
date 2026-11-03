/* The character body: a character class (ONCC) links a body set (TRBS: five bodies, TRCM, lowest detail
   first) and a texture per part (TRMA). A body has a geometry per bone (TRGA), each bone's offset from
   its parent (TRTA) and the hierarchy (TRIA). Format: docs/formats/oncc.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { ONCC_TAG = 0x4f4e4343, TRCM_TAG = 0x5452434d };

enum {
    ONCC_BODY_SET = 0xc34,      /* TRBS */
    ONCC_TEXTURES = 0xc38,      /* TRMA */
    ONCC_ANIMATIONS = 0xc80,    /* TRAC */
    ONCC_AIMING = 0xc84,        /* TRSC */
    TRCM_BONES = 0x04,          /* u16 */
    TRCM_GEOMETRIES = 0x54,     /* TRGA: M3GM per bone */
    TRCM_TRANSLATIONS = 0x58,   /* TRTA: x, y, z per bone */
    TRCM_HIERARCHY = 0x5c,      /* TRIA: parent, first child, next sibling, padding (bytes) */
    BODY_MAX_BONES = 32,
};

/* A 3x4 matrix by columns, the last column the translation (the MU module's layout). */
typedef float BodyMatrix[12];

/* The class's body at a detail level (0 lowest .. 4 highest), NULL if none. */
void *body_of_class(const void *oncc, int detail);
/* The class's texture for a part, NULL if none. */
void *body_texture(const void *oncc, uint32_t part);
uint32_t body_bones(const void *trcm);
void *body_geometry(const void *trcm, uint32_t bone);           /* the bone's M3GM */
/* iBuildMatriciesRecursive 0x427570: each bone's matrix from the root's (bone 0's parent): the parent's,
   translated by the bone's offset (0x434c90), times its rotation (MUrMatrixStack_Quaternion 0x434c00).
   quats: 4 floats per bone (tram.h), NULL for the rest pose. False if the arrays are inconsistent. */
bool body_matrices(const void *trcm, const BodyMatrix root, const float *quats, BodyMatrix *out);
/* MUrQuatToMatrix 0x4338c0: the 3x3 rotation, by columns. */
void body_quat_to_matrix(const float q[4], float m[9]);
/* MUrMatrix_MultiplyPoint 0x4339e0. */
void body_transform(const BodyMatrix m, const float p[3], float out[3]);
