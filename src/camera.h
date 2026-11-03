/* CA, the game camera (Oni_Camera.c): one camera in five modes, the follow camera behind the player, the
   orbit, the scripted OBAN animations and interpolations, and the debug fly camera. Its state lives in the
   game state (ONrGameState +0x80 points at it, 0x6364c0, 0x84 bytes); CArUpdate 0x4d5ca0 runs the mode
   and hands location, view and up to the Motoko camera the draw uses (0x41b3f0). The follow camera never
   collides with walls: it makes the quads between it and its target see-through instead (the "jello",
   CAiTrackJello 0x4d6b10). Notes: docs/re/camera.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The modes (+0x00 of the state). */
enum { CAMERA_ORBIT = 0, CAMERA_FOLLOW = 1, CAMERA_ANIMATE = 2, CAMERA_MANUAL = 3, CAMERA_INTERPOLATE = 4 };

/* What the camera reads of the character it follows (state +0x54), filled by the caller each tick: the
   original reads the character and its active part directly. */
typedef struct {
    float pos[3];       /* C+0x148: the feet */
    float bottom;       /* C+0x190: the body's bottom y */
    float facing;       /* 0x4e33a0: C+0x170 less the pending turn C+0x174, in [0, 2pi) */
    bool armed;         /* C+0x194: a weapon in hand */
    bool active;        /* the character has an active part (0x4f1180); without it the aim fields are unused */
    uint16_t variant;   /* A+0x1ca0: 0x7800 the weapon kind */
    float aim[3];       /* A+0x21b0: the aim direction (unit) */
    float aim_lr;       /* A+0x21bc: aim left/right, radians */
    float aim_ud;       /* A+0x21c0: aim up/down, radians, up positive */
} CameraTarget;

/* What the debug fly camera takes from each of the frame's input records (0x30 bytes in the original;
   the offsets are the record's, the meanings inferred from the use). */
typedef struct {
    float turn_x, turn_y;   /* +0x08, +0x0c: turn about up and about the side, 6 degrees a unit */
    float forward;          /* +0x10: along the view, 5 units a unit */
    float rise;             /* +0x14: straight up */
    float strafe;           /* +0x18: along the side, 5 units a unit */
    float look_x, look_y;   /* +0x24, +0x28: turn, 1/60 rad a unit, in fly mode only */
} CameraManualInput;

/* The Motoko camera's location, view direction and up (0x41b3f0's three vectors). */
typedef struct {
    float location[3], view[3], up[3];
} CameraView;

/* 0x4d5a10: the state zeroed (orbit mode, no target). Once a level, before camera_reset. */
void camera_init(void);
/* CArInitialize 0x4d5a30 (the cm_* variables and cm_jello) and the script commands cm_anim, cm_anim_block,
   cm_interpolate, cm_interpolate_block, cm_orbit, cm_orbit_block, cm_detach, cm_wait, cm_reset (the
   script table at 0x4bc... registers them). After sl_reset. */
void camera_register_commands(void);

/* The environment the jello marks (the level's AKEV; NULL: no jello). */
void camera_set_environment(void *env);
/* The character followed (state +0x54); NULL for none, when the follow and orbit modes do nothing. */
void camera_set_target(const CameraTarget *t);
/* 0x4f1f40, which cm_interpolate and cm_reset call first: every active character's A+0x221c set to 6. */
void camera_set_cut_hook(void (*fn)(void));

/* CArUpdate 0x4d5ca0 (its path when no camera film records or plays): runs the mode for ticks ticks, then
   for each of the n manual input records. The game calls it once a tick with (1, 0, NULL) after the
   heartbeat, and once a frame with (0, n, inputs) (iComputeDeltaTicks 0x4fafb0). now: the game time in
   ticks, for the OBAN player. */
void camera_update(uint32_t ticks, int n, const CameraManualInput *inputs, uint32_t now);

/* The view the draw uses; false before any mode has set one. */
bool camera_view(CameraView *v);
/* The same as an OpenGL view matrix (column-major, a look along view with up). */
bool camera_view_matrix(float m[16]);

/* The mode entries. */
void camera_reset(void);                                    /* 0x4d73d0: follow, at the goal at once */
void camera_orbit(float speed, float stop);                 /* 0x4d6660: radians a tick; stop < 0: none */
bool camera_animate(const void *oban, uint32_t now);        /* 0x4d7510 */
void camera_interpolate(const float m[12], uint32_t ticks); /* 0x4d77c0: to a 3x4 matrix (by columns) */
void camera_interpolate_to(const CameraView *v, uint32_t ticks); /* 0x4d78e0 */
void camera_detach(void);                                   /* 0x4d7930 */
void camera_manual(void);                                   /* 0x4d7560 */
/* The developer key in 0x4f6200 (at 0x4f65e2): follow -> manual -> manual fly -> follow. */
void camera_manual_toggle(void);
void camera_enable_jello(bool on);                          /* CArEnableJello 0x4d59a0 */
bool camera_busy(void);                                     /* 0x4d7b60: +0x80 */
int camera_mode(void);

/* The quads the jello made see-through on the last update (indices into the AKEV's quads), for tests. */
int camera_jello_quads(const uint32_t **quads);
