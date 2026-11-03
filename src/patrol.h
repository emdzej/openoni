/* Patrol paths (OBJ's "Patrol Path" type: the read method 0x4c9440) and the AI's patrol
   (AI2rPatrol_Enter 0x4af970, AI2rPatrol_Update 0x4afc90, AI2iPatrol_DoWaypoint 0x4b0650). Format:
   docs/formats/objc.md (PATR).

   The waypoints' moves go through the stand-in mover (ai.h) until the path follower
   (AI2rPath_GoToPoint) is ported; looking and glancing (types 3, 4, 14), scanning's turn (17) and the
   facing locks (9, 13) are not ported yet. */
#pragma once
#include "ai.h"

#include <stdbool.h>
#include <stdint.h>

enum { PATROL_MAX_POINTS = 64 };

typedef struct {
    uint32_t type;
    uint16_t a, b;      /* the u16 fields: a flag, frames */
    uint32_t n;         /* a u32 field: frames, a mode, an index */
    float f[4];         /* floats: a point, a distance, an angle */
} PatrolPoint;

/* Reads the level's paths; returns how many. */
int patrol_load(void);
int patrol_find(const char *name);         /* AI2rPatrol_FindByName 0x4b04c0; -1 none */
int patrol_find_id(uint16_t id);
const char *patrol_name(int path);
int patrol_points(int path, const PatrolPoint **out);

/* AI2rPatrol_Enter: start the path at its first waypoint. */
void patrol_start(AiState *a, int path);
/* AI2rPatrol_Update, one tick of a patrolling character (before the mover). who: its name, for the
   waypoints' scripts. */
void patrol_update(AiState *a, const float pos[3], float facing, const char *who);
