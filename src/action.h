/* The action key's objects (HandleStartTauntOrAction 0x4f80d0's part before the taunt): the console
   whose marker the character can use (0x4f5840), else the door it faces (OBJrDoor_TryDoorAction
   0x4c2a50); then the console action under way, each tick (ONrCharacter_ConsoleAction_Update 0x4ee220):
   the character slides to the marker, faces the console, plays the console animation and, at its action
   frame, uses the console. Notes: docs/re/console.md. */
#pragma once
#include "character.h"

#include <stdbool.h>

/* Sets character_action_hook to the port's. */
void action_install(void);
/* 0x4f5840: whether the character can use the marker from where it is (wide: the AI's wider limits). */
bool action_can_use(const Character *c, int marker, bool wide);
