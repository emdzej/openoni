/* TRAM, an animation (Totoro, the body animation engine's TRtAnimation): per-frame bone rotations as
   keyframe streams, the root's motion and height, and the moves' attack, throw and sound data. Its bulk
   is in the .raw. Format: docs/formats/tram.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { TRAM_TAG = 0x5452414d };

/* Fields, from the instance data (tm.h). 0x04..0x30 and 0x134 are .raw offsets (TRrAnimation_Prepare
   0x428610 turns them into pointers). */
enum {
    TRAM_HEIGHTS = 0x04,        /* a float per frame */
    TRAM_VELOCITIES = 0x08,     /* x, z per frame (TRrAnimation_GetPosition 0x428750) */
    TRAM_SHORTCUTS = 0x18,      /* 8 bytes each: the state (u16), ...; the count at 0x17d */
    TRAM_ATTACKS = 0x24,        /* 0x18 bytes each, the count at 0x17f */
    TRAM_ROTATIONS = 0x2c,      /* the keyframe streams, one per bone */
    TRAM_FLAGS = 0x34,          /* bit 0 is the run time's "prepared" */
    TRAM_FPS = 0x156,
    TRAM_ROTATION_SIZE = 0x158, /* bytes per key: 6 (Euler angles) or 16 (quaternion) on the CD */
    TRAM_TYPE = 0x15a,
    TRAM_AIM_TYPE = 0x15c,
    TRAM_FROM_STATE = 0x15e,    /* TRrAnimation_GetFrom 0x428720 */
    TRAM_TO_STATE = 0x160,
    TRAM_BONES = 0x162,
    TRAM_FRAMES = 0x164,
    TRAM_DURATION = 0x166,      /* TRrAnimation_GetDuration 0x428740 */
    TRAM_VARIENT = 0x168,       /* TRrAnimation_GetVarient 0x428700 */
    TRAM_LEVEL = 0x176,         /* the level the move is learned in: the lookup skips it before */
    TRAM_SHORTCUT_COUNT = 0x17d,
};

typedef struct {
    uint16_t fps, rotation_size, type, aim_type, from_state, to_state, bones, frames, duration, varient;
    uint32_t flags;
} TramInfo;

void tram_info(const void *tram, TramInfo *out);

/* The 0x427820 walk: every bone's rotation (x, y, z, w) at a frame, from the keys around it, lerped
   (MUrQuat_Lerp 0x433360). quats holds 4 floats per bone. False if the streams can't be read or use a
   key size the port doesn't decode. */
bool tram_rotations(const void *tram, uint32_t frame, float *quats);
/* TRrAnimation_GetPosition 0x428750: the root's motion over the frame (x, z). */
bool tram_velocity(const void *tram, uint32_t frame, float v[2]);
/* The root's height at a frame (0x428820 lerps between these). */
bool tram_height(const void *tram, uint32_t frame, float *h);

/* The Euler angle key (0x437050): three angles (radians) to a quaternion. */
void tram_euler_to_quat(float x, float y, float z, float q[4]);
/* MUrQuat_Lerp 0x433360: the shorter way, renormalized. */
void tram_quat_lerp(const float a[4], const float b[4], float t, float out[4]);
