/* The particles' draw (P3rDisplay 0x460660): sprites (P3rDisplayClass_Sprite 0x461150, built as
   MSrGeomContext_Method_Sprite_Draw 0x4118b0 builds its world-space quads) and geometry
   (P3rDisplayClass_Geometry 0x460800). Not yet: the contrails (0x461d20), the vectors (0x460cf0), the
   lens flares' visibility test and the decals. */
#pragma once
#include <stdint.h>

/* With the view matrix the frame draws with (column-major) and the game time in ticks. */
void particle_draw(const float view[16], uint32_t now);
