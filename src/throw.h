/* Throws (ONI_Character.c): the throw a throw move becomes when a character is in reach
   (RemapAnimationHook 0x4ea550, AttemptThrow 0x4eaa10, AttemptSpecificThrow 0x4ea950), the victim's
   part as the throw starts (NewAnimationHook 0x4e97a0), and the tick's after-pass of
   ONrGameState_UpdateCharacters 0x4dccf0: the victim held at the thrower's hands on the first frame,
   and an animation's own damage (the victim's, from its thrower).

   Not ported yet: the AI's throws (AI2rExecutor_TryingToThrow 0x498d50), the AI's notice of being
   thrown (0x48b530), the throw's noise (AI2rKnowledge_Sound 0x49a8a0, 150), the camera's (+0x2815), and
   the damage boost of an overpowered thrower (class +0xc8d). */
#pragma once
#include "character.h"

/* Installs the hooks in chr_anim (chr_remap_hook, chr_throw_hook). */
void throw_install(void);
/* The after-pass, over the characters in use (alive or not). */
void throw_update(Character **chars, int n);
