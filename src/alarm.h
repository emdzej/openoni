/* The AI's alarm (AI2rAlarm, the alarm state of ai.h, C+0x1f8 7): an AI runs to an alarm console's
   action marker and uses it, fighting on the way only the enemies that come close or hurt it enough
   (the combat settings' alarm part), and the alarm event that alerts the AIs of an alarm group.
   Notes: docs/re/ai.md, "The alarm". */
#pragma once
#include "ai.h"

struct Chr;

/* AI2rAlarm_FindAlarmConsole 0x48eb70: the marker of the active, unused alarm console with the
   shortest way to it within the alarm's find distance (C+0xfec), -1 none. */
int alarm_find_console(const AiState *a, const Character *c);
/* AI2rAlarm_Setup 0x48e810: the alarm state, running to the marker (-1: the one it had; none, false).
   A run to another marker ends the old one first. */
bool alarm_setup(AiState *a, Character *c, int marker);
/* The alarm state's tick (0x48e410). */
void alarm_update(AiState *a, Character *c);
/* AI2rUpdateAI 0x489d40's part: an alarm run left for a fight goes on once its fight time is out. */
void alarm_fight_tick(AiState *a, Character *c);
/* 0x49ac30, the event list's "activate alarm" (6): every AI of the group (C+0xfd8), not passive, but
   the one who set it off, learns of the alarm (an alarm contact, strength 3, at the setter's place),
   unless the setter is an AI that isn't its enemy. who: NULL for none. */
void alarm_notify(unsigned group, struct Chr *who);
/* ai2_doalarm char [console] (AI2iScript_SetState_Alarm 0x4b4db0): an AI (not passive) runs to the
   console of that id (active and unused), or (0) the nearest alarm console by way. */
void alarm_script(struct Chr *ch, uint16_t console);
