/* A character's body drawn posed (TRrBody_Draw 0x427630: each bone's geometry under its matrix). */
#pragma once
#include "character.h"

#include <stdint.h>

/* Draws the class's highest-detail body at pos (the feet), turned by facing (radians about +y), posed
   by an animation frame (NULL: the rest pose). */
void char_draw(const void *oncc, const float pos[3], float facing, const void *tram, uint32_t frame);
/* The same, posed by a quaternion (x, y, z, w) per bone (NULL, or not the body's bone count: the rest
   pose), the root height above the feet. */
void char_draw_pose(const void *oncc, const float pos[3], float facing, float height, const float *quats, uint32_t bones);
/* A Character (character.h) in its blended pose: the quaternions of ONrCharacter_BuildQuaternions
   0x4e5360 and the pelvis height C+0x18c. */
void char_draw_character(const Character *c, int slot);
/* The level's corpses (corpse.h). */
void char_draw_corpses(void);
/* The characters' display 0x4e1540: the characters' shadows taken down before they are drawn again. */
void char_shadows_clear(void);
