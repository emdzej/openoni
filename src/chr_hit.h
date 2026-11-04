/* Hits between characters: the contacts step of ONrGameState_UpdateCharacters (0x4e7130 and
   iCollideTwoLikelyCharacters 0x4e73e0), the attacks that land (0x4e8e70, HandleAttackMask 0x4e7740),
   the push apart (HandleGenericMask 0x4e8c50), and ONrCharacter_TakeDamage 0x4e65f0. Notes:
   docs/re/character.md, "Hits".

   Not ported yet: the AI's block decision (the AI doesn't block), the hit's material,
   impact effects and sounds, AI notification, throws, the difficulty
   and boss damage factors, in-air hits (0x4ecd70), the knockdown upgrades (0x4e8b80,
   ONrAnimType_UpgradeToKnockdown), and the death's own animation hook. */
#pragma once
#include "character.h"

/* The class's hit points (ONCC+0xc50) plus extra, at least 1 (ONrGameState_NewCharacter 0x4dac50). */
void chr_hit_init(Character *c, int32_t extra);
/* The contacts of a tick, after every character's frame: each two closer than 25 units, then the
   reactions the hits asked for. */
void chr_collide_all(Character **chars, int n);
/* ONrCharacter_TakeDamage 0x4e65f0: damage hit points, knockback along dir (the world, may be NULL),
   knockdown the reaction when it kills (0: the one asked, A+0x1b06). */
void chr_take_damage(Character *c, uint32_t damage, float knockback, const float dir[3], uint16_t knockdown,
                     Character *from);
/* What TakeDamage tells the AI of a character it hurt and didn't kill (0x48b480, for the AI's
   characters): from the attacker, melee (WPrOwner_MakeMeleeDamageFromCharacter 0x51ce00, kind
   0x2000000) or not. Set by the AI's side (chars.c). */
extern void (*chr_hurt_hook)(Character *c, Character *from, uint32_t damage, bool melee);
/* The death hook's scripts (ONrCharacter_Die_1_Animating_Hook 0x4eb550), as a character dies: set
   by chars.c, which keeps the scripts. */
extern void (*chr_die_hook)(Character *c);
/* The other scripts of ONrCharacter_TakeDamage 0x4e65f0, each deferred once (C+0x44's bits): the hurt
   script (C+0xc8, bit 0x10) at a hit that isn't stopped by invincibility; the defeated one (C+0xe8, bit
   0x20) at a hit on the dead, or the hit an unkillable character survives. Set by chars.c. */
enum { CHR_SCRIPT_HURT, CHR_SCRIPT_DEFEATED };
extern void (*chr_script_hook)(Character *c, int script);
/* What ONrCharacter_Knockdown tells the AI (0x48b530) of its character knocked down from not lying.
   Set by chars.c. */
extern void (*chr_knockdown_hook)(Character *c);
/* The AI characters' block function (C+0x15ec): whether c blocks an attack it could block. */
extern bool (*chr_block_hook)(Character *c);
/* A part's place in the world (the translation of its matrix, A+0x2278 + 0x30 part + 0x24): 0 the
   pelvis (ONrCharacter_GetPelvisPosition 0x4e34f0), 10 the head (0x4e3460). False if the body has no
   such part. */
bool chr_part_point(const Character *c, unsigned part, float out[3]);
/* ONrCharacter_Knockdown 0x4e4e10. */
void chr_knockdown(Character *c, uint16_t type);

/* The weapon node's matrix (the active record's matrix 19, A+0x2608): the fist's, turned by the hand
   (3x3 by columns, then the place). False without a body. */
bool chr_weapon_matrix(const Character *c, float out[12]);
