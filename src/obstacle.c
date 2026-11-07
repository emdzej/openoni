/* The run-time obstacles in the rooms' grids: see obstacle.h. */
#include "obstacle.h"

#include "chars.h"
#include "chr_hit.h"
#include "door.h"
#include "object.h"
#include "ui.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    OBSTACLES = 32,             /* the list at the node's +0x38: a count from 1, then 31 entries */
    REMAKE_TICKS = 0x14,
    KIND_OBJECT = 0, KIND_DOOR = 0x4000, KIND_CHARACTER = 0x8000,
};

typedef struct {
    uint16_t kind;
    int index;                  /* the object's or the door's */
    const Character *c;
} Obstacle;

typedef struct {
    bool made;
    uint32_t time;              /* +0x34 */
    uint32_t w, h;
    uint8_t *layer;             /* +0x2c */
    uint8_t count;              /* +0x38 */
    Obstacle list[OBSTACLES];
} Room;

static Room *rooms;
static int nrooms;
static uint32_t pass;           /* 0x5ed33c: one more each time a layer is made */
static uint32_t *link_pass;     /* the link's +0x28: the pass its door was last added in */
static int nlinks;

void obstacle_level_begin(void)
{
    for (int i = 0; i < nrooms; i++) free(rooms[i].layer);
    free(rooms);
    rooms = NULL;
    nrooms = 0;
    free(link_pass);
    link_pass = NULL;
    nlinks = 0;
}

/* 0x43c000: a cell set, if inside the grid */
static void set(Room *r, int x, int z, uint8_t v)
{
    if (x >= 0 && (uint32_t)x < r->w && z >= 0 && (uint32_t)z < r->h) r->layer[(uint32_t)z * r->w + (uint32_t)x] = v;
}

/* PHrAddCharacterToGrid 0x50c4c0: a character in the room, its 5 x 5 cells without the corners */
static void add_character(Room *r, const GraphGrid *g, const Character *c)
{
    int16_t x, z;
    graph_to_cell(g, c->pos, &x, &z);       /* PHrWorldToGridSpaceDangerous 0x43c290 */
    if (!(x > -3 && x < (int)r->w + 2 && z > -3 && z < (int)r->h + 2)) return;
    if (r->count >= OBSTACLES) return;      /* "PHrAddCharacterToGrid: too many obstructions" */
    uint8_t n = r->count++;
    r->list[n] = (Obstacle){ KIND_CHARACTER, -1, c };
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) set(r, x + dx, z + dz, n);
    for (int d = -1; d <= 1; d++) {
        set(r, x + d, z - 2, n);
        set(r, x - 2, z + d, n);
        set(r, x + 2, z + d, n);
        set(r, x + d, z + 2, n);
    }
}

/* PHrAddObjectToGrid 0x50c780: r = the radius in cells (rounded) + 1; only the row at z + r is filled,
   x - r to x + r (one loop over x: the original's, kept) */
static void add_object(Room *r, const GraphGrid *g, int i, const float pos[3], float radius)
{
    int16_t x, z;
    graph_to_cell(g, pos, &x, &z);
    int k = (int)lrintf(radius / g->cell) + 1;     /* MUrUnsignedSmallFloat_To_Uns_Round 0x4340a0 */
    if (!(x >= -k && x < (int)r->w + k && z >= -k && z < (int)r->h + k)) return;
    if (r->count >= OBSTACLES) return;      /* "PHrAddObjectToGrid: too many obstructions" */
    uint8_t n = r->count++;
    r->list[n] = (Obstacle){ KIND_OBJECT, i, NULL };
    for (int d = -k; d <= k; d++) set(r, x + d, z + k, n);
}

/* PHrDynamicBresenham2 0x43c460: a line of cells from a to b (Bresenham's, 16-bit), those inside the grid and still empty
   (the last argument 0) taking v */
