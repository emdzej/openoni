/* The AI's combat (AI2rCombat, the combat state of ai.h): the combat settings (the level's Combat
   objects), the range to the target and the behaviour for it, the maneuvers (hold, advance, retreat, a gun
   to pick up, melee), the targeting (AI2rTargeting: the target's motion led, the aim from the weapon, the fire
   decision) and the executor's aim, which turns the aim point into the character's aim angles.
   Notes: docs/re/ai.md, "Combat". */
#pragma once
#include "ai.h"

enum { COMBAT_RANGE_NONE = 0, COMBAT_RANGE_LONG = 1, COMBAT_RANGE_MEDIUM = 2, COMBAT_RANGE_SHORT = 3,
       COMBAT_RANGE_MEDIUM_RETREAT = 4, COMBAT_RANGE_LONG_RETREAT = 5 };
/* The behaviours' messages (their handlers' second argument; the default AI2rBehavior_Default 0x493120
   takes those a behaviour leaves). */
enum { MSG_ENTER = 0, MSG_WEIGHTS = 1, MSG_EXIT = 2, MSG_REPORT = 3, MSG_MELEE = 4, MSG_FLAGS = 5,
       MSG_DAZE = 6, MSG_CLOSE = 7, MSG_FIRE = 8, MSG_KEEP_AIM = 9, MSG_HURT = 10, MSG_ANIM_START = 11,
       MSG_ANIM_END = 12, MSG_DODGE = 13, MSG_STATE = 14, MSG_RANGE = 15 };
enum { COMBAT_MANEUVER_HOLD, COMBAT_MANEUVER_ADVANCE, COMBAT_MANEUVER_RETREAT, COMBAT_MANEUVER_GUN,
       COMBAT_MANEUVER_ALARM, COMBAT_MANEUVER_MELEE, COMBAT_MANEUVER_GETUP };

/* The level's Combat objects (OBJiCombat_Read 0x4c01c0), after the level's objects are loaded. */
void combat_load(void);
/* AI2rInitializeCharacter 0x48a6b0's copy: the Combat object of that id, else the defaults. */
void combat_settings(AiState *a, uint16_t id);
/* The settings of a character with no combat profile (FUN_004936b0's defaults). */
void combat_default_settings(AiState *a);
/* AI2rCombat_Enter 0x48f290's resets: the maneuver (0x49db70: none, the gun's search and the guns out
   of reach forgotten) and the gun's chances. */
void combat_enter(AiState *a);
/* AI2rCombat_SetNewTarget 0x4908e0's targeting part: the targeting's new target, the range to it and
   the behaviour for that range. */
void combat_new_target(AiState *a, Character *c, const Character *target);
/* The rest of AI2rCombat_Update 0x48f3e0, once the lost target and the pursuit are dealt with
   (has_target: the target is there this tick). */
void combat_tick(AiState *a, Character *c, const void *env, bool has_target);
/* AI2rExecutor_UpdateAim 0x498f50 and the AI's aim in DoCharacterFrame 0x4dccf0: the aim point
   turned toward the wanted one at the aiming speed, then into the character's aim angles. Every
   tick of a character the AI moves, before its input. */
void combat_aim(AiState *a, Character *c);
/* AI2rCombat_NotifyKnowledge 0x490510's message 10: a hit (contact e, of type 7 or 8) to the behaviour. */
struct KnowEntry;
void combat_hurt(AiState *a, const Character *c, const struct KnowEntry *e);

/* A debug line per second for each character in combat (set by the host for --param ailog=1). */
extern void (*combat_log)(const char *line);
