/* The AI's combat (AI2rCombat, the combat state of ai.h): the combat settings (the level's Combat
   objects), the range to the target and the behaviour for it, the maneuvers (hold, advance, retreat, a gun
   to pick up, melee), the targeting (AI2rTargeting: the target's motion led, the aim from the weapon, the fire
   decision) and the executor's aim, which turns the aim point into the character's aim angles.
   Notes: docs/re/ai.md, "Combat". */
#pragma once
#include "ai.h"

enum { COMBAT_RANGE_NONE = 0, COMBAT_RANGE_LONG = 1, COMBAT_RANGE_MEDIUM = 2, COMBAT_RANGE_SHORT = 3,
       COMBAT_RANGE_MEDIUM_RETREAT = 4, COMBAT_RANGE_LONG_RETREAT = 5 };
enum { COMBAT_MANEUVER_HOLD, COMBAT_MANEUVER_ADVANCE, COMBAT_MANEUVER_RETREAT, COMBAT_MANEUVER_GUN,
       COMBAT_MANEUVER_ALARM, COMBAT_MANEUVER_MELEE, COMBAT_MANEUVER_GETUP };

/* The level's Combat objects (OBJiCombat_Read 0x4c01c0), after the level's objects are loaded. */
void combat_load(void);
/* AI2rInitializeCharacter 0x48a6b0's copy: the Combat object of that id, else the defaults. */
void combat_settings(AiState *a, uint16_t id);
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

/* A debug line per second for each character in combat (set by the host for --param ailog=1). */
extern void (*combat_log)(const char *line);
