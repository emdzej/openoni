/* The character (ONrCharacter, ONI_Character.c): a class, a place, and the animation state machine that
   moves from one animation to the next. One record here stands for the original's two: the character
   (0x16a0 bytes) and its active record (0x2838), with the fields the animation engine uses; the offsets
   are in the comments (C+ the character, A+ the active record; docs/re/character.md).

   Ported: the animation engine (chr_anim.h: ONrCharacter_DoAnimation 0x4e4400, NextAnimation 0x4e2ea0,
   the interpolated change 0x4e41e0, UpdateAnimationForVarient 0x4eb1a0, BuildQuaternions 0x4e5360), the
   animation step of ONrGameState_DoCharacterFrame 0x4ddba0, and the movement and attack handlers of
   ONrCharacter_HandleHeartbeatInput 0x4f6ad0 that depend only on the animation state and the input.
   Jumping and falling are ported (HandleStartJumping 0x4fa2f0, the landing and the fall of
   HandleHeartbeatInput, step 9 of DoCharacterFrame); the ground they meet is a stand-in.
   Stand-ins: the floor and walls (akev.h) instead of the physics, and the game time (a tick count per
   character). Not yet: AI, weapons, throws, damage, particles and sounds. */
#pragma once
#include "oban.h"

#include <stdbool.h>
#include <stdint.h>

enum { CHR_BONES = 19 };

/* A line to say (ONrSpeech_Say 0x516940's six words): its kind (the higher interrupts a lower one, 7
   always; 3 a conversation's line, 4 a neutral's call or abort, 6 its enemy line), the ticks before and
   after it, the flag set while it waits (NULL none), the ambient's name. */
typedef struct {
    uint32_t type, pre_delay;
    bool *waiting;
    char name[32];
    uint32_t post_delay;
} ChrSpeech;

/* One tick's input. turn is added to the desired facing (radians, + to the left): the mouse's part of
   HandleTurnInput 0x4f7b90. The keys map to the original's action bits (chr_input_bits); the other keys
   (crouch, jump, turn left and right, ...) go through character_update_keys. */
typedef struct {
    int forward;        /* 1 forward, -1 back */
    int strafe;         /* -1 step left, 1 step right */
    bool walk;
    float turn;
    float pitch;        /* added to the aim's pitch (radians, + up): the mouse's other part */
    bool punch, kick;   /* held this tick: a press is held for at least one tick */
    bool jump, crouch;
    bool action;        /* held (Ctrl: the exe's "bind leftcontrol to action") */
    bool fire, swap, drop, reload;  /* mouse 1, Q (pick up), E, R */
    bool hypo;          /* Tab ("bind tab to hypo") */
} CharacterInput;

/* An overlay (ONrOverlay_Set 0x4d7b80): an animation over the pose, 12 bytes from A+0x1c58. */
typedef struct {
    const void *anim;
    uint16_t frame;
    uint32_t flags;
} ChrOverlay;
enum { CHR_OVERLAYS = 4, CHR_OVERLAY_RECOIL = 0, CHR_OVERLAY_WEAPON = 1, CHR_OVERLAY_HIT = 2 };

/* The input block of the active record (A+0x2158): the mouse, then the action bits (u64) held, not held,
   gone down this tick and gone up. */
typedef struct {
    float turn_x, turn_y;
    uint64_t held, not_held, down, up;
} ChrInput;

