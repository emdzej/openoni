/* The AI's pursuit (AI2rPursuit, the pursuit state of ai.h, C+0x1f8 6): a character that has noticed a
   contact goes to where it was, or glances or looks around for it, by the contact's strength and the
   alert (the CHAR's pursuit modes), and gives up as the contact fades: back to its job, on looking, or
   to the alarm. The look around is the guard's scan (AI2rGuard_UpdateScan). Notes: docs/re/ai.md,
   "The pursuit". */
#pragma once
#include "ai.h"

struct Chr;
struct KnowEntry;

/* AI2rPursuit_Enter 0x4b0ea0, on the contact (target_entry, set by the caller). */
void pursuit_enter(AiState *a, Character *c);
/* AI2rPursuit_Exit 0x4b0f50. */
void pursuit_exit(AiState *a);
/* AI2rPursuit_Update 0x4b0fb0 (a name of the port), the state's tick. env: the AKEV for the sight lines. */
void pursuit_update(AiState *a, Character *c, const void *env);
/* 0x4b1750, the state's knowledge handler (C+0xf80). */
void pursuit_notify(struct Chr *c, struct KnowEntry *e, bool degraded);
/* 0x4b1830: the contact e is forgotten (AI2rKnowledgeState_Forget). */
void pursuit_forgotten(struct Chr *c, struct KnowEntry *e);
/* 0x4b0f90 (through AI2rIsHurrying 0x48b020): going to the contact, hurrying. */
bool pursuit_hurrying(const AiState *a);
/* The guard's scan (the pursuit's look around and the patrol's guard and scan points): 0x499970, a new
   scan, its first look at angle (1000 the facing); AI2rGuard_UpdateScan 0x499b50, its tick. */
void ai_scan_set(AiScan *s, float angle);
void ai_scan_update(AiState *a, AiScan *s, const Character *c, const void *env);
/* A patrol's scan point while the character moves (0x4afc90): the look the way it goes (dir, 1000 none). */
void ai_scan_follow(AiState *a, AiScan *s, float dir);
