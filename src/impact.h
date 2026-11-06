/* The impact effects: what a hit of an impact type on a material makes (ONrImpactEffect 0x4fe5b0), from
   the impact types' and the materials' trees (MArImpacts_PreProcess 0x4874a0, MArMaterials_PreProcess
   0x487aa0) and the game's table of effects (the BINA ONIE block, 0x4fe9b0). Notes: docs/re/impact.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "particle.h"

/* The trees, once level 0 is loaded (the templates' preprocess): each type's index is its place in a
   walk from the root down, the children of a node by name. */
void impact_types_load(void);
/* MArImpactType_GetByName 0x487140 (the Impt instance's name, without the tag); -1 none. */
int impact_type_by_name(const char *name);
/* The material's index (Mtrl+0); -1 for none. MArMaterialType_GetByName 0x4871d0 by name. */
int impact_material_index(const void *mtrl);
int impact_material_by_name(const char *name);
int impact_type_count(void);
int impact_material_count(void);
/* The parent of a type or material (-1 at the root), a material's depth, a type's "blunt" mark (it or
   one above it is Blunt), a material's glass mark (Glass and below, but not Unbreak_Glass and below). */
int impact_type_parent(int type);
bool impact_type_blunt(int type);
int impact_material_parent(int material);
int impact_material_depth(int material);
bool impact_material_glass(int material);

/* The index of an impact type's instance (Impt; the first u16 the preprocess numbers), -1 none. */
int impact_type_of(const void *impt);
/* The melee's impact effect of x's blow on d (HandleAttackMask 0x4e86f2..): e the attack, outcome 0 hit,
   1 blocked, 3 killed; the attacks' parts (attack) and d's parts they touched (defend). */
void impact_melee(Character *x, Character *d, const uint8_t *e, int outcome, uint32_t attack, uint32_t defend);
/* Its impact type (-1 none) and modifier (0: by the damage) for an attack of that name with the parts;
   pulse: the attacker's particles on the parts pulsed, as a blow does. */
int impact_melee_type(const Character *x, const char *attack_name, int outcome, uint32_t attack, int *modifier,
                      bool pulse);

/* 0x4f04f0: the class's animation impact (0..14) of an animation type, -1 none. */
int impact_animation_index(uint16_t anim_type);
/* ONrCharacter_PlayAnimationImpact 0x4f0e80: the character's animation impact (a footstep) on its floor,
   at pos (NULL: its place). */
void impact_animation(Character *c, const float *pos);

/* DoCharacterFrame 0x4df6d2: a throw's impact particle coming in with its victim dead (C+4 1) and not
   yet so struck (0x20000000): the "Self_Damage_Killed" impact at c's bone, then the victim marked. */
void impact_throw_killed(Character *c, unsigned bone, Character *victim, Character *other);

/* ONrCharacter_GetMaterialType 0x4e6200: the material of the character's body part (0..18). */
int impact_character_material(const Character *c, int part, bool ignore_shield);

/* The ONIE table (level 0's), its particle classes and impulses found again at each level's start (once
   the level's particle classes are loaded). */
void impact_effects_load(void);
void impact_effects_level_begin(void);

/* ONiImpactEffect_Lookup 0x4fe7f0: the entries for the three components (-1 none) of a hit of the type on
   the material with the modifier (0 any, 1 heavy, 2 medium, 3 light). */
void impact_lookup(int type, int material, int modifier, int32_t out[3]);
/* ONrImpactEffect 0x4fe5b0: the entries' sounds at the place, and their particles created there
   (P3rCreateEffect). An entry's sound with flag 1 is also one the AI hears (AI2rKnowledge_Sound), its
   radius scaled by ai_scale, of source at target (either may be NULL). */
void impact_effect(int type, int material, int modifier, const P3EffectLoc *loc, float ai_scale, Character *source,
                   Character *target);
/* For the tests: the table's counts (impact names, material names, particles, sounds, entries). */
bool impact_effects_counts(uint32_t out[5]);
int impact_effects_unresolved(void);     /* the particles whose class the level hasn't */