/* The action bits (the binding table built at 0x4b7c00: "forward" 0x200000, ...). */
enum {
    CHR_KEY_FORWARD = 0x200000, CHR_KEY_BACK = 0x400000, CHR_KEY_TURN_LEFT = 0x800000,
    CHR_KEY_TURN_RIGHT = 0x1000000, CHR_KEY_STEP_LEFT = 0x2000000, CHR_KEY_STEP_RIGHT = 0x4000000,
    CHR_KEY_JUMP = 0x8000000, CHR_KEY_CROUCH = 0x10000000, CHR_KEY_PUNCH = 0x20000000, CHR_KEY_KICK = 0x40000000,
    CHR_KEY_LOOK = 0x8000,
};
#define CHR_KEY_WALK ((uint64_t)1 << 32)
#define CHR_KEY_ACTION ((uint64_t)2 << 32)
#define CHR_KEY_HYPO ((uint64_t)4 << 32)
#define CHR_KEY_RELOAD ((uint64_t)8 << 32)
#define CHR_KEY_SWAP ((uint64_t)0x10 << 32)
#define CHR_KEY_DROP ((uint64_t)0x20 << 32)
#define CHR_KEY_FIRE ((uint64_t)0x40 << 32)
#define CHR_KEY_ALT_FIRE ((uint64_t)0x80 << 32)

