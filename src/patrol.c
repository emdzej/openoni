/* Patrol paths and the patrol: see patrol.h. */
#include "patrol.h"

#include "chars.h"
#include "chr_hit.h"
#include "combat.h"
#include "weapon.h"
#include "graph.h"
#include "movement.h"
#include "objc.h"
#include "pursuit.h"
#include "sl.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum { PATHS_MAX = 256 };

typedef struct {
    char name[32];
    uint16_t id;
    uint16_t flags;     /* the u16 at 0x26 (+0x22): 1 the patrol starts at the nearest waypoint */
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
    pa->flags = rd16(o->data + 0x26);
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
    a->script = 0;
    a->glance_wait = false;
    a->scanning = a->guarding = a->guard_begun = a->point_scan_set = false;
    a->scan_ticks = 0;
    /* (AI2rPatrol_Enter: the shooting forgotten, its targeting not stopped) */
    a->shoot_init = a->shooting = false;
    a->shoot_ticks = 0;
    patrol_nearest(a);
}

/* 0x4b05e0: a waypoint's flag (+4 in the original's waypoint) for the types 0, 3, 5, 10, 15 and 19 */
static bool point_flag(const PatrolPoint *w, uint16_t *id)
{
    switch (w->type) {
    case 0: case 3: case 5: case 10: case 0xf: case 0x13: *id = w->a; return true;
    default: return false;
    }
}

void patrol_nearest(AiState *a)
{
    if (a->patrol < 0 || a->patrol >= npaths || !(paths[a->patrol].flags & 1)) return;
    /* AI2rPatrol_Enter 0x4afa2e: the waypoint whose flag is the shortest way (PHrPathDistance, as the
       character) from the character, if any (from 1e9, 0x4e6e6b28) */
    const Character *c = &((const Chr *)((const char *)a - offsetof(Chr, ai)))->c;
    const Path *pa = &paths[a->patrol];
    float best = 1e9f;
    int at = -1;
    for (int i = 0; i < pa->n; i++) {
        uint16_t id;
        ObjcObject f;
        if (!point_flag(&pa->p[i], &id) || !objc_flag_find(id, &f)) continue;     /* ONrLevel_Flag_ID_To_Flag */
        float loc[3];
        int from = chr_room(c, loc), to = graph_room_at(f.pos);
        float d = 0.0f;
        if (from < 0 || to < 0 || !graph_find_path_within(from, loc, to, f.pos, true, -1, NULL, &d)) continue;
        if (d < best) best = d, at = i;
    }
    if (at >= 0) a->point = at;
}

static bool flag_at(uint16_t id, float pos[3], float *facing)
{
    ObjcObject f;
    if (!objc_flag_find(id, &f)) return false;     /* "flag %d does not exist" */
    memcpy(pos, f.pos, sizeof f.pos);
    *facing = f.rot[1] * 0.017453293f;
    return true;
}

/* AI2iPatrol_Trigger 0x4b0520 (the patrol's targeting callback +0, 0x4b05c0): the trigger held while
   the character may fire (combat_may_fire), else let go (A+0x21e3, the release, follows from the key
   let go). */
static void shoot_trigger(void *o, bool on)
{
    Chr *ch = o;
    AiState *a = &ch->ai;
    if (on && combat_may_fire(&ch->c)) {
        a->shoot_held = true;
        a->aim.fire = true;
        return;
    }
    a->shoot_held = false;
    a->aim.fire = false;
}

static const TargetingOps patrol_ops = { shoot_trigger, combat_op_speed, NULL, combat_op_place, combat_op_vector,
                                         combat_op_point, combat_op_char };

/* The shooting's end (its ticks run out, AI2rPatrol_Update 0x4afd20; AI2rPatrol_Exit 0x4afba0): the
   trigger let go, the targeting stopped (0x4bab70). */
static void shoot_stop(AiState *a)
{
    shoot_trigger((char *)a - offsetof(Chr, ai), false);
    a->shooting = false;
    if (a->shoot_init) {
        targeting_stop(&a->shoot);
        a->shoot_init = false;
    }
}

void patrol_exit(AiState *a)
{
    if (a->shooting) shoot_stop(a);
}

/* The shooting's tick (AI2rPatrol_Update 0x4b02b6, while a point waits): the weapon changed, the
   targeting's set for it (its firing mode, ONWC+0xd0; the matrix the hand's, node 19, 0x4f13e0); then
   AI2rTargeting_Update (target, aim, fire) and the fire decision (aim, want, line of fire). */
static void shoot_tick(AiState *a, const Character *c)
{
    if (!a->shooting) return;
    const Weapon *w = c->weapon;
    if (a->shoot_weapon != (const void *)w) {
        targeting_set_weapon(&a->shoot, w ? w->cls + 0xd0 : NULL);
        a->shoot_weapon = w;
    }
    if (w && !chr_weapon_matrix(c, a->shoot.matrix)) memcpy(a->shoot.matrix, w->m, sizeof a->shoot.matrix);
    targeting_update(&a->shoot, true, true, true, NULL);
    targeting_fire_decision(&a->shoot, true, true, true);
}