static void line(Room *r, int16_t ax, int16_t az, int16_t bx, int16_t bz, uint8_t v)
{
    int16_t dx = (int16_t)(bx - ax), dz = (int16_t)(bz - az);
    int16_t adx = (int16_t)abs(bx - ax), adz = (int16_t)abs(bz - az);
    int16_t sx = dx < 0 ? -1 : dx != 0, sz = dz < 0 ? -1 : dz != 0;
    int16_t x = ax, z = az;
    #define PLOT() do { \
        if (x >= 0 && (uint32_t)x < r->w && z >= 0 && (uint32_t)z < r->h && !r->layer[(uint32_t)z * r->w + (uint32_t)x]) \
            r->layer[(uint32_t)z * r->w + (uint32_t)x] = v; \
    } while (0)
    if (adx > adz) {
        int16_t e = (int16_t)((adz - adx) / 2);
        for (;;) {
            PLOT();
            if (x == bx) break;
            if (e >= 0) z = (int16_t)(z + sz), e = (int16_t)(e - adx);
            x = (int16_t)(x + sx), e = (int16_t)(e + adz);
        }
    } else {
        int16_t e = (int16_t)((adx - adz) / 2);
        for (;;) {
            PLOT();
            if (z == bz) break;
            if (e >= 0) x = (int16_t)(x + sx), e = (int16_t)(e - adz);
            z = (int16_t)(z + sz), e = (int16_t)(e + adx);
        }
    }
    #undef PLOT
}

/* PHrAddDoorToGrid 0x50c870: the door's lines (a pair of points each, the second pair NULL for one),
   one obstacle for both, made at the first line touching the grid's bounds */
static void add_door_lines(Room *r, const GraphGrid *g, uint16_t id, float (*p)[3], int lines)
{
    int n = -1;
    for (int k = 0; k < lines; k++) {
        int16_t ax, az, bx, bz;
        graph_to_cell(g, p[2 * k], &ax, &az);
        graph_to_cell(g, p[2 * k + 1], &bx, &bz);
        if (ax < 0 && bx < 0) continue;
        if (ax >= (int)r->w && bx >= (int)r->w) continue;
        if (az < 0 && bz < 0) continue;
        if (az >= (int)r->h && bz >= (int)r->h) continue;
        if (n < 0) {
            if (r->count >= OBSTACLES) return;      /* "PHrAddDoorToGrid: too many obstructions" */
            n = r->count++;
            r->list[n] = (Obstacle){ KIND_DOOR, id & 0x3fff, NULL };
        }
        line(r, ax, az, bx, bz, (uint8_t)n);
    }
}

/* 0x50d840: a link's door, once a pass for the link (the same door on the link back is another
   obstacle) */
static void add_link_door(Room *r, const GraphGrid *g, int li)
{
    const GraphLink *l = graph_link(li);
    if (!l || l->door < 0 || li >= nlinks || link_pass[li] == pass) return;
    float p[4][3];
    uint16_t id = 0;
    int n = door_obstruction(l->door, &id, p);
    if (n >= 2) add_door_lines(r, g, id, p, n >= 4 ? 2 : 1);
    link_pass[li] = pass;
}

