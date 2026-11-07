/* Hits between characters: the contacts step of ONrGameState_UpdateCharacters (0x4e7130 and
   iCollideTwoLikelyCharacters 0x4e73e0), the attacks that land (0x4e8e70, HandleAttackMask 0x4e7740),
   the push apart (HandleGenericMask 0x4e8c50), and ONrCharacter_TakeDamage 0x4e65f0. Notes:
   docs/re/character.md, "Hits".

   The pain (ONrCharacter_Pain 0x4f2250): the hurt's cries. Not ported yet: in-air hits (0x4ecd70),
   Knockdown's in-air remap, and TakeDamage's fight cookie for a blow's owner (0x4998f0). */
#pragma once
#include "character.h"

/* The class's hit points (ONCC+0xc50) plus extra, at least 1 (ONrGameState_NewCharacter 0x4dac50). */
void chr_hit_init(Character *c, int32_t extra);
/* The contacts of a tick, after every character's frame: each two closer than 25 units, then the
   reactions the hits asked for. */
void chr_collide_all(Character **chars, int n);
/* ONrCharacter_TakeDamage 0x4e65f0: damage hit points, knockback along dir (the world, may be NULL),
   knockdown the reaction when it kills (0: the one asked, A+0x1b06); kind the damage owner's kind (its
   top byte: 0x1000000 a weapon's, 0x2000000 a blow's or a throw's (WPrOwner_MakeMeleeDamageFromCharacter
   0x51ce00), 0x5000000 a fall's, 0x6000000 the kill command's, 0 none). */
/* ONrCharacter_Die_1_Animating_Hook 0x4eb550: dead (no more animations asked), the die hook, then
   ONiCharacter_RunDeathAnimation 0x4eb970. */
void chr_die(Character *c, Character *killer);
void chr_take_damage(Character *c, uint32_t damage, float knockback, const float dir[3], uint16_t knockdown,
                     Character *from, uint32_t kind);
enum { CHR_DAMAGE_WEAPON = 0x1000000, CHR_DAMAGE_MELEE = 0x2000000, CHR_DAMAGE_FALL = 0x5000000,
       CHR_DAMAGE_KILL = 0x6000000 };
/* What TakeDamage tells the AI of a character it hurt and didn't kill (0x48b480, for the AI's
   characters): from the attacker, with the damage's owner kind. Set by the AI's side (chars.c). */
extern void (*chr_hurt_hook)(Character *c, Character *from, uint32_t damage, uint32_t kind);
/* 0x4998f0, a blow or a throw between characters: an AI fighting (the combat state) with the melee
   cookie keeps it longer for the damage it deals, and loses it sooner for the damage it takes (chars.c
   sets it). */
/* AI2rNotifyAttackLanded 0x48b700 and 0x48b9d0, an attack landing on an AI's character, blocked or
   not: a block held against its target's attack let go, a daze cut to its last tick (chars.c sets
   it). */
extern void (*chr_attack_landed_hook)(Character *attacker, Character *defender);
extern void (*chr_melee_score_hook)(Character *attacker, Character *victim, uint32_t damage);
/* 0x48b060 for an AI hurt: in a calm state (the table 0x52a4d8; a patrol ignoring the player not) it is
   startled (C+0x1f0 0x8000) and cries for sure (chars.c sets it). */
extern bool (*chr_calm_hurt_hook)(Character *c);
/* DoCharacterFrame's pain (0x4e07ef): the damage summed forgotten in time, the queued cry played. */
void chr_pain_frame(Character *c);
/* The death hook's scripts (ONrCharacter_Die_1_Animating_Hook 0x4eb550), as a character dies: set
   by chars.c, which keeps the scripts. */
extern void (*chr_die_hook)(Character *c, Character *killer);
/* The other scripts of ONrCharacter_TakeDamage 0x4e65f0, each deferred once (C+0x44's bits): the hurt
   script (C+0xc8, bit 0x10) at a hit that isn't stopped by invincibility; the defeated one (C+0xe8, bit
   0x20) at a hit on the dead, or the hit an unkillable character survives. Set by chars.c. */
enum { CHR_SCRIPT_HURT, CHR_SCRIPT_DEFEATED };
extern void (*chr_script_hook)(Character *c, int script);
/* What ONrCharacter_Knockdown tells the AI (0x48b530) of its character knocked down from not lying.
   Set by chars.c. */
extern void (*chr_knockdown_hook)(Character *c);
/* AI2rTryToPathfindThrough 0x48b670, for a character the AI's movement drives (C+4 0x400000) pushed off
   another: whether its path goes through that one (chars.c). */
extern bool (*chr_path_through_hook)(const Character *c, const Character *o);
/* The AI's movement told of what a character it drives (C+4 0x400000) ran into (movement.h): kind 0 a
   wall (id its quad), 1 a physics context (id its owner), 2 a character; the point hit and the plane
   facing the character (chars.c). */
enum { CHR_HIT_WALL, CHR_HIT_OBJECT, CHR_HIT_CHARACTER };
extern void (*chr_movement_hit_hook)(Character *c, int kind, uintptr_t id, const float point[3], const float plane[4]);
/* The player's cheat switches (ONrCheater 0x4f5c30, and the game's variables of 0x4fbf10): unstoppable
   (G 0x5ece91), invincible (0x5ece92), omnipotent (0x5ece93: the player's blows and throws deal at least
   the victim's hit points); unthrowable (0x5ece90) has no variable. All cleared at the level's start
   (ONrCharacter_LevelBegin 0x4d87b0). */
extern bool chr_cheat_unthrowable, chr_cheat_unstoppable, chr_cheat_invincible, chr_cheat_omnipotent;
/* Fists of legend (0x5ece8f): the player's blows knock down (reaction 0xd6); cleared with the others. */
extern bool chr_cheat_fists;
void chr_cheats_clear(void);
/* Unstoppable: C+0x04 0x100 (chr_unstoppable), or the player with the cheat. */
bool chr_is_unstoppable(const Character *c);
/* 0x4e6530: the HUD's hit arc for a hit from dir (NULL: none). 0x4e64d0: the super shield up. */
void chr_hit_direction(Character *c, const float dir[3]);
bool chr_super_shielded(const Character *c);
/* 0x4ef8e0: the bits of the parts within r of p (their bounding spheres). */
uint32_t chr_parts_near(const Character *c, const float p[3], float r);
/* The AI characters' block function (C+0x15ec): whether c blocks an attack it could block. */
extern bool (*chr_block_hook)(Character *c);
/* A part's place in the world (the translation of its matrix, A+0x2278 + 0x30 part + 0x24): 0 the
   pelvis (ONrCharacter_GetPelvisPosition 0x4e34f0), 10 the head (0x4e3460). False if the body has no
   such part. */
bool chr_part_point(const Character *c, unsigned part, float out[3]);
/* ONrCharacter_Knockdown 0x4e4e10. */
void chr_knockdown(Character *c, uint16_t type);
/* ONrAnimType_IsKnockdown 0x4eccb0: a reaction that leaves the character down. */
bool chr_is_down_type(uint16_t t);

/* The weapon node's matrix (the active record's matrix 19, A+0x2608): the fist's, turned by the hand
   (3x3 by columns, then the place). False without a body. */
bool chr_weapon_matrix(const Character *c, float out[12]);
/* ONrCharacter_CouldBlock 0x4e62b0: d could block an attack of x now; high and low, at which heights. */
bool chr_could_block(const Character *d, const Character *x, bool *high, bool *low);
