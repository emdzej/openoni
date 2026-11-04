/* The AI's knowledge (AI2rKnowledge): what each AI character knows of the others, its contacts, kept
   in order of importance; what it sees (AI2iKnowledge_CheckVisible 0x49adf0) and what hurts it
   (AI2rKnowledge_HurtBy 0x49a7f0) posted as contacts, a tick later (AI2iKnowledge_PostContact
   0x49b3f0) told to the state (C+0xf80: AI2rAlert_NotifyKnowledge 0x48ecc0 for the calm states), and
   forgotten with time (AI2rKnowledge_Update 0x499e80). Notes: docs/re/ai.md.

   Not ported yet: sounds (AI2rKnowledge_Sound 0x49a8a0), projectiles and firing spreads to dodge, the
   alarm, the combat and pursuit states' notifications, cloaked characters, aiming's view direction. */
#pragma once
#include "ai.h"

#include <stdbool.h>
#include <stdint.h>

/* The kinds of event (entry +0x1c): 2..4 count as combat sounds (AI2rKnowledge_IsCombatSound 0x49ad70),
   5 and 6 sightings, 7 and 8 hurts. */
enum { KNOW_SOUND_UNIMPORTANT = 0, KNOW_SOUND_INTERESTING = 1, KNOW_SOUND_DANGER = 2, KNOW_SOUND_MELEE = 3,
       KNOW_SOUND_GUNFIRE = 4, KNOW_SEEN_PERIPHERAL = 5, KNOW_SEEN_CENTRAL = 6, KNOW_HURT = 7,
       KNOW_HURT_MELEE = 8, KNOW_ALARM = 9 };
/* Strengths (+0x24): 1 forgotten, 2 a hint, 3 heard or hit, 4 seen. */
enum { KNOW_FORGOTTEN = 1, KNOW_WEAK = 2, KNOW_STRONG = 3, KNOW_DEFINITE = 4 };

typedef struct KnowEntry {
    struct Chr *owner;          /* +0x00 */
    struct Chr *target;         /* +0x04: NULL for a sound with no one */
    uint32_t updated;           /* +0x08 */
    uint32_t posted;            /* +0x0c */
    float pos[3];               /* +0x10: where */
    int type;                   /* +0x1c: KNOW_* */
    uint32_t value;             /* +0x20: the damage, for hurts */
    int strength;               /* +0x24 */
    int last_strength;          /* +0x28: as last told to the state */
    int hostility;              /* +0x2c: 2 the owner is the target's enemy, 1 the other way, 3 forced */
    uint32_t first;             /* +0x34 */
    uint32_t degrade_at;        /* +0x38 */
    uint32_t damage;            /* +0x3c: taken from the target */
    uint32_t repeats;           /* +0x40: unimportant sounds in a row */
    int highest;                /* +0x44 */
    bool raised;                /* +0x48: the last post raised the highest */
    bool hurt, hurt_melee;      /* +0x49, +0x4a */
    bool hostile;               /* +0x4b: made hostile by its hits */
    uint32_t hostile_until;     /* +0x4c */
    struct KnowEntry *prev, *next;  /* +0x50, +0x54 */
} KnowEntry;

void knowledge_reset(void);
/* AI2iKnowledge_AddPending 0x49b300 (immediate posts: 0x49b3f0): owner learns of target at pos. */
void knowledge_post(struct Chr *owner, int type, int strength, const float pos[3], struct Chr *target,
                    uint32_t value, bool silent, bool force_hostile);
/* AI2rKnowledge_HurtBy 0x49a7f0. */
void knowledge_hurt_by(struct Chr *victim, struct Chr *attacker, uint32_t damage, bool melee);
/* A character leaves (AI2rKnowledge_ThisCharacterWasDeleted, AI2rKnowledge_Delete 0x49b990): the
   contacts of it and its own go. */
void knowledge_forget(struct Chr *c);
/* AI2rKnowledgeState_Forget 0x49c480: owner's contacts of target (NULL: all of them) forgotten, the state
   told of each (strength 1), then dropped. */
void knowledge_forget_contacts(struct Chr *owner, const struct Chr *target);
/* AI2rKnowledge_Update 0x499e80, once a tick before the characters. env: the AKEV for the sight lines. */
void knowledge_update(const void *env);
KnowEntry *knowledge_find(const struct Chr *owner, const struct Chr *target);   /* 0x49b8c0 */
/* AI2rTeam_FriendOrFoe 0x48b2c0: 0 friends, 1 neutral, 2 enemies. */
int ai_team_relation(uint32_t a, uint32_t b);
/* 0x48b1d0: whether a takes b for an enemy (known: also when made hostile by hits). */
bool ai_is_hostile(const struct Chr *a, const struct Chr *b, bool known);
