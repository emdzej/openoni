# Pathfinding

How the AI finds its way: a graph of the environment's rooms (the BNV volumes) for the global path, and
each room's grid for the local one. Read in the Windows exe; ported in `src/graph.c` (the graph, the
blockages, the room under a point, the rooms' A*) and used by the AI's mover (`src/ai.c`). Confirmed
means read in the exe; inferred means from the context.

## The environment's data

**The BNV volumes** (`AKVA`, the environment's +0x20, 0x74 bytes each; level 1 has 240):

| Offset | What |
|---|---|
| 0x00 | The volume's BSP root, an index into `AKBP` (0xc bytes a node: the plane, the child behind, the child in front) |
| 0x04 | Its id |
| 0x08, 0x0c | Its sides: first, end (indices into `AKBA`) |
| 0x10, 0x14 | Run time (-1 on disk) |
| 0x18 | Run time: its graph node's index (`PHrBuildGraph`) |
| 0x1c | The grid: width, height (u32) |
| 0x24, 0x28 | Its run-length data: the `.raw` offset and size |
| 0x2c | The cell's size (4.0) |
| 0x30 | The volume's box: min xyz, max xyz; the grid's origin is the min |
| 0x48, 0x4a | The grid's offsets, in cells (i16; -2, -2 on level 1) |
| 0x5c | Flags: 4 a room (all of level 1's), 1 stairs (41 of them) |
| 0x60 | Stairs: a plane (a, b, c, d) and, at 0x70, the slab's height above it |

**The sides** (`AKBA`, 0x1c bytes): the plane (bit 31 negated), then the first and end of the side's
adjacencies (+4, +8), then two more ranges (not read here).

**The adjacencies** (`AKAA`, 0xc bytes): the BNV on the other side, the quad between them (a ghost or a
stair quad), a u32 (0).

**The octree's BNV lists**: an octree leaf's +0x20 packs the first index (bits 8 to 23) and the count
(bits 0 to 7) of its BNVs, in the octree's second `IDXA` (`AKOT` +0x10).

**The grid** (`PHrGrid_Decompress` 0x43c600): runs, a byte each: the value in the low 4 bits, the length
in the high 4, or in the next byte when they are 0. Rows of `width` cells, `height` of them. The cells
of level 1's rooms: 0 open; 1 and 4 a falloff along the walls; 6, 8 and 9 obstacles (walls, crates).
A point's cell (`PHrWorldToGridSpace` 0x43c0c0, unchecked 0x43c290): ((x − cell/2 − x0) / cell − ox),
rounded to nearest (0x434030, `fistp`); likewise z. A cell's middle (`PHrGridToWorldSpace` 0x43c050):
((ox + i) + 0.5) × cell + x0.

## The graph

`PHrBuildGraph` 0x50af70, at the level's load (`FUN_005059d0`):

1. A node (0x9c bytes) for each BNV with flag 4, in order; the BNV keeps the node's index at +0x18.
2. For each node, each side of its BNV, each adjacency of the side whose quad has the flags 0xe (ghost
   or stairs): a link (+0x00 flags, +0x04 from node, +0x08 to node, +0x0c and +0x0e its indices in the
   two nodes' lists, +0x10 the quad, +0x14 the middle, +0x20 the width, +0x2c a door, +0x34..+0x3c the
   lists' links, +0x40 and +0x42 the blockages). The quad's lowest corner and the lowest of the others
   make the link's bottom edge; the middle is their average, the width their distance across (x, z). A
   corner is free unless another adjacency of the room (any side) has a lowest-pair corner within 0.1
   (0.01 squared, 0x52b818) of it: flags 4 and 8 for the two corners, or 0xe when either room is a stair
   one; bits 16..17 and 18..19 hold the two corners' numbers. The link goes to the head of the from
   node's out list (+0x04, the count at +0x00) and the to node's in list (+0x0c, +0x08).
3. Each node's distances: for each link in and each link out, the distance between their middles, in
   a matrix (the node's +0x14 its start, in × out entries). The lists are numbered from their heads.
4. **The blockages** (once for a link and the one back through the same quad, which gets a copy):
   `PHiCheckForBlockages` 0x50df80 on both rooms' grids, the edge from the lowest corner to the other.
   Unless both ends are in the grid ("connection ... outside the grid"): the edge stretched back by two
   cells' worth of its length (−2 / the cells across, per axis), but not past the grid's side behind,
   and on likewise; its cells clamped into the grid; then cell by cell (Bresenham, the error starting at
   half the difference, toward zero) with the fraction along the edge carried (the start cell's middle
   projected, each step 4 × the axis's length over the edge's squared length). Each cell of 6, 8 or 9
   adds the run [t − 4/len, t + 4/len], within 0..1, to the link's (`PHiAddBlockage` 0x50e7c0: sorted,
   joined to a run it meets within 0.01, and that one to the next; at most 16). A single run from before
   4/width to past 1 − 4/width blocks the link: flag 1. On level 1, 32 of the 528 links are blocked:
   the closed doors (their panels are in the grid).

## The doors on the links

`PHrBuildGraph` hands each new link to `OBJrDoor_MakeConnectionLink` 0x4c1f60, which enumerates the
doors (`OBJiDoor_MakeConnectionLink_Enum` 0x4c1f90) until one takes it. A door takes a link whose
middle is within half its width (0.5, 0x529560) of the door's position (D+0x0c, distance squared). The
door keeps two links (D+0x244, D+0x248), their distances squared (+0x24c, +0x250) and widths (+0x254,
+0x258). A link joining the same two rooms as one slot's never takes the other slot. In order:

1. An empty first slot, unless the link matches the second slot's rooms; then an empty second slot,
   unless it matches the first's.
2. A slot whose link is farther than 1.1 times this one's distance (0x5295a0); the first slot, then
   the second (as in each step below).
3. A slot whose link is farther than 0.9 times this one's distance (0x5295a4) and wider.
4. A slot whose link is farther than 0.9 times this one's distance and wider than 0.9 times its width,
   when the other slot holds this link's way back.
5. Otherwise the door passes it on to the next door.

Taken, the link gets the door (+0x2c), unless it already has one (a debugger message, and the
enumeration stops either way). The link also gets the side of the door its from room's middle is on
(+0x24: `OBJiDoor_PointOnSide` 0x4c2370 of the middle of the BNV's box, BNV+0x30..+0x44). A link
pushed out of a slot keeps its door.

The character's search (below) skips a link whose door `OBJrDoor_CharacterCanOpen` 0x4c24b0 refuses
for that side. It refuses when the door is jammed, or locked and not one way with the side let through
(−1 passes), and without the key. The cheat byte 0x5ec5f5 passes every door. A search without a
character holds a shut door (S+0xc not 1) of a class (+0x8c) to the class's danger: by its type (DOOR
+0x10; the jump table 0x50c380) type 0 any search, 1 a search with the key 2, 3 or 4, 2 the key 4 only,
3 none; then a danger (DOOR +0xc) above 0.99 (0x52a03c) none, and one above 0 makes the rooms beyond cost
1 / (1 − danger) times as much (the factor kept at the room's +0x8c). Every class in the game has type 2
and danger 0.5. The key is `PHrPathDistance`'s (edx): −1 for the characters' searches; the sounds
(`AI2rKnowledge_Sound` 0x49a8a0, the only search without a character) give their type, so only gunfire
(4) is heard through a shut door, at twice the way beyond. The flag-1 links are skipped only with a
character. `PHrPathDistance`'s callers: the alarm console's (`AI2iAlarm_ConsoleCallback` 0x48ebc0), the
gun's (`AI2iManeuver_TryPathToGun` 0x49e810), the patrol's start (`AI2rPatrol_Enter` 0x4af970) and the
pursuit's (`AI2iPursuit_DesirePursuit` 0x4b1620, twice) pass the character.

`AI2rPath_Update` 0x4ae670 keeps the door of the path's current link in C+0x1144. It is set when the
path is made (`AI2iPath_MakePath`) and as each link is passed, and is none in the destination's room.
Each tick, a character within the door's activation distance (`OBJrDoor_CharacterProximity` 0x4c2a20:
distance squared from C+0x148 below S+8) opens it as the action key does (`OBJrDoor_CharacterOpen`
0x4c2520). That opens manual doors too: on level 10 the guards behind the manual blast doors 1 and 2
come through before the script locks them. `AI2iMovementState_SetupPath` 0x4a9d70 starts the movement
state's waypoints at the second (+0x18ba = 1); the first is the start's own cell.

## The room under a point

`AKrNodeFromPoint` 0x440600 (`FUN_00440600`): the BNVs of the octree leaf holding the point. For each:
down its BSP (a plane with the point at least 1/1024 in front goes to the child in front, else
behind; −1 behind is inside); outside, it still counts when the last plane is within 5 and nearly
vertical (|normal y| ≤ 0.5). The point must be at most 5 below the volume's floor (min y); the score is
the height above the floor plus the distance outside. A stair volume needs the point within its slab.
The first volume the point is inside is taken; until then, the outside ones compete. After that:
stairs win over the rest; else the lower score (by 1/1024), or the smaller volume when as low. The
graph node is the BNV's (+0x18, 0x50c9c0).

## The global path

`AI2rPath_GoToPoint` 0x4ae480 keeps the destination (C+0x1100), its radius (C+0x10f4, at least 3.0,
0x529808), the facing asked for (C+0x10f8, C+0x10fc), and sets C+0x10f0 to 1. A radius of 0, as a
neutral's talk asks for at its own place, is still reached within 3 (confirmed).

`AI2iPath_MakePath` 0x4aeb10 (from `AI2rPath_GoToPoint` 0x4ae480 and `AI2rPath_Repath` 0x4ae3d0): the
character's node (C+0x188) and the destination's. The path is kept if it was made for the same two
nodes, to within 20 of the point (400 squared, 0x529dec), less than 90 ticks ago. In one node there is
no path to search. A node without links out (or the destination's without links in) is an error
(C+0x1490 = 3, 4; no path is 5; `AI2iReport_Pathfinding_*`).

**The search** (0x50bdb0): A* over the nodes, with the character (avoiding the links with flag 1)
and a key (−1, for the doors). The open and closed lists are linked through +0x7c, appended at the
end; the open node with the lowest estimate (the first of equals, from FLT_MAX) is taken. Its cost
+0x80, the estimate's rest +0x84, the estimate +0x88, a cost factor +0x8c (1, raised by a door's
+0xc), the parent +0x90, the link in +0x94, the state +0x78 (0, 1 open, 2 closed), the depth +0x7a. A
step: from the start, the distance from the start point to the link's middle; else the matrix entry
from the link in to the link out; times the factor. The rest: the link's middle to the destination
point. A closed node reached cheaper is opened again and the saving passed on to the nodes reached
through it (0x50c390). The path is the links from the parents (at most 199, the character's array
+0x1174..+0x1490), and its count is the depth + 1.

## The local path

`AI2rMovementState_MakePath` 0x4a9f20 makes one for the room the character is in (the global path's
link `+0x1150`, its from node; the destination's node in the last). From: the character in the first
room, else the crossing of the link before (`PHrWaypointFromGunk` 0x43c300). To: the destination in the
last room, else the crossing of the link out (`AI2rPath_FindGhostWaypoint` 0x4aea60: from up to 4 links
ahead, `PHrWaypointFromGunk` there and projected back link by link with `PHrProjectOntoConnection`
0x50e9a0). `AI2iMovementState_SetupPath` 0x4a9d70 then runs `ASrPath_SetParams` 0x4be240 and
`ASrPath_Generate` 0x4bd8c0 into the movement state's waypoints (+0x8b8, 0x10 bytes each, at most
256; their count +0x18b8, the next +0x18ba).

**`PHrWaypointFromGunk`** 0x43c300: the quad's lowest corner a and the lowest of the others b
(0x437b60). Without a point to be near, or with an edge of at most 20 (400 squared): their middle.
Else the edge's point nearest to it (0x432e40, from b to a, clamped), moved to 10 from an end it is
within 10 of (100 squared, 0x529d54).

**`ASrPath_SetParams`**: the room's node, its grid (BNV +0x1c), the character (+0x48), the start and
end points (+0x30, +0x3c) and cells (+0x24, +0x28), whether cells of 7 may be entered (+0x4c: the next
room is a stair one, `+0x1148`); `PHrPrepareRoomForPath` 0x50d660 (the run-time obstacle layer:
characters 0x50c4c0, objects 0x50c780, doors 0x50d840); the node map (+0x20, a u16 per cell) cleared.

**`PHrSquareIsPassable`** 0x50db00: out of the grid or a 9, impassable (value 9). Else the obstacle
layer's entry (0: the cell's value, passable; else unless the obstruction is ignored, 0x50d9f0,
impassable and blocked).

**`ASrPath_Generate`**: A* over the cells.

1. The end cell, if impassable: the rings at 1, 2, 3 around it, x − r then x + r for each z from
   z − r up, then z − r and z + r for each x between, for a passable cell the character's tree gets
   to from the end point (`ASiCollisionTest` 0x4be610 from a copy of the tree there;
   `ASiTryEndPoint` 0x4be830). None: no path. That cell is the end (+0x2c).
2. The start node (nodes of 0x20 bytes, at most 0x2800: +0x00 its heap place, +0x04 the parent,
   +0x0c the estimate, +0x10 the cost, +0x14 the cell, +0x1c impassable, +0x1d blocked, +0x1e a 7):
   the estimate the Manhattan distance to the end. At the end already: done. Into the heap (a binary
   min-heap of node indices from 1, by the estimate: 0x4be960 compares, 0x4be9b0 notes the place; a
   new node bubbles up while lower than its parent; 0x4be8a0 bubbles down to the lower child, the
   right only if strictly lower, while higher than it).
3. While the heap isn't empty: the top taken (the last moved there and bubbled down). For the 8
   neighbours (0 north z − 1, then clockwise; the odd ones diagonal; the parent's cell skipped):
   impassable ones only from an impassable cell with a cost under 3 (0x529808), and not from a
   blocked one to one that isn't; passable ones from an impassable cell only if the tree gets there
   from the start (`ASiCollisionTest` from the character's own tree, moved by the cell less the start);
   a 7 only from a 7 or when allowed. The step's cost: the cell value's (the table at 0x529dc4: 1, 2,
   2.5, 3, 3.5, 5, 10, 20, 400, 1000, ...), ×1.4 (0x529d80) on a diagonal. A diagonal needs one of the
   straight steps beside it (the pairs at 0x54b0ba). The order they are taken in turns through 4 (the
   table at 0x54b078: 0..7, 7..0, the even then the odd, the odd then the even), one more each node.
   A new cell's node is made (at the end: done) and pushed; a known one taken over only when cheaper,
   then moved in the heap from its place (even a node already taken out keeps its old place: the heap
   is shuffled there; kept).

**`ASiList_CalcWaypoints`** 0x4be310: the start; unless the room is a stair one, the cells where the
way turns (at most 254); the adjusted end, if not the end; the end. Then from each kept one, a point is
dropped when the line from the last kept one to the point after passes (`PHrLocalPathWeight` 0x50dc60
with a threshold 3 and a tolerance 3: Bresenham, every cell under 3 but up to 3 at the line's start, a 9
after a blocked cell ending it; not blocked at its first cell; its highest value under 3), while there
is room. A waypoint: the cell's middle on the grid's floor (the last: the end point), and a kind by the
cell's value (the table at 0x54b0c8: 0, 0, 0, 2, 1, 2, 2, 2, 3, 0).

## The run-time obstacles

Read from the exe (confirmed). Port: `src/obstacle.c` (the layer, the lines, the ignore rules),
`door_obstruction` in `src/door.c`, the passable test in `src/localpath.c`; test `tests/door_test.c`.

`PHrPrepareRoomForPath` 0x50d660 (from `ASrPath_SetParams`) keeps, for each room's node: a byte per cell
(+0x2c, the obstacle's number, 0 none), the list of obstacles (+0x38: a u16 count that starts at 1,
then up to 31 u16 entries), and the time it was made (+0x34). It is made again when it is the room's
first path, or 20 ticks have passed (0x14). Each making adds one to a pass counter (0x5ed33c), then:

- **The characters** (`PHrAddCharacterToGrid` 0x50c4c0), each active one without C+4 0x40004 whose
  room is this one (C+0x184's grid): its position (C+0x148) in the grid's cells (`PHrWorldToGridSpaceDangerous`
  0x43c290: (x − cell / 2 − the grid's x) / cell − the grid's first column, rounded; the same for z),
  from 2 outside the grid to 2 past it. The entry is 0x8000 | its slot. Its cells are the 5 × 5 square
  round it without the four corners (21 cells; each inside the grid, 0x43c000).
- **The objects** (`PHrAddObjectToGrid` 0x50c780), each of the level's objects with flag 0x200 (+0x16)
  but not 0x102, whose +0x128 bit 0 is clear: its position (+0x84 of its +0x18) in cells, r = its
  radius over the cell size rounded, plus 1. The entry is its index. Only one row is filled: the cells
  from x − r to x + r at z + r (one loop over x, at the square's last row: an original bug, to keep).
- **The doors** (0x50d840 for each link out of the room, then each link in, with a door; once a pass
  by the link's +0x28, so a door on a link and on the link back is two obstacles). The door's panels
  come from `OBJrDoor_ComputeObstruction` 0x4c2ee0 as 2 points (one line) or 4 (two, flag 0x80, a
  double door):
  1. The door's own animation (S+0x164) at the open frame (OBAN +0x74) if the door is open (S+0xc ==
     1), else at its last frame (OBAN +0x72 − 1): `OBrAnim_Matrix` 0x4395f0, M.
  2. The width w (`OBJiDoor_GetGeometryWidth` 0x4c2e60): the class's (DOOR +0) OFGA's first geometry
     (+0x1c, read once for each of its entries), its points' box, max x − min x, at least 0.
  3. The line (−w/2, 0, 0) to (w/2, 0, 0) through M; a double door's second line Rx(π)·M·Rx(π)
     (0x434530).
  4. Each point (x, y, z) to (x, z, −y), then through the panel's placement: `OBJiDoor_SetOSD` 0x4c3b30
     makes S+0x104 (the first line's) and S+0x134 (the second's) from the class's box: a move by (the
     box's middle x, −min z, the middle y), a double door's both by (w/2, −min z, the middle y); then
     a half turn about y from the left (0x434d70) for the first if the door has 0x100, for a double
     door's second if it hasn't.
  5. y less −min z (S+0x18), through the door's rotation (S+0x1d8: 0x437130, order 0, of the degrees),
     plus the door's position.
  6. A line whose ends are both 15 or more up (0x529d84) is dropped; both dropped, no obstacle.

  `PHrAddDoorToGrid` 0x50c870 takes each line's ends to cells (0x43c290). A line wholly to one side of
  the grid is skipped; the first that isn't takes the obstacle (0x4000 | the id & 0x3fff; a full list
  ends the door). `PHrDynamicBresenham2` 0x43c460 draws it: Bresenham's line with 16-bit steps, the error starting at half the
  difference of the spans, writing only the cells inside the grid that are still 0.

On level 1 a shut door's line lies across the doorway (30 long for a single door, two of 22.5 from the
middle for a double); a sliding door's open line lies beside the doorway, and a door that rolls up has
none open.

`PHrSquareIsPassable` 0x50db00 then reads the cell: out of the grid or a 9, impassable. A cell with an
obstacle asks `PHrIgnoreObstruction` 0x50d9f0 once a path (two bit vectors keep the answers):

- an object: ignored unless it has flag 0x200 without 0x100;
- a door (0x4000): not ignored when the mode is 0; else the first door of the id
  (`OBJrDoor_FindDoorWithID` 0x4c1d30; none, ignored) is not ignored when it is open (0x4c2490) or the
  character can't open it (0x4c24b0, side 0xff), nor in mode 1 when it opens by hand (0x4c24a0);
  otherwise ignored. So a shut door the character may open is passed, and an open one's panels are in
  the way;
- a character (0x8000): itself is ignored; another is not in mode 0, and in the other modes only an
  AI-moved one (C+4 0x400000) asks `AI2rTryToPathfindThrough`.

Not ignored, the cell is impassable and blocked (9). The modes: the local path (`ASrPath_Generate`) asks
with 2; the local movement's tests (`AI2rTryLocalMovement`, `AI2rFindLocalPath`, `AI2rFindNearbyPoint`)
with 1 (0x5eb860, 0x5eb864), for the character moved.

**`PHrProjectOntoConnection`** 0x50e9a0 (graph, link; the near point, the point moved): the line from
the near point to the point, in x and z, crossed with the link's edge (its corners by the link's bits
16..19; the normal (a.z − b.z, b.x − a.x)). Parallel (the two sides' difference under 0.001,
0x529810): the edge's middle. The crossing's distance along the edge (its dot with b − a over the
link's width, +0x20) is kept in from the ends by 10 (0x52a190) at an end the link's flag 4 (the first)
or 8 (the second) marks, else 0 (0x529514); an edge too short for both, the middle. Inside a blocked
run (+0x40 the first of +0x42 runs in the graph's table, a u32 each: the run's start and end fractions
in the high and low 16 bits, over 65535, `MUrUnsignedSmallFloat_To_Uns_Round` 0x4340a0 when the graph
is built): to its end if its start is within 0.01 (0x5295bc) of the low margin; else to its start if
its end is within 0.01 of the high one or the point is in its first half; else to its end. Moved, the
point is the edge's at that distance (its height too); else the crossing (the height on the line).

`AI2rPath_FindGhostWaypoint` 0x4aea60 (the character, the near point: its location C+0x160, the
room's index k): from link k + 4, its `PHrWaypointFromGunk` near the character, or, when k + 4 is past
the path's count less 2 (C+0x114c), the destination (C+0x1160) from the path's last link; then
projected onto each link back to k. The way out of a room so aims at the rooms ahead.
`AI2iMovementStub_SetupPath` 0x4ac4d0's caller (the stub's, not ported) does the same.

**The character's room** C+0x188 (`PHrNodeFromBNV` 0x50c9c0: the BNV's +0x18 graph node; the BNV
C+0x184 from `AKrNodeFromPoint`): set from the location in `ONrGameState_NewCharacter` (0x4db713,
before the movement's start) and in `ONrGameState_UpdateCharacters` once a tick (0x4dda33, after the
characters' collisions, before the physics' forces; 0x4e91c0 for the animations' teleport), and from
the new place in `ONrCharacter_Teleport` 0x4e9210, `ONiCharacter_MoveThroughPhysics` 0x4e9330 and
`iSetAnyCharacterLocation` 0x4da020 (0x4da260, 0x4da800 alike). `AI2iPath_MakePath` takes it as it is
(none: error 3) and the location as the start; `PHrPathDistance`'s callers pass it and the location
(without it, the room is found from the point). The run-time obstacle layer adds the characters whose
C+0x184 is the room. Port: `Character.room`, `chr_room`.

## The clear point

`AI2rMovementState_Initialize` 0x4a8d60, from `ONrGameState_NewCharacter` 0x4dac50 for a character the AI
moves (C+4 0x400000), with no path to restore (the argument C+0x15b0 only with the creation's flag), ends
by `AI2iMovementState_FindClearPoint` 0x4a9170 at the character's position (C+0x148), and
`ONrCharacter_Teleport` 0x4e9210 if it moved:

1. The room under the point (`AKrNodeFromPoint`), its layer made (`PHrPrepareRoomForPath`), the point's
   cell clamped to the grid.
2. The cell's obstacle, if there is one and `PHrIgnoreObstruction` (mode 0, as the character) doesn't let
   it through: a character (0x50d960: an active one, C+4 0x8000) more than 6 away (its location,
   C+0x160; 36 squared, 0x52a994) keeps the point; a door (0x50d9b0, the first of the id) the character
   can't open (`OBJrDoor_CharacterCanOpen`, side 0xff) keeps it.
3. Otherwise `AI2rFindNearbyPoint` 0x49cec0: 15 toward the character's facing (C+0x16c), at least 5,
   steps of 30 degrees (0x3f060a92), 6 of them, cells under 8, 5 blocked at the start. Found, the point
   takes its x and z.

So a character spawned onto another (`ai2_spawn` at a flag someone stands on) or onto a crate steps
aside. Port: `movement_find_clear_point`, `movement_place_clear` (`src/movement.c`), from the
characters' creation in `src/chars.c`. The port loads the level's objects and doors after its first
characters, so their placement at the level's start sees the characters only (inferred; the original's
order of `OBJrLevel_Load` and the characters wasn't checked); the layers are made again once the objects
and doors are in (`obstacle_invalidate`).

## The port

`src/graph.c` builds the graph once per environment (`graph_for`), with the blockages from the rooms'
grids, and finds the room under a point and the path. The AI's mover (`src/ai.c`) makes the path as
`AI2iPath_MakePath` does and walks it straight from link to link, then to the point; a link is passed
in the room beyond, or within 2 of its middle (a stand-in). In each room it makes the local path (`src/localpath.c`:
`ASrPath_SetParams`, `ASrPath_Generate`, `ASiList_CalcWaypoints`, `PHrWaypointFromGunk`) and heads for
its waypoints from the second, opening the doors on its links.

A waypoint counts as passed within a radius set by its kind (0x4a95d0, run by
`AI2rMovement_Update` 0x4a8550): 12 for kind 0, 8 for kind 1, and 3 for the others. The movement
state's flag +0x18c4 makes it 3 for every waypoint; the port doesn't keep that flag. The last waypoint
of the last room uses the goal's radius when that is smaller, but never less than 3 (0x529808).

The steering (`AI2iMovementState_Calculate_Movement` 0x4aa250) compares the facing with the way. If
it is off by more than 80 degrees (0x52af5c), or 120 in the run modes 6 and 7 (0x52ade4), the
character turns where it stands (move 4). With obstacles in the movement state's list (+0x14) the
limit is 40 degrees (0x52a8a0).

Within the limit, the original also tests the turning circle. The radius needed to reach the waypoint
is (forward² + side²) / (2 × side), where side is the lateral distance less 12, 8 or 3 by the
waypoint's kind. If the class's speed (`ONrCharacterClass_MovementSpeed` 0x4ee020) divided by its turn
rate (0x498520) exceeds 0.9 of that radius, it stops and turns (+0x898) until it is within 5 degrees
(0x52af50).

The port (`src/movement.c`) has the turns, the obstacle list's 40 degrees and the turning-circle test.
The ghost waypoint (`ghost_waypoint`, `graph_project_onto_link`), the character's room kept as the
original keeps it (below), the doors' danger factors, the grids' run-time obstacle layer and the clear
point are ported (above, "The run-time obstacles" and "The clear point"). Stand-in left: a link counts
as passed in the room beyond or within 2 of its middle. Keys need nothing: none are ever given ([door.md](door.md)).

`tests/graph_test.c`: level 1's 240 rooms and 528 links, each with one back, 32 blocked; the training
room apart from the warehouse; a 68-room path across the warehouse. `tests/ai_test.c` walks a thug
to a room three links away (6 rooms with the local paths); `AI_DEPTH=25` sends it 25 links away, up
the warehouse's stairs (26 rooms, 9,171 ticks).