/* AI2iPatrol_DoWaypoint 0x4b0650 starts a waypoint; AI2rPatrol_Update 0x4afc90 sees it done. Returns
   whether the waypoint is done (the next one starts at once). */
static bool do_point(AiState *a, const Character *c, const PatrolPoint *w, const char *who)
{
    float p[3], face;
    const float *pos = c->pos;
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
    case 2:     /* pause: the countdown (+0x538) set as it starts, then down by one at each later tick's
                   start (AI2rPatrol_Update 0x4afc90): on n ticks later, at once for 0 */
        if (start) {
            a->wait = w->n;
            return a->wait == 0;
        }
        if (a->wait) a->wait--;
        return a->wait == 0;
    case 6:     /* loop */
        a->point = -1;
        return true;
    case 3:     /* look at the flag (AI2rMovement_LookAtPoint) */
        if (!flag_at(w->a, p, &face)) return true;
        a->aim.look = 1;
        memcpy(a->aim.look_at, p, sizeof a->aim.look_at);
        return true;
    case 4:     /* look at the point */
        a->aim.look = 1;
        memcpy(a->aim.look_at, w->f, sizeof a->aim.look_at);
        return true;
    case 7:     /* the movement mode (AI2rMovement_ChangeMovementMode) */
        ai_set_movement_mode(a, (int)w->n);
        return true;
    case 9:     /* the facing locked to a kind of move (0x4a81a0) */
        a->facing_mode = 1;
        a->move_kind = (int)w->n;
        return true;
    case 0xc:   /* AI2rMovement_StopAiming */
        a->aim.look = 0;
        return true;
    case 0xd:   /* AI2rMovement_FreeFacing */
        a->facing_mode = 0;
        return true;
    case 8:     /* move to the point */
        if (start) ai_move_to(a, w->f, false, 0.0f);
        return !a->moving;
    case 10:    /* move through the flag: on within the distance, not stopping */
    case 0xf:   /* move near the flag */
        if (start) {
            if (!flag_at(w->a, p, &face)) return true;
            ai_go_to(a, p, w->f[0], w->type == 0xf, false, 0.0f);
        }
        return !a->moving;
    case 0xb:   /* move through the point */
        if (start) ai_go_to(a, w->f, w->f[3], false, false, 0.0f);
        return !a->moving;
    case 0xe:   /* glance at the flag for the ticks (AI2rPatrol_Update 0x4afc90) */
        if (start) {
            /* another glance under way: this one waits to start */
            if (a->glance_ticks) {
                a->point_started = false;
                return false;
            }
            if (!flag_at(w->a, p, &face)) return true;  /* AI2rError (0x4936b0) */
            /* 0x4a8150: at the point, for the ticks, no delay */
            a->glance_kind = 1;
            a->glance_ticks = w->n;
            a->glance_delay = 0;
            memcpy(a->glance_at, p, sizeof a->glance_at);
            /* still on the way, or arrived with no facing to keep (C+0x112c, 0x4aefb0): on at once, the
               glance played as it goes; else the move stopped (0x4ae660) and the patrol waits */
            a->glance_wait = !a->moving && a->goal_facing;
            if (!a->glance_wait) return true;
            a->moving = false;
            return false;
        }
        return !a->glance_wait || !a->glance_ticks;
    case 0x10: {    /* loop to a waypoint (0x4b0650): to itself, the next one (the first after the last) */
        int n = paths[a->patrol].n, to = (int)w->n < n - 1 ? (int)w->n : n - 1;
        if (to == a->point) to = (a->point + 1) % n;
        a->point = to - 1;
        return true;
    }
    case 0x11:  /* scan for the frames: the guard's scan all round, held (10000), swept within the angle every
                   60 to 100 ticks; done when its ticks run out (patrol_update) */
        if (!a->scanning && !a->point_scan_set) {
            if (a->glance_ticks) return false;      /* (C+0x1598: a glance under way first) */
            ai_scan_set(&a->scan, c->facing);
            a->scanning = true;
            a->guarding = false;
            a->point_scan_set = true;
            a->scan_ticks = w->a;
            a->scan.center = 1000.0f;
            a->scan.hold_base = 10000, a->scan.hold_rand = 0;
            a->scan.sweep_base = 60, a->scan.sweep_rand = 40;
            a->scan.sweep_range = w->f[0] * 0.017453292f;
        }
        return !a->scanning;
    case 0x13:  /* guard: to the flag, facing its way; there, the guard's scan within the angle of that
                   facing (a look held 240 to 480 ticks, swept by up to 45 degrees every 90 to 150) for
                   the frames (0: for good; patrol_update ends it) */
        if (start) {
            a->guard_begun = a->guarding = false;
            a->scan_ticks = 0;
            if (!flag_at(w->a, p, &face)) return true;
            ai_move_to(a, p, true, face);
        }
        if (!a->arrived) return false;
        if (!a->guard_begun) {
            ai_scan_set(&a->scan, c->facing);
            a->scanning = false;
            a->guarding = a->guard_begun = true;
            a->scan_ticks = w->b;
            a->scan.on = true;
            a->scan.face = a->facing_mode == 3 ? a->face_at : c->facing;
            float r = w->f[0] * 0.017453292f;
            a->scan.range = r < 0.0f ? 0.0f : r > 3.14159265f ? 3.14159265f : r;
            a->scan.hold_base = 240, a->scan.hold_rand = 240;
            a->scan.sweep_base = 90, a->scan.sweep_rand = 60;
            a->scan.sweep_range = 0.7853982f;
        }
        return !a->guarding;
    case 0x14:  /* run patrolscript%04d */
    case 0x15: {    /* the same, waited for: the thread kept at +0x5ac */
        if (start) {
            char name[32];
            snprintf(name, sizeof name, "patrolscript%04d", (int)(int16_t)w->a);
            const char *args[1] = { who ? who : "" };
            uint32_t id = sl_start(name, who ? 1 : 0, args);
            if (w->type == 0x15) a->script = id;
        }
        return w->type == 0x14 || !sl_running(a->script);
    }
    case 0x16:  /* ignore the player */
        a->ignore_player = w->n != 0;
        return true;
    case 0x17:  /* shoot at the flag for the ticks (AI2iPatrol_Shooting): the character made active
                   (0x4f11b0), the targeting set up once (0x4ba6d0: the class's AI block, ONCC+0x138, and
                   skills, +0x154), for the weapon held, aimed at the flag's place (+0x604, the
                   targeting's point, set) */
        if (start) {
            if (!flag_at(w->a, p, &face)) return true;      /* AI2rError (0x4936b0) */
            Chr *ch = (Chr *)((char *)a - offsetof(Chr, ai));
            if (!a->shoot_init) {
                const uint8_t *cls = (const uint8_t *)c->oncc + 0x138;
                targeting_init(&a->shoot, 0, &patrol_ops, ch, NULL, cls, cls + 0x1c);
                a->shoot_init = true;
            }
            a->shoot_weapon = NULL;
            if (c->weapon) {
                targeting_set_weapon(&a->shoot, c->weapon->cls + 0xd0);
                a->shoot_weapon = c->weapon;
            }
            memcpy(a->shoot.point, p, sizeof a->shoot.point);
            a->shoot.has_point = true;
            a->shoot_held = false;
            a->shooting = true;
            memcpy(&a->shoot_unused, &w->n, sizeof a->shoot_unused);
            a->shoot_ticks = (uint32_t)(int32_t)(int16_t)w->b;
        }
        return a->shoot_ticks == 0;
    default:    /* the scan's turn: not ported */
        (void)pos;
        return true;
    }
}

