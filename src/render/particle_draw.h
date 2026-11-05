/* The particles' draw (P3rDisplay 0x460660): sprites (P3rDisplayClass_Sprite 0x461150, built as
   MSrGeomContext_Method_Sprite_Draw 0x4118b0 builds its world-space quads), geometry
   (P3rDisplayClass_Geometry 0x460800), the vectors (0x460cf0), the contrails (0x461d20) and the lens
   flares. Not yet: the decals. */
#pragma once
#include <stdint.h>

/* With the view matrix the frame draws with (column-major) and the game time in ticks. */
void particle_draw(const float view[16], uint32_t now);
/* The lens flares (0x460660 with its argument 1, from ONi_GameState_Display_All_UI_Elements 0x4fbaa0),
   after the scene, through walls; near is the projection's near plane. */
void particle_draw_flares(const float view[16], float near, uint32_t now);
