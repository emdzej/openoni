/* One M3GM drawn under a 3x4 matrix (by columns, as BodyMatrix), lit by a fixed light: what the body
   parts (TRrBody_Draw 0x427630) and the level's objects (OBiDraw 0x438fb0) draw with until the draw
   engine's lighting is ported. */
#pragma once

void geom_draw(const void *m3gm, const float m[12], const void *txmp);
