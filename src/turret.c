/* The level's turrets: see turret.h. */
#include "turret.h"

#include "akev.h"
#include "chars.h"
#include "objc.h"
#include "particle.h"
#include "sl.h"
#include "tm.h"
#include "trac.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f;     /* 0x529548, 0x529544 */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* The TURR class: +0x4e the shooters' count and +0x8c the shooters (0x4c bytes: +0x00 the particle
   class's name (16), +0x14 the cooldown after a shot, +0x16 the shooter's group (0xffff all), +0x18 a
   matrix on the barrel: its orientation, then at +0x3c its place), +0x58 the base's geometry, +0x64 the
   head's, +0x6c the barrel's, +0x74 the head's pivot on the base, +0x80 the barrel's on the head, +0x54c
   the AI firing mode, +0x5cc the AI block and +0x5e8 its skills, +0x600 the ticks it stays active (0 no
   end), +0x604/+0x608 the barrel's pitch limits, +0x60c/+0x610 the head's yaw limits, +0x614/+0x618 their
   turn speeds a tick, +0x61c the active sound's name. */
enum {
    T_SHOOTERS = 0x4e, T_SHOOTER = 0x8c, SH_SIZE = 0x4c, SH_COOLDOWN = 0x14, SH_GROUP = 0x16, SH_MATRIX = 0x18,
    T_HEAD_GEOM = 0x64, T_BARREL_GEOM = 0x6c, T_HEAD_PIVOT = 0x74, T_BARREL_PIVOT = 0x80,
    T_FIRING = 0x54c, T_AI = 0x5cc, T_SKILLS = 0x5e8, T_ACTIVE_TIME = 0x600, T_PITCH_MIN = 0x604, T_PITCH_MAX = 0x608,
    T_YAW_MIN = 0x60c, T_YAW_MAX = 0x610, T_PITCH_SPEED = 0x614, T_YAW_SPEED = 0x618,
};
enum { TURRETS_MAX = 64 };

static Turret turrets[TURRETS_MAX];
static int n_turrets;
/* show_turrets (0x5ec6c8, on) and turret_show_debug (0x5ec6c9) */
static bool show_turrets = true, show_debug;

int turret_count(void) { return n_turrets; }
Turret *turret_get(int i) { return i >= 0 && i < n_turrets ? &turrets[i] : NULL; }

static unsigned shooters(const Turret *t)
{
    unsigned n = rd16(t->cls + T_SHOOTERS);
    return n > TURRET_SHOOTERS ? TURRET_SHOOTERS : n;
}
static const uint8_t *shooter(const Turret *t, unsigned k) { return t->cls + T_SHOOTER + SH_SIZE * k; }

/* ---- matrices (3x4 by columns) ---- */

/* The Euler rotation (0x437130, order 0): R = Rz Ry Rx of the degrees (as console.c's). */
static void rotation(const float deg[3], float m[9])
{
    const float rad = 0.017453292f;
    float ci = cosf(deg[0] * rad), si = sinf(deg[0] * rad);
    float cj = cosf(deg[1] * rad), sj = sinf(deg[1] * rad);
    float ch = cosf(deg[2] * rad), sh = sinf(deg[2] * rad);
    const float r[9] = { cj * ch, cj * sh, -sj,
                         sj * si * ch - ci * sh, sj * si * sh + ci * ch, cj * si,
                         sj * ci * ch + si * sh, sj * ci * sh - si * ch, cj * ci };
    memcpy(m, r, sizeof r);
}

/* MUrMatrix_Multiply 0x4341c0: a b. */
static void multiply(const float a[12], const float b[12], float out[12])
{
    float r[12];
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 3; i++)
            r[3 * c + i] = a[i] * b[3 * c] + a[3 + i] * b[3 * c + 1] + a[6 + i] * b[3 * c + 2] + (c == 3 ? a[9 + i] : 0.0f);
    memcpy(out, r, sizeof r);
}

/* The turret's matrices (the draw 0x4ce300): the head turned about y by the yaw (0x434d70) at its pivot,
   the barrel about x by 2 pi - the pitch (0x434cf0) at its, and the barrel's in the world. The original
   makes them as it draws the turret (and its targeting reads the last drawn); the port each tick. */