void patrol_update(AiState *a, const Character *c, const void *env, const char *who)
{
    /* AI2rPatrol_Update 0x4afc90: nothing more while the script of a point of type 0x15 runs */
    if (a->script) {
        if (sl_running(a->script)) return;
        a->script = 0;
    }
    /* the shooting's ticks out (0x4afd0b) */
    if (a->shoot_ticks && --a->shoot_ticks == 0) shoot_stop(a);
    /* a scan's or a guard's ticks out: the look stopped (AI2rMovement_StopAiming), the next point */
    if (a->scan_ticks && --a->scan_ticks == 0 && (a->scanning || a->guarding)) {
        a->scanning = a->guarding = false;
        a->aim.look = 0;
        a->point++;
        a->point_started = a->point_scan_set = false;
    }
    /* the points done at once go on until one takes time, or the way comes back to the point it
       started from: "patrol %s is looping endlessly" (0x4b0331), the point past the end wrapped to 0,
       and the patrol left: the job's patrol for idle (AI2rExitState 0x48ae80, state 1,
       AI2rEnterState 0x48ad50), another back to the job (AI2rReturnToJob 0x48af80). A loop to a later
       point, which the original would go round for good, stops at the path's points in a tick. */
    int first = a->point;
    for (int guard = 0; a->patrol >= 0 && guard <= paths[a->patrol].n; guard++) {
        const Path *pa = &paths[a->patrol];
        if (a->point < 0 || a->point >= pa->n) {
            a->patrol = -1;     /* past the last waypoint: the patrol ends */
            return;
        }
        if (!do_point(a, c, &pa->p[a->point], who)) {
            /* the scan of the point under way: a scan point looks the way the character goes (0x4a83e0), then
               AI2rGuard_UpdateScan */
            if (a->scanning) ai_scan_follow(a, &a->scan, movement_direction((const struct Chr *)c));
            if (a->scanning || a->guarding) ai_scan_update(a, &a->scan, c, env);
            shoot_tick(a, c);
            return;
        }
        a->point++;
        a->point_started = a->point_scan_set = false;
        if (a->point == first) {
            /* "patrol %s is looping endlessly" */
            if (a->point >= pa->n) a->point = 0;
            if (a->state == a->job) {
                ai_exit_state(a);
                a->state = AI_STATE_IDLE;
            } else {
                ai_return_to_job(a);
            }
            return;
        }
    }
}
