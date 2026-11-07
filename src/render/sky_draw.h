/* The sky (ONrSky, the level's ONSK): a box of five textures round the camera and the planets, drawn
   first and behind everything, on the frames a visibility ray gets out of the level (akev_vis.h).
   Notes: docs/re/visibility.md "The sky". */
#pragma once

/* ONrSky_LevelBegin 0x5105a0: the level's sky (ONLV +0x54, NULL none), the planets at half the far plane's
   distance. The stars (none in the game's skies: ONSK +0xe0 is 0) aren't ported. */
void sky_level_begin(const void *onsk, float far);
/* ONrSky_Draw 0x511160: round the camera's location; right and up its axes (the planets face it). */
void sky_draw(const float eye[3], const float right[3], const float up[3]);
