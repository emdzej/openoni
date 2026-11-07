/* The run-time obstacles in the rooms' grids (PHrPrepareRoomForPath 0x50d660): each room keeps a byte
   per cell, the number of the obstacle over it (0 none), and the list of its obstacles: the characters
   in it (PHrAddCharacterToGrid 0x50c4c0), the objects (PHrAddObjectToGrid 0x50c780) and the doors of
   its links (PHrAddDoorToGrid 0x50c870). A cell with an obstacle is impassable unless the path's
   character may ignore it (PHrSquareIsPassable 0x50db00, PHrIgnoreObstruction 0x50d9f0). Notes:
   docs/re/pathfinding.md "The run-time obstacles". */
#pragma once
#include "character.h"
#include "graph.h"

#include <stdbool.h>
#include <stdint.h>

/* The answers kept through one search (two bits an obstacle: asked, ignored), for one room. */
typedef struct {
    int room;
    uint64_t asked, ignored;
} ObstacleCache;

/* The level's start: no room has its layer. */
void obstacle_level_begin(void);
/* PHrPrepareRoomForPath: the room's layer made (again on the room's first search, or 20 ticks after the
   last). */
void obstacle_prepare(int room, const GraphGrid *g);
/* The cell's obstacle in the room's layer, for c's search in the mode (0, 1 the AI's local movement, 2
   the local path): true if it is in the way (not ignored). cache: cleared for another room. */
bool obstacle_blocks(int room, const GraphGrid *g, int x, int z, const Character *c, int mode, ObstacleCache *cache);
