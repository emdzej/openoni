/* Films (ONrFilm, the FILM instances): a character's input recorded tick by tick and played back in its
   place, the way the cutscenes move their actors (the script command "playback"). Layout of a FILM:
   +0 the start position, +0xc the facing, +0x10 the desired facing, +0x14/+0x18 the aim (left/right,
   up/down), +0x1c the length in ticks, +0x20/+0x24 two TRAM links, +0x34 the key count, +0x38 the keys
   (0x18 bytes: the mouse x, y as floats, the held action bits as a u64, the tick it starts at, 4 unused). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "character.h"

enum { FILM_START_HERE_PLACED, FILM_START_FROM_HERE, FILM_START_INTERP };   /* ONrFilm_Start's mode */

/* ONrFilm_Start 0x4f4060: plays the film on the character. Placed: put at the film's start; from here:
   where it is; interp: moved there over ticks (at least 1), refused from more than 80 away. */
bool film_start(Character *c, const void *film, int mode, uint32_t ticks);
/* ONrFilm_Stop 0x4f41f0: false if none was playing. */
bool film_stop(Character *c);
bool film_playing(const Character *c);
/* DoFilmUpdate 0x4ed420's playback: the tick's input replaced by the film's (call with the input built
   for the tick, before the character takes it). */
void film_update(Character *c, ChrInput *in);
/* DoCharacterFrame 0x4ddba0, before step 12: a tick of the move to the film's start. */
void film_interp_step(Character *c);