typedef struct Character {
    const void *oncc;               /* C+0x0c */
    uint32_t flags;                 /* C+0x04 (character.md: 1 dead, 0x10 leave at the end of the atomic part, ...) */
    float pos[3];                   /* C+0x148, the feet */
    float prev_pos[3];              /* C+0x154: before this tick's move (DoCharacterFrame step 12) */
    float facing;                   /* C+0x16c, radians [0, 2 pi) */
    float desired_facing;           /* C+0x170 */
    float super;                    /* C+0x1664: the super value (chr_super), the Daodan glow's */
    uint32_t poison_last;           /* C+0x1678: the game time of the last poison tick */
    int32_t poison_count;           /* C+0x167c: the poison's ticks counted */
    float super_level;              /* C+0x1668: the value shown, moved toward it each tick; Mutant Muro's
                                       shield (a class with ONCC+0xc8e is shielded above 0.5) */
    uint8_t hit_flash[4];           /* C+0x166c: the HUD's hit arcs (the ring's right, bottom, left and top
                                       quarters), 255 at a hit from that side, fading by 3 a tick */
    float facing_offset;            /* C+0x174: the diagonal or sideways run's turn, undone each tick */
    float pelvis_height;            /* C+0x18c */
    float scale;                    /* C+0x34: the body's size, random in the class's range */
    uint32_t last_action;           /* C+0x1e0, game time */
    uint32_t idle_delay;            /* C+0x1e4 */
    uint32_t sound_serial;          /* C+0x1dc: counts the animations started */
    uint32_t sound_heard;           /* A+0x1c9c: the serial of the last animation's noise */

    const void *anim;               /* A+0x1af8, TRAM */
    uint16_t state;                 /* A+0x1afc: the current animation's end state */
    uint16_t from_state;            /* A+0x1afe */
    uint16_t type;                  /* A+0x1b00 */
    uint16_t queued;                /* A+0x1b02: played when the current one ends, 0 none */
    uint16_t tick_type;             /* A+0x1b04: the type at the start of the tick */
    /* the level of detail (chr_lod.c): shown, wanted and the frames they have differed */
    int lod_base;                   /* A+0x2218: by the distance */
    int lod_shown;                  /* A+0x221c: 0 lowest .. 4, 6 none yet */
    int lod_wanted;                 /* A+0x2220 */
    uint16_t lod_frames;            /* A+0x2224 */
    float camera_d2;                /* C+0x1660: from the camera, squared */
    uint32_t unseen;                /* C+0x163c: frames out of the camera's view */
    bool interpolating;             /* A+0x1b08 */
    float interp_height;            /* A+0x1b0c */
    uint16_t interp_state;          /* A+0x1b10 */
    float interp_velocity[3];       /* A+0x1b14 */
    uint16_t interp_tick, interp_length;    /* A+0x1b20, 0x1b22 */
    float interp_quats[4 * CHR_BONES];      /* A+0x1b24 */
    uint32_t frame;                 /* A+0x1c88 (a u16 in the original) */
    uint16_t hard_pause, soft_pause;        /* A+0x1c8a, 0x1c8c */
    uint16_t next_interpolation;    /* A+0x1c8e */
    uint32_t hold;                  /* A+0x1c90 */
    uint16_t block_pause, stun_pause;       /* A+0x1c94, 0x1c96 */
    uint16_t countdown_1c98, countdown_1c9a;
    uint32_t last_particle;         /* A+0x18c: the particle that last hurt it (one that hits once) */
    uint16_t varient;               /* A+0x1ca0 */
    uint16_t fight_timer;           /* A+0x1ca2 */
    uint16_t combo_type;            /* A+0x1eb0: the last attack, for combos */
    uint16_t left_type;             /* the type of the animation last left (NewAnimationHook's old one) */
    uint32_t combo_time;            /* A+0x1eb4: when it ended (0xffffffff: playing) */
    /* throws (throw.h) */
    struct Character *throw_target; /* A+0x1edc: whom this throw throws */
    const void *throw_victim_anim;  /* A+0x1ee0: the target's animation for it (from the thrower's moves) */
    struct Character *threw;        /* A+0x1ed8: the last one thrown (an index in the original) */
    struct Character *thrown_by;    /* A+0x1eda: who attempted to throw this one (an index, 0xffff none) */
    float quats[4 * CHR_BONES];     /* A+0x1ef0: the pose, a quaternion (x, y, z, w) per bone */
    float aimed[4 * CHR_BONES];     /* A+0x2020: the pose with the overlays, what draws and touches */
    ChrOverlay overlay[CHR_OVERLAYS];       /* A+0x1c58 */
    /* weapons (weapon.h) */
    struct Weapon *weapon;          /* C+0x194: the one in the hand */
    struct Weapon *stowed[2];       /* C+0x198, 0x19c: the ones put away (holstered) */
    struct Weapon *pickup;          /* C+0x1d8: the one a pickup animation goes for */
    struct Powerup *pickup_powerup; /* C+0x1d8 too: the powerup a pickup animation goes for */
    /* the inventory (C+0x1a2..0x1ba, WPrInventory) */
    uint16_t ammo, hypos, cells;    /* C+0x1a2, 0x1a4, 0x1a6: ballistic clips, hypos, energy cells (6 at most) */
    uint16_t shield, invisibility;  /* C+0x1b8, 0x1ba: what is left of them */
    uint16_t shield_shown;          /* C+0x1b6: the shield's part the HUD shows, toward shield 2 a tick */
    uint16_t hypo_heal;             /* C+0x1ae: the hypo's healing still to come */
    uint16_t heal_wait;             /* C+0x1b0: ticks to the next point healed (or lost over the maximum) */
    uint32_t invisible_ticks;       /* C+0x1bc: ticks invisible since the last hit (the AI sees through
                                       the first 90) */
    bool invisible_shown;           /* A+0x2815: the invisibility's effect on */
    float visibility;               /* A+0x2818: how much of the body shows (1 all), while it is on or a
                                       teleport (0xde) fades it (C+4 0x40000) */
    bool lsi;                       /* C+0x1b4: carries the LSI */
    uint16_t drop_ammo, drop_hypos, drop_cells;     /* C+0x1a8, 0x1aa, 0x1ac: dropped at death */
    bool drop_shield, drop_invisibility;            /* C+0x1b2, 0x1b3: a shield, an invisibility dropped */
    float aim_kick;                 /* C+0x1d0: the recoil's speed (class +0x80 a shot) */
    float recoil;                   /* C+0x1d4: the recoil, raising the aim (ONrCharacter_BuildQuaternions) */
    bool fire_down, fire2_down, fire_again, fire_empty, stop_fire, stop_fire2;  /* A+0x21e0, 0x21e1, 0x21df, 0x21e2, 0x21e3, 0x21e4 */
    bool reload_wanted;             /* C+0x08 bit 0: fire went down with an empty weapon and nothing to reload */
    bool reload_key;                /* the reload key went down this tick (A+0x217c bit 3, read by DoCharacterFrame) */
    float aim_lr, aim_ud;           /* A+0x21bc, 0x21c0 */
    float aim_lr_add, aim_ud_add;   /* A+0x21cc, 0x21d0: added to the aim (set by the AI's aiming: 0 here) */
    const void *aim_screen;         /* A+0x1ee4: the aiming screen (TRSC data) in use */
    uint32_t aim_screen_time;       /* A+0x1ee8: when it changed */
    const void *aim_screen_prev;    /* A+0x1eec: the one it fades from */
    bool turning_left, turning_right;       /* A+0x21e5, 0x21e6: a standing turn under way */

    uint64_t held;                  /* the previous tick's held bits (DoFilmUpdate's A+0x2188) */
    uint32_t film_flags;            /* A+0x21f4: 1 playing, 2 recording, 4 moving to the film's start */
    uint32_t film_frame;            /* A+0x21f8 */
    const void *film;               /* A+0x2214, the FILM */
    uint32_t film_interp;           /* A+0x2204: the ticks of the move to the start left */
    float film_delta[3];            /* A+0x2208: that move, a tick's part */
    uint32_t time;                  /* stand-in for the game time (GS+0x144): ticks of this character */
    /* jumping and falling (DoCharacterFrame's step 9, docs/re/character.md) */
    uint16_t no_ground_ticks;       /* A+0xe0: at 12 a fall starts */
    float fall_height;              /* A+0xf0: where the ground went (for the landing's damage) */
    uint16_t jumped;                /* A+0xf4: 1 for a jump, 0 for a fall */
    uint16_t air_ticks;             /* A+0xf6: 0 on the ground */
    uint16_t settled_ticks;         /* A+0x2830: dying, still on the ground */
    uint16_t dying_ticks;           /* A+0x2832 */
    float air_velocity[3];          /* A+0xf8, per tick, in the character's space */
    /* the teleport (the super ninja's behaviours, boss.c): set (A+0x178), the point (A+0x17c) and the
       facing (A+0x188) that the animation 0xdf (a teleport's second half) puts the character at */
    bool teleport;
    float teleport_to[3];
    float teleport_facing;
    /* the animation's particles (A+0x1ca4..0x1da8, set up by NewAnimationHook): each the class's ONCP
       entry, the particle (by handle) and its class, the bone it hangs from */
    uint8_t anim_particles;         /* A+0x1ca4 */
    uint16_t anim_particles_on;     /* A+0x1ca6: those in their frames, a bit each */
    struct {
        const uint8_t *entry;
        uint32_t handle;
        void *cls;
        uint16_t bone;
    } anim_particle[16];            /* A+0x1ca8, 0x10 each */
    float bone_world[19][12];       /* A+0x2278 (0x30 each): the bones' matrices the particles hang from */
    bool placed;                    /* put somewhere (0x4e9210) since the bones' last matrices: the location
                                       (C+0x160) is the position, y + 0.5 */
    float jump_velocity[3];         /* A+0x104: the motion as the jump was asked for */
    bool jump_given;                /* A+0x110 */
    uint16_t jump_delay;            /* A+0x112 (nothing in v1.0 sets it) */
    bool start_falling;             /* A+0x21dc */
    bool landing, hard_landing;     /* A+0x21dd, 0x21de */
    /* hits (chr_hit.h) */
    uint16_t id;                    /* C+0x00: the slot, what attack lists hold (0 none) */
    bool ai;                        /* C+0x1e8 != 0: not the player's */
    uint32_t hit_points;            /* C+0x38 */
    uint32_t max_hit_points;        /* C+0x3c */
    float knockback[3];             /* A+0xb8, in the world */
    float hit_velocity[3];          /* A+0xd4: the last hit's direction times its damage (5 to 15) */
    uint16_t knockdown_type;        /* A+0x1b06: the reaction asked by a hit, played after the contacts */
    uint16_t hit_list[8];           /* A+0x1eb8: who the current animation's attacks have hit */
    /* the body and the physics (docs/re/physics.md) */
    float bottom, top;              /* C+0x190, C+0x1658: the body box's (0x4e5ff0) */
    float spheres[7][4];            /* A+0x08: the sphere tree: the root, 4 leaves, 2 middles */
    float phys_pos[3];              /* the physics context's position (+0x84) */
    float phys_accel[3];            /* its acceleration (+0x11c), the move added in */
    float move[3];                  /* A+0xc4..0xcc: the tick's move, x, the rise, z */
    float drop;                     /* A+0xd0: the move down */
    int32_t ground_quad;            /* A+0x128: the floor's quad, -1 none */
    float ground_height;            /* A+0x12c */
    float ground_point[3];          /* A+0xe4 */
    uint16_t wall_hits;             /* A+0xb0: walls met this tick */
    uint16_t pushes;                /* A+0xb4: the characters it was pushed off this tick (0x4e8c50) */
    bool blocked_by_char;           /* A+0xb6: pushed off a character its AI would path through
                                       (AI2rTryToPathfindThrough 0x48b670) */
    bool phys_pending;              /* its frame ran with chr_physics_later: physics_update is due */
    bool has_physics;               /* A+0x04 != 0 */
    /* The physics context's animation (chr_envanim: P+0x130, its state +0x134 with 2 for no rotation,
       the frame +0x136, the step +0x138, the time +0x13c): while it plays the character is where it is. */
    ObanPlayer envanim;
    /* the console action (action.h) */
    int32_t console_marker;         /* C+0x1680: the action marker it goes to use, -1 none */
    uint32_t console_deadline;      /* C+0x1684: the time it has to reach it by, 0 not yet set */
    uint32_t sprint_time;           /* A+0x2190: the last forward press, 0xffffffff sprinting */
    /* the speech (ONrSpeech, neutral.h): saying, begun, over (C+0x15f0..0x15f2), the sound's id and
       when it counts as over (+0x15f4, +0x15f8), the line and the one queued (+0x15fc, +0x1614) */
    bool speaking, speech_begun, speech_over;
    uint32_t speech_id, speech_end;
    ChrSpeech speech, speech_next;
    /* a neutral's conversation (neutral.h): the other one (C+0x162c) and the player's moves that end
       it (C+0x1630) */
    struct Character *talk_partner;
    uint32_t talk_moves;
} Character;

