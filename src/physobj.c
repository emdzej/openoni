/* The physics objects other than the characters: see physobj.h. */
#include "physobj.h"

#include "akev.h"
#include "akev_collide.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const float EPS = 0.0009765625f;     /* 0x529cc8 */

enum { HITS_MAX = 256 };    /* 0x2820 bytes of 0x28-byte records */

void physobj_init(PhysObject *o)
{
    memset(o, 0, sizeof *o);
    o->gravity = 0.294f;    /* 0x3e96872b */
    o->flags = PHYS_ACTIVE;
    physobj_matrix(o);
}

void physobj_matrix(PhysObject *o)
{
    /* MUrMatrixStack_Translate 0x4346c0, the turn (MUrMatrixStack_Quaternion 0x434c00 of the identity),
       the scale (0x434b90, +0xa0 = 1) */
    static const float id[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    memcpy(o->m, id, sizeof id);
    memcpy(o->m + 9, o->pos, 3 * sizeof(float));
}

/* PHrPhysics_Callback_FindEnvCollisions 0x43b870: the sphere moving by mv through the octree (the walls
   with the flags 0x44800 skipped, and 0x4000 with PHYS_ENV_SKIP); a quad of flags 0x2a0 (two-sided) a
   second hit with the plane turned. */
static unsigned find_env(const PhysObject *o, const void *env, const float mv[3], PhysHit *out)
{
    static AkevHit hits[AKEV_HITS_MAX];
    uint32_t skip = 0x44800u | ((o->flags & PHYS_ENV_SKIP) ? 0x4000u : 0);
    unsigned n = akev_collide_sphere(env, o->sphere, mv, skip, 0xff, hits), k = 0;
    for (unsigned i = 0; i < n && k + 2 <= HITS_MAX; i++) {
        PhysHit *h = &out[k++];
        h->quad = hits[i].quad;
        memcpy(h->plane, hits[i].plane, sizeof h->plane);
        memcpy(h->point, hits[i].point, sizeof h->point);
        AkevQuad q;
        if (akev_quad(env, h->quad, &q) && (q.flags & 0x2a0)) {
            PhysHit *g = &out[k++];
            *g = *h;
            for (int j = 0; j < 4; j++) g->plane[j] *= -1.0f;   /* 0x529804 */
        }
    }
    return k;
}

static int by_d2(const void *a, const void *b)
{
    const PhysHit *x = a, *y = b;
    return x->d2 < y->d2 ? -1 : x->d2 > y->d2;   /* 0x43aa90 */
}

/* 0x433da0: the part of v into the plane (when v·n < 0). */
static void into(const float v[3], const float n[3], float out[3])
{
    float d = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
    if (d < 0.0f) out[0] = d * n[0], out[1] = d * n[1], out[2] = d * n[2];
    else out[0] = out[1] = out[2] = 0.0f;
}

static bool big(const float d[3]) { return EPS <= fabsf(d[0]) || EPS <= fabsf(d[1]) || EPS <= fabsf(d[2]); }

/* PHiPhysics_FindCollisions 0x43a710: the hits nearest first; unless PHYS_NO_RESPOND the move slid along
   them until one takes some of it (PHYS_TOUCHED, the hit). The post-collision callback sees them all. */
static bool slide(PhysObject *o, const void *env, float mv[3])
{
    static PhysHit h[HITS_MAX];
    unsigned n = find_env(o, env, mv, h);
    for (unsigned i = 0; i < n; i++) {
        float x = o->sphere[0] - h[i].point[0], y = o->sphere[1] - h[i].point[1], z = o->sphere[2] - h[i].point[2];
        h[i].d2 = z * z + x * x + y * y;
    }
    qsort(h, n, sizeof *h, by_d2);
    bool hit = false;
    if (!(o->flags & PHYS_NO_RESPOND))
        for (unsigned i = 0; i < n; i++) {
            float d[3];
            into(mv, h[i].plane, d);
            for (int k = 0; k < 3; k++) mv[k] -= d[k];
            if (big(d)) {
                hit = true;
                o->flags |= PHYS_TOUCHED;   /* (the callbacks +0x1c, +0x14 and the other's +0x20: none) */
                break;
            }
        }
    if (o->post_collision) o->post_collision(o, h, n);
    return hit;
}

/* PHiPhysics_Respond 0x43aac0: still colliding after the slide: the move stops. */
static void respond(PhysObject *o, const void *env, float mv[3])
{
    static PhysHit h[HITS_MAX];
    unsigned n = find_env(o, env, mv, h);
    for (unsigned i = 0; i < n; i++) {
        float d[3];
        into(mv, h[i].plane, d);
        if (big(d)) mv[0] = mv[1] = mv[2] = 0.0f;
    }
}

/* PHrPhysics_Callback_Friction 0x43bee0 (types other than 0, 1 and 3): the speed less friction_sub, no
   less than 0 and no more than friction_mul of it; a speed under 1/1024 stops. */
static void friction(PhysObject *o)
{
    float *v = o->vel;
    float s = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (fabsf(s) < EPS) {
        v[0] = v[1] = v[2] = 0.0f;
        return;
    }
    float t = s - o->friction_sub;
    if (t < 0.0f) t = 0.0f;
    float most = s * o->friction_mul;
    if (t > most) t = most;
    float k = t / s;
    v[0] *= k, v[1] *= k, v[2] *= k;
}

void physobj_update(PhysObject *o, const void *env)
{
    if (!(o->flags & PHYS_ACTIVE)) return;
    if (!(o->flags & PHYS_NO_GRAVITY)) o->accel[1] -= o->gravity;
    for (int k = 0; k < 3; k++) o->vel[k] += o->accel[k];
    memset(o->accel, 0, sizeof o->accel);
    o->flags &= ~(uint32_t)PHYS_TOUCHED;
    float mv[3] = { o->vel[0], o->vel[1], o->vel[2] };     /* types 2 and 4 move by the velocity */
    if (!(o->flags & PHYS_NO_COLLISION) && env) {
        o->flags |= 0x10000u;
        bool hit = slide(o, env, mv);
        o->flags &= ~0x10000u;
        if (!(o->flags & PHYS_ACTIVE)) return;      /* gone in a callback */
        if (o->flags & PHYS_TWICE) {
            hit = slide(o, env, mv);
            if (!(o->flags & PHYS_ACTIVE)) return;
        }
        if (hit) respond(o, env, mv);
    }
    for (int k = 0; k < 3; k++) o->pos[k] += mv[k];
    memcpy(o->vel, mv, sizeof mv);
    if (!(o->flags & PHYS_FRICTION_ON_CONTACT) || (o->flags & PHYS_TOUCHED)) friction(o);
    physobj_matrix(o);
}