static void matrices(Turret *t)
{
    float c = cosf(t->yaw), s = sinf(t->yaw);
    const float h[12] = { c, 0, -s, 0, 1, 0, s, 0, c, rdf(t->cls + T_HEAD_PIVOT), rdf(t->cls + T_HEAD_PIVOT + 4), rdf(t->cls + T_HEAD_PIVOT + 8) };
    memcpy(t->head, h, sizeof h);
    float p = TWO_PI - t->pitch;
    c = cosf(p), s = sinf(p);
    const float b[12] = { 1, 0, 0, 0, c, s, 0, -s, c, rdf(t->cls + T_BARREL_PIVOT), rdf(t->cls + T_BARREL_PIVOT + 4), rdf(t->cls + T_BARREL_PIVOT + 8) };
    memcpy(t->barrel, b, sizeof b);
    float m[12];
    multiply(t->base, t->head, m);
    multiply(m, t->barrel, t->world);
}

/* ---- the shooters' particles ---- */

/* OBJiTurret_CreateParticles 0x4cd5c0: each shooter's particle at its place on the barrel, its orientation
   the shooter's, hanging from the barrel's matrix, still, owned by the turret, its create (11) and
   background-stop (5) events. */
static void create_particles(Turret *t)
{
    uint32_t now = ui_time();
    float time = (float)now * (1.0f / 60.0f);       /* 0x5297fc */
    for (unsigned k = 0; k < shooters(t); k++) {
        const uint8_t *sh = shooter(t, k);
        char name[17];
        memcpy(name, sh, 16);
        name[16] = 0;
        t->particle[k] = 0;
        P3Class *pc = p3_class(name);
        P3Particle *p = pc ? p3_create(pc, now) : NULL;
        if (!p) continue;
        float *pos = p3_position(pc, p);
        if (!pos) continue;
        for (int i = 0; i < 3; i++) pos[i] = rdf(sh + SH_MATRIX + 0x24 + 4 * i);     /* MUrMatrix_GetCol(3) */
        float *o = p3_block(pc, p, P3_X_ORIENTATION);
        if (o) for (int i = 0; i < 9; i++) o[i] = rdf(sh + SH_MATRIX + 4 * i);
        float *v = p3_block(pc, p, P3_X_VELOCITY);
        if (v) v[0] = v[1] = v[2] = 0.0f;
        float *off = p3_block(pc, p, P3_X_OFFSET);
        if (off) off[0] = off[1] = off[2] = 0.0f;
        uint8_t *at = p3_block(pc, p, P3_X_ATTACH);
        if (at) {
            uint32_t id = p3_attach_id(t->world);
            memcpy(at, &id, 4);
            p3_set_flags(p, p3_flags(p) | 0x2000);
        }   /* else "particle class '%s' attached to turret but no dynamic matrix" */
        uint8_t *b;
        if ((b = p3_block(pc, p, P3_X_OWNER))) {
            uint32_t owner = 0x3000000u | t->id;       /* 0x51ce10: the turret's id as an owner */
            memcpy(b, &owner, 4);
        }
        if ((b = p3_block(pc, p, P3_X_TEX_INDEX))) { uint32_t r = trac_local_random(); memcpy(b, &r, 4); }
        if ((b = p3_block(pc, p, P3_X_TEX_TIME))) memcpy(b, &now, 4);
        if ((b = p3_block(pc, p, P3_X_PREV_POS))) memset(b, 0, 4);
        if ((b = p3_block(pc, p, P3_X_LENSFLARE))) memset(b, 0, 4);
        if ((b = p3_block(pc, p, P3_X_CACHE))) memset(b, 0, 20);
        p3_set_life(p, 0.0f);
        p3_send_event(pc, p, P3_EV_CREATE, time);
        p3_send_event(pc, p, P3_EV_BG_STOP, time);
        t->particle[k] = p3_handle(p);
    }
    t->flags |= TURRET_PARTICLES;
}

/* 0x4cd490: an event to the shooters of the group (0xffff all; a shooter of group 0xffff takes every
   one), a moving particle stopped first. */