bool character_init(Character *c, const void *oncc, const float pos[3], float facing);
/* ONrCharacter_SetCharacterClass 0x4d7c30, for a living character: false if the class lacks its parts. */
bool chr_set_class(Character *c, const void *oncc);
/* The location (C+0x160, docs/re/character.md "The location"), which the port doesn't keep apart: the
   pelvis's x and z; for y the body's bottom (C+0x190), or at least the ground's height (A+0xe8) while on
   the ground; then 0.5 up. */
void chr_location(const Character *c, float out[3]);
/* The weapons' parts (weapon.c): HandleStartPickup (true if it took the input), the weapon's keys of
   HandleHeartbeatInput, and two steps of DoCharacterFrame: the pickup animation's action frame (at the
   start) and the trigger (after the overlays). */
enum { CHR_FRAME_PICKUP, CHR_FRAME_FIRE };
/* DoCharacterFrame 0x4df571: at the animation's action frame (TRAM +0x174), after the pickup's, an AI's
   combat behaviour hears of it (message 11, chars.c). */
extern void (*character_action_frame_hook)(Character *c);
/* HandleStartSidestep 0x4f83f0's 0x4a93c0: a character the AI drives sidestepped with a turned run
   (walking or not): its movement moves sideways no more in that mode (chars.c). */
