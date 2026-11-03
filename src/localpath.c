/* The AI's local paths: see localpath.h. */
#include "localpath.h"

#include "akev.h"
#include "akev_collide.h"
#include "exe.h"
#include "graph.h"
#include "physics.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { NODES_MAX = 0x2800 };

typedef struct {
    uint16_t heap;          /* +0x00: its place in the heap */
    int parent;             /* +0x04 */
    int next;               /* +0x08: toward the end (the waypoints' walk) */
    float f, g;             /* +0x0c, +0x10 */
    int16_t x, z;           /* +0x14 */
    bool impassable;        /* +0x1c: its cell can't be entered */
    bool blocked;           /* +0x1d: by an obstacle (the run-time layer) */
    bool seven;             /* +0x1e: a cell of 7 */
} Node;

/* The path's state (the one ASrPath, *0x637108). */
static struct {
    int room;
    GraphGrid grid;
    const void *env;
    const Character *c;
    float start[3], end[3];         /* +0x30, +0x3c */
    int16_t sx, sz, ex, ez, ax, az; /* +0x24, +0x28, +0x2c: the start, end and adjusted end cells */
    bool allow7;                    /* +0x4c */
    uint16_t order;                 /* +0x60 */
    uint16_t *map;                  /* +0x20: each cell's node, 0xffff none */
    uint32_t map_cells;
    Node nodes[NODES_MAX];          /* +0x58 */
    uint16_t nnodes;                /* +0x5c */
    uint16_t heap[NODES_MAX];       /* +0x14 */
    uint32_t heap_count;            /* +0x10: 1 when empty */
    bool tree_ready;                /* +0x46d */
    float tree[7][4];               /* +0x470: the character's tree at the end point */
} P;

/* ---- the cells ---- */

/* PHrSquareIsPassable 0x50db00: out of the grid or a 9 is impassable (value 9); else passable, with the
   cell's value. The run-time obstacle layer (+0x2c) is clear: not ported. */
static bool passable(int x, int z, bool *blocked, uint8_t *value)
{
    if (blocked) *blocked = false;
    if (!((uint16_t)x < (uint16_t)P.grid.w && (uint16_t)z < (uint16_t)P.grid.h)) {
        if (value) *value = 9;
        return false;
    }
    uint8_t v = P.grid.cells ? P.grid.cells[(uint32_t)(uint16_t)z * P.grid.w + (uint16_t)x] : 0;
    if (v == 9) {
        if (value) *value = 9;
        return false;
    }
    if (value) *value = v;
    return true;
}

/* 0x49ec30: the tree moving by mv runs into a wall (|normal y| ≤ 0.5) it isn't moving away from (a
   two-sided one counts from both sides), ending behind it with the touch point not in front. */
static bool tree_collides(const float tree[7][4], const float mv[3])
{
    static AkevHit hits[AKEV_HITS_MAX];
    unsigned n = akev_collide_sphere(P.env, tree[0], mv, 0x48800, 0xff, hits);
    for (unsigned i = 0; i < n; i++) {
        float pl[4];
        memcpy(pl, hits[i].plane, sizeof pl);
        if (!(fabsf(pl[1]) <= 0.5f)) continue;
        if (0.0f < pl[0] * mv[0] + pl[1] * mv[1] + pl[2] * mv[2]) {
            AkevQuad q;
            if (!akev_quad(P.env, hits[i].quad, &q) || !(q.flags & 0x2a0)) continue;
            for (int k = 0; k < 4; k++) pl[k] = -pl[k];
        }
        const float *c = tree[0];
        if ((c[0] + mv[0]) * pl[0] + (c[1] + mv[1]) * pl[1] + (c[2] + mv[2]) * pl[2] + pl[3] <= 0.0f &&
            (hits[i].point[0] - c[0]) * pl[0] + (hits[i].point[1] - c[1]) * pl[1] + (hits[i].point[2] - c[2]) * pl[2] <= 0.0f &&
            physics_tree_hits(tree, P.env, hits[i].quad, mv))
            return true;
    }
    return false;
}

