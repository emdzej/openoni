/* The AI's pathfinding graph: see graph.h. */
#include "graph.h"
#include "obstacle.h"

#include "akev.h"
#include "door.h"
#include "tm.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

static const float EPS = 0.0009765625f;     /* 0x529cc8 */

static const uint8_t *elements(const uint8_t *a, unsigned count_at, uint32_t *n)
{
    *n = a ? rd32(a + count_at) : 0;
    return a ? a + count_at + 4 : NULL;
}

/* The environment's arrays the graph reads. */
static struct {
    const uint8_t *points, *planes, *quads, *bnv, *sides, *bsp, *adj, *leaves, *bnv_index, *nodes;
    uint32_t npoints, nplanes, nquads, nbnv, nsides, nbsp, nadj, nleaves, nbnv_index, nnodes;
} E;

enum { BNV_SIZE = 0x74, SIDE_SIZE = 0x1c, ADJ_SIZE = 0xc, BSP_SIZE = 0xc };

typedef struct {
    int bnv;
    int *out, *in;          /* +0x04, +0x0c: the lists, newest first */
    int nout, nin;          /* +0x00, +0x08 */
    uint32_t matrix;        /* +0x14: where its distances start */
    uint8_t *grid;          /* +0x28: the grid, decompressed */
    /* the search's (+0x78..+0x94) */
    int16_t state;          /* 0 unseen, 1 open, 2 closed */
    uint16_t depth;
    int next;               /* +0x7c: in the open or closed list */
    float g, h, f, cost;
    int parent, via;
} Room;

static Room *rooms;
static int nrooms;
static GraphLink *links;
static int nlinks;
static float *matrix;
static const void *built;     /* the environment the graph is of */

static void point_of(uint32_t i, float p[3])
{
    const uint8_t *q = i < E.npoints ? E.points + 12u * i : NULL;
    for (int k = 0; k < 3; k++) p[k] = q ? rdf(q + 4 * k) : 0.0f;
}

static uint32_t quad_point(uint32_t quad, int k) { return quad < E.nquads ? rd32(E.quads + 0x38u * quad + 4 * k) : 0; }
static uint32_t quad_flags(uint32_t quad) { return quad < E.nquads ? rd32(E.quads + 0x38u * quad + 0x30) : 0; }
static const uint8_t *bnv(int i) { return E.bnv + (size_t)BNV_SIZE * (uint32_t)i; }

static float dist(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(z * z + y * y + x * x);
}