extern void (*character_sidestep_hook)(Character *c, bool walk);
extern bool (*character_pickup_hook)(Character *c, const ChrInput *in);
extern void (*character_weapon_input_hook)(Character *c, const ChrInput *in);
extern void (*character_frame_hook)(Character *c, int step);
/* One tick (1/60 s): the input (HandleHeartbeatInput), then the frame (DoCharacterFrame: the motion, the
   floor, the animation step, the pose). env: the AKEV for the floor stand-in, or NULL. */
void character_update(Character *c, const CharacterInput *in, const void *env);
/* Set while chars_update runs the frames: the characters' physics then waits for the contacts (the
   original runs every context after all the characters' frames and their pushes, 0x4e8c50); the frames
   mark it pending (phys_pending). Clear, each frame runs its own physics right away. */
extern bool chr_physics_later;
/* HandleStartTauntOrAction 0x4f80d0's part with the objects (action.h), in HandleHeartbeatInput after
   the jump: true if it took the input. NULL (the default): the taunt only. */
extern bool (*character_action_hook)(Character *c, const ChrInput *in);
/* The same from the action bits held (CHR_KEY_*) and the mouse's turn: what went down and up this tick
   comes from the last tick's bits. */
void character_update_keys(Character *c, uint64_t held, float turn, const void *env);
/* The same with the mouse's pitch too. */
void character_update_aim(Character *c, uint64_t held, float turn, float pitch, const void *env);
/* The same from a whole input block (the AI's executor makes its own "went down" bits). */
void character_update_input(Character *c, const ChrInput *in, const void *env);
/* 0x4e3d00: the character doesn't turn to its desired facing by itself now. */
bool chr_holds_facing(const Character *c);
/* 0x4e3230: how far the facing is from the desired one (signed, the long way round kept as the turn
   makes it). */