/* ASiCollisionTest 0x4be610: the character's tree moved to the cell; from the character, by the cell
   less the path's start (from_start), or from a copy of the tree at the end point, by the cell less
   the end. */
static bool collision_test(int x, int z, bool from_start)
{
    if (!P.c || !P.c->has_physics) return false;
    float w[3];
    graph_cell_middle(&P.grid, x, z, NULL, w);
    if (from_start) {
        float mv[3] = { w[0] - P.start[0], 0.0f, w[2] - P.start[2] };
        return tree_collides(P.c->spheres, mv);
    }
    if (!P.tree_ready) {
        float d[3] = { P.end[0] - P.start[0], P.end[1] - P.start[1], P.end[2] - P.start[2] };
        for (int s = 0; s < 7; s++)
            for (int k = 0; k < 3; k++) P.tree[s][k] = P.c->spheres[s][k] + d[k];
        for (int s = 0; s < 7; s++) P.tree[s][3] = P.c->spheres[s][3];
        P.tree_ready = true;
    }
    float mv[3] = { w[0] - P.end[0], 0.0f, w[2] - P.end[2] };
    return tree_collides(P.tree, mv);
}

/* ASiTryEndPoint 0x4be830: a cell in the grid, passable, the tree getting to it from the end: the end. */
static bool try_end(int x, int z)
{
    if (x < 0 || x >= (int16_t)P.grid.w || z < 0 || z >= (int16_t)P.grid.h) return false;
    if (!passable(x, z, NULL, NULL) || collision_test(x, z, false)) return false;
    P.ax = (int16_t)x, P.az = (int16_t)z;
    return true;
}

/* ---- the heap (by the estimate, the first of equals kept) ---- */

/* ASiAstarHeap_CompareFunc 0x4be960: 1 when a's estimate is the lower, 0 equal, -1 higher. */
static int compare(uint16_t a, uint16_t b)
{
    float fa = P.nodes[a].f, fb = P.nodes[b].f;
    if (fa < fb) return 1;
    return fa <= fb ? 0 : -1;
}

static void place(uint16_t node, uint32_t at) { P.heap[at] = node, P.nodes[node].heap = (uint16_t)at; }

static void bubble_up(uint32_t at)
{
    uint16_t item = P.heap[at];
    while (1 < at) {
        uint16_t up = P.heap[at >> 1];
        if (compare(item, up) < 1) break;
        place(up, at);
        at >>= 1;
    }
    place(item, at);
}

/* 0x4be8a0 */
static void bubble_down(uint32_t at)
{
    uint16_t item = P.heap[at];
    for (;;) {
        uint32_t c = (at * 2) & 0xffff;
        if (P.heap_count <= c) break;
        uint16_t child = P.heap[c];
        if (c + 1 < P.heap_count && 0 < compare(P.heap[c + 1], child)) child = P.heap[++c];
        if (-1 < compare(item, child)) break;
        place(child, at);
        at = c;
    }
    place(item, at);
}

static void push(uint16_t node)
{
    if (P.heap_count >= NODES_MAX) return;
    uint32_t at = P.heap_count++;
    P.heap[at] = node;
    bubble_up(at);
}

/* ---- the waypoints ---- */

static const int32_t *point_kinds(void)
{
    static int32_t kinds[16];
    static bool loaded;
    if (!loaded) {
        for (int i = 0; i < 16; i++) kinds[i] = (int32_t)exe_u32(0x54b0c8 + 4u * (uint32_t)i);
        loaded = true;
    }
    return kinds;
}

/* PHrLocalPathWeight 0x50dc60: the cells along the line (Bresenham) from (x0, z0) to (x1, z1) under the
   threshold all the way, but for up to `tolerance` at its start; the line's highest and lowest values. */
