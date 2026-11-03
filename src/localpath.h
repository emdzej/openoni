/* The AI's local paths: through one room's grid, from a point to a point (ASrPath_SetParams 0x4be240,
   ASrPath_Generate 0x4bd8c0: an A* over the cells), made into waypoints (ASiList_CalcWaypoints
   0x4be310). Notes: docs/re/pathfinding.md, "The local path".

   Not yet: the grid's run-time obstacles (PHrPrepareRoomForPath 0x50d660: characters, objects, doors),
   so every cell's obstacle layer is clear. */
#pragma once
#include "character.h"

#include <stdbool.h>
#include <stdint.h>

enum { LOCAL_PATH_MAX = 256 };  /* the movement state's waypoints, +0x8b8 to +0x18b8 */

typedef struct LocalPoint {
    float p[3];
    int32_t kind;           /* +0x0c: by the cell's value (the table at 0x54b0c8) */
} LocalPoint;

/* The path in the room from start to end for the character c (its sphere tree tests the cells it
   would leave a blocked cell for); allow7: the cells of value 7 may be entered (+0x1148, the next
   room is a stair one); simplify: the waypoints that a straight line passes by are dropped. Returns
   the waypoints' count (0: no path). */
int local_path(const void *env, int room, const float start[3], const float end[3], const Character *c, bool allow7,
               bool simplify, LocalPoint *out);
