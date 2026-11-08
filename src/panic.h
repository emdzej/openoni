/* The panic state (AI2rPanic, C+0x1f8 9): a non-combatant (CHAR flag 0x08) that meets danger (a hurt,
   or a melee sound, gunfire or a sighting of a hostile) cowers for its combat settings' panic time,
   crouching once the danger is near, lies through its daze when knocked down, and gets up away from
   it. Notes: docs/re/ai.md, "Panic". */
#pragma once
#include "chars.h"

struct KnowEntry;

/* AI2rNonCombatant_NotifyKnowledge 0x4adeb0: the non-combatants' knowledge handler. */
void panic_notify(Chr *c, struct KnowEntry *e, bool degraded);
/* AI2rCharacter_Panic 0x4ae1e0: into the panic state (if not in it) for e's kind's time, or ticks
   without e; the strongest fear kept. */
void panic_start(Chr *c, struct KnowEntry *e, uint32_t ticks);
/* 0x4ae040, a contact forgotten (AI2iKnowledge_RemoveEntry 0x49b8e0, panicking): the fear's, it calms
   180 ticks after the last news of it. */
void panic_forgotten(Chr *c, struct KnowEntry *e);
/* The state's update (0x4adc80) and exit (0x4adc50). */
void panic_update(Chr *c);
void panic_exit(Chr *c);