static void shooters_event(Turret *t, int ev, uint16_t group)
{
    float time = (float)ui_time() * (1.0f / 60.0f);
    for (unsigned k = 0; k < shooters(t); k++) {
        uint16_t g = rd16(shooter(t, k) + SH_GROUP);
        if (group != 0xffff && g != 0xffff && g != group) continue;
        P3Class *pc;
        P3Particle *p = p3_from_handle(t->particle[k], &pc);
        if (!p) {
            t->particle[k] = 0;
            continue;
        }
        float *v = p3_block(pc, p, P3_X_VELOCITY);
        if (v) v[0] = v[1] = v[2] = 0.0f;
        p3_send_event(pc, p, ev, time);
    }
}

/* 0x4cd870: the particles go. */
static void delete_particles(Turret *t)
{
    shooters_event(t, P3_EV_STOP, 0xffff);
    shooters_event(t, P3_EV_BG_STOP, 0xffff);
    for (unsigned k = 0; k < shooters(t); k++) {
        P3Class *pc;
        P3Particle *p = p3_from_handle(t->particle[k], &pc);
        if (p) p3_kill(pc, p);
        t->particle[k] = 0;
    }
}

/* ---- the targeting's callbacks (0x54c348) ---- */

/* 0x4cdcb0: the yaw and pitch toward a point, in the turret's own frame from the barrel's pivot. */
static void aim_at(Turret *t, const float p[3])
{
    const float *m = t->base;
    float d[3] = { p[0] - m[9], p[1] - m[10], p[2] - m[11] }, l[3];
    for (int i = 0; i < 3; i++) l[i] = m[3 * i] * d[0] + m[3 * i + 1] * d[1] + m[3 * i + 2] * d[2];
    float x = l[0] - (rdf(t->cls + T_BARREL_PIVOT) + rdf(t->cls + T_HEAD_PIVOT));
    float z = l[2] - (rdf(t->cls + T_BARREL_PIVOT + 8) + rdf(t->cls + T_HEAD_PIVOT + 8));
    float y = l[1] - (rdf(t->cls + T_BARREL_PIVOT + 4) + rdf(t->cls + T_HEAD_PIVOT + 4));
    float yaw = atan2f(x, z), pitch = atan2f(y, sqrtf(x * x + z * z));
    if (pitch < 0.0f) pitch += TWO_PI;
    else if (pitch >= TWO_PI) pitch -= TWO_PI;
    if (yaw < 0.0f) yaw += TWO_PI;
    else if (yaw >= TWO_PI) yaw -= TWO_PI;
    t->want_pitch = pitch;
    t->want_yaw = yaw;
}

static void op_fire(void *o, bool on) { ((Turret *)o)->fire = on; }       /* 0x4cdc50 */

/* 0x4cdc60: the shooter's place: the turret's, with the two pivots (unturned). */
static void op_place(void *o, float out[3])
{
    const Turret *t = o;
    for (int i = 0; i < 3; i++) out[i] = rdf(t->cls + T_BARREL_PIVOT + 4 * i) + rdf(t->cls + T_HEAD_PIVOT + 4 * i) + t->pos[i];
}

/* 0x4cde70: along the vector, from the shooter's place. */
static void op_vector(void *o, const float v[3])
{
    Turret *t = o;
    float p[3];
    op_place(t, p);
    for (int i = 0; i < 3; i++) p[i] += v[i];
    aim_at(t, p);
}

static void op_point(void *o, const float p[3]) { aim_at(o, p); }        /* 0x4cdca0 */

/* 0x4cde30: at the character's pelvis. */
static void op_char(void *o, const Character *c)
{
    float p[3] = { c->pos[0], c->pos[1] + c->pelvis_height, c->pos[2] };
    aim_at(o, p);
}

static const TargetingOps turret_ops = { op_fire, NULL, NULL, op_place, op_vector, op_point, op_char };

/* ---- the objects ---- */

