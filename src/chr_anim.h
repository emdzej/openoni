/* The character's animation engine: what ONI_Character.c does with a character's animations (switching,
   queueing, pausing, interpolating, posing), and the Totoro (TR) queries it makes of an animation.
   Format of the fields: docs/formats/tram.md; the records: docs/re/character.md. */
#pragma once
#include "character.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* TRAM fields the engine reads (instance offsets, as in tram.h). */
enum {
    TRAM_ATTACK_LIST = 0x0c,        /* .raw: 0x20 bytes each (parts u32, damage s16 at +0xc, frames +0xe..+0x10) */
    TRAM_THROW = 0x1c,              /* .raw: the throw, 0 if none */
    TRAM_LINKS = 0x38,              /* two TRAM links: the moves that follow without a pause */
    TRAM_FINAL_ROTATION = 0x48,     /* float: the turn the animation makes, applied at its end */
    TRAM_SOFT_PAUSE = 0x148,
    TRAM_HARD_PAUSE = 0x14a,
    TRAM_ATOMIC_START = 0x16c,
    TRAM_ATOMIC_END = 0x16e,
    TRAM_END_INTERPOLATION = 0x170,
    TRAM_MAX_INTERPOLATION = 0x172,
    TRAM_ACTION_FRAME = 0x174,
    TRAM_ATTACK_COUNT = 0x17a,      /* byte */
};

/* Bits of TRAM+0x34 (TRrAnimation_TestFlag). */
enum {
    TRAM_FLAG_ATTACK = 4, TRAM_FLAG_NO_TURN = 8, TRAM_FLAG_NO_VELOCITY_LERP = 14,
    TRAM_FLAG_THROW_SOURCE = 15, TRAM_FLAG_THROW_TARGET = 16,
    TRAM_FLAG_AIMING = 18,          /* the body aims (the aiming screens apply) */
    TRAM_FLAG_OVERLAY_NO_AIM = 19,  /* an overlay that, playing, stops the aiming (inferred) */
};

/* The flags of DoAnimation. */
enum {
    CHR_ANIM_WHEN_CAN = 1,  /* now if the current one may be cut, else queued */
    CHR_ANIM_QUEUE = 2,     /* when the current one ends */
    CHR_ANIM_NOW = 4,       /* set by DoAnimation itself: decided to play now */
    CHR_ANIM_FROM_PREVIOUS = 8, /* none from the state: retry from the previous one in the first 8 frames */
};

/* Animation types the engine itself names. */
enum {
    CHR_TYPE_WALK = 2, CHR_TYPE_RUN = 3, CHR_TYPE_SLIDE = 4, CHR_TYPE_STAND = 6, CHR_TYPE_STANDING_TURN_LEFT = 7,
    CHR_TYPE_STANDING_TURN_RIGHT = 8, CHR_TYPE_RUN_BACKWARDS = 9, CHR_TYPE_SIDESTEP_LEFT = 10,
    CHR_TYPE_SIDESTEP_RIGHT = 11, CHR_TYPE_KICK = 12, CHR_TYPE_WALK_BACKWARDS = 15, CHR_TYPE_CROUCH = 0x11,
    CHR_TYPE_PUNCH = 0x16, CHR_TYPE_IDLE = 0x5d, CHR_TYPE_RUN_STOP = 0xce, CHR_TYPE_WALK_STOP = 0xcf,
    CHR_TYPE_RUN_START = 0xd0, CHR_TYPE_WALK_START = 0xd1, CHR_TYPE_RUN_BACKWARDS_START = 0xd2,
    CHR_TYPE_WALK_BACKWARDS_START = 0xd3, CHR_TYPE_1STEP_STOP = 0xd8, CHR_TYPE_SIDESTEP_LEFT_START = 0xd9,
    CHR_TYPE_SIDESTEP_RIGHT_START = 0xda,
    CHR_STATE_STANDING = 7, CHR_STATE_CROUCHING = 0x13,
};