float chr_facing_delta(const Character *c);
/* The key state of a CharacterInput as the action bits held. */
uint64_t chr_input_bits(const CharacterInput *in);
/* The input with only the keys of those action bits (lock_keys). The mouse's turn and pitch are
   not keys; the turn keys (not in any group) are the port's arrow keys, left as they are. */
void chr_input_mask(CharacterInput *in, uint64_t keys);
/* 0x4f5be0: the diagonal or sideways run's turn (C+0x174) set to a, the desired facing with it. */
void character_set_facing_offset(Character *c, float a);
/* C+0x04 0x400000: the AI's movement drives the character (every AI's; the player's with ai2_takecontrol). */
enum { CHR_FLAG_AI_MOVEMENT = 0x400000 };
/* C+0x04 0x10000000: no AI or a boss (CHAR or setup flag 0x400; AI2rInitializeCharacter 0x48a6b0). */
enum { CHR_FLAG_BOSS = 0x10000000 };

/* The body's root (FUN_004e5a30's start): T(position + the pelvis's height) R(facing about y); while an
   environment animation plays, its matrix at the frame and a quarter turn about x (none with no
   rotation: its translation alone, the character turned no way). */
void chr_root_matrix(const Character *c, float out[12]);
/* The parts' matrices of the pose (A+0x2278, built by 0x4e5a30): the aimed pose under the root, the
   head grown with chr_big_head (0x5ecb98, the script variable "Draws everyone with a big head"). False
   for a body that isn't the characters' 19 parts. */
extern bool chr_big_head;
/* chr_mini_me (0x5ecb99, "Draws the main character small"): the player at half the size. */
extern bool chr_mini_me;
bool chr_pose_matrices(const Character *c, float (*m)[12]);
/* chr_envanim's start (AI2iScript_EnvAnimate_Internal 0x4b7010): the animation from its start, a frame
   on; NULL stops it (chr_envanim_stop 0x4b7240). */
void chr_envanim_start(Character *c, const void *oban, bool no_rotation);
bool chr_envanim_playing(const Character *c);