static bool add_turret(const ObjcObject *o, void *user)
{
    (void)user;
    if (n_turrets == TURRETS_MAX || o->size < 0x6b) return true;
    Turret *t = &turrets[n_turrets];
    memset(t, 0, sizeof *t);
    /* the packed data (0x4ce9e0): the class's name (63), the id (+0x3f), the flags (+0x41, bit 2 kept),
       the target teams (+0x67) */
    memcpy(t->name, o->data, 63);
    t->id = rd16(o->data + 0x3f);
    t->flags = rd16(o->data + 0x41) & 2;
    t->teams = rd32(o->data + 0x67);
    memcpy(t->pos, o->pos, sizeof t->pos);
    memcpy(t->rot, o->rot, sizeof t->rot);
    /* OBJiTurret_SetOSD 0x4ceb80: the class by name, else the first ("failed to find turret class") */
    t->cls = tm_instance_get(TM_TAG("TURR"), t->name);
    if (!t->cls) {
        TmFile *f = tm_file_at(0);
        for (uint32_t i = 0; f && i < tm_file_instance_count(f) && !t->cls; i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (in->tmpl->tag == TM_TAG("TURR") && in->data) t->cls = in->data;
        }
        if (!t->cls) return true;
    }
    rotation(t->rot, t->base);              /* 0x437130 */
    memcpy(t->base + 9, t->pos, sizeof t->pos);
    matrices(t);
    /* the in-memory data's "active at the start" (+4) is never set by the reader: every turret starts off */
    create_particles(t);
    t->cooldown = 60;
    n_turrets++;
    return true;
}

int turret_load(void)
{
    turret_unload();
    objc_each("Turret", add_turret, NULL);
    return n_turrets;
}

void turret_unload(void)
{
    for (int i = 0; i < n_turrets; i++) delete_particles(&turrets[i]);
    n_turrets = 0;
}

/* 0x4cd9c0: off to active, its active time from 0, the active sound, the shooters' background start
   (4), the targeting (the barrel's axis z). */
static void activate(Turret *t)
{
    if (t->state != 0) return;
    t->active_ticks = 0;
    t->state = 1;
    t->flags |= TURRET_SOUND;           /* the active sound (+0x63c) isn't played: no positional ambients yet */
    shooters_event(t, P3_EV_BG_START, 0xffff);
    targeting_init(&t->t, 1, &turret_ops, t, t->cls + T_FIRING, t->cls + T_AI, t->cls + T_SKILLS);
}

/* 0x4cdab0 (and 0x4cded0, 0x4cdb90): active to off: the aim back to rest, no target, the targeting
   stopped, the shooters stopped and their background too. The original sets the flags with OR 0xfffb
   where it means to clear the firing bit (so every other bit comes on and the firing bit stays as it
   was); kept. */
static void deactivate(Turret *t)
{
    if (t->state != 1) return;
    t->want_yaw = t->want_pitch = 0.0f;
    t->t.target = NULL;
    targeting_stop(&t->t);
    if (t->flags & TURRET_FIRING) {
        t->flags |= 0xfffb;
        shooters_event(t, P3_EV_STOP, 0xffff);
    }
    shooters_event(t, P3_EV_BG_STOP, 0xffff);
    t->flags &= (uint16_t)~TURRET_SOUND;
    t->fire = false;
    t->state = 0;
}

void turret_activate(uint16_t id)
{
    for (int i = 0; i < n_turrets; i++)
        if (id == 0xffff || turrets[i].id == id) activate(&turrets[i]);
}

bool turret_inaccuracy(uint16_t id, float *out)
{
    /* WPrParticleInaccuracy 0x51ced0 for a turret: the object by its id (0x5078c0), its class's skills'
       inaccuracy (+0x5e8 +0x10) */
    for (int i = 0; i < n_turrets; i++)
        if (turrets[i].id == id) {
            *out = turrets[i].cls ? rdf(turrets[i].cls + T_SKILLS + 0x10) : 0.0f;
            return true;
        }
    return false;
}

void turret_deactivate(uint16_t id)
{
    for (int i = 0; i < n_turrets; i++)
        if (turrets[i].id == id) deactivate(&turrets[i]);     /* 0x4cdab0: by the id only */
}