/* ---- TR queries ---- */
/* A .raw block of the animation: the field holding its offset, then at; len bytes inside the file; NULL if none. */
const uint8_t *tr_raw_block(const void *tram, unsigned field, size_t at, size_t len);
bool tr_test_flag(const void *tram, unsigned bit);              /* TRrAnimation_TestFlag 0x428a20 */
uint16_t tr_type(const void *tram);                             /* TRrAnimation_GetType 0x428710 */
uint16_t tr_from(const void *tram);                             /* TRrAnimation_GetFrom 0x428720 */
uint16_t tr_to(const void *tram);                               /* TRrAnimation_GetTo 0x428730 */
uint16_t tr_duration(const void *tram);                         /* TRrAnimation_GetDuration 0x428740 */
uint16_t tr_varient(const void *tram);                          /* TRrAnimation_GetVarient 0x428700 */
uint16_t tr_soft_pause(const void *tram, const void *next);     /* 0x428a40 */
uint16_t tr_hard_pause(const void *tram, const void *next);     /* 0x428a70 */
bool tr_is_attack(const void *tram);                            /* TRrAnimation_IsAttack 0x428aa0 */
bool tr_is_shortcut(const void *tram, uint16_t state);          /* TRrAnimation_IsShortcut 0x428b00 */
uint8_t tr_shortcut_length(const void *tram, uint16_t state);   /* 0x428b70 */
bool tr_shortcut_flag(const void *tram, uint16_t state, unsigned bit); /* 0x428bf0 */
bool tr_is_atomic(const void *tram, uint16_t frame);            /* TRrAnimation_IsAtomic 0x428c90 */
uint16_t tr_end_interpolation(const void *tram, const void *next); /* TRrAnimation_GetEndInterpolation 0x428cb0 */
float tr_height(const void *tram, uint16_t frame);              /* 0x428820 */
void tr_position(const void *tram, uint16_t frame, float v[3]); /* TRrAnimation_GetPosition 0x428750 */

/* The animation's attack i (TRAM+0x0c, 0x20 bytes), NULL if none. */
const uint8_t *tr_attack(const void *tram, unsigned i);
/* TRrCollection_Lookup 0x427f00 in the character's collection, for its variant. */
const void *chr_lookup(const Character *c, uint16_t type, uint16_t state);
/* The class's collection (ONCC+0xc80). */
const void *chr_collection(const Character *c);
const void *chr_lookup_varient(const Character *c, uint16_t type, uint16_t state, uint16_t varient);

/* ---- the character's animation ---- */
/* ONrCharacter_DoAnimation 0x4e4400: the animation of the type from the character's state, played,
   queued or refused; the animation (played or queued), or NULL. */
const void *chr_do_animation(Character *c, unsigned flags, uint16_t type);
/* ONrCharacter_NextAnimation 0x4e2ea0: at the end of the current one. */
void chr_next_animation(Character *c);
/* 0x4e41e0 (the Mac build matches it to ONrCharacter_SetAnimation_External, unchecked): change to tram
   with an interpolation of length ticks from the current pose. */
void chr_set_animation_interpolated(Character *c, uint16_t from_state, const void *tram, uint16_t length);
/* The direct change (as in NextAnimation, DoAnimation and 0x4d7f90): no interpolation. */
void chr_set_animation(Character *c, const void *tram);
/* 0x4eb340: a script's animation (chr_animate), interpolated over interp ticks if > 0. */
/* AI2rVocalize 0x48ba60 for an animation's cry (NewAnimationHook 0x4e97fb; chars.c sets it). */
extern void (*chr_vocalize_hook)(Character *c, uint32_t type);
void chr_set_animation_external(Character *c, uint16_t from_state, const void *tram, int32_t interp);
/* ONrCharacter_UpdateAnimationForVarient 0x4eb1a0. */
void chr_update_animation_for_varient(Character *c);
/* ONrCharacter_ForceStand 0x4f14c0. */
void chr_force_stand(Character *c);
/* 0x4eb0b0: fight mode (the variant 0x0200) for the class's fight time. */
void chr_enter_fight_mode(Character *c);
/* ONrCharacter_RecalculateIdleDelay 0x4dc130. */
void chr_recalculate_idle_delay(Character *c);
/* ONrCharacter_BuildQuaternions 0x4e5360, the part without aiming and overlays: the pose at the current
   frame, blended from the interpolation's start pose while interpolating. */
