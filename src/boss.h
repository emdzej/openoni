/* The bosses' combat behaviours (the Combat objects' behaviours 5 to 13, the table at 0x52a6c4): Barabbas
   (5 shoot, 6 advance, 7 melee: AI2iBehavior_Barabbas 0x491540 under each), the super ninja (8 fireball,
   9 advance, 10 melee) and Mutant Muro (12 melee, 13 thunderbolt), and Run For Alarm (11). Notes:
   docs/re/ai.md, "The bosses". */
#pragma once
#include "ai.h"

/* ai2_barabbas_run (0x545770, true): Barabbas runs while he carries his gun. */
extern bool boss_barabbas_run;

/* A behaviour's handler for a message (combat.h's MSG_*): *handled false leaves it to the default's.
   p5, p6, ptr: the message's arguments. */
int boss_behaviour(AiState *a, Character *c, int id, int msg, bool *handled, int p5, int p6, const void *ptr);
