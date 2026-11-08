/* The AI's movement layer: see movement.h. */
#include "movement.h"
#include "ai.h"
#include "akev.h"
#include "akev_collide.h"
#include "chars.h"
#include "chr_anim.h"
#include "door.h"
#include "exe.h"
#include "graph.h"
#include "localpath.h"
#include "obstacle.h"
#include "particle.h"
#include "physics.h"
#include "trac.h"
#include "weapon.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f;     /* 0x529548, 0x529544 */
static const float HALF_PI = 1.5707963705062866f, SIXTH_PI = 0.5235987901687622f;  /* 0x529cd0, 0x52a750 */
static const float NONE = 1000.0f;                                                 /* 0x52a87c */

/* the kinds' turns of the body from the way it goes (0x52aec8) */
static const float kind_offset[4] = { 0.0f, 3.1415927410125732f, 1.5707963705062866f, 4.71238899230957f };

static float wrap_2pi(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }
static float wrap_pi(float a) { return a < -PI ? a + TWO_PI : a > PI ? a - TWO_PI : a; }
static float angle_of(float x, float z) { return wrap_2pi(atan2f(x, z)); }

static AiState *ai_of(Chr *ch) { return &ch->ai; }
/* the character whose AI it is */
static const Character *chr_of(const AiState *a) { return &((const Chr *)((const char *)a - offsetof(Chr, ai)))->c; }

