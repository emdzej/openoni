/* Throws (ONI_Character.c): the throw a throw move becomes when a character is in reach
   (RemapAnimationHook 0x4ea550, AttemptThrow 0x4eaa10, AttemptSpecificThrow 0x4ea950), the victim's
   part as the throw starts (NewAnimationHook 0x4e97a0), and the tick's after-pass of
   ONrGameState_UpdateCharacters 0x4dccf0: the victim held at the thrower's hands on the first frame,
   and an animation's own damage (the victim's, from its thrower).

   The AI's throws come through throw_ai_hook (AI2rExecutor_TryingToThrow 0x498d50). An AI's victim
   is told as of a knockdown (0x48b530); the throw makes a melee noise (150) and reveals both; an
   overpowered thrower's (class +0xc8d) damage is boosted (chr_overpower). */
#pragma once
#include "character.h"

/* AI2rExecutor_TryingToThrow 0x498d50: the throw the AI's executor asks for (its animation, the
   victim's, the target), false if none. Set by chars.c. */
extern bool (*throw_ai_hook)(const Character *c, const void **anim, const void **victim, Character **target);
/* Whether the target can be thrown now (the AI's melee): its class not unthrowable (+0xc8f), not the
   unstoppable (0x4e6500: the unstoppable cheat's player, 0x5ece91, or flag 0x100), its animation without the throw-proof flag 1
   and not in its invulnerable frames (TRrAnimation_IsInvulnerable 0x42c2f0). */
bool throw_target_ok(const Character *t);
/* Installs the hooks in chr_anim (chr_remap_hook, chr_throw_hook). */
void throw_install(void);
/* The after-pass, over the characters in use (alive or not). */
void throw_update(Character **chars, int n);