void obstacle_prepare(int room, const GraphGrid *g)
{
    if (room < 0 || !g || !g->w || !g->h) return;
    if (room >= nrooms) {
        int n = graph_room_count();
        if (room >= n) return;
        Room *m = realloc(rooms, sizeof *m * (size_t)n);
        if (!m) return;
        memset(m + nrooms, 0, sizeof *m * (size_t)(n - nrooms));
        rooms = m, nrooms = n;
    }
    Room *r = &rooms[room];
    uint32_t now = ui_time();
    if (r->made && (r->w != g->w || r->h != g->h)) free(r->layer), r->made = false, r->time = 0;
    bool first = !r->made;
    if (first) {
        r->w = g->w, r->h = g->h;
        r->layer = malloc(r->w * r->h);
        if (!r->layer) return;
        r->made = true;
    }
    if (r->time && !first && now < r->time + REMAKE_TICKS) return;
    if (nlinks != graph_link_count()) {
        free(link_pass);
        nlinks = graph_link_count();
        link_pass = calloc(nlinks ? (size_t)nlinks : 1, sizeof *link_pass);
        if (!link_pass) { nlinks = 0; return; }
    }
    memset(r->layer, 0, r->w * r->h);
    pass++;
    r->count = 1;
    /* the active characters (without C+4 0x40004) whose room (C+0x184) is this one */
    for (int i = 0; i < chars_slots(); i++) {
        const Chr *ch = chars_get(i);
        if (!ch || !chars_in_use(ch) || (ch->c.flags & 0x40004u)) continue;
        if (graph_room_at(ch->c.pos) != room) continue;
        add_character(r, g, &ch->c);
    }
    /* the objects with flag 0x200 but not 0x102, whose context hasn't flag 1 */
    for (int i = 0; i < object_count(); i++) {
        uint16_t fl;
        uint32_t cf;
        float pos[3], radius;
        if (!object_obstacle(i, &fl, &cf, pos, &radius)) continue;
        if ((fl & 0x200) && !(cf & 1) && !(fl & 0x102)) add_object(r, g, i, pos, radius);
    }
    /* the doors of its links, out then in */
    for (int k = 0; k < graph_out_count(room); k++) add_link_door(r, g, graph_out(room, k));
    for (int k = 0; k < graph_in_count(room); k++) add_link_door(r, g, graph_in(room, k));
    r->time = now;
}

/* PHrIgnoreObstruction 0x50d9f0 */
static bool ignored(const Obstacle *o, const Character *c, int mode)
{
    switch (o->kind) {
    case KIND_OBJECT: {
        uint16_t fl;
        uint32_t cf;
        float pos[3], radius;
        if (object_obstacle(o->index, &fl, &cf, pos, &radius) && (fl & 0x200) && !(fl & 0x100)) return false;
        return true;
    }
    case KIND_CHARACTER:
        if (o->c == c) return true;
        if (mode == 0) return false;
        /* AI2rTryToPathfindThrough 0x48b670, for a character the AI moves */
        if (c && (c->flags & 0x400000u) && chr_path_through_hook) return chr_path_through_hook(c, o->c);
        return false;
    case KIND_DOOR: {
        if (mode == 0) return false;
        int d = door_find((uint16_t)o->index);
        if (d < 0) return true;
        /* open (its panels in the way), or not one the character may open (OBJrDoor_CharacterCanOpen
           0x4c24b0, side 0xff), or a manual door for the local movement: in the way */
        if (door_state(d) == 1 || !door_side_passes(d, -1)) return false;
        if (mode == 1 && (door_flags(d) & DOOR_MANUAL)) return false;
        return true;
    }
    default:
        return false;
    }
}

bool obstacle_blocks(int room, const GraphGrid *g, int x, int z, const Character *c, int mode, ObstacleCache *cache)
{
    (void)g;
    /* (no character: the route harness's plans, which the original hasn't; it keeps to the baked grid) */
    if (!c) return false;
    if (room < 0 || room >= nrooms || !rooms[room].made) return false;
    const Room *r = &rooms[room];
    if (x < 0 || (uint32_t)x >= r->w || z < 0 || (uint32_t)z >= r->h) return false;
    uint8_t n = r->layer[(uint32_t)z * r->w + (uint32_t)x];
    if (!n || n >= r->count) return false;
    if (cache->room != room) cache->room = room, cache->asked = cache->ignored = 0;
    uint64_t bit = (uint64_t)1 << n;
    if (!(cache->asked & bit)) {
        cache->asked |= bit;
        if (ignored(&r->list[n], c, mode)) cache->ignored |= bit;
    }
    return !(cache->ignored & bit);
}
