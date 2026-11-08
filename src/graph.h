/* The AI's pathfinding graph: one node per BNV room of the environment (AKVA), linked through the
   ghost and stair quads between them (PHrBuildGraph 0x50af70); the room holding a point
   (AKrNodeFromPoint 0x440600); the rooms' A* from one point to another (0x50bdb0). Notes:
   docs/re/pathfinding.md.

   The links' blockages come from the rooms' grids (PHiCheckForBlockages 0x50df80): a link blocked all
   across gets flag 1, which characters don't take. The doors on the links (+0x2c) come from the level's
   doors (graph_link_doors). The grids' run-time obstacles (characters, objects, doors) are obstacle.h's
   (PHrPrepareRoomForPath 0x50d660). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { GRAPH_PATH_MAX = 199 };  /* the path's links: the character's array, +0x1174 to +0x1490 */

typedef struct {
    uint32_t flags;         /* +0x00: 4, 8 the low corners free; 0xe a stair room's; bits 16..19 the corners */
    int from, to;           /* +0x04, +0x08: the rooms */
    uint16_t out_index;     /* +0x0c: in the from room's out list */
    uint16_t in_index;      /* +0x0e: in the to room's in list */
    uint32_t quad;          /* +0x10: the quad crossed */
    float mid[3];           /* +0x14: the middle of its bottom edge */
    float width;            /* +0x20: the bottom edge's length across */
    int8_t door_side;       /* +0x24: the from room's side of the door (OBJiDoor_PointOnSide) */
    int door;               /* +0x2c: the door (door.h index), -1 none */
    int16_t blocks;         /* +0x42: the blocked runs along the edge (both rooms' grids) */
    float block[16][2];     /* their fractions of the edge (the original keeps them rounded, +0x40) */
} GraphLink;

/* Loads the graph of the level's environment (env: the AKEV instance); false without one. */
bool graph_build(const void *env);
void graph_free(void);
/* The graph of env, built the first time it is asked for (the level's load, ONrLevel_Load). */
bool graph_for(const void *env);
/* The doors on the links (OBJiDoor_MakeConnectionLink_Enum 0x4c1f90, which PHrBuildGraph runs for
   each new link): again whenever the graph or the doors are loaded. */
void graph_link_doors(void);
int graph_room_count(void);
int graph_link_count(void);
const GraphLink *graph_link(int i);
/* The room's out links, in the original's list order (its newest first). */
int graph_out_count(int room);
int graph_out(int room, int k);
/* Its in links, the same order. */
int graph_in_count(int room);
int graph_in(int room, int k);
/* The room's BNV (its AKVA index). */
int graph_room_bnv(int room);
/* The room's grid (BNV +0x1c: PHrGrid_Decompress 0x43c600 of its .raw run-length data): w x h cells of
   4 bit values, row by row; NULL without one. */
const uint8_t *graph_room_grid(int room, uint32_t *w, uint32_t *h);
/* The grid's header (BNV +0x1c): its size, the cell's size, the origin, the offsets in cells. */
typedef struct {
    uint32_t w, h;
    float cell, x0, y0, z0;
    int16_t ox, oz;
    const uint8_t *cells;   /* NULL without one */
} GraphGrid;
bool graph_grid(int room, GraphGrid *g);
/* PHrWorldToGridSpace 0x43c0c0: p's cell, rounded to nearest (not clamped). */
void graph_to_cell(const GraphGrid *g, const float p[3], int16_t *x, int16_t *z);
/* PHrGridToWorldSpace 0x43c050: the cell's middle, at y (the grid's floor + 0.5 for NULL). */
void graph_cell_middle(const GraphGrid *g, int x, int z, const float *y, float out[3]);
/* 0x437b60: a quad's lowest corner and the lowest of the others. */
void graph_quad_low_pair(uint32_t quad, float lo[3], float lo2[3]);
/* PHrWaypointFromGunk 0x43c300: the point to cross a link's quad by: the middle of its bottom edge, or
   (an edge longer than 20) the point of it nearest to near, kept 10 from its ends. */
void graph_waypoint(uint32_t quad, const float *near, float out[3]);
/* PHrProjectOntoConnection 0x50e9a0: p moved to where the line from near to it crosses the link (in its
   margins, out of its blockages). */
void graph_project_onto_link(int link, const float near[3], float p[3]);
/* Its BNV's flags (+0x5c): 4 a room, 1 stairs. */
uint32_t graph_room_flags(int room);
/* 0x50ed50: the height of the link's bottom edge (its two corners', by the flags' bits 16..19, halved). */
float graph_link_height(int link);
/* Its BNV's box (+0x30): min xyz, max xyz. */
void graph_room_box(int room, float box[6]);

/* AKrNodeFromPoint 0x440600: the BNV holding p (inside its BSP, or the nearest within 5 of a wall
   plane), -1 none. Its room is the same index. */
int graph_room_at(const float p[3]);

/* 0x50bdb0: the rooms' A* from (room a, point pa) to (room b, point pb): the links crossed, in order, to
   path (at most GRAPH_PATH_MAX); returns their count + 1 (the rooms), or 0 without a path. A character
   (by_character) avoids the links with flag 1. */
int graph_find_path(int a, const float pa[3], int b, const float pb[3], bool by_character, int *path);
/* The same with the way's length (0x50bdb0's +0x20): *distance, if above 0, bounds the search (no room
   reached beyond it is kept), and is set to the length found. path may be NULL. mode: the search's key
   (PHrPathDistance's, -1 none; the sounds give their type), which a search without a character holds
   up to a shut door's danger type (DOOR +0x10, +0xc the danger). */
int graph_find_path_within(int a, const float pa[3], int b, const float pb[3], bool by_character, int mode, int *path,
                           float *distance);
