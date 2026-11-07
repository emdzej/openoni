/* The character's inventory over time (WPrInventory, ONI_Weapon.c): the hypos' healing and the health
   over the maximum running down, a hypo used, and the invisibility's countdown and fade
   (ONrCharacter_Invis_Update). Notes: docs/re/character.md, "The inventory over time". */
#pragma once
#include "character.h"

#include <stdbool.h>

/* WPrInventory_Update 0x51bad0, each tick in DoCharacterFrame after the pickup. */
void inventory_update(Character *c);
/* The regeneration cheat ("elderrune", 0x620134): the player heals a point a second. */
extern bool inventory_cheat_regeneration;
/* 0x51c770 for a hypo (the kind 2): one used if the health with the healing to come isn't over the
   maximum. True if used. */
bool inventory_use_hypo(Character *c);
/* ONrCharacter_Invis_Update 0x4ef530, late in DoCharacterFrame (and for the dying). */
void inventory_shield_update(Character *c);     /* ONrCharacter_Shield_Update 0x4ef710 */
void inventory_shield_hit(Character *c, uint32_t parts);    /* 0x4ef850: the parts' bits */
void inventory_invis_update(Character *c);
/* A hit (HandleAttackMask 0x4e7740: the attacker's ticks) or a throw (both): the invisible seen again
   (its ticks from 0) and its body shown to at least 0.6. */
void inventory_reveal(Character *c, bool ticks);
