/* The characters' levels of detail (ONrGameState_ComputeCharacterLOD 0x4e2900, from the characters'
   display 0x4e1540): each seen character's body (of the five of its class, ONCC +0xc34) by its distance
   from the camera, all of them stepped down (or up) together to keep within a triangle budget of the
   graphics quality, the change shown only after it has held a few frames. Notes: docs/re/character.md
   "The level of detail". */
#pragma once
#include "character.h"

#include <stdbool.h>

/* The camera the display looks through (M3rCamera_GetStaticData 0x41b460, M3rCamera_GetViewData
   0x41b4a0): its place, view (unit), field of view and far distance. */
typedef struct {
    float pos[3], view[3];
    float fov, far;
} LodCamera;

/* 0x4e27d0: whether the character is in the camera's view (within the field of view and a sixteenth
   turn more, and the far distance), or was until three frames ago with a level of detail. Its distance
   (camera_d2) set first. */
bool chr_lod_visible(Character *c, const LodCamera *cam);
/* 0x4e2900 for the n characters seen this frame (reordered: the player first, then the nearest). */
void chr_lod_compute(Character **list, int n, const Character *player);
/* The detail a draw takes (ONiCharacter_Display_Body 0x4e1d70): the one shown, one less while it fades
   (alpha under 1), at least 1 at the highest quality. */
int chr_lod_draw(const Character *c, float alpha);
/* chr_lod (0x54e174, -1): a level of detail 0..4 forced for every character, others none. */
extern int32_t chr_lod_forced;
