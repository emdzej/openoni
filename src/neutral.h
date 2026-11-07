/* The neutral characters (AI2rNeutral, ONI_AI2_Neutral.c): an AI character with a Neutral behaviour (the
   level's NEUT objects, by the CHAR's id or ai2_neutralbehavior) calls the player over, or is asked with
   the action key, and runs a conversation: lines said in turn by it and the player, with their
   animations, a give of weapons and powerups, then the behaviour's end script. And the characters'
   speech (ONrSpeech): one line at a time, a higher kind interrupting, one more queued. Notes:
   docs/re/ai.md, "Neutral characters". */
#pragma once
#include "chars.h"

/* The flags of the behaviour (C+0x1000). */
enum {
    NEUTRAL_ENTERED = 0x01,     /* the neutral state was entered */
    NEUTRAL_TALKED = 0x02,      /* a conversation began */
    NEUTRAL_GAVE = 0x04,        /* its give was made */
    NEUTRAL_FINISHED = 0x08,    /* its last line was said */
    NEUTRAL_DONE = 0x10,        /* no more conversations (the Neutral's flag 1 once begun, 2 once given) */
};

/* AI2rNeutral_ChangeBehavior 0x4ac870: c takes the Neutral of the id (-1 none). Unless force, the one it
   has already is kept. True if it has one. */
bool neutral_change_behavior(Chr *c, int id, bool force);
/* The id of the Neutral of that name (the NEUT objects'), -1 none. */
int neutral_id_named(const char *name);
/* AI2rUpdateAI 0x489d40's parts: the end script due (flag 0x20), first for every AI character; then,
   before its state's update, whether the player comes near enough to start a conversation by itself,
   or the wait before it may. */
void neutral_end_script(Chr *c);
void neutral_check(Chr *c);
/* The neutral state (C+0x1f8 8): AI2rNeutral_Enter 0x4acdb0, the update 0x4ad1f0, the exit 0x4acf20. */
void neutral_enter(Chr *c);
void neutral_update(Chr *c);
void neutral_exit(Chr *c);
/* AI2rTryNeutralInteraction 0x48bb40: whether a neutral is there to talk to with the action key (in a
   conversation already: true); start: it begins. */
bool neutral_try_interaction(Chr *player, bool start);
/* 0x4fa520's part: during a conversation the player's keys are taken; firing or a blow ends it, and so
   does moving long enough (unless it can't be ended). */
void neutral_filter_input(Chr *player, CharacterInput *in);
/* AI2rNeutral_Stop 0x4acf70: c's conversation ends (none: true); refused during the neutral's give
   unless forced. Aborted, the neutral waits before it may start again; else c runs the end script. */
bool neutral_stop(Chr *c, bool force, bool aborted, bool enemy, bool to_job);
/* AI2rCharacter_IsDangerousContact 0x4ae070: whether c's contact e means danger near pos. */
struct KnowEntry;
bool ai_dangerous_contact(const Chr *c, const Chr *other, bool known, const struct KnowEntry *e, const float pos[3]);
/* A character dies (0x48b370's part: its conversation is ended) or goes (0x48b3e0's: its partner
   forgotten). */
void neutral_died(Chr *c);
void neutral_deleted(Chr *c);

/* AI2rVocalize 0x48ba60: c's cry of a type (0 taunt, 1 alert, 2 startle, 3 checkbody, 4 pursue, 5 cower,
   6 superpunch, 7 superkick, 8 super3, 9 super4), the class's ambient (ONCC+0x2b4, 0x20 a type) said at the type's
   priority (0x52a584), unforced only by the class's chance (ONCC+0x2a8, percent); none for a fighter's
   taunt once it taunted (+0x76). True when said or queued. */
bool ai_vocalize(Chr *c, uint32_t type, bool force);
/* ONrSpeech_Say 0x516940: 0 said now, 1 queued, 2 refused. ONrSpeech_Update 0x516a60, a tick of it. */
int speech_say(Character *c, const ChrSpeech *s, bool interrupt);
void speech_update(Chr *c);
/* 0x516a00: c's line cut short (its sound stopped: at once, 0x5116b0, or as an ambient, 0x511e40), the
   line and the one queued forgotten. */
void speech_stop(Character *c);