static float d2(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

/* 0x437b60: a quad's lowest corner and the lowest of the others (by point, not by corner). */
static void low_pair(uint32_t quad, float lo[3], float lo2[3])
{
    uint32_t a = quad_point(quad, 0);
    for (int k = 1; k < 4; k++) {
        uint32_t b = quad_point(quad, k);
        float pb[3], pa[3];
        point_of(b, pb), point_of(a, pa);
        if (pb[1] < pa[1]) a = b;
    }
    bool have = false;
    uint32_t c = 0;
    for (int k = 0; k < 4; k++) {
        uint32_t b = quad_point(quad, k);
        float pb[3], pc[3];
        point_of(b, pb), point_of(c, pc);
        if (b != a && (!have || pb[1] < pc[1])) c = b, have = true;
    }
    point_of(a, lo);
    point_of(c, lo2);
}

static void load_grids(const void *env);
static void check_blockages(int room, const float p[3], const float q[3], GraphLink *l);

void graph_free(void)
{
    for (int i = 0; i < nrooms; i++) free(rooms[i].out), free(rooms[i].in), free(rooms[i].grid);
    free(rooms), free(links), free(matrix);
    rooms = NULL, links = NULL, matrix = NULL;
    built = NULL;
    nrooms = nlinks = 0;
}

bool graph_for(const void *env)
{
    if (env == built && rooms) return true;
    built = graph_build(env) ? env : NULL;
    return built != NULL;
}

bool graph_build(const void *envp)
{
    graph_free();
    obstacle_level_begin();     /* the rooms' run-time obstacle layers go with the graph */
    memset(&E, 0, sizeof E);
    const uint8_t *env = envp;
    if (!env) return false;
    E.points = elements(tm_link(env + AKEV_POINTS), 0x34, &E.npoints);
    E.planes = elements(tm_link(env + AKEV_PLANES), 0x14, &E.nplanes);
    E.quads = elements(tm_link(env + AKEV_QUADS), 0x14, &E.nquads);
    E.bnv = elements(tm_link(env + AKEV_BNV_NODES), 0x14, &E.nbnv);
    E.sides = elements(tm_link(env + AKEV_SIDES), 0x14, &E.nsides);
    E.bsp = elements(tm_link(env + AKEV_BSP_NODES), 0x14, &E.nbsp);
    E.adj = elements(tm_link(env + AKEV_ADJACENCY), 0x14, &E.nadj);
    const uint8_t *ot = tm_link(env + AKEV_OCTREE);
    if (ot) {
        E.nodes = elements(tm_link(ot), 0x14, &E.nnodes);
        E.leaves = elements(tm_link(ot + 4), 0x14, &E.nleaves);
        E.bnv_index = elements(tm_link(ot + 0x10), 0x14, &E.nbnv_index);
    }
    if (!E.points || !E.quads || !E.bnv || !E.sides || !E.adj) return false;
    /* the rooms: the BNVs with flag 4, in order (their index at BNV +0x18) */
    int *room_of = malloc(sizeof(int) * (E.nbnv ? E.nbnv : 1));
    rooms = calloc(E.nbnv ? E.nbnv : 1, sizeof *rooms);
    if (!room_of || !rooms) { free(room_of); return false; }
    for (uint32_t i = 0; i < E.nbnv; i++) {
        room_of[i] = -1;
        if (rd32(bnv((int)i) + 0x5c) & 4) {
            room_of[i] = nrooms;
            rooms[nrooms++].bnv = (int)i;
        }
    }
    /* the links: through each side's adjacencies by ghost or stair quads */
    int cap = 0;
    for (int r = 0; r < nrooms; r++) {
        const uint8_t *b = bnv(rooms[r].bnv);
        for (uint32_t s = rd32(b + 8); s < rd32(b + 0xc) && s < E.nsides; s++) {
            const uint8_t *side = E.sides + SIDE_SIZE * s;
            for (uint32_t a = rd32(side + 4); a < rd32(side + 8) && a < E.nadj; a++) {
                const uint8_t *adj = E.adj + ADJ_SIZE * a;
                uint32_t target = rd32(adj), quad = rd32(adj + 4);
                if (!(quad_flags(quad) & 0xe) || target >= E.nbnv || room_of[target] < 0) continue;
                /* the quad's lowest corner, and the lowest of the other corners */
                int lo = 0, lo2 = -1;
                float pl[3], p2[3], q[3];
                point_of(quad_point(quad, 0), pl);
                for (int k = 1; k < 4; k++) {
                    point_of(quad_point(quad, k), q);
                    if (q[1] < pl[1]) memcpy(pl, q, sizeof q), lo = k;
                }
                for (int k = 0; k < 4; k++) {
                    point_of(quad_point(quad, k), q);
                    if (k != lo && (lo2 < 0 || q[1] < p2[1])) memcpy(p2, q, sizeof q), lo2 = k;
                }
                /* a low corner is free unless another of the room's openings shares it */
                bool free1 = true, free2 = true;
                for (uint32_t s2 = rd32(b + 8); s2 < rd32(b + 0xc) && s2 < E.nsides && (free1 || free2); s2++) {
                    const uint8_t *side2 = E.sides + SIDE_SIZE * s2;
                    for (uint32_t a2 = rd32(side2 + 4); a2 < rd32(side2 + 8) && a2 < E.nadj; a2++) {
                        if (a2 == a) continue;
                        float o1[3], o2[3];
                        low_pair(rd32(E.adj + ADJ_SIZE * a2 + 4), o1, o2);
                        if (free1 && (d2(o1, pl) < 0.01f || d2(o2, pl) < 0.01f)) free1 = false;    /* 0x52b818 */
                        if (free2 && (d2(o1, p2) < 0.01f || d2(o2, p2) < 0.01f)) free2 = false;
                        if (!free1 && !free2) break;
                    }
                }
                if (nlinks == cap) {
                    cap = cap ? cap * 2 : 256;
                    GraphLink *n = realloc(links, sizeof *links * (size_t)cap);
                    if (!n) { free(room_of); return false; }
                    links = n;
                }
                GraphLink *l = &links[nlinks++];
                memset(l, 0, sizeof *l);
                int to = room_of[target];
                if (!(rd32(b + 0x5c) & 1) && !(rd32(bnv(rooms[to].bnv) + 0x5c) & 1)) {
                    if (free1) l->flags = 4;
                    if (free2) l->flags |= 8;
                } else {
                    l->flags = 0xe;
                }
                l->flags |= ((uint32_t)lo2 << 2 | (uint32_t)lo) << 16;
                for (int k = 0; k < 3; k++) l->mid[k] = (pl[k] + p2[k]) * 0.5f;   /* 0x52a1b0 */
                l->width = sqrtf((p2[2] - pl[2]) * (p2[2] - pl[2]) + (p2[0] - pl[0]) * (p2[0] - pl[0]));
                l->quad = quad;
                l->from = r;
                l->to = to;
                l->door = -1;
            }
        }
    }
    free(room_of);
    /* the lists, newest first, and their indices */
    for (int i = 0; i < nlinks; i++) rooms[links[i].from].nout++, rooms[links[i].to].nin++;
    for (int r = 0; r < nrooms; r++) {
        rooms[r].out = malloc(sizeof(int) * (size_t)(rooms[r].nout + 1));
        rooms[r].in = malloc(sizeof(int) * (size_t)(rooms[r].nin + 1));
        if (!rooms[r].out || !rooms[r].in) return false;
        rooms[r].nout = rooms[r].nin = 0;
    }
    for (int i = nlinks - 1; i >= 0; i--) {
        Room *f = &rooms[links[i].from], *t = &rooms[links[i].to];
        links[i].out_index = (uint16_t)f->nout;
        f->out[f->nout++] = i;
        links[i].in_index = (uint16_t)t->nin;
        t->in[t->nin++] = i;
    }
    /* each room's distances from the links coming in to the links going out */
    uint32_t total = 0;
    for (int r = 0; r < nrooms; r++) {
        rooms[r].matrix = total;
        total += (uint32_t)(rooms[r].nin * rooms[r].nout);
    }
    matrix = calloc(total ? total : 1, sizeof *matrix);
    if (!matrix) return false;
    for (int r = 0; r < nrooms; r++)
        for (int i = 0; i < rooms[r].nin; i++)
            for (int o = 0; o < rooms[r].nout; o++) {
                const GraphLink *in = &links[rooms[r].in[i]], *out = &links[rooms[r].out[o]];
                matrix[rooms[r].matrix + (uint32_t)(in->in_index * rooms[r].nout + out->out_index)] = dist(in->mid, out->mid);
            }
    /* the blockages: each link's bottom edge (from its lowest corner to the next) through both rooms'
       grids, once for a link and the one back through the same quad */
    load_grids(envp);
    for (int i = 0; i < nlinks; i++) links[i].blocks = -1;
    for (int r = 0; r < nrooms; r++)
        for (int k = 0; k < rooms[r].nout; k++) {
            GraphLink *l = &links[rooms[r].out[k]];
            if (l->blocks != -1) continue;
            float p[3], q[3];
            point_of(quad_point(l->quad, (int)(l->flags >> 16 & 3)), p);
            point_of(quad_point(l->quad, (int)(l->flags >> 18 & 3)), q);
            l->blocks = 0;
            check_blockages(l->from, p, q, l);
            check_blockages(l->to, p, q, l);
            float e = 4.0f / l->width;
            if (l->blocks == 1 && l->block[0][0] < e && 1.0f - e < l->block[0][1]) l->flags |= 1;
            for (int j = 0; j < rooms[r].nin; j++) {
                GraphLink *m = &links[rooms[r].in[j]];
                if (m->quad != l->quad) continue;
                if (m->blocks == -1) {
                    m->blocks = l->blocks;
                    memcpy(m->block, l->block, sizeof m->block);
                    if (l->flags & 1) m->flags |= 1;
                }
                break;
            }
        }
    graph_link_doors();
    return true;
}

static bool same_rooms(const GraphLink *a, const GraphLink *b) { return a->from == b->from && a->to == b->to; }
static bool back_rooms(const GraphLink *a, const GraphLink *b) { return a->from == b->to && a->to == b->from; }

/* OBJiDoor_MakeConnectionLink_Enum 0x4c1f90 through OBJrDoor_MakeConnectionLink 0x4c1f60, for each link
   as PHrBuildGraph makes it: the first door (in the objects' order) within half the link's width of its
   middle that keeps it in one of its two slots takes it. */
void graph_link_doors(void)
{
    for (int i = 0; i < nlinks; i++) links[i].door = -1, links[i].door_side = 0;
    int n = door_count();
    if (!n || !nlinks) return;
    /* each door's two links (+0x244, +0x248), their distances squared (+0x24c, +0x250) and widths
       (+0x254, +0x258) */
    struct { int link[2]; float d[2], w[2]; } *slot = calloc((size_t)n, sizeof *slot);
    if (!slot) return;
    for (int i = 0; i < n; i++) slot[i].link[0] = slot[i].link[1] = -1;
    for (int li = 0; li < nlinks; li++) {
        GraphLink *l = &links[li];
        /* the enumeration of the doors, in order, until one takes the link */
        for (int i = 0; i < n; i++) {
            float p[3];
            door_position(i, p);
            float d = d2(p, l->mid), r = l->width * 0.5f;     /* 0x529560 */
            if (d > r * r) continue;
            const GraphLink *a0 = slot[i].link[0] >= 0 ? &links[slot[i].link[0]] : NULL;
            const GraphLink *a1 = slot[i].link[1] >= 0 ? &links[slot[i].link[1]] : NULL;
            bool same0 = a0 && same_rooms(l, a0), same1 = a1 && same_rooms(l, a1);
            float d0 = slot[i].d[0], d1 = slot[i].d[1], w0 = slot[i].w[0], w1 = slot[i].w[1], w = l->width;
            int k = -1;
            if (!same1 && !a0) k = 0;
            else if (!same0 && !a1) k = 1;
            /* a nearer link (by a tenth: 0x5295a0, 0x5295a4), or a narrower one nearly as near, or the
               way back of the other slot's link, takes a slot */
            else if (!same1 && d * 1.1f < d0) k = 0;
            else if (!same0 && d * 1.1f < d1) k = 1;
            else if (!same1 && d * 0.9f < d0 && w0 > w) k = 0;
            else if (!same0 && d * 0.9f < d1 && w1 > w) k = 1;
            else if (!same1 && d * 0.9f < d0 && w * 0.9f < w0 && a1 && back_rooms(l, a1)) k = 0;
            else if (!same0 && d * 0.9f < d1 && w * 0.9f < w1 && a0 && back_rooms(l, a0)) k = 1;
            if (k < 0) continue;
            /* (a link already taken: "OBJrDoor_MakeConnectionLink: door ... already", and the
               enumeration stops) */
            if (l->door < 0) {
                float box[6];
                graph_room_box(l->from, box);
                /* the from room's middle (its BNV's box, PHrBuildGraph's context) */
                float c[3] = { (box[0] + box[3]) * 0.5f, (box[1] + box[4]) * 0.5f, (box[2] + box[5]) * 0.5f };
                l->door = i;
                l->door_side = (int8_t)door_point_side(i, c);
                slot[i].link[k] = li, slot[i].d[k] = d, slot[i].w[k] = w;
            }
            break;
        }
    }
    free(slot);
}

int graph_room_count(void) { return nrooms; }
int graph_link_count(void) { return nlinks; }
const GraphLink *graph_link(int i) { return i >= 0 && i < nlinks ? &links[i] : NULL; }
int graph_out_count(int room) { return room >= 0 && room < nrooms ? rooms[room].nout : 0; }
int graph_out(int room, int k) { return rooms[room].out[k]; }
int graph_in_count(int room) { return room >= 0 && room < nrooms ? rooms[room].nin : 0; }
int graph_in(int room, int k) { return rooms[room].in[k]; }
int graph_room_bnv(int room) { return room >= 0 && room < nrooms ? rooms[room].bnv : -1; }

uint32_t graph_room_flags(int room) { return room >= 0 && room < nrooms ? rd32(bnv(rooms[room].bnv) + 0x5c) : 0; }

void graph_room_box(int room, float box[6])
{
    const uint8_t *b = room >= 0 && room < nrooms ? bnv(rooms[room].bnv) : NULL;
    for (int k = 0; k < 6; k++) box[k] = b ? rdf(b + 0x30 + 4 * k) : 0.0f;
}

/* ---- the rooms' grids ---- */

typedef GraphGrid Grid;

static bool grid_of(int room, Grid *g)
{
    const uint8_t *b = bnv(rooms[room].bnv) + 0x1c;
    g->w = rd32(b), g->h = rd32(b + 4);
    g->cell = rdf(b + 0x10), g->x0 = rdf(b + 0x14), g->y0 = rdf(b + 0x18), g->z0 = rdf(b + 0x1c);
    g->ox = (int16_t)(rd32(b + 0x2c) & 0xffff), g->oz = (int16_t)(rd32(b + 0x2c) >> 16);
    g->cells = rooms[room].grid;
    return g->cells != NULL;
}

/* PHrGrid_Decompress 0x43c600: runs of a value (the low 4 bits) as long as the high 4, or as the next
   byte when they are 0. */
static uint8_t *decompress(const uint8_t *src, size_t n, uint32_t cells)
{
    uint8_t *out = calloc(1, cells + 16);
    if (!out) return NULL;
    uint32_t at = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t v = src[i];
        uint32_t len = v >> 4;
        if (!len && i + 1 < n) len = src[++i];
        for (uint32_t k = 0; k < len && at < cells; k++) out[at++] = v & 0xf;
    }
    return out;
}