/* 0x4cf7e0: the nearest character (by the rounded squared distance) of a target team. */
static const Chr *nearest_target(const Turret *t)
{
    const Chr *best = NULL;
    uint32_t bd = 0xffffffffu;
    for (int i = 0; i < chars_slots(); i++) {
        const Chr *c = chars_get(i);
        if (!c || c->team >= 32 || !(t->teams & (1u << c->team))) continue;
        float dx = t->pos[0] - c->c.pos[0], dy = t->pos[1] - c->c.pos[1], dz = t->pos[2] - c->c.pos[2];
        uint32_t d = (uint32_t)lrintf(dx * dx + dy * dy + dz * dz);     /* 0x4264e0 */
        if (d < bd) bd = d, best = c;
    }
    return best;
}

/* OBJrTurret_PerformLOS 0x4cf6b0: within 15 of the target's pelvis, seen; else the line from 15 along the
   way to it, clear of the environment (flags 0x242000). */
static bool line_of_sight(const Turret *t, const void *env)
{
    const Character *c = t->t.target;
    if (!c) return false;
    float d[3] = { c->pos[0] - t->pos[0], c->pos[1] + c->pelvis_height - t->pos[1], c->pos[2] - t->pos[2] };
    float l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (l2 < 225.0f) return true;       /* 0x52af74 */
    float k = 15.0f / sqrtf(l2);        /* 0x433d60: to length 15 */
    float s[3], v[3];
    for (int i = 0; i < 3; i++) s[i] = t->pos[i] + d[i] * k, v[i] = d[i] - d[i] * k;
    return !(env && akev_segment_hits(env, s, v, 0x242000));
}

/* The turn toward the wanted angle: the wanted one signed, held within [lo, hi], back in [0, 2 pi); the
   angle moves the shorter way, at most speed a tick. */
static float clamp_want(float want, float lo, float hi)
{
    if (want >= PI) want -= TWO_PI;
    if (want < lo) want = lo;
    else if (want > hi) want = hi;
    if (want < 0.0f) want += TWO_PI;
    else if (want >= TWO_PI) want -= TWO_PI;
    return want;
}

static float turn(float now, float want, float speed)
{
    float a, b;     /* the direct way and the way round */
    if (want <= now) a = -(now - want), b = (TWO_PI - now) + want;
    else a = want - now, b = -((TWO_PI - want) + now);
    if (fabsf(a) < fabsf(b)) b = a;     /* a tie goes round */
    if (b < -speed) b = -speed;
    else if (b > speed) b = speed;
    float r = now + b;
    if (r < 0.0f) r += TWO_PI;
    else if (r >= TWO_PI) r -= TWO_PI;
    return r;
}

/* 0x4cd400: a shot of the group: the first since the firing began starts the shooters (2), each pulses
   them (1) and waits the shooter's cooldown. */
static void shoot(Turret *t, uint16_t group)
{
    if (t->cooldown) return;
    unsigned k = 0;
    while (k < shooters(t) && rd16(shooter(t, k) + SH_GROUP) != group) k++;
    if (k == shooters(t)) return;
    if (!(t->flags & TURRET_FIRING)) {
        t->flags |= TURRET_FIRING;
        shooters_event(t, P3_EV_START, group);
    }
    shooters_event(t, P3_EV_PULSE, group);
    t->cooldown = rd16(shooter(t, k) + SH_COOLDOWN);
}