static float oncc_float(const Character *c, unsigned off)
{
    const uint8_t *p = (const uint8_t *)c->oncc + off;
    uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* ---- the class's motion ---- */

/* ONrCharacterClass_MovementSpeed 0x4ee020: the distance a tick of the kind of move in the mode
   (aiming, with a rifle, running), 0 standing */
static float movement_speed(int kind, int mode, bool aim, bool rifle)
{
    if (kind == AI_MOVE_NONE) return 0.0f;
    switch (mode) {
    case 3:
        return kind == AI_MOVE_BACK ? 0.3499999940395355f : 0.3700000047683716f;
    case 4: case 5:
        return kind <= AI_MOVE_BACK ? 0.2199999988079071f : 0.20000000298023224f;
    case 6:
        aim = false;
        /* fall through */
    case 7:
        switch (kind) {
        case AI_MOVE_FORWARD:
            if (!aim || !rifle) return 0.8799999952316284f;
            break;
        case AI_MOVE_BACK:
            if (aim) return rifle ? 0.5899999737739563f : 0.699999988079071f;
            break;
        default:
            return aim && rifle ? 0.75f : 0.8399999737739563f;
        }
        return 0.800000011920929f;
    default:
        return 0.0f;
    }
}

/* 0x498520: the class's turn a tick (ONCC+0x124, a hundredth of a turn, 0x52a878), half of it walking
   or creeping unless the turn is full */
static float movement_turn_rate(const Character *c, int mode, bool full)
{
    float r = oncc_float(c, 0x124) * 0.06283185631036758f;
    if (!full && (mode == 3 || mode == 4 || mode == 5)) r *= 0.5f;
    return r;
}

/* 0x4ee140: the distance a move takes to stop: 6 walking or creeping (0x5297f8), running 15 forward or
   back (0x529d84) and 8 sideways (0x52a740) */
static float movement_stop_distance(int kind, int mode)
{
    if (kind == AI_MOVE_NONE) return 0.0f;
    if (mode >= 3 && mode <= 5) return 6.0f;
    if (mode == 6 || mode == 7) return kind <= AI_MOVE_BACK ? 15.0f : 8.0f;
    return 0.0f;
}

/* 0x4e4140: the animation goes to a moving state (TRAM +0x160, the runs, walks and their starts) */
static bool chr_anim_moving(uint16_t to)
{
    switch (to) {
    case 2: case 3: case 5: case 6: case 8: case 9: case 10: case 0xb: case 0xf: case 0x10: case 0x11:
    case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x20: case 0x21: case 0x22:
    case 0x23: case 0x24: case 0x30: case 0x31: case 0x33: case 0x34:
        return true;
    default:
        return false;
    }
}

void movement_init(MoveState *m)
{
    memset(m, 0, sizeof *m);
    m->dir = m->last_dir = m->hold = NONE;
    m->kind = AI_MOVE_NONE;
    m->look_ahead = true;
    m->room = m->prev_room = -1;
}

/* the mode the movement runs in (the movement mode, else the alert level's, 0x4a8d00) */
static int movement_mode(const Chr *ch)
{
    return ch->ai.movement_mode ? ch->ai.movement_mode : ai_default_mode(&ch->ai, ch->player);
}

void movement_new_mode(Chr *ch)
{
    MoveState *m = &ch->ai.move;
    int mode = movement_mode(ch);
    if (m->slowed) m->slow_mode = mode > 5 ? mode - 2 : mode;
    /* creep, walk and run aim; walk_noaim and run_noaim don't (the aiming byte forced, C+0x1580, has
       no user here); the weapon in the hand is hidden while not aiming (0x4ecf80) */
    if (mode == 3 || mode == 5 || mode == 7) m->aiming = true;
    else if (mode == 4 || mode == 6) m->aiming = false;
    chr_hide_weapon(&ch->c, !m->aiming);
}

/* 0x4a93f0: as the alert changes, the mode by the alert's */
void movement_alert_change(Chr *ch)
{
    if (ch->ai.movement_mode || !(ch->c.flags & CHR_FLAG_AI_MOVEMENT)) return;
    MoveState *m = &ch->ai.move;
    int mode = ai_default_mode(&ch->ai, ch->player);
    if (mode == 3 || mode == 5 || mode == 7) m->aiming = true;
    else if (mode == 4 || mode == 6) m->aiming = false;
    chr_hide_weapon(&ch->c, !m->aiming);
}

void movement_no_sidestep(Chr *ch, bool walk)
{
    MoveState *m = &ch->ai.move;
    if (walk) m->no_side_walk = true;
    else m->no_side_run = true;
    if (m->kind == AI_MOVE_LEFT || m->kind == AI_MOVE_RIGHT) m->rekind = true;
}

/* ---- the collisions ---- */

/* AI2iMovementState_MakeCollision 0x4ab6b0: the record of the thing hit, renewed (its age from 0 again
   if it had aged) or made (17th: none, the original's report); then hit this tick, the character's
   location and the point kept, one more hit. */
static MoveCollision *make_collision(MoveState *m, const float loc[3], int kind, uintptr_t id, uint32_t quad,
                                     const float point[3], const float plane[4])
{
    MoveCollision *r = NULL;
    for (unsigned i = 0; i < m->ncoll; i++) {
        MoveCollision *x = &m->coll[i];
        if (x->kind != kind) continue;
        if (kind == MOVE_WALL ? x->quad == quad : x->id == id) {
            r = x;
            break;
        }
    }
    if (r) {
        if (r->flags & MOVE_OLD) {
            r->age = 0;
            r->flags &= ~MOVE_OLD;
        }
    } else {
        if (m->ncoll >= MOVE_COLLISIONS) return NULL;
        r = &m->coll[m->ncoll++];
        memset(r, 0, sizeof *r);
        r->kind = kind;
        r->id = id;
        r->quad = quad;
        memcpy(r->plane, plane, sizeof r->plane);
        r->angle = angle_of(-plane[0], -plane[2]);
    }
    r->flags |= MOVE_HIT;
    memcpy(r->made, loc, sizeof r->made);
    memcpy(r->point, point, sizeof r->point);
    r->hits++;
    return r;
}

/* the tests the three notices share: a move under way, a wall (|ny| <= 0.9, 0x5295a4), the character's
   location in front of it */
static bool notice(Chr *ch, const float plane[4], float loc[3])
{
    const MoveState *m = &ch->ai.move;
    if (!(ch->c.flags & CHR_FLAG_AI_MOVEMENT) || m->kind == AI_MOVE_NONE || m->dir == NONE) return false;
    if (fabsf(plane[1]) > 0.9f) return false;
    chr_location(&ch->c, loc);
    return loc[0] * plane[0] + loc[1] * plane[1] + loc[2] * plane[2] + plane[3] >= 0.0f;
}

void movement_hit_wall(Chr *ch, uint32_t quad, const float point[3], const float plane[4])
{
    float loc[3];
    if (notice(ch, plane, loc)) make_collision(&ch->ai.move, loc, MOVE_WALL, 0, quad, point, plane);
}

void movement_hit_object(Chr *ch, uintptr_t id, const float point[3], const float plane[4])
{
    float loc[3];
    if (notice(ch, plane, loc)) make_collision(&ch->ai.move, loc, MOVE_OBJECT, id, 0, point, plane);
}

void movement_hit_character(Chr *ch, const Chr *other, const float point[3], const float plane[4])
{
    float loc[3];
    /* (AI2rTryToPathfindThrough 0x48b670, tested by the caller) */
    if (notice(ch, plane, loc)) make_collision(&ch->ai.move, loc, MOVE_OBJECT, (uintptr_t)other, 0, point, plane);
}

/* ---- the path ---- */

/* 0x4aefb0: whether the path's end is to be stopped at, and its radius (out). */
static bool path_end(const AiState *a, float *radius)
{
    *radius = 0.0f;
    if (a->path_type == 0 && a->nmod) return false;
    if (a->arrived && a->move.waiting) return false;
    if (a->path_type == 1) {
        *radius = a->goal_radius;
        return a->path_stop;
    }
    if (a->path_type == 2) *radius = 12.0f;
    return true;
}

/* 0x4a9580 (0x4a84f0): the local path and the state's stops cleared. */
static void clear_local(AiState *a)
{
    if (chr_of(a)->inactive) {
        a->stub.on = false;     /* 0x4ac1f0 */
        return;
    }
    MoveState *m = &a->move;
    m->room = m->prev_room = -1;
    a->local_n = a->local_k = 0;
    m->near_all = false;
    m->has_point = false;
    m->stuck = false;
    m->slowed = m->stop_arrive = m->stop_turn = false;
    m->arrive_ticks = m->kind_ticks = 0;
}

/* 0x4a9560 (0x4a84c0): the collisions forgotten (a new destination). */
static void clear_collisions(MoveState *m)
{
    m->ncoll = m->nconfirmed = 0;
    m->narc = 0;
    m->near_all = false;
}

/* 0x4a9b10 (0x4a85e0): arrived; the first time, the facing asked for is turned to (facing mode 3);
   then done once within 5 degrees of it (0x52af50). */
static bool arrived_turned(AiState *a, const Character *c)
{
    if (c->inactive) {
        /* 0x4ac490: the facing asked for is taken (facing mode 3), and it counts as turned */
        if (a->path_type == 1 && a->goal_facing) {
            a->face_at = a->goal_angle;
            a->facing_mode = 3;
        }
        return true;
    }
    MoveState *m = &a->move;
    if (!m->arrived_seen) {
        m->arrived_seen = true;
        if (a->path_type == 1 && a->goal_facing) {
            a->face_at = a->goal_angle;
            a->facing_mode = 3;
        }
        return false;
    }
    if (a->facing_mode == 3) return fabsf(wrap_pi(c->facing - a->face_at)) < 0.0872664600610733f;
    return true;
}

/* 0x4af030: arrived within the radius (at least 3, 0x529808; heights within 22.5 of it count as level,
   0x52afac) of the point, measured from the location; no path is arrived. */
static void test_arrival(AiState *a, const Character *c)
{
    if (a->path_type == 0) {
        a->arrived = a->turned = true;
        return;
    }
    float loc[3];
    chr_location(c, loc);
    float dx = a->goal[0] - loc[0], dy = a->goal[1] - loc[1], dz = a->goal[2] - loc[2];
    if (fabsf(dy) < 22.5f) dy = 0.0f;
    float d2 = dx * dx + dy * dy + dz * dz, r = 3.0f;
    a->start_d2 = a->path_pending ? d2 : a->start_d2;
    if (a->path_type == 1 && a->goal_radius >= 3.0f) r = a->goal_radius;
    if (d2 < r * r) a->arrived = true;
    a->turned = a->arrived ? arrived_turned(a, c) : false;
}

/* the rooms' path's end: arrived (0x112d), the rooms and the local path cleared (0x4a84f0) */
static void path_arrived(AiState *a)
{
    a->path_n = a->path_k = 0;
    a->path_made = false;
    a->local_seg = -1;
    a->arrived = true;
    clear_local(a);
}

/* AI2rPath_FindGhostWaypoint 0x4aea60: the crossing of the link out of room `seg`, looked for from 4
   links ahead: that link's crossing near the character (or, the path ending sooner, the destination)
   brought back link by link, each the point where the line from the character to it crosses the link
   before (PHrProjectOntoConnection 0x50e9a0). So the way through a run of rooms keeps straight. */
static void ghost_waypoint(const AiState *a, const float near[3], int seg, float out[3])
{
    int i = seg + 4;
    if (i > a->path_n - 1) {    /* the count (C+0x114c) less 2 */
        i = a->path_n;
        memcpy(out, a->goal, 12);
    } else {
        graph_waypoint(graph_link(a->path[i])->quad, near, out);
    }
    for (int j = i - 1; j >= seg; j--) graph_project_onto_link(a->path[j], near, out);
}

/* AI2rMovementState_MakePath 0x4a9f20 (AI2iMovementState_SetupPath 0x4a9d70) for the global path's room
   `seg` (path_n: the destination's): from the character (in the first room) or the crossing of the
   link before (PHrWaypointFromGunk, near the character), to the crossing of the link out
   (ghost_waypoint) or the destination, through the room's grid. The next waypoint is the second, the
   first being the start's cell. False with no path (its stops cleared). */
static void stub_setup(AiState *a);
static bool make_local(AiState *a, const Character *c, const void *env, int seg)
{
    if (c->inactive) {
        /* 0x4ac590: the stub's line, from the location (its height the link's, past the first room,
           0x50ed50) to the link out's crossing (ghost_waypoint) or, in the last room, the goal */
        a->local_seg = seg;
        if (!a->path_made) {        /* no rooms' path (C+0x114c 0) */
            a->stub.on = false;
            return true;
        }
        chr_location(c, a->stub.from);
        if (seg != 0 && seg <= a->path_n) a->stub.from[1] = graph_link_height(a->path[seg - 1]);
        if (seg >= a->path_n) memcpy(a->stub.to, a->goal, sizeof a->stub.to);
        else {
            float near[3];
            chr_location(c, near);
            ghost_waypoint(a, near, seg, a->stub.to);
        }
        stub_setup(a);
        return true;
    }
    MoveState *m = &a->move;
    if (!a->local) a->local = malloc(sizeof(LocalPoint) * LOCAL_PATH_MAX);
    a->local_seg = seg;
    a->local_n = a->local_k = 0;
    m->has_point = false;
    if (!a->local || !env || !graph_for(env)) return false;
    float start[3], end[3];
    int room;
    if (seg == 0 || seg > a->path_n) chr_location(c, start);
    else graph_waypoint(graph_link(a->path[seg - 1])->quad, c->pos, start);
    if (seg >= a->path_n) {
        memcpy(end, a->goal, sizeof end);
        room = a->path_to;
    } else {
        float near[3];
        chr_location(c, near);  /* C+0x160 */
        ghost_waypoint(a, near, seg, end);
        room = graph_link(a->path[seg])->from;
    }
    memcpy(a->local_end, end, sizeof a->local_end);
    if (room < 0) return false;
    if (room != m->room) m->prev_room = m->room, m->room = room;
    /* +0x1148: the room the link out goes to is a stair one */
    bool stairs = seg < a->path_n && (graph_room_flags(graph_link(a->path[seg])->to) & 1);
    a->local_n = local_path(env, room, start, end, c, stairs, true, a->local);
    if (a->local_n > 0) {
        a->local_k = 1;
        m->has_point = true;
        return true;
    }
    clear_local(a);
    return false;
}

/* AI2iPath_MakePath 0x4aeb10: the path kept while it goes between the same rooms to within 20 of the
   same point (400 squared, 0x529dec), is younger than 90 ticks and isn't arrived; else the rooms' A*
   again, from the character's room (C+0x188, no other) and location (C+0x160). Then the local path of the room
   it is in; none: arrived (and the failure's time kept). */
static bool make_path(AiState *a, const Character *c, const void *env, bool force)
{
    if (a->path_type == 2 && a->follow) chr_location(a->follow, a->goal);
    float g[3] = { a->goal[0], a->goal[1] + 0.5f, a->goal[2] };
    float loc[3];
    chr_location(c, loc);
    int from = c->room, to = graph_room_at(g);
    float dx = a->path_goal[0] - a->goal[0], dy = a->path_goal[1] - a->goal[1], dz = a->path_goal[2] - a->goal[2];
    bool keep = !force && a->path_made && !a->turned && a->ticks < a->path_time + 90 &&
                dz * dz + dy * dy + dx * dx < 400.0f && from == a->path_from && to == a->path_to;
    if (from < 0 || to < 0) {
        a->fail_set = true, a->fail_time = a->ticks;    /* AI2iReport_Pathfinding_* */
        return false;
    }
    if (!keep) {
        path_arrived(a);
        a->path_made = true;
        a->path_time = a->ticks;
        a->path_from = from, a->path_to = to;
        memcpy(a->path_goal, a->goal, sizeof a->path_goal);
        if (from != to) {
            int n = graph_find_path(from, loc, to, a->goal, true, a->path);
            if (n < 2) {
                a->path_made = false;
                a->fail_set = true, a->fail_time = a->ticks;
                return false;
            }
            a->path_n = n - 1;
        }
        a->path_k = 0;
    }
    if (make_local(a, c, env, a->path_k)) {
        a->arrived = false;
        return true;
    }
    a->fail_set = true, a->fail_time = a->ticks;
    a->arrived = true;
    return false;
}

/* the path given (AI2rPath_GoToPoint 0x4ae480, AI2rPath_FollowCharacter 0x4ae580, AI2rPath_Repath
   0x4ae3d0's tail): arrived at once, or the path made unless a failure is younger than 90 ticks */
static void path_given(AiState *a, const Character *c, const void *env)
{
    a->arrived = false;
    test_arrival(a, c);
    a->path_pending = false;
    if (a->arrived) {
        path_arrived(a);
        return;
    }
    if (a->fail_set && a->ticks < a->fail_time + 90) return;
    if (!make_path(a, c, env, false)) a->fail_set = true, a->fail_time = a->ticks;
}

/* 0x4aef00: a repath, at most 4 in a row (then "cannot find path", halted); the next check in 30
   ticks, and the count forgotten one by one every 300. */
static void repath(AiState *a, const Character *c, const void *env)
{
    if (++a->repaths < 5) {
        if (!make_path(a, c, env, true)) a->fail_set = true, a->fail_time = a->ticks;
    } else {
        ai_halt(a);
    }
    a->check_delay = 30;
    a->repath_timer = 300;
}

/* 0x50c410: in the room's volume, its grid's cells across, its height within its box (a stair room
   any height). */
static bool in_room(int room, const float p[3])
{
    GraphGrid g;
    if (room < 0) return false;
    graph_grid(room, &g);
    int16_t x, z;
    graph_to_cell(&g, p, &x, &z);
    if (x < 0 || (uint32_t)x >= g.w || z < 0 || (uint32_t)z >= g.h) return false;
    if (graph_room_flags(room) & 3) return true;
    float box[6];
    graph_room_box(room, box);
    return box[1] <= p[1] && p[1] <= box[4];
}

/* AI2rMovementState_CheckFailure 0x4a9bc0: stuck (reason 7), out of the path's rooms (8), or off the
   way from the last point to the next (along it from -0.5 to 1.5 of it, 14 to the side at most): a
   repath. */
static bool check_failure(AiState *a, const Character *c, const void *env)
{
    if (c->inactive) return false;  /* 0x4bece0 */
    const MoveState *m = &a->move;
    if (m->stuck) {
        repath(a, c, env);
        return true;
    }
    if (!in_room(m->prev_room, c->pos) && !in_room(m->room, c->pos)) {
        repath(a, c, env);
        return true;
    }
    if (a->local_n && a->local_k && m->has_point) {
        const float *p = a->local[a->local_k].p, *q = a->local[a->local_k - 1].p;
        float sx = p[0] - q[0], sz = p[2] - q[2], rx = c->pos[0] - q[0], rz = c->pos[2] - q[2];
        float l2 = sx * sx + sz * sz;
        if (l2 > 0.001f) {
            float t = (rx * sx + rz * sz) / l2;
            if (t >= -0.5f && t <= 1.5f && fabsf((rx * sz - rz * sx) / sqrtf(l2)) <= 14.0f) return false;
            repath(a, c, env);
            return true;
        }
    }
    return false;
}

/* the local path's point passed within 12 (a cell of kind 0), 8 (kind 1) or 3 (every point, +0x18c4);
   the last of the last room's within the path's radius, if less, and at least 3 */
static float point_radius(const AiState *a, int k, bool *last)
{
    const LocalPoint *w = &a->local[k];
    float r = a->move.near_all ? 3.0f : w->kind == 0 ? 12.0f : w->kind == 1 ? 8.0f : 3.0f;
    *last = k >= a->local_n - 1 && a->path_k >= a->path_n;
    if (*last) {
        float gr;
        if (path_end(a, &gr) && gr < r) r = gr;
        if (r <= 3.0f) r = 3.0f;
    }
    return r;
}

/* 0x4a95d0 (0x4a8520): the vector to the local path's point; passed, the next (each one passed
   clears the stops); true once the last is passed. */
static bool next_point(AiState *a, const Character *c)
{
    if (c->inactive) return a->arrived || !a->stub.on;     /* 0x4ac200: its line done */
    MoveState *m = &a->move;
    if (a->arrived || a->local_n == 0) {
        a->local_n = a->local_k = 0;
        m->has_point = false;
        return false;
    }
    for (;;) {
        if (!m->has_point) {
            a->local_k = 1;
            m->has_point = true;
            return false;
        }
        if (a->local_k >= a->local_n) return true;
        const LocalPoint *w = &a->local[a->local_k];
        float loc[3];
        chr_location(c, loc);
        for (int j = 0; j < 3; j++) m->vec[j] = w->p[j] - loc[j];
        m->dist2 = m->vec[2] * m->vec[2] + m->vec[0] * m->vec[0];
        bool last;
        float r = point_radius(a, a->local_k, &last);
        if (r * r < m->dist2) return false;
        if (++a->local_k >= a->local_n) {
            m->near_all = false;
            return true;
        }
        m->slowed = m->stop_arrive = m->stop_turn = false;
        m->arrive_ticks = m->kind_ticks = 0;
    }
}

bool movement_path_now(Chr *ch, const void *env)
{
    AiState *a = ai_of(ch);
    if (!a->path_pending) return false;
    bool had = a->fail_set;
    uint32_t when = a->fail_time;
    path_given(a, &ch->c, env);
    return a->fail_set && (!had || when != a->fail_time) && a->fail_time == a->ticks;
}

bool movement_path_short(const Chr *ch)
{
    const AiState *a = &ch->ai;
    if (a->arrived) return true;
    if (a->path_n >= 1) return false;
    return a->local_n <= a->local_k + 1;
}

void movement_path_update(Chr *ch, const void *env)
{
    AiState *a = ai_of(ch);
    Character *c = &ch->c;
    MoveState *m = &a->move;
    if (a->path_pending) path_given(a, c, env);
    if (a->path_type == 0) {
        /* 0x4a8480, the arrival (0x4af030: none is arrived), the rooms and local path cleared */
        if (a->facing_mode == 3) a->facing_mode = 0;
        m->arrived_seen = m->waiting = false;
        test_arrival(a, c);
        path_arrived(a);
    } else {
        /* a destination moved by more than 15 (225 squared, 0x52af74), or the arrival's change: the
           collisions forgotten, the repaths' counts too */
        float dx = a->goal[0] - a->last_goal[0], dy = a->goal[1] - a->last_goal[1], dz = a->goal[2] - a->last_goal[2];
        if (a->turned != a->was_turned || dx * dx + dy * dy + dz * dz > 225.0f) {
            a->repaths = a->check_delay = a->repath_timer = 0;
            a->fail_set = false;
            clear_collisions(m);
        }
    }
    a->was_turned = a->turned;
    memcpy(a->last_goal, a->goal, sizeof a->last_goal);
    /* the door of the link out of the room (C+0x1144: none in the destination's room), opened as the
       action key opens it (OBJrDoor_CharacterOpen 0x4c2520) once the character is near it */
    if (!a->arrived && a->path_k < a->path_n) {
        int door = graph_link(a->path[a->path_k])->door;
        if (door >= 0 && door_near(door, c->pos)) door_action_open(door, ch);
    }
    test_arrival(a, c);
    a->moving = a->path_type != 0 && !a->turned;
    if (a->arrived) return;
    /* the failures' checks: due every tick, or once their delay runs out; without one a repath is
       forgotten every 300 ticks */
    if (a->path_made) {
        bool due = a->check_delay == 0 || --a->check_delay == 0;
        if (due && !check_failure(a, c, env) && a->repath_timer && --a->repath_timer == 0 && a->repaths &&
            --a->repaths != 0)
            a->repath_timer = 300;
    }
    if (a->arrived || a->path_type == 0) return;
    /* the rooms passed: the local path's last point passed, the next room's (none: arrived) */
    while (next_point(a, c)) {
        if (++a->path_k > a->path_n) {
            path_arrived(a);
            return;
        }
        if (!make_local(a, c, env, a->path_k)) {
            a->fail_set = true, a->fail_time = a->ticks;
            a->arrived = true;
            return;
        }
    }
}

/* ---- the movement state's update ---- */

/* 0x4dc100: the character's radius, 3 for an AI's (0x529808), 4 for the player's (0x5296c8) */
static float char_radius(const Chr *ch) { return ch->player ? 4.0f : 3.0f; }

/* 0x4a97b0's decay: the hit ones renewed, the others aged and weakened by the distance from where they
   were hit (gone past 20), the age (gone at 90 ticks) and the hits (none up to 3, half from 30); a hit
   one with more than 3 is confirmed (stuck from 30); one not hit weakened by its plane (gone more
   than 5 behind it; behind it less; ahead past the character's radius by t tenths of 10: gone from
   1, under it kept only while the move heads into it). The gone ones are replaced by the last. */
static void decay(Chr *ch)
{
    MoveState *m = &ch->ai.move;
    if (m->kind_ticks) m->kind_ticks--;
    if (m->arrive_ticks) m->arrive_ticks--;
    float loc[3];
    chr_location(&ch->c, loc);
    m->nconfirmed = 0;
    unsigned i = 0;
    while (i < m->ncoll) {
        MoveCollision *r = &m->coll[i];
        if (!(r->flags & (MOVE_HIT | MOVE_OLD))) {
            r->age = 0;
            r->flags |= MOVE_OLD;
        }
        uint32_t before = r->flags;
        r->age++;
        float by_dist = 1.0f, by_age = 1.0f;
        bool gone = false;
        if (!(before & MOVE_HIT)) {
            float dx = loc[0] - r->made[0], dy = loc[1] - r->made[1], dz = loc[2] - r->made[2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 400.0f) gone = true;
            else {
                if (d2 > 100.0f) by_dist = (20.0f - sqrtf(d2)) * 0.1f;
                if (r->age >= 90) gone = true;
                else if (r->age > 29) by_age = (90.0f - r->age) * 0.01666666753590107f;
            }
        }
        if (gone) {
            r->flags &= ~(uint32_t)(MOVE_HIT | MOVE_OLD | MOVE_CONFIRMED);
        } else {
            float by_hits = 0.0f;
            if (r->hits > 3) {
                if (r->flags & MOVE_HIT) r->flags |= MOVE_CONFIRMED;
                by_hits = 0.5f;
                if (r->hits < 30) by_hits = (float)(r->hits - 3) * 0.5f * 0.03703703731298447f;
                else if (r->flags & MOVE_HIT) m->stuck = true;
            }
            float s = by_hits * by_age * by_dist;
            r->strength = s;
            if (s > 0.0f && !(r->flags & MOVE_HIT)) {
                float d = loc[2] * r->plane[2] + loc[0] * r->plane[0] + r->plane[1] * r->point[1] + r->plane[3];
                float rad = char_radius(ch);
                if (d < -5.0f) {
                    r->flags &= ~(uint32_t)(MOVE_HIT | MOVE_OLD | MOVE_CONFIRMED);
                } else if (d <= 0.0f) {
                    r->strength = (d * 0.2f + 1.0f) * r->strength;
                } else {
                    float t = (d - rad) * 0.1f;
                    if (t > 1.0f) r->strength = 0.0f;
                    else if (t > 0.0f) {
                        if (m->dir == NONE) r->strength = 0.0f;
                        else if (sinf(m->dir) * r->plane[0] + cosf(m->dir) * r->plane[2] <= 0.2f)
                            r->strength = (1.0f - t) * r->strength;
                        else r->strength = 0.0f;
                    }
                }
            }
        }
        if (!(r->flags & (MOVE_HIT | MOVE_OLD))) {
            if (i < m->ncoll - 1) *r = m->coll[m->ncoll - 1];
            m->ncoll--;
            continue;
        }
        if (r->flags & MOVE_CONFIRMED) m->nconfirmed++;
        if ((r->flags & MOVE_CONFIRMED) != (before & MOVE_CONFIRMED)) m->reweigh = true;
        i++;
    }
}

/* 0x4ab400: the arcs' costs from the breakpoint `from` on to the angle `to`: the ramp's cost added
   along the way from v0 to v1 (linear in the angle), a breakpoint put in where the angle falls
   between two (its cost between theirs). Returns the breakpoint at `to`. */
static int arc_ramp(MoveState *m, int from, float to, float v0, float v1)
{
    MoveArc *A = m->arc;
    float start = A[from].angle, end = to;
    bool wrap = to < start;
    if (wrap) end = to + TWO_PI;
    int cur = from, nxt = from;
    if (fabsf(A[from].angle - to) >= 0.0009765625f) {
        do {
            float at = wrap ? to + TWO_PI : to;
            nxt = A[cur].next;
            A[cur].add = v1 - (at - A[cur].angle) * (v1 - v0) / (end - start);
            if (wrap) {
                if (A[nxt].angle < to) wrap = false;
            } else if (to < A[nxt].angle) {
                if (m->narc > 63) return cur;    /* (the original's report) */
                int k = (int)m->narc++;
                A[k].next = A[cur].next;
                A[cur].next = k;
                A[k].add = 0.0f;
                A[k].angle = to;
                A[k].add = v1;
                A[k].cost = (A[nxt].cost - A[cur].cost) * ((to - A[cur].angle) / (A[nxt].angle - A[cur].angle)) +
                            A[cur].cost;
                return k;
            }
            if (nxt == from) return nxt;
            cur = nxt;
        } while (fabsf(A[nxt].angle - to) >= 0.0009765625f);
    }
    A[nxt].add = v1;
    return nxt;
}

static void look(Chr *ch, float facing, float *face, bool *glancing, bool *full_turn);

/* AI2iMovementState_Calculate_Movement 0x4aa250 */
static void calculate(Chr *ch)
{
    AiState *a = &ch->ai;
    Character *c = &ch->c;
    MoveState *m = &a->move;
    bool panic = false, melee = false, may_slow = false;
    int mode, slow_to = 0;
    /* 1. the mode: the slowed one, else the movement's; a run not barred from slowing may slow to
       its walk */
    if (!m->slowed) {
        mode = movement_mode(ch);
        if (!a->no_slow && mode > 5) slow_to = mode - 2, may_slow = true;
    } else {
        mode = m->slow_mode;
    }
    /* 2. the direction: toward the path's point; arrived, the wait before the path is halted (when it
       doesn't stop there), the local path cleared */
    if (!a->arrived) {
        if (!m->has_point) {
            m->kind = AI_MOVE_NONE;
            m->dir = NONE;
        } else {
            m->dir = angle_of(m->vec[0], m->vec[2]);
            if (m->kind == AI_MOVE_NONE) m->kind = AI_MOVE_FORWARD;
            if (a->facing_mode == 3) a->facing_mode = 1, a->move_kind = AI_MOVE_FORWARD;
        }
    } else {
        float r;
        if (!m->waiting) {
            if (path_end(a, &r)) {
                m->kind = AI_MOVE_NONE;
                m->dir = NONE;
            } else {
                m->waiting = true;
                m->wait = 60;
            }
        } else {
            if (m->wait) m->wait--;
            if (m->wait == 0) {
                m->waiting = false;
                ai_halt(a);
            }
        }
        a->local_n = a->local_k = 0;
        m->has_point = false;
    }
    /* 3. the modifiers: their weights again when asked (none within 30 degrees of a confirmed
       collision hit this tick, less up to 90; none within 60 of the direction to keep away from,
       which ends the count, less up to 100); the direction the sum's */
    m->counted = 0;
    if (a->nmod) {
        float sx = 0.0f, sz = 0.0f;
        if (m->dir != NONE) sx = sinf(m->dir), sz = cosf(m->dir);
        for (int k = 0; k < a->nmod; k++) {
            if (m->reweigh) {
                a->mod_left[k] = a->mod_weight[k];
                if (m->nconfirmed)
                    for (unsigned i = 0; i < m->ncoll; i++) {
                        const MoveCollision *r = &m->coll[i];
                        if (!(r->flags & MOVE_HIT) || !(r->flags & MOVE_CONFIRMED)) continue;
                        float d = fabsf(wrap_pi(a->mod_dir[k] - r->angle));
                        if (d < SIXTH_PI) {
                            a->mod_left[k] = 0.0f;
                            break;
                        }
                        if (d <= HALF_PI) a->mod_left[k] = (SIXTH_PI - d) * a->mod_left[k] * -0.9549296498298645f;
                    }
                if (a->avoid != NONE) {
                    float d = fabsf(wrap_pi(a->mod_dir[k] - a->avoid));
                    if (d < 1.0471975803375244f) {
                        a->mod_left[k] = 0.0f;
                        break;
                    }
                    if (d < 1.7453292608261108f) a->mod_left[k] = (1.0471975803375244f - d) * a->mod_left[k] * -1.4323945045471191f;
                }
            }
            if (a->mod_left[k] > 0.0f) {
                m->counted++;
                sx += a->mod_left[k] * sinf(a->mod_dir[k]);
                sz += a->mod_left[k] * cosf(a->mod_dir[k]);
            }
        }
        if (m->counted) m->dir = angle_of(sx, sz);
    }
    m->reweigh = false;
    /* 4. the arcs: a circle of costs over the directions, each collision a trapezoid (none at 90
       degrees from its direction, its strength within 30); the way the breakpoint's whose cost and
       a fifth of its angle from the one wanted is least */
    if (m->ncoll && (m->dir != NONE || m->counted < (unsigned)a->nmod)) {
        MoveArc *A = m->arc;
        m->narc = 2;
        A[0] = (MoveArc){ 0.0f, 0.0f, 0.0f, 1 };
        A[1] = (MoveArc){ 0.0f, 0.0f, TWO_PI, 0 };
        for (unsigned i = 0; i < m->ncoll; i++) {
            const MoveCollision *r = &m->coll[i];
            if (fabsf(r->strength) < 0.0009765625f) continue;
            float a0 = r->angle - HALF_PI, a1 = r->angle - SIXTH_PI, a2 = r->angle + SIXTH_PI, a3 = r->angle + HALF_PI;
            if (a0 < 0.0f) a0 += TWO_PI;
            if (a1 < 0.0f) a1 += TWO_PI;
            if (a2 >= TWO_PI) a2 -= TWO_PI;
            if (a3 >= TWO_PI) a3 -= TWO_PI;
            float s = r->strength;
            int n = arc_ramp(m, 0, a0, 0.0f, 0.0f);
            n = arc_ramp(m, n, a1, 0.0f, s);
            n = arc_ramp(m, n, a2, s, s);
            arc_ramp(m, n, a3, s, 0.0f);
            for (unsigned k = 0; k < m->narc; k++) {
                A[k].cost += A[k].add;
                A[k].add = 0.0f;
            }
        }
        float want = m->dir;
        if (want == NONE && (want = m->last_dir) == NONE) want = c->facing;
        float best_angle = 0.0f, best = 1e9f;
        arc_ramp(m, 0, want, 0.0f, 0.0f);
        for (int k = 0; A[k].next != 0; k = A[k].next) {
            float cost = fabsf(wrap_pi(A[k].angle - want)) * 0.2f + A[k].cost;
            if (cost < best) best_angle = A[k].angle, best = cost;
        }
        m->dir = best_angle;
    }
    if (m->dir == NONE) m->kind = AI_MOVE_NONE;
    else if (m->kind == AI_MOVE_NONE) {
        m->kind = AI_MOVE_FORWARD;
        m->last_dir = m->dir;
    }
    /* 5. the facing: standing, by the facing mode; moving, the direction less the kind's turn (the
       kind fixed in the facing mode 1) */
    bool strafe = true;
    float facing = c->facing;
    if (m->dir == NONE) {
        if (a->facing_mode == 2) facing = a->face_at;
        else if (a->facing_mode == 3) strafe = false, facing = a->face_at;
    } else {
        m->hold = NONE;
        if (a->facing_mode == 1) m->kind = a->move_kind;
        strafe = a->facing_mode != 1;
        facing = wrap_2pi(m->dir - kind_offset[m->kind]);
    }
    /* the states that count: panic, and a melee in combat */
    if (!ch->player) {
        if (a->state == AI_STATE_PANIC) panic = true;
        else if (a->state == AI_STATE_COMBAT) melee = a->combat.maneuver == 5;
    }
    bool armed = c->weapon && mode != 6 && mode != 4;
    /* 6. the look (or the glance under way) */
    bool glancing, full_turn;
    float look_face;
    look(ch, facing, &look_face, &glancing, &full_turn);
    /* 7. the strafe */
    if (strafe) {
        int emode = a->mode;
        bool keep = may_slow;      /* (the walk and the run that may not move sideways) */
        if (emode >= 3 && emode <= 5) keep = m->no_side_walk;
        else if (emode == 6 || emode == 7) keep = m->no_side_run;
        bool no_back = !keep;    /* (back only where it is cleared below) */
        float to;
        if (m->dir == NONE) no_back = false, to = look_face;
        else if (glancing) to = m->dir;
        else if (a->path_type == 0) no_back = false, to = look_face;
        else if (a->start_d2 <= 225.0f) to = a->path_type == 1 && a->goal_facing ? a->goal_angle : c->facing;
        else if ((glancing ? a->glance_kind : a->aim.look) == 3) to = look_face;
        else to = m->dir;
        if (keep) m->look_ahead = fabsf(wrap_pi(to - m->dir)) < HALF_PI;
        if (to == NONE) {
            if (m->dir != NONE) m->kind = AI_MOVE_FORWARD;
        } else if (m->dir == NONE) {
            /* standing: the facing held turns to the look only past a threshold */
            m->stop_turn = false;
            if (m->hold != NONE) facing = m->hold;
            float off = wrap_2pi(to - facing), limit;
            if (c->type == 0xe0 || c->type == 0xe7) limit = 0.06981316953897476f;
            else if (melee) limit = 0.1745329201221466f;
            else if (panic) limit = 0.4363323152065277f;
            else if (!ch->player && a->state == AI_STATE_NEUTRAL) limit = 0.1745329201221466f;
            else if (glancing && !armed) limit = 2.0071287155151367f;
            else if (!ch->player && a->state == AI_STATE_PURSUIT) limit = 1.483529806137085f;
            else limit = 0.6981316804885864f;
            if (limit < off && off < TWO_PI - limit) facing = m->hold = to;
        } else {
            float off = wrap_pi(to - facing), limit;
            if (melee && !keep) limit = 0.1745329201221466f;
            else if (armed) limit = 1.483529806137085f;
            else limit = 2.0071287155151367f;
            if (m->rekind || limit <= fabsf(off)) {
                float e = wrap_pi(to - m->dir), d = fabsf(e);
                int kind;
                if (!keep) {
                    if (d < 0.7853981852531433f) kind = AI_MOVE_FORWARD;
                    else if (d > 2.356194496154785f && !no_back) kind = AI_MOVE_BACK;
                    else kind = e <= 0.0f ? AI_MOVE_LEFT : AI_MOVE_RIGHT;
                } else if (no_back) kind = AI_MOVE_FORWARD;
                else kind = d >= HALF_PI ? AI_MOVE_BACK : AI_MOVE_FORWARD;
                if (kind != m->kind) {
                    m->kind_ticks = 40;
                    m->kind = kind;
                }
                m->rekind = false;
            }
            facing = wrap_2pi(m->dir - kind_offset[m->kind]);
        }
    }
    int kind = m->stop_turn ? AI_MOVE_NONE : m->kind;
    if (m->dir != NONE) {
        /* 8. the turn: too far from the facing (40 degrees with collisions, else 80, 120 running), it
           stops to turn; within 5 the slowing and the stop are over */
        float off = wrap_pi(c->facing - (facing + c->facing_offset));
        float limit = m->ncoll ? 0.6981316804885864f : mode > 5 ? 2.094395160675049f : 1.3962633609771729f;
        if (fabsf(off) <= limit) {
            if (fabsf(off) < 0.0872664600610733f) m->slowed = m->stop_turn = false;
            if (kind != AI_MOVE_NONE && m->has_point) {
                /* 9. the turning circle through the point, against the class's speed over its turn
                   rate; at the path's last point, the stopping distance against the way left */
                uint16_t kt = m->kind_ticks;
                float f = wrap_2pi(c->facing - kind_offset[m->kind]), cs = cosf(f), sn = sinf(f);
                float along = fabsf(cs * m->vec[2] + sn * m->vec[0]);
                float side = fabsf(cs * m->vec[0] - sn * m->vec[2]);
                bool tight = false, overrun = false, aim = false, rifle = false;
                float circle = 0.0f;
                if (kt == 0) {
                    bool last;
                    float r = point_radius(a, a->local_k, &last);
                    float s = side - r;
                    if (s <= 0.1f) circle = 1000.0f;
                    else circle = (along * along + s * s) / (s + s), side = s;
                    if (c->weapon && armed) aim = true, rifle = (weapon_options(c->weapon) & ONWC_RIFLE) != 0;
                    float rate = movement_turn_rate(c, mode, full_turn);
                    tight = movement_speed(m->kind, mode, aim, rifle) / rate > circle * 0.9f;
                }
                float gr = 0.0f;
                bool at_last = a->local_k >= a->local_n - 1 && a->path_k >= a->path_n && path_end(a, &gr);
                bool stopping = false;
                if (at_last && !m->stop_arrive && side <= along && c->anim && chr_anim_moving(tr_to(c->anim))) {
                    stopping = true;
                    along = (gr + along) * 0.9f;
                    overrun = movement_stop_distance(m->kind, mode) > along;
                }
                if (tight || overrun) {
                    if (!may_slow) {
                        if (tight) m->stop_turn = true, kind = AI_MOVE_NONE;
                        else m->stop_arrive = true, m->arrive_ticks = 40;
                    } else {
                        if (kt == 0) {
                            float rate = movement_turn_rate(c, slow_to, full_turn);
                            tight = movement_speed(m->kind, slow_to, aim, rifle) / rate > circle * 0.9f;
                        }
                        if (stopping) overrun = movement_stop_distance(m->kind, slow_to) > along;
                        if (tight) m->stop_turn = true, kind = AI_MOVE_NONE;
                        else if (overrun) m->stop_arrive = true, m->arrive_ticks = 40;
                        else m->slowed = true, m->slow_mode = slow_to;
                    }
                }
                /* 10. near the point, a turn at most of 1 - d^2/25 quarter turns */
                float t = 1.0f - m->dist2 * 0.04f;
                if (!m->stop_turn && t > 0.0f) a->turn_once = t * HALF_PI;
            }
        } else {
            kind = AI_MOVE_NONE;
        }
        if (m->arrive_ticks) kind = AI_MOVE_NONE;
    }
    /* 11. the executor's move */
    ai_executor_move(a, facing, mode, kind, full_turn);
}

/* the look of step 6: the executor's aim at the point, along the direction or at the character (gone:
   the aim stopped and the facing kept), with the weapon's speed when aiming; the glance under way
   instead; the full turn is the glance's, and an AI's out of the calm states (0x52a4d8) */
static void look(Chr *ch, float facing, float *face, bool *glancing, bool *full_turn)
{
    AiState *a = &ch->ai;
    Character *c = &ch->c;
    AiAim *m = &a->aim;
    int kind = m->look;
    const float *at = m->look_at;
    *glancing = false;
    *full_turn = false;
    if (a->glance_ticks && !a->glance_delay) {
        kind = a->glance_kind;
        at = a->glance_at;
        *glancing = true;
        *full_turn = false;     /* (C+0x159c: 0x4a8150's flag, which the patrol's glance passes as 0) */
    }
    if (!ch->player && (a->state < 0 || a->state > 11 || !exe_data(0x52a4d8 + (uint32_t)a->state, 1)[0])) *full_turn = true;
    switch (kind) {
    case 0:
    default:
        *face = NONE;
        if (m->on) m->stop = true;          /* 0x498da0 */
        break;
    case 1:
        *face = angle_of(at[0] - c->pos[0], at[2] - c->pos[2]);
        m->on = true, m->at_point = true, m->weapon_speed = a->move.aiming;   /* 0x498dc0 */
        memcpy(m->at, at, sizeof m->at);
        break;
    case 2:
        *face = angle_of(at[0], at[2]);
        m->on = true, m->at_point = false, m->weapon_speed = a->move.aiming;  /* 0x498e10 */
        memcpy(m->at, at, sizeof m->at);
        break;
    case 3:
        if (!m->look_char || !chars_in_use((const Chr *)m->look_char)) {
            *face = facing;
            if (m->on) m->stop = true;
            break;
        }
        *face = angle_of(m->look_char->pos[0] - c->pos[0], m->look_char->pos[2] - c->pos[2]);
        m->on = true, m->at_point = true, m->weapon_speed = a->move.aiming;
        /* the character's pelvis */
        m->at[0] = m->look_char->pos[0];
        m->at[1] = m->look_char->pos[1] + m->look_char->pelvis_height;
        m->at[2] = m->look_char->pos[2];
        break;
    }
}

float movement_direction(const Chr *ch)
{
    if (ch->c.inactive) return ch->ai.stub.on ? ch->ai.stub.facing : NONE;    /* 0x4ac1d0 */
    return ch->ai.move.dir;
}

/* ---- the movement stub ---- */

/* AI2iMovementStub_SetupPath 0x4ac4d0: the line's facing, length, and its start */
static void stub_setup(AiState *a)
{
    float f = atan2f(a->stub.to[0] - a->stub.from[0], a->stub.to[2] - a->stub.from[2]);
    a->stub.facing = f < 0.0f ? f + TWO_PI : f;
    float d[3] = { a->stub.from[0] - a->stub.to[0], a->stub.from[1] - a->stub.to[1], a->stub.from[2] - a->stub.to[2] };
    memcpy(a->stub.pos, a->stub.from, sizeof a->stub.pos);
    a->stub.done = 0.0f;
    a->stub.on = true;
    a->stub.length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

void movement_stub_init(Chr *ch, bool from_local)
{
    AiState *a = ai_of(ch);
    Character *c = &ch->c;
    a->stub.facing = c->facing;
    memcpy(a->stub.pos, c->pos, sizeof a->stub.pos);
    if (from_local && a->local_n) {
        chr_location(c, a->stub.from);
        memcpy(a->stub.to, a->local_end, sizeof a->stub.to);
        stub_setup(a);
    } else
        a->stub.on = false;
    a->stub.floor_quad = -1;
    a->stub.floor_wait = a->stub.on ? (uint32_t)((trac_random() * 30) >> 16) + 1 : 0;
}

/* ONrCharacter_FindFloorWithRay 0x4daa10: the floor under p, a ray down steps times the class's first
   gravity (ONCC+0x00), and 12; the hit to out, its quad returned (-1 none) */
static int32_t floor_ray(const Character *c, uint32_t steps, const float p[3], float out[3])
{
    const void *env = p3_environment();
    if (!env) return -1;
    if (!steps) steps = 1;
    float g;
    memcpy(&g, c->oncc, 4);
    const float v[3] = { 0.0f, (float)steps * -g - 12.0f, 0.0f };
    static AkevHit hit[AKEV_HITS_MAX];
    if (!akev_collide_point(env, p, v, 0x148800, 1, hit)) return -1;
    memcpy(out, hit[0].point, 12);
    return (int32_t)hit[0].quad;
}

void movement_stub_update(Chr *ch)
{
    AiState *a = ai_of(ch);
    Character *c = &ch->c;
    if (!a->stub.on) return;
    /* AI2rMovementStub_Update 0x4ac220: forward at the mode's speed (aiming with a weapon in the hand,
       a rifle's) */
    bool armed = c->weapon != NULL, rifle = armed && (weapon_options(c->weapon) & ONWC_RIFLE);
    a->stub.done += movement_speed(AI_MOVE_FORWARD, movement_mode(ch), armed, rifle);
    if (a->stub.done > a->stub.length) {
        /* the end: there, on the floor under it */
        a->stub.done = a->stub.length;
        a->stub.pos[0] = a->stub.to[0];
        a->stub.pos[2] = a->stub.to[2];
        a->stub.floor_quad = floor_ray(c, 0, a->stub.pos, a->stub.pos);
        a->stub.on = false;
        return;
    }
    float t = a->stub.done / a->stub.length;
    a->stub.pos[0] = t * (a->stub.to[0] - a->stub.from[0]) + a->stub.from[0];
    a->stub.pos[2] = t * (a->stub.to[2] - a->stub.from[2]) + a->stub.from[2];
    if (a->stub.floor_wait) a->stub.floor_wait--;
    if (a->stub.floor_wait == 0) {
        a->stub.floor_quad = floor_ray(c, 30, a->stub.pos, a->stub.pos);
        a->stub.floor_wait = 30;
        return;
    }
    /* between the rays, the height on the floor's plane (not below the room's bottom, 2 up), within 8 */
    float pl[4];
    const void *env = p3_environment();
    if (a->stub.floor_quad == -1 || !env || !akev_quad_plane(env, (uint32_t)a->stub.floor_quad, pl)) return;
    if (!(fabsf(pl[1]) > 0.001f)) return;   /* 0x529dfc */
    float y = -((pl[0] * a->stub.pos[0] + pl[2] * a->stub.pos[2] + pl[3]) / pl[1]);
    if (c->room >= 0) {
        float box[6];
        graph_room_box(c->room, box);
        if (y <= box[1] + 2.0f) y = box[1] + 2.0f;
    }
    if (a->stub.pos[1] < y + 8.0f && y - 8.0f < a->stub.pos[1]) a->stub.pos[1] = y;  /* 0x52a740 */
}

void movement_update(Chr *ch)
{
    decay(ch);
    calculate(ch);
    for (unsigned i = 0; i < ch->ai.move.ncoll; i++) ch->ai.move.coll[i].flags &= ~(uint32_t)MOVE_HIT;
}

bool movement_find_clear_point(const Character *c, float p[3])
{
    int room = graph_room_at(p);       /* AKrNodeFromPoint 0x440600 */
    GraphGrid g;
    if (room < 0 || !graph_grid(room, &g) || !g.w || !g.h) return false;
    obstacle_prepare(room, &g);
    int16_t x, z;
    graph_to_cell(&g, p, &x, &z);
    /* the cell clamped to the grid (16-bit) */
    x = x < 0 ? 0 : x > (int16_t)(g.w - 1) ? (int16_t)(g.w - 1) : x;
    z = z < 0 ? 0 : z > (int16_t)(g.h - 1) ? (int16_t)(g.h - 1) : z;
    ObstacleInfo o;
    if (!obstacle_at(room, x, z, &o) || obstacle_ignored(&o, c, 0)) return false;
    if (o.kind == OBSTACLE_CHARACTER) {
        /* 0x50d960: an active one (C+4 0x8000) only within 6 (36 squared, 0x52a994) of its location */
        float l[3];
        chr_location(o.c, l);
        float dx = p[0] - l[0], dz = p[2] - l[2];
        if (!(dx * dx + dz * dz <= 36.0f)) return false;      /* MUrPoint_Distance_SquaredXZ 0x433250 */
    } else if (o.kind == OBSTACLE_DOOR) {
        int d = door_find((uint16_t)o.index);   /* 0x50d9b0 */
        if (d >= 0 && !door_side_passes(d, -1)) return false;
    }
    float out[3];
    /* 15 (0x41700000) at least 5 (0x40a00000), toward C+0x16c, 0.5236 (0x3f060a92) a step, 6 steps,
       the cells under 8, 5 blocked at the start */
    if (!local_find_nearby_point(c, p, 15.0f, 5.0f, c->facing, 0.52359879f, 6, 8, 5, out)) return false;
    p[0] = out[0], p[2] = out[2];
    return true;
}

void movement_activated(Chr *ch, bool on_line)
{
    AiState *a = ai_of(ch);
    Character *c = &ch->c;
    MoveState *m = &a->move;
    if (on_line) {
        /* AI2rMovementState_Initialize 0x4a8d60 from the stub: its facing, the local path of the room
           it goes through (AI2iMovementState_SetupPath 0x4a9d70) */
        m->dir = m->last_dir = a->stub.facing;
        if (!make_local(a, c, p3_environment(), a->path_k)) a->local_n = a->local_k = 0;
        return;
    }
    /* without: off any obstacle (AI2iMovementState_FindClearPoint, 0x4e9210), no local path */
    movement_place_clear(ch);
    a->local_n = a->local_k = 0;
}

void movement_place_clear(Chr *ch)
{
    if (!(ch->c.flags & CHR_FLAG_AI_MOVEMENT)) return;
    float p[3] = { ch->c.pos[0], ch->c.pos[1], ch->c.pos[2] };
    if (movement_find_clear_point(&ch->c, p)) physics_place(&ch->c, p);
}
