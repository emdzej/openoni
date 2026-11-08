/* The characters' animation sounds (Oni_Sound_Animation.c): the variant list's sound records, loaded
   from the binary data's SABD class (docs/formats/sabd.md), and the impulse an animation plays at a
   frame for a character variant. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The binary data's SABD class (OSiSABinaryData_Load 0x5161d0, registered by OSiSABinaryData_Register
   0x516110) over the variant list (OSrVariantList_Initialize 0x515c90): level 0's blobs, once its
   impulses are loaded. */
void sound_anim_load(void);
void sound_anim_unload(void);

/* TRrAnimation_IsSoundFrame 0x428780: whether the animation sounds at the frame. With a sound of its
   own (TRAM +0x150, set by the records of the kind "Animation"), at that sound's frame (+0x154);
   without one, at frame 0. */
bool sound_anim_at_frame(const void *tram, uint16_t frame);

/* OSiVariant_Impulse_Get 0x516670: the impulse of the animation for the variant (an ONCV), played at
   pos (OSrImpulse_Play 0x512480). An impulse of the animation's own that doesn't exist is reported
   (COrConsole_Printf) through sound_anim_log. */
extern void (*sound_anim_log)(const char *line);
/* A debug aid (--param soundlog=1): each impulse played, with the animation. */
extern void (*sound_anim_trace)(const char *line);
const void *sound_anim_impulse(const void *variant, const void *tram);
void sound_anim_play(const void *variant, const void *tram, const float pos[3]);
