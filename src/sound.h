/* The sound system: the SS2 layer (SS2r*, SSr*, SSi*: sound channels, groups, impulses, ambients) and the
   game's layer over it (OSr*, OSi*: the sound binary data, music, dialog, the script commands). Format of the
   data: docs/formats/osbd.md; the samples: sndd.h.

   The original streams each sound through a DirectSound buffer per channel (16 of them, 22,050 Hz, 16 bits)
   from a thread. The port keeps the same channels and rules, with the buffers' volume, pan and frequency in
   DirectSound's own units, and mixes them itself in sound_mix() at the frame's rate. Everything runs on the
   game's thread and on the tick counter (no wall clock), so the output is reproducible.

   Calls, in the order the game makes them:
     sound_initialize()                      SS2rInitializeFull 0x483310 + OSiScriptCommands_Initialize 0x512cb0
     sound_register_commands()               the sound_* script commands (sl.h)
     sound_level_load(level)                 OSrLevel_Load 0x514970, after tm_level_load(level)
     sound_update()                          once a tick: OSrUpdate 0x514f20, then the frame update 0x483bd0
     sound_mix(stereo, frames)               from app_audio: the frame's audio
     sound_level_unload()                    before tm_level_unload
     sound_terminate()
   */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SOUND_RATE = 44100 };    /* sound_mix's output rate (APP_AUDIO_RATE) */

void sound_initialize(void);
void sound_terminate(void);
/* Registers the script commands (OSiScriptCommands_Initialize 0x512cb0). After sl_reset. */
void sound_register_commands(void);

/* OSrLevel_Load 0x514970: level 0 builds the sound databases from the OSBD instances loaded so far; every
   level resolves the permutations' sounds (SNDD instances) again. False if there are no sound data. */
bool sound_level_load(int level);
/* Stops every sound and forgets the pointers into the level's instances (0x483710, 0x47f680). */
void sound_level_unload(void);

/* One tick (1/60 s). */
void sound_update(void);
/* The mix: frames x 2 floats (left, right) at SOUND_RATE. */
void sound_mix(float *stereo, unsigned frames);

/* The listener for 3D sounds (OSrUpdate 0x514f20 passes the camera's position and facing to 0x482890).
   Until the camera calls it: the origin, facing +x. */
void sound_set_listener(const float pos[3], const float facing[3]);
/* SS2rVolume_Set 0x483e00: the overall volume, 0..1 (ONrPersist_GetOverallVolume at level load); the
   mixer slides to it by 0.05 a frame. */
void sound_set_volume(float v);

/* The script commands, callable from C. Names as the scripts give them. */
bool sound_dialog_play(const char *name);           /* false: a dialog is playing, nothing started */
bool sound_dialog_playing(void);
void sound_dialog_interrupt(const char *name);
/* A character's line (OSiDialog_Play 0x5125c0) at pos: its id (UINT32_MAX none), *ticks its length, *missing
   no such ambient or a sound of it missing. */
uint32_t sound_speech_play(const char *name, const float pos[3], uint32_t *ticks, bool *missing);
bool sound_speech_playing(uint32_t id);
void sound_speech_stop(uint32_t id);
void sound_music_start(const char *name, float volume);
void sound_music_stop(const char *name);
void sound_music_volume(const char *name, float volume, float seconds);
void sound_impulse_play(const char *name, float volume);
/* An impulse by name (OSrImpulse_GetByName 0x512440, the scripts' case-insensitive lookup), NULL if none;
   its impact velocity (+0x84); OSrImpulse_Play 0x512480: at pos (NULL: at the listener), volume NULL
   for full. */
const void *sound_impulse_find(const char *name);
float sound_impulse_impact_velocity(const void *impulse);
void sound_impulse_play_at(const void *impulse, const float pos[3], const float *volume);
/* An ambient by name (OSrAmbient_GetByName 0x511670), NULL if none. OSrAmbient_Start 0x511730 at pos (the
   particles'): its id, UINT32_MAX if none started. OSrAmbient_Update 0x511ec0: moved, whether it plays.
   OSrAmbient_Stop 0x511e40. */
const void *sound_ambient_find(const char *name);
uint32_t sound_ambient_start_at(const void *amb, const float pos[3]);
bool sound_ambient_move(uint32_t id, const float pos[3]);
void sound_ambient_stop(uint32_t id);
/* ONrGameState_EventSound_Play 0x4fdac0: the game's event sound of that index (the impulse named in
   the game settings, ONGS+0x7d0, 32 bytes each, 18 of them; SSrImpulse_UpdateGroupPointers 0x4f49b0),
   not placed. */
void sound_event_play(int index);
float sound_volume(void);       /* SS2rVolume_Get 0x483df0: the overall volume's target */

/* What the databases hold, for tests. */
typedef struct { uint32_t groups, ambients, impulses, permutations, unresolved; } SoundStats;
SoundStats sound_stats(void);
bool sound_has_ambient(const char *name);           /* the scripts' lookup (case-insensitive) */
bool sound_has_impulse(const char *name);
/* Channels playing now (of 16). */
int sound_channels_playing(void);
/* Logging of "SOUND NOT FOUND" and the like (COrConsole_Printf in the original); NULL: none. */
typedef void (*SoundLogFn)(const char *msg);
void sound_set_log(SoundLogFn fn);
