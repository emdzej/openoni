/* OC, the cinematic inserts: the speakers' portraits that slide in at a screen edge during dialogue
   (OCrInitialize 0x4f3610, OCrCinematic_Start 0x4f2be0, the stop 0x4f30c0, the update 0x4f3420, drawn by
   OCiCinematic_Draw 0x4f2910). Positions are in a 1024 by 728 screen. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const void *txmp;
    float x, y, w, h;       /* in the 1024 by 728 screen */
    bool mirror;
} CinematicInsert;

void cinematic_reset(void);
void cinematic_update(void);            /* once a tick */
int cinematic_count(void);
bool cinematic_get(int i, CinematicInsert *out);
/* cinematic_start, cinematic_stop, and the cinematic_xoffset, cinematic_yoffset variables. */
void cinematic_register_commands(void);
