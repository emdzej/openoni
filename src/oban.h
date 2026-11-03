/* OBAN, object animations: the key frames of something that moves as one piece (doors, forklifts, trucks,
   the cutscene cameras), each a rotation and a position at a frame. OBrAnimation_GetMatrix 0x439610 turns a
   frame into a matrix; PHrPhysics_Update_Animation 0x43a620 plays one. Format: docs/formats/oban.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { OBAN_TAG = 0x4f42414e };

/* The fields (instance data, tm.h). */
enum {
    OBAN_FLAGS = 0x0c,          /* OBAN_LOOP, ... */
    OBAN_START = 0x10,          /* 3x4 matrix (by columns): where the object stands before it plays */
    OBAN_BASE = 0x40,           /* 3x4 matrix: applied under every frame's transform */
    OBAN_TICKS = 0x70,          /* u16: see oban_update */
    OBAN_FRAMES = 0x72,         /* u16 */
    OBAN_DOOR_OPEN = 0x74,      /* u16: the frame a door is open at (0 for the others) */
    OBAN_KEY_COUNT = 0x76,      /* u16 */
    OBAN_KEYS = 0x78,           /* 0x20 each: quaternion x y z w, position x y z, u32 frame */
    OBAN_KEY_SIZE = 0x20,
};

enum {
    OBAN_LOOP = 0x01,           /* from the start again at the end */
    OBAN_PINGPONG = 0x02,       /* back and forth */
    OBAN_RANDOM_START = 0x04,   /* starts at a random frame */
    OBAN_AUTOSTART = 0x08,      /* plays without being started */
    OBAN_Z_UP = 0x10,           /* the matrix is turned a quarter about x (the doors) */
};

typedef struct {
    uint32_t flags;
    uint16_t frames, door_open, keys, ticks;
} ObanInfo;

bool oban_info(const void *oban, ObanInfo *info);
/* The matrix at a frame (3x4 by columns, as BodyMatrix): T(position) R(rotation) Base, the rotation left
   out with no_rotation (the caller's flag 2), then the quarter turn for OBAN_Z_UP. False if the animation
   has no keys. */
bool oban_matrix(const void *oban, int frame, bool no_rotation, float out[12]);

/* A playing animation (the original's context: the animation, its state, the frame, the step, the frame
   to stop at, the time of the last step). */
typedef struct {
    const void *oban;
    uint16_t state;             /* OBAN_PLAYING, OBAN_PAUSED */
    int16_t frame, step, stop;
    uint32_t last;
} ObanPlayer;
enum { OBAN_PLAYING = 0x01, OBAN_PAUSED = 0x10 };

void oban_player_init(ObanPlayer *p, const void *oban);
/* OBrAnim_Start 0x439580: from the start (or a random frame), forwards. now: the game time in ticks. */
void oban_start(ObanPlayer *p, uint32_t now);
/* PHrPhysics_Update_Animation 0x43a620, once a tick: true when it stopped at an end this tick. */
bool oban_update(ObanPlayer *p, uint32_t now);
