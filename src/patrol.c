/* Patrol paths and the patrol: see patrol.h. */
#include "patrol.h"

#include "objc.h"
#include "sl.h"

#include <stdio.h>
#include <string.h>

enum { PATHS_MAX = 256 };

typedef struct {
    char name[32];
    uint16_t id;
    int n;
    PatrolPoint p[PATROL_MAX_POINTS];
} Path;

static Path paths[PATHS_MAX];
static int npaths;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* The read method 0x4c9440: the name (32 bytes), the count (u32 at 0x20), the id (u16 at 0x24) and a
   u16 at 0x26, then each waypoint, its type (u32) and its fields, their size by the type. */
static bool add_path(const ObjcObject *o, void *user)
{
    (void)user;
    if (o->size < 0x28 || npaths == PATHS_MAX) return true;
    Path *pa = &paths[npaths];
    memset(pa, 0, sizeof *pa);
    memcpy(pa->name, o->data, 31);
    uint32_t n = rd32(o->data + 0x20);
    pa->id = rd16(o->data + 0x24);
    const uint8_t *q = o->data + 0x28, *end = o->data + o->size;
    for (uint32_t i = 0; i < n && i < PATROL_MAX_POINTS && q + 4 <= end; i++) {
        PatrolPoint *w = &pa->p[pa->n];
        memset(w, 0, sizeof *w);
        w->type = rd32(q);
        q += 4;
        size_t need;
        switch (w->type) {
        case 0: case 3: case 5: case 0x14: case 0x15: need = 2; break;
        case 2: case 7: case 9: case 0x10: need = 4; break;
        case 4: case 8: need = 12; break;
        case 0xb: need = 16; break;
        case 10: case 0xe: case 0x11: case 0xf: need = 6; break;
        case 0x13: case 0x17: need = 8; break;
        case 0x16: need = 1; break;
        default: need = 0; break;
        }
        if (q + need > end) break;
        switch (w->type) {
        case 0: case 3: case 5: case 0x14: case 0x15: w->a = rd16(q); break;
        case 2: case 7: case 9: case 0x10: w->n = rd32(q); break;
        case 4: case 8: for (int k = 0; k < 3; k++) w->f[k] = rdf(q + 4 * k); break;
        case 0xb: for (int k = 0; k < 4; k++) w->f[k] = rdf(q + 4 * k); break;
        case 10: case 0xe: case 0x11: case 0xf: w->a = rd16(q), w->f[0] = rdf(q + 2), w->n = rd32(q + 2); break;
        case 0x13: w->b = rd16(q), w->a = rd16(q + 2), w->f[0] = rdf(q + 4); break;   /* frames, then the flag */
        case 0x17: w->a = rd16(q), w->b = rd16(q + 2), w->n = rd32(q + 4); break;
        case 0x16: w->n = q[0]; break;
        default: break;
        }
        q += need;
        pa->n++;
    }
    npaths++;
    return true;
}

int patrol_load(void)
{
    npaths = 0;
    objc_each("Patrol Path", add_path, NULL);
    return npaths;
}

int patrol_find(const char *name)
{
    for (int i = 0; i < npaths; i++)
        if (!strcmp(paths[i].name, name)) return i;
    return -1;
}

int patrol_find_id(uint16_t id)
{
    for (int i = 0; i < npaths; i++)
        if (paths[i].id == id) return i;
    return -1;
}

const char *patrol_name(int path) { return path >= 0 && path < npaths ? paths[path].name : ""; }

int patrol_points(int path, const PatrolPoint **out)
{
    if (path < 0 || path >= npaths) return 0;
    *out = paths[path].p;
    return paths[path].n;
}

void patrol_start(AiState *a, int path)
{
    a->patrol = path;
    a->point = 0;
    a->point_started = false;
    a->wait = 0;
}

static bool flag_at(uint16_t id, float pos[3], float *facing)
{
    ObjcObject f;
    if (!objc_flag_find(id, &f)) return false;     /* "flag %d does not exist" */
    memcpy(pos, f.pos, sizeof f.pos);
    *facing = f.rot[1] * 0.017453293f;
    return true;
}

/* AI2iPatrol_DoWaypoint 0x4b0650 starts a waypoint; AI2rPatrol_Update 0x4afc90 sees it done. Returns
   whether the waypoint is done (the next one starts at once). */
static bool do_point(AiState *a, const PatrolPoint *w, const float pos[3], const char *who)
{
    float p[3], face;
    bool start = !a->point_started;
    a->point_started = true;
    switch (w->type) {
    case 0:     /* move to the flag */
    case 5:     /* move to it and face its way */
        if (start) {
            if (!flag_at(w->a, p, &face)) return true;
            ai_move_to(a, p, w->type == 5, face);
        }
        return !a->moving;
    case 1:     /* stop: the patrol is over, idle (state 1) */
        a->patrol = -1;
        return false;
    case 2:     /* pause */
        if (start) a->wait = w->n;
        if (a->wait) a->wait--;
        return a->wait == 0;
    case 6:     /* loop */
        a->point = -1;
        return true;
    case 7:     /* the movement mode */
        a->mode = (int)w->n;
        return true;
    case 8:     /* move to the point */
        if (start) ai_move_to(a, w->f, false, 0.0f);
        return !a->moving;
    case 10:    /* move through the flag: on within the distance */
    case 0xf:   /* move near the flag */
        if (start) {
            if (!flag_at(w->a, p, &face)) return true;
            ai_move_near(a, p, w->f[0] > 0.0f ? w->f[0] : 4.0f, false, 0.0f);
        }
        return !a->moving;
    case 0xb:   /* move through the point */
        if (start) ai_move_near(a, w->f, w->f[3] > 0.0f ? w->f[3] : 4.0f, false, 0.0f);
        return !a->moving;
    case 0x10: {    /* loop to a waypoint */
        int n = paths[a->patrol].n, to = (int)w->n < n - 1 ? (int)w->n : n - 1;
        a->point = to - 1;
        return true;
    }
    case 0x11:  /* scan for frames: the scan's turn isn't ported */
        if (start) a->wait = w->a;
        if (a->wait) a->wait--;
        return a->wait == 0;
    case 0x13:  /* move to the flag, face its way and wait (0 frames: for good) */
        if (start) {
            if (!flag_at(w->a, p, &face)) return true;
            ai_move_to(a, p, true, face);
            a->wait = w->b;
        }
        if (a->moving) return false;
        if (!w->b) return false;
        if (a->wait) a->wait--;
        return a->wait == 0;
    case 0x14:  /* run patrolscript%04d */
    case 0x15: {
        char name[32];
        snprintf(name, sizeof name, "patrolscript%04d", (int)(int16_t)w->a);
        const char *args[1] = { who ? who : "" };
        sl_call(name, who ? 1 : 0, args);
        return true;
    }
    case 0x16:  /* ignore the player */
        a->ignore_player = w->n != 0;
        return true;
    default:    /* looking, glancing, facing locks: not ported */
        (void)pos;
        return true;
    }
}

void patrol_update(AiState *a, const float pos[3], float facing, const char *who)
{
    (void)facing;
    for (int guard = 0; guard < PATROL_MAX_POINTS && a->patrol >= 0; guard++) {
        const Path *pa = &paths[a->patrol];
        if (a->point < 0 || a->point >= pa->n) {
            a->patrol = -1;     /* past the last waypoint: the patrol ends */
            return;
        }
        if (!do_point(a, &pa->p[a->point], pos, who)) return;
        a->point++;
        a->point_started = false;
    }
}
