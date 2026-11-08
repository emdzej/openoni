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
/* M3rSprite_Draw 0x41a830 in the sprite mode 4 (M3rGeom_State_Set 6, as WPrDisplay 0x519710 and the
   powerups' 0x51c2b0 set it): a quad lying along the matrix's x and z columns about pos, sx and sy out
   each way, white, at alpha (0..255), the texture animated by the game time. */
void sprite_draw_flat(const void *txmp, const float pos[3], const float m[12], float sx, float sy, uint8_t alpha,
                      uint32_t now);

/* The player's laser sight (weapon.h): its beam and its tunnel's sprites (0x4e1900, in the characters'
   display), and its crosshair (0x4f1fa0, after the scene, before the lens flares). */
struct WeaponSight;
void sight_draw(const float view[16], const struct WeaponSight *s, uint32_t now);
void sight_draw_crosshair(const float view[16], const struct WeaponSight *s, uint32_t now);