static void load_grids(const void *env)
{
    size_t rs = 0;
    const uint8_t *raw = tm_raw(env, &rs);
    for (int r = 0; r < nrooms; r++) {
        const uint8_t *b = bnv(rooms[r].bnv);
        uint32_t w = rd32(b + 0x1c), h = rd32(b + 0x20), off = rd32(b + 0x24), size = rd32(b + 0x28);
        if (!raw || !off || !size || (uint64_t)off + size > rs || !w || !h) continue;
        rooms[r].grid = decompress(raw + off, size, w * h);
    }
}

bool graph_grid(int room, GraphGrid *g)
{
    if (room < 0 || room >= nrooms) return false;
    grid_of(room, g);
    return true;
}


void graph_cell_middle(const GraphGrid *g, int x, int z, const float *y, float out[3])
{
    out[0] = ((float)(g->ox + (int)(uint16_t)x) + 0.5f) * g->cell + g->x0;
    out[1] = y ? *y : g->y0 + 0.5f;
    out[2] = ((float)(g->oz + (int)(uint16_t)z) + 0.5f) * g->cell + g->z0;
}

void graph_quad_low_pair(uint32_t quad, float lo[3], float lo2[3]) { low_pair(quad, lo, lo2); }

static float d2v(const float a[3], const float b[3]) { return d2(a, b); }

