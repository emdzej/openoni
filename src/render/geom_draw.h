/* One M3GM drawn under a 3x4 matrix (by columns, as BodyMatrix), lit by the software geometry engine's
   lights (MSrGeomEngine: the ambient and the directional lights, per vertex, 0x417f00) and with its
   texture's reflection map: what the body parts (TRrBody_Draw 0x427630) and the level's objects (OBiDraw
   0x438fb0) draw with. */
#pragma once

#include <stdint.h>

void geom_draw(const void *m3gm, const float m[12], const void *txmp);
/* The same blended at alpha (of 255; 255 opaque, 0 nothing). */
void geom_draw_alpha(const void *m3gm, const float m[12], const void *txmp, uint8_t alpha);

/* The lights (0x4135c0, 0x4135e0: n of them, each a direction and a colour). */
void geom_set_ambient(float r, float g, float b);
void geom_set_lights(int n, const float (*l)[6]);
/* ONrGameState_SetupDefaultLighting 0x4f4a30: the game's lights, by the graphics quality. */
void geom_default_lighting(void);
/* 0x4fb290: an object's own shade (0x00rrggbb) as the only light. */
void geom_set_shade(uint32_t shade);
/* The camera's location, for the reflection maps (the camera's +8, 0x417690). */
void geom_set_eye(const float p[3]);