static void update(Turret *t, const void *env)
{
    if (t->state == 1) {
        uint32_t limit = rd32(t->cls + T_ACTIVE_TIME);
        if (limit == 0 || t->active_ticks <= limit) {
            t->active_ticks++;
            bool has = t->t.target != NULL;
            if (t->retarget == 0) {
                t->retarget = 15;
                const Chr *c = nearest_target(t);
                t->t.target = c ? &c->c : NULL;
                has = c != NULL;
                if (has) t->los = line_of_sight(t, env);
                else t->los = false, t->want_yaw = t->want_pitch = 0.0f;
            } else {
                t->retarget--;
            }
            if (has) {
                memcpy(t->t.matrix, t->world, sizeof t->world);
                targeting_update(&t->t, true, true, t->los, NULL);
                targeting_fire_decision(&t->t, true, true, t->los);
            }
        } else if (t->want_pitch == t->pitch && t->want_yaw == t->yaw) {
            deactivate(t);      /* its time over and back at rest */
        }
    } else {
        t->fire = false;
    }
    /* the turn */
    const uint8_t *c = t->cls;
    t->want_pitch = clamp_want(t->want_pitch, rdf(c + T_PITCH_MIN), rdf(c + T_PITCH_MAX));
    t->want_yaw = clamp_want(t->want_yaw, rdf(c + T_YAW_MIN), rdf(c + T_YAW_MAX));
    if (t->want_pitch != t->pitch || t->want_yaw != t->yaw) {
        t->pitch = turn(t->pitch, t->want_pitch, rdf(c + T_PITCH_SPEED));
        t->yaw = turn(t->yaw, t->want_yaw, rdf(c + T_YAW_SPEED));
    }
    matrices(t);
    /* seen (drawn this frame): the shooters' background starts; unseen for 120 ticks, it stops */
    if (t->flags & TURRET_DRAWN) {
        t->hidden_ticks = 0;
        if (!(t->flags & TURRET_SHOWN)) {
            t->flags |= TURRET_SHOWN;
            shooters_event(t, P3_EV_BG_START, 0xffff);
        }
    } else if (++t->hidden_ticks >= 120 && (t->flags & TURRET_SHOWN)) {
        t->flags &= (uint16_t)~TURRET_SHOWN;
        shooters_event(t, P3_EV_BG_STOP, 0xffff);
    }
    t->flags &= (uint16_t)~TURRET_DRAWN;
    /* the trigger: a shot of group 0 (unless every AI is passive, 0x5ec0c4: not ported), else the
       shooters stopped */
    if (t->cooldown) {
        t->cooldown--;
        return;
    }
    if (t->fire) {
        shoot(t, 0);
        t->fire = false;
        return;
    }
    if (t->flags & TURRET_FIRING) {
        shooters_event(t, P3_EV_STOP, 0xffff);
        t->flags &= (uint16_t)~TURRET_FIRING;
    }
}

void turret_update(const void *env)
{
    for (int i = 0; i < n_turrets; i++) update(&turrets[i], env);
}

const void *turret_geometry(int i, int k, float m[12])
{
    Turret *t = turret_get(i);
    if (!t || !show_turrets || k < 0 || k > 1) return NULL;
    float h[12];
    multiply(t->base, t->head, h);
    /* OBJiTurret_Draw: drawn (and marked so) while one of the leaves 8 each way of its head is visible
       (0x440900 once, while it has none, then 0x440b50) */
    if (!t->leaves.count) {
        float box[6] = { h[9] - 8.0f, h[10] - 8.0f, h[11] - 8.0f, h[9] + 8.0f, h[10] + 8.0f, h[11] + 8.0f };
        vis_list(box, VIS_LIST_MAX, &t->leaves);
    }
    if (!vis_list_visible(&t->leaves)) {
        t->flags &= (uint16_t)~TURRET_DRAWN;
        return NULL;
    }
    t->flags |= TURRET_DRAWN;
    if (k == 0) {
        memcpy(m, h, sizeof h);
        return tm_link(t->cls + T_HEAD_GEOM);
    }
    multiply(h, t->barrel, m);
    return tm_link(t->cls + T_BARREL_GEOM);
}

/* turret_activate / turret_deactivate / turret_reset id (AIiScript 0x4b6340 and its neighbours: the
   turret of that id; with no id, every one for activate). turret_reset: off, at rest. */
static uint16_t arg_id(int c, const SlValue *v)
{
    if (c < 1) return 0xffff;
    return (uint16_t)(v[0].type == SL_FLOAT ? (int)v[0].f : v[0].i);
}

static bool cmd_activate(SlThread *th, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)th, (void)r, (void)u;
    turret_activate(arg_id(c, v));
    return true;
}

static bool cmd_deactivate(SlThread *th, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)th, (void)r, (void)u;
    turret_deactivate(arg_id(c, v));
    return true;
}

void turret_register_commands(void)
{
    sl_register("turret_activate", cmd_activate, NULL);
    sl_register("turret_deactivate", cmd_deactivate, NULL);
    sl_register_bool("show_turrets", &show_turrets);
    sl_register_bool("turret_show_debug", &show_debug);
}