void graph_waypoint(uint32_t quad, const float *near, float out[3])
{
    float a[3], b[3];
    low_pair(quad, a, b);
    if (!near || d2v(b, a) <= 400.0f) {     /* 0x529dec */
        for (int k = 0; k < 3; k++) out[k] = (a[k] + b[k]) * 0.5f;
        return;
    }
    /* 0x432e40: the nearest point of the edge (from b to a) */
    float e[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    float ee = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
    float t = ee > 0.0f ? ((near[0] - b[0]) * e[0] + (near[1] - b[1]) * e[1] + (near[2] - b[2]) * e[2]) / ee : 0.0f;
    if (t < 0.0f) memcpy(out, b, 12);
    else if (1.0f < t) memcpy(out, a, 12);
    else for (int k = 0; k < 3; k++) out[k] = e[k] * t + b[k];
    /* 10 in from an end it is within 10 of (100 squared, 0x529d54) */
    if (d2v(out, b) < 100.0f) {
        float n = 10.0f / sqrtf(ee);
        for (int k = 0; k < 3; k++) out[k] = e[k] * n + b[k];
    } else if (d2v(out, a) < 100.0f) {
        float n = 10.0f / sqrtf(ee);
        for (int k = 0; k < 3; k++) out[k] = -e[k] * n + a[k];
    }
}

/* PHrProjectOntoConnection 0x50e9a0: the line from near to p (in x and z) crossed with the link's edge
   (its corners by the flags' bits 16..19); along the edge, kept 10 from an end the flags 4, 8 mark (0
   from the others), and out of a blocked run (the side with room, or the nearer); a parallel line, or
   an edge too short for its margins, the edge's middle. */
void graph_project_onto_link(int link, const float near[3], float p[3])
{
    if (link < 0 || link >= nlinks) return;
    const GraphLink *l = &links[link];
    float a[3], b[3];
    point_of(quad_point(l->quad, (int)(l->flags >> 16 & 3)), a);
    point_of(quad_point(l->quad, (int)(l->flags >> 18 & 3)), b);
    float nx = a[2] - b[2], nz = b[0] - a[0];
    float sp = (p[0] - a[0]) * nx + (p[2] - a[2]) * nz;
    float sn = (near[0] - a[0]) * nx + (near[2] - a[2]) * nz;
    float d = sp - sn;
    if (fabs((double)d) < 0.001 /* 0x529810 */) {
        for (int k = 0; k < 3; k++) p[k] = (b[k] + a[k]) * 0.5f;
        return;
    }
    float tn = sp / d, tp = -(sn / d), x[3];
    for (int k = 0; k < 3; k++) x[k] = tn * near[k] + tp * p[k];
    float s = ((x[0] - a[0]) * nz + (b[2] - a[2]) * (x[2] - a[2])) / l->width;
    float lo = (l->flags & 4) ? 10.0f : 0.0f;   /* 0x52a190, 0x529514 */
    float hi = l->width - ((l->flags & 8) ? 10.0f : 0.0f);
    if (hi < lo) {
        for (int k = 0; k < 3; k++) p[k] = (b[k] + a[k]) * 0.5f;
        return;
    }
    bool moved = false;
    if (s < lo) s = lo, moved = true;
    else if (hi < s) s = hi, moved = true;
    if (l->blocks > 0) {
        float f = s / l->width, flo = lo / l->width, fhi = hi / l->width;
        for (int i = 0; i < l->blocks; i++) {
            /* the runs as the original keeps them: 16 bit fractions (0x5294f8, 0x5294fc) */
            float b0 = (float)lrintf(l->block[i][0] * 65535.0f) * 1.5259022e-05f;
            float b1 = (float)lrintf(l->block[i][1] * 65535.0f) * 1.5259022e-05f;
            if (f < b0 || b1 < f) continue;
            if (b0 - flo < 0.01f) s = b1 * l->width;                                   /* 0x5295bc */
            else if (fhi - b1 < 0.01f || !((b1 + b0) * 0.5f < f)) s = b0 * l->width;
            else s = b1 * l->width;
            moved = true;
            break;
        }
    }
    if (moved) {
        float t = s / l->width, u = 1.0f - t;
        for (int k = 0; k < 3; k++) x[k] = t * b[k] + u * a[k];
    }
    memcpy(p, x, sizeof x);
}

const uint8_t *graph_room_grid(int room, uint32_t *w, uint32_t *h)
{
    Grid g;
    if (room < 0 || room >= nrooms || !grid_of(room, &g)) return NULL;
    *w = g.w, *h = g.h;
    return g.cells;
}

/* PHrWorldToGridSpace 0x43c0c0 (and its unchecked twin 0x43c290): the cell under p, rounded to nearest. */
static void to_cell(const Grid *g, const float p[3], int16_t *x, int16_t *z)
{
    *x = (int16_t)lrintf(((p[0] - g->cell * 0.5f) - g->x0) / g->cell - (float)g->ox);
    *z = (int16_t)lrintf(((p[2] - g->cell * 0.5f) - g->z0) / g->cell - (float)g->oz);
}

void graph_to_cell(const GraphGrid *g, const float p[3], int16_t *x, int16_t *z) { to_cell(g, p, x, z); }

/* 0x43c210: p's cell inside the grid. */
static bool in_grid(const Grid *g, const float p[3])
{
    int16_t x, z;
    to_cell(g, p, &x, &z);
    return x >= 0 && (uint32_t)x < g->w && z >= 0 && (uint32_t)z < g->h;
}

/* PHiAddBlockage 0x50e7c0: [t - half, t + half] within 0..1 into the sorted runs, joined to one it
   meets (and that one to the next, if it now reaches it). */
static void add_blockage(GraphLink *l, float t, float half)
{
    float lo = t - half, hi = t + half;
    if (lo < 0.0f) lo = 0.0f;
    else if (lo > 1.0f) lo = 1.0f;
    if (hi < 0.0f) hi = 0.0f;
    else if (hi > 1.0f) hi = 1.0f;
    if (!(lo < hi)) return;
    const float e = 0.01f;     /* 0x5295bc */
    int n = l->blocks, k = 0;
    while (k < n && !(lo - e < l->block[k][1])) k++;
    if (k == n || hi + e < l->block[k][0]) {
        if (n >= 16) return;    /* "PHiAddBlockage: overflowed max blockages" */
        memmove(l->block[k + 1], l->block[k], sizeof l->block[0] * (size_t)(n - k));
        l->block[k][0] = lo, l->block[k][1] = hi;
        l->blocks++;
        return;
    }
    if (lo < l->block[k][0]) l->block[k][0] = lo;
    if (l->block[k][1] < hi) {
        l->block[k][1] = hi;
        if (k + 1 < n && l->block[k + 1][0] - e < hi) {
            l->block[k][1] = l->block[k + 1][1];
            memmove(l->block[k + 1], l->block[k + 2], sizeof l->block[0] * (size_t)(n - k - 2));
            l->blocks--;
        }
    }
}

static bool blocking(uint8_t v) { return v == 6 || (v > 7 && v < 10); }

/* PHiCheckForBlockages 0x50df80: the edge from p to q across the room's grid, stretched back and on by
   up to 2 cells (not past the grid's sides), stepped through cell by cell (Bresenham); each obstacle
   cell (6, 8, 9) blocks the edge's fraction it projects to, 4 / the edge's length either way. */
static void check_blockages(int room, const float p[3], const float q[3], GraphLink *l)
{
    Grid g;
    if (!grid_of(room, &g)) return;
    if (!in_grid(&g, p) || !in_grid(&g, q)) return;     /* "PHrBuildGraph: connection ... outside the grid" */
    int16_t px, pz, qx, qz;
    to_cell(&g, p, &px, &pz);
    to_cell(&g, q, &qx, &qz);
    int cdx = (uint16_t)qx - (uint16_t)px, cdz = (uint16_t)qz - (uint16_t)pz;
    int ax = cdx < 0 ? -cdx : cdx, az = cdz < 0 ? -cdz : cdz;
    float dx = q[0] - p[0], dy = q[1] - p[1], dz = q[2] - p[2];
    if (!ax && !az) {
        if ((uint16_t)px < g.w && (uint16_t)pz < g.h && blocking(g.cells[(uint32_t)(uint16_t)pz * g.w + (uint16_t)px]))
            add_blockage(l, 0.0f, 1.0f);
        return;
    }
    /* back from p: two cells' worth, or to the grid's side behind */
    float tx = 0.0f, tz = 0.0f, t;
    if (ax) {
        float edge = cdx > 0 ? (p[0] - g.x0) + (float)-g.ox * g.cell : (float)(g.w - (uint32_t)(int)g.ox) * g.cell - (p[0] - g.x0);
        tx = -2.0f / (float)ax;
        float v = -(edge / dx);
        if (tx <= v) tx = v;
    }
    if (az) {
        float edge = cdz > 0 ? (p[2] - g.z0) + (float)-g.oz * g.cell : (float)(g.h - (uint32_t)(int)g.oz) * g.cell - (p[2] - g.z0);
        tz = -2.0f / (float)az;
        float v = -(edge / dz);
        if (tz <= v) tz = v;
    }
    t = !az ? tx : (!ax ? tz : (tx > tz ? tx : tz));
    float a[3] = { p[0], p[1], p[2] };
    if (t < 0.0f) a[0] += t * dx, a[1] += t * dy, a[2] += t * dz;
    int16_t sx, sz;
    to_cell(&g, a, &sx, &sz);
    /* on from q: likewise */
    float b[3] = { q[0], q[1], q[2] };
    if (ax) {
        float edge = cdx < 0 ? (q[0] - g.x0) + (float)-g.ox * g.cell : (float)(g.w - (uint32_t)(int)g.ox) * g.cell - (q[0] - g.x0);
        tx = 2.0f / (float)ax;
        float v = edge / (q[0] - p[0]);
        if (v < tx) tx = v;
    }
    if (az) {
        float edge = cdz < 0 ? (q[2] - g.z0) + (float)-g.oz * g.cell : (float)(g.h - (uint32_t)(int)g.oz) * g.cell - (q[2] - g.z0);
        tz = 2.0f / (float)az;
        float v = edge / (q[2] - p[2]);
        if (v < tz) tz = v;
    }
    t = !az ? tx : (!ax ? tz : (tx < tz ? tx : tz));
    if (0.0f < t) b[0] += t * dx, b[1] += t * dy, b[2] += t * dz;
    int16_t ex, ez;
    to_cell(&g, b, &ex, &ez);
    /* into the grid */
    int16_t wmax = (int16_t)(g.w - 1), hmax = (int16_t)(g.h - 1);
    sx = sx < 0 ? 0 : (sx > wmax ? wmax : sx);
    sz = sz < 0 ? 0 : (sz > hmax ? hmax : sz);
    ex = ex < 0 ? 0 : (ex > wmax ? wmax : ex);
    ez = ez < 0 ? 0 : (ez > hmax ? hmax : ez);
    /* the start cell's middle projected on the edge, and the steps' fractions */
    float c[3] = { ((float)(g.ox + sx) + 0.5f) * g.cell + g.x0, 0.0f, ((float)(g.oz + sz) + 0.5f) * g.cell + g.z0 };
    float l2 = dz * dz + dx * dx;
    float tt = ((c[2] - p[2]) * dz + (c[0] - p[0]) * dx) / l2;
    float step_x = dx * 4.0f / l2, step_z = dz * 4.0f / l2, half = 4.0f / sqrtf(l2);   /* 0x5296c8 */
    int ddx = ex - sx, ddz = ez - sz;
    int nx = ddx < 0 ? -ddx : ddx, nz = ddz < 0 ? -ddz : ddz;
    int ix = ddx < 0 ? -1 : (ddx != 0), iz = ddz < 0 ? -1 : (ddz != 0);
    int x = sx, z = sz;
    if (nx > nz) {
        int err = (nz - nx) / 2;
        for (;;) {
            if (blocking(g.cells[(uint32_t)z * g.w + (uint32_t)x])) add_blockage(l, tt, half);
            if (x == ex && z == ez) break;
            if (err >= 0) z += iz, err -= nx, tt += (float)iz * step_z;
            else x += ix, err += nz, tt += (float)ix * step_x;
        }
    } else {
        int err = (nx - nz) / 2;
        for (;;) {
            if (blocking(g.cells[(uint32_t)z * g.w + (uint32_t)x])) add_blockage(l, tt, half);
            if (x == ex && z == ez) break;
            if (err >= 0) x += ix, err -= nz, tt += (float)ix * step_x;
            else z += iz, err += nx, tt += (float)iz * step_z;
        }
    }
}

/* ---- the room holding a point ---- */

/* 0x4411a0: the octree's leaf holding p. */
static uint32_t find_leaf(const float p[3])
{
    uint32_t node = 0;
    float half = 4096.0f, cx = 0.0f, cy = 0.0f, cz = 0.0f;
    for (int guard = 0; guard < 64 && E.nodes; guard++) {
        uint32_t k = (cx < p[0]) | (cy < p[1]) << 1 | (cz < p[2]) << 2;
        if (node >= E.nnodes) return 0xffffffffu;
        node = rd32(E.nodes + 0x20u * node + 4 * k);
        if (node & 0x80000000u) return node;
        half *= 0.5f;
        cx += (k & 1) ? half : -half;
        cy += (k & 2) ? half : -half;
        cz += (k & 4) ? half : -half;
    }
    return 0xffffffffu;
}

static float volume(const uint8_t *b)
{
    return fabsf(rdf(b + 0x34) - rdf(b + 0x40)) * fabsf(rdf(b + 0x30) - rdf(b + 0x3c)) * fabsf(rdf(b + 0x38) - rdf(b + 0x44));
}

int graph_room_at(const float p[3])
{
    uint32_t leaf = find_leaf(p);
    if (leaf == 0xffffffffu || (leaf & 0x7fffffffu) >= E.nleaves) return -1;
    uint32_t packed = rd32(E.leaves + 0x24u * (leaf & 0x7fffffffu) + 0x20);
    uint32_t first = packed >> 8 & 0xffff, n = packed & 0xff;
    int best = -1;
    bool none = true, best_stairs = false;
    float best_d = 1e9f;
    for (uint32_t j = first; j < first + n && j < E.nbnv_index; j++) {
        int bi = (int)rd32(E.bnv_index + 4 * j);
        if ((uint32_t)bi >= E.nbnv) continue;
        const uint8_t *b = bnv(bi);
        /* down the BNV's BSP: in front (by 1/1024) to +8, else to +4; -1 behind is inside */
        uint32_t node = rd32(b);
        bool inside = false;
        float d = 0.0f, ny = 0.0f;
        for (int guard = 0; guard < 4096 && node < E.nbsp; guard++) {
            const uint8_t *bn = E.bsp + BSP_SIZE * node;
            uint32_t pi = rd32(bn);
            const uint8_t *pp = E.planes + 16u * (pi & 0x0fffffffu);
            float pl[4];
            for (int k = 0; k < 4; k++) pl[k] = (pi & 0x0fffffffu) < E.nplanes ? rdf(pp + 4 * k) : 0.0f;
            if (pi & 0x80000000u) for (int k = 0; k < 4; k++) pl[k] = -pl[k];
            d = pl[2] * p[2] + pl[0] * p[0] + pl[1] * p[1] + pl[3];
            ny = pl[1];
            if (EPS <= d) {
                node = rd32(bn + 8);
                if (node == 0xffffffffu) break;
            } else {
                node = rd32(bn + 4);
                if (node == 0xffffffffu) { inside = true, d = 0.0f; break; }
            }
        }
        /* outside: only near (within 5) a wall-like plane (normal y within 0.5) */
        if (!inside && !(d < 5.0f && fabsf(ny) <= 0.5f)) continue;
        float dy = p[1] - rdf(b + 0x34);
        if (dy < -5.0f) continue;
        bool stairs = rd32(b + 0x5c) & 1;
        float f = fabsf(dy) + d;
        if (stairs) {
            /* a stair room's: between its slab's planes */
            float s = rdf(b + 0x64) * p[1] + rdf(b + 0x60) * p[0] + rdf(b + 0x68) * p[2] + rdf(b + 0x6c);
            if (!(-EPS <= s && -EPS <= rdf(b + 0x70) - s)) continue;
        }
        if (inside && none) {
            none = false;
            best = bi, best_stairs = stairs, best_d = f;
            continue;
        }
        if (!inside && !none) continue;
        /* the stairs are taken over the rest; else the nearer, or the smaller when as near */
        if (!stairs && best_stairs) continue;
        if (stairs && !best_stairs) {
            none = false;
            best = bi, best_stairs = stairs, best_d = f;
            continue;
        }
        if (EPS <= fabsf(f - best_d)) {
            if (f < best_d) best = bi, best_d = f;
        } else if (best >= 0 && volume(b) < volume(bnv(best))) {
            best = bi;
        }
    }
    return best;
}

/* ---- the search ---- */

static int open_head, closed_head;

static void list_append(int *head, int r)
{
    if (*head < 0) *head = r;
    else {
        int i = *head;
        while (rooms[i].next >= 0) i = rooms[i].next;
        rooms[i].next = r;
    }
    rooms[r].next = -1;
}

static void list_remove(int *head, int r)
{
    if (*head == r) { *head = rooms[r].next; return; }
    for (int i = *head; i >= 0; i = rooms[i].next)
        if (rooms[i].next == r) { rooms[i].next = rooms[r].next; return; }
}

/* 0x50c390: a cheaper way to a closed room passed on to the rooms reached through it. */
static void propagate(int r, int depth, float delta)
{
    for (int k = 0; k < rooms[r].nout; k++) {
        const GraphLink *l = &links[rooms[r].out[k]];
        Room *t = &rooms[l->to];
        if (t->state && t->via == rooms[r].out[k]) {
            t->depth = (uint16_t)(t->depth + depth);
            t->f += delta;
            t->g += delta;
            propagate(l->to, depth, delta);
        }
    }
}

int graph_find_path(int a, const float pa[3], int b, const float pb[3], bool by_character, int *path)
{
    return graph_find_path_within(a, pa, b, pb, by_character, -1, path, NULL);
}

int graph_find_path_within(int a, const float pa[3], int b, const float pb[3], bool by_character, int mode, int *path,
                           float *distance)
{
    if (a < 0 || a >= nrooms || b < 0 || b >= nrooms) return 0;
    bool bounded = distance && *distance > 0.0f;
    for (int i = 0; i < nrooms; i++) {
        Room *r = &rooms[i];
        r->g = r->h = r->f = 0.0f, r->cost = 1.0f;
        r->next = -1, r->parent = r->via = -1;
        r->state = 0, r->depth = 0xffff;
    }
    open_head = closed_head = -1;
    Room *s = &rooms[a];
    s->depth = 0, s->g = 0.0f, s->cost = 1.0f;
    s->h = s->f = dist(pa, pb);
    if (bounded && s->f > *distance) return 0;
    list_append(&open_head, a);
    s->state = 1;
    while (open_head >= 0) {
        /* the open room with the least estimate (the first of equals) */
        int cur = -1;
        float best = FLT_MAX;   /* 0x52b820 */
        for (int i = open_head; i >= 0; i = rooms[i].next)
            if (rooms[i].f < best) best = rooms[i].f, cur = i;
        if (cur < 0) return 0;
        list_remove(&open_head, cur);
        Room *c = &rooms[cur];
        c->state = 0;
        if (cur == b) {
            int n = c->depth;
            if (path) {
                if (n > GRAPH_PATH_MAX) return 0;
                int r = cur;
                for (int k = n - 1; k >= 0; k--) {
                    path[k] = rooms[r].via;
                    r = rooms[r].parent;
                }
            }
            /* the way's length: to the last link's middle, then on to the point */
            if (distance) *distance = c->via < 0 ? dist(pa, pb) * c->cost : dist(links[c->via].mid, pb) * c->cost + c->g;
            return n + 1;
        }
        for (int k = 0; k < c->nout; k++) {
            int li = c->out[k];
            const GraphLink *l = &links[li];
            if (l->to == c->parent || (by_character && (l->flags & 1))) continue;
            float step = c->via < 0 ? dist(pa, l->mid)
                                    : matrix[c->matrix + (uint32_t)(links[c->via].in_index * c->nout + l->out_index)];
            /* a character's search passes a link's door by its lock (OBJrDoor_CharacterCanOpen); the
               others a shut door by its class's danger: type 0 any search, 1 the modes 2 to 4, 2 mode 4, 3
               none; a danger above 0.99 (0x52a03c) none, above 0 the rooms beyond cost 1 / (1 - danger) */
            float factor = 1.0f;
            if (l->door >= 0) {
                if (by_character) {
                    if (!door_side_passes(l->door, l->door_side)) continue;
                } else if (door_state(l->door) != 1) {
                    uint32_t type;
                    float danger;
                    if (door_danger(l->door, &type, &danger)) {
                        bool passes = type == 0 || (type == 1 && mode >= 2 && mode <= 4) || (type == 2 && mode == 4);
                        if (!passes || danger > 0.99f) continue;
                        if (danger > 0.0f) factor = 1.0f / (1.0f - danger);
                    }
                }
            }
            float g = step * c->cost + c->g;
            float h = dist(l->mid, pb) * c->cost;
            float f = h + g;
            Room *t = &rooms[l->to];
            if (bounded && f > *distance) continue;
            if (t->state != 0 && !(f < t->f)) continue;
            int depth0 = c->depth, depth1 = t->depth;
            float delta = f - t->f;
            t->parent = cur;
            t->via = li;
            t->depth = (uint16_t)(depth0 + 1);
            t->f = f, t->h = h, t->g = g;
            t->cost = factor * c->cost;
            if (t->state == 2) {
                propagate(l->to, depth0 - depth1 + 1, delta);
                list_remove(&closed_head, l->to);
                t->state = 0;
            }
            if (t->state != 1) {
                list_append(&open_head, l->to);
                t->state = 1;
            }
        }
        list_append(&closed_head, cur);
        c->state = 2;
    }
    return 0;
}