static bool line_weight(int x0, int z0, int x1, int z1, uint8_t threshold, int tolerance, bool *first_blocked,
                        uint8_t *lowest, uint8_t *highest)
{
    int dx = x1 - x0, dz = z1 - z0, nx = dx < 0 ? -dx : dx, nz = dz < 0 ? -dz : dz;
    int sx = dx < 0 ? -1 : (dx != 0), sz = dz < 0 ? -1 : (dz != 0);
    uint8_t lo = 9, hi = 0;
    bool first = true, prev_blocked = false;
    *first_blocked = false;
    int x = x0, z = z0, err = nx > nz ? (nz - nx) / 2 : (nx - nz) / 2;
    for (;;) {
        bool blocked;
        uint8_t v;
        if (!passable(x, z, &blocked, &v)) v = 9;
        if (v < lo) lo = v;
        if (hi < v) hi = v;
        if (v < threshold) {
            if (x == x1 && z == z1) { *highest = hi, *lowest = lo; return true; }
        } else {
            if (lo < threshold || !tolerance || (x == x1 && z == z1) || (prev_blocked && 8 < v && !blocked)) {
                *highest = hi, *lowest = lo;
                return false;
            }
            if (first) *first_blocked = true;
            tolerance--;
        }
        if (nx > nz) {
            if (err < 0) x += sx, err += nz;
            else z += sz, err -= nx;
        } else {
            if (err < 0) z += sz, err += nx;
            else x += sx, err -= nz;
        }
        first = false;
        prev_blocked = blocked;
    }
}

/* ASiList_CalcWaypoints 0x4be310: the start, the cells where the way turns (none in a stair room), the
   adjusted end and the end; then, from each kept one, those a straight line gets past (weight 3, up to 3
   blocked at its start) dropped. Each waypoint: its cell's middle on the grid's floor (the last: the end
   point), and its kind. */
static int waypoints(int found, bool simplify, LocalPoint *out)
{
    int16_t cell[LOCAL_PATH_MAX + 4][2];
    int n = 0;
    cell[n][0] = P.sx, cell[n][1] = P.sz, n++;
    uint32_t bnv_flags = graph_room_flags(P.room);
    if (!(bnv_flags & 1)) {
        int r = found;
        P.nodes[r].next = -1;
        while (P.nodes[r].parent >= 0) {
            P.nodes[P.nodes[r].parent].next = r;
            r = P.nodes[r].parent;
        }
        for (int p = r, q = P.nodes[r].next; q >= 0; p = q, q = P.nodes[q].next) {
            if (n - 1 > 0xfd) break;
            int pa = P.nodes[p].parent;
            if (pa < 0) continue;
            if ((int16_t)(P.nodes[pa].x - P.nodes[p].x) != (int16_t)(P.nodes[p].x - P.nodes[q].x) ||
                (int16_t)(P.nodes[pa].z - P.nodes[p].z) != (int16_t)(P.nodes[p].z - P.nodes[q].z))
                cell[n][0] = P.nodes[p].x, cell[n][1] = P.nodes[p].z, n++;
        }
    }
    if (P.ax != P.ex || P.az != P.ez) cell[n][0] = P.ax, cell[n][1] = P.az, n++;
    cell[n][0] = P.ex, cell[n][1] = P.ez;
    int last = n;   /* the entries are 0..last */
    int count = 0;
    int16_t kx = P.sx, kz = P.sz;
    const int32_t *kinds = point_kinds();
    for (int i = 0; i <= last; i++) {
        bool emit = i == 0 || i >= last;
        if (!emit) {
            if (!(count + 1 < LOCAL_PATH_MAX)) continue;
            if (simplify) {
                bool blocked;
                uint8_t lo, hi;
                if (line_weight(kx, kz, cell[i + 1][0], cell[i + 1][1], 3, 3, &blocked, &lo, &hi) && !blocked && hi < 3)
                    continue;
            }
        }
        LocalPoint *w = &out[count];
        if (i < last) {
            kx = cell[i][0], kz = cell[i][1];
            graph_cell_middle(&P.grid, kx, kz, &P.grid.y0, w->p);
        } else {
            memcpy(w->p, P.end, sizeof w->p);
        }
        uint8_t v = 0;
        if (P.grid.cells && (uint16_t)cell[i][0] < P.grid.w && (uint16_t)cell[i][1] < P.grid.h)
            v = P.grid.cells[(uint32_t)(uint16_t)cell[i][1] * P.grid.w + (uint16_t)cell[i][0]];
        w->kind = kinds[v & 0xf];
        count++;
    }
    return count;
}