void chr_build_quaternions(Character *c);
/* The animation step of ONrGameState_DoCharacterFrame 0x4ddba0. chr_frame_countdowns is its step 6 (the
   hold, the pauses, the pelvis height), chr_frame_motion the root's motion of step 9 (blended while
   interpolating, in the character's space), chr_frame_advance step 17 (the frame, the interpolation, the
   fight timer, the end). */
void chr_frame_countdowns(Character *c);
void chr_frame_motion(const Character *c, float v[3]);
/* ONrCharacter_GetVelocityEstimate 0x4eafc0. */
void chr_velocity_estimate(const Character *c, float v[3]);
void chr_frame_advance(Character *c);

/* The victim types (0x4e36f0): hit, knocked down, thrown. */
bool chr_is_victim_type(uint16_t type);
/* The AI's part of the new-animation hook (AI2rMelee_NotifyAnimation 0x49fd10, for AI characters): set
   by the AI's side (chars.c). */
extern void (*chr_animation_hook)(Character *c);
/* A sound the AI hears (AI2rKnowledge_Sound 0x49a8a0) of c at target: set by the AI's side (chars.c). */
extern void (*chr_sound_hook)(Character *c, int type, const float pos[3], float radius, Character *target);
/* The throws' hooks (throw.c sets them): the animation about to start, which a throw may replace
   (RemapAnimationHook 0x4ea550), and a throw's start (its part of NewAnimationHook 0x4e97a0). */
/* ONrOverlay_Set 0x4d7b80. */
void chr_overlay_set(Character *c, int slot, const void *anim, uint32_t flags);
/* The overlays' step of DoCharacterFrame: a frame on, gone at their duration; chr_overlay_hook (the
   weapons') at slot 1's action frame. */
void chr_overlays_advance(Character *c);
extern void (*chr_overlay_hook)(Character *c, const void *anim);
extern const void *(*chr_remap_hook)(Character *c, const void *next);
extern void (*chr_throw_hook)(Character *c);
/* NewAnimationHook 0x4e97a0's turn for an attack (not a throw) starting: the player's character on the
   ground turns to the nearest enemy in front (throw.c sets it). */
extern void (*chr_attack_turn_hook)(Character *c);
/* NewAnimationHook 0x4e97a0's drop: an animation of TRAM flag 5 throws the hand's weapon (weapon.c sets it). */
extern void (*chr_drop_hook)(Character *c);
/* ONrCharacter_InBadVarientState 0x4e43d0: the current animation has variant bits the character lacks. */
bool chr_in_bad_varient(const Character *c);
/* The class's fight time (ONCC+0xc44) and idle delays (+0xc48, +0xc4c). */
uint16_t chr_class_fight_time(const void *oncc);

/* The health's colour (0x4ef450, the Mac build's ONrCharacter_GetPartLocation): the game settings'
   (ONGS "game_settings") thresholds at +0x18 (their count at +0x14) and colours at +0x58, lerped
   (0x447c10) between the two around the fraction. */
uint32_t chr_health_colour(float fraction);
/* The bones' matrices (A+0x2278) the animation's particles hang from, from the pose shown. */
void chr_bone_matrices(Character *c);
/* The active record's delete (0x4dc580): the super particles and the animation's killed, without
   their stop events. */
void chr_anim_particles_delete(Character *c);
/* The animation's particles (A+0x1ca4): DoCharacterFrame's start and stop by their frames, and their
   end (OldAnimationHook 0x4ead30, the character's deletion). */
void chr_anim_particles_frame(Character *c);
void chr_anim_particles_stop(Character *c);
/* HandleAttackMask 0x4e84f6: a landed blow pulses (event 1) the attacker's live animation particles in
   their frames on its attacking parts (a bone mask). */
void chr_anim_particles_pulse(Character *c, uint32_t parts);
/* Step 14's end (DoCharacterFrame 0x4df937..0x4dfafa): the super particles by the shown super value. */
void chr_super_particles(Character *c);
/* 0x4ef450: the colour of the character's health. */
uint32_t chr_health_tint(const Character *c);
