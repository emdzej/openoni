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
   (P3rCreateEffect). ai_scale scales the AI's sound's radius (the AI's hearing is not ported). */
void impact_effect(int type, int material, int modifier, const P3EffectLoc *loc, float ai_scale);
/* For the tests: the table's counts (impact names, material names, particles, sounds, entries). */
bool impact_effects_counts(uint32_t out[5]);
int impact_effects_unresolved(void);     /* the particles whose class the level hasn't */