/* ---- the search ---- */

static int manhattan(int x, int z) { return abs(P.az - z) + abs(P.ax - x); }

/* The 8 directions (the jump table at 0x4be21c): north (z - 1) first, clockwise; the odd ones diagonal. */
static const int8_t DX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, DZ[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

int local_path(const void *env, int room, const float start[3], const float end[3], const Character *c, bool allow7,
               bool simplify, LocalPoint *out)
{
    if (!env || !graph_grid(room, &P.grid)) return 0;
    /* ASrPath_SetParams */
    P.env = env, P.room = room, P.c = c, P.allow7 = allow7;
    memcpy(P.start, start, sizeof P.start);
    memcpy(P.end, end, sizeof P.end);
    graph_to_cell(&P.grid, start, &P.sx, &P.sz);
    graph_to_cell(&P.grid, end, &P.ex, &P.ez);
    uint32_t cells = P.grid.w * P.grid.h;
    if (cells > P.map_cells) {
        free(P.map);
        P.map = malloc(sizeof *P.map * cells);
        P.map_cells = P.map ? cells : 0;
        if (!P.map) return 0;
    }
    memset(P.map, 0xff, sizeof *P.map * cells);
    P.nnodes = 0;
    P.heap_count = 1;
    P.tree_ready = false;
    /* the order table (0x54b078) and the costs (0x529dc4) */
    uint16_t order[4][8];
    float cost[16];
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 8; i++) order[j][i] = (uint16_t)(exe_u32(0x54b078 + 2u * (uint32_t)(j * 8 + i)) & 0xffff);
    for (int i = 0; i < 16; i++) {
        uint32_t v = exe_u32(0x529dc4 + 4u * (uint32_t)i);
        memcpy(&cost[i], &v, 4);
    }
    uint32_t dv = exe_u32(0x529d80);
    float diagonal;
    memcpy(&diagonal, &dv, 4);
    /* the end: a passable cell, else the nearest ring's within 3 the tree gets to */
    if (passable(P.ex, P.ez, NULL, NULL)) {
        P.ax = P.ex, P.az = P.ez;
    } else {
        bool got = false;
        for (int r = 1; r < 4 && !got; r++) {
            for (int z = P.ez - r; z <= P.ez + r && !got; z++) {
                int x = P.ex - r;
                if (x >= 0 && x < (int16_t)P.grid.w && z >= 0 && z < (int16_t)P.grid.h && passable(x, z, NULL, NULL) &&
                    !collision_test(x, z, false)) {
                    P.ax = (int16_t)x, P.az = (int16_t)z;
                    got = true;
                } else if (try_end(P.ex + r, z)) {
                    got = true;
                }
            }
            for (int x = P.ex - r + 1; x <= P.ex + r - 1 && !got; x++)
                got = try_end(x, P.ez - r) || try_end(x, P.ez + r);
        }
        if (!got) return 0;
    }
    /* the start */
    Node *s = &P.nodes[P.nnodes++];
    memset(s, 0, sizeof *s);
    s->parent = -1;
    s->x = P.sx, s->z = P.sz;
    s->f = (float)manhattan(P.sx, P.sz);
    s->heap = 0xffff;
    uint8_t v;
    s->impassable = !passable(P.sx, P.sz, &s->blocked, &v);
    s->seven = v == 7;
    if (s->x == P.ax && s->z == P.az) return waypoints(0, simplify, out);
    if ((uint16_t)s->x < P.grid.w && (uint16_t)s->z < P.grid.h) P.map[(uint32_t)(uint16_t)s->z * P.grid.w + (uint16_t)s->x] = 0;
    push(0);
    while (1 < P.heap_count) {
        uint16_t first = P.heap[1];
        P.heap_count--;
        if (1 < P.heap_count) {
            P.heap[1] = P.heap[P.heap_count];
            bubble_down(1);
        }
        if (first == 0xffff) return 0;
        int cur = first;
        Node *n = &P.nodes[cur];
        bool allowed[8], impass[8], blocked[8], seven[8];
        int16_t nx[8], nz[8];
        float g[8];
        for (int d = 0; d < 8; d++) {
            allowed[d] = false;
            nx[d] = (int16_t)(n->x + DX[d]), nz[d] = (int16_t)(n->z + DZ[d]);
            if (n->parent >= 0 && P.nodes[n->parent].x == nx[d] && P.nodes[n->parent].z == nz[d]) continue;
            uint8_t val;
            bool ok = passable(nx[d], nz[d], &blocked[d], &val);
            impass[d] = !ok;
            seven[d] = val == 7;
            if (!ok) {
                if (!n->impassable || !(n->g < 3.0f)) continue;     /* 0x529808 */
                if (!blocked[d] && n->blocked) continue;
            } else {
                if (n->impassable && collision_test(nx[d], nz[d], true)) continue;
                if (seven[d] && !n->seven && !P.allow7) continue;
            }
            float step = cost[val & 0xf];
            if (n->x != nx[d] && n->z != nz[d]) step *= diagonal;
            g[d] = step + n->g;
            allowed[d] = true;
        }
        /* a diagonal needs one of the straight steps beside it (the pairs at 0x54b0ba) */
        for (int d = 1; d < 8; d += 2)
            if (allowed[d] && !allowed[d - 1] && !allowed[(d + 1) & 7]) allowed[d] = false;
        P.order = (uint16_t)((P.order + 1) & 3);
        for (int k = 0; k < 8; k++) {
            int d = order[P.order][k] & 7;
            if (!allowed[d]) continue;
            uint32_t at = (uint32_t)(uint16_t)nz[d] * P.grid.w + (uint16_t)nx[d];
            if (at >= cells) continue;
            uint16_t ni = P.map[at];
            bool fresh = ni == 0xffff;
            if (fresh) {
                if (P.nnodes >= NODES_MAX) return 0;    /* "ASrPath_Generate: overflowed ASc nodes" */
                ni = P.nnodes++;
                P.map[at] = ni;
                memset(&P.nodes[ni], 0, sizeof P.nodes[ni]);
                P.nodes[ni].heap = 0xffff;
                P.nodes[ni].x = nx[d], P.nodes[ni].z = nz[d];
            } else if (P.nodes[ni].g <= g[d]) {
                continue;
            }
            Node *m = &P.nodes[ni];
            m->g = g[d];
            m->parent = cur;
            m->f = (float)manhattan(m->x, m->z) + g[d];
            m->impassable = impass[d];
            m->blocked = blocked[d];
            m->seven = seven[d];
            n = &P.nodes[cur];
            if (fresh) {
                if (m->x == P.ax && m->z == P.az) return waypoints(ni, simplify, out);
                push(ni);
            } else {
                uint16_t at2 = m->heap;
                if (1 < at2 && 0 < compare(P.heap[at2], P.heap[at2 >> 1])) bubble_up(at2);
                else bubble_down(at2);
            }
        }
    }
    return 0;
}
