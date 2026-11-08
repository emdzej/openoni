/* CA, the game camera: see camera.h and docs/re/camera.md. */
#include "camera.h"

#include "akev.h"
#include "akev_collide.h"
#include "oban.h"
#include "object.h"
#include "sl.h"
#include "tm.h"

#include <math.h>
#include <string.h>

/* Float constants of the exe's .rdata the camera uses (the values, rounded as stored). */
#define TWO_PI 6.2831854820251465f     /* 0x529548 (and -2pi at 0x52b1a0) */
#define PI_F 3.1415927410125732f       /* 0x529544 */
#define HALF_PI 1.5707963705062866f    /* 0x529cd0 */
#define DEG 0.01745329238474369f       /* 0x529550 */

/* The script variables (CArInitialize 0x4d5a30) with the exe's initial values (.data 0x54d634..). */
static bool cm_plane_test = true;      /* 0x54d634 "jello camera plane test" */
static float cm_height = 15.0f;        /* 0x54d638 "camera height" */
static float cm_distance = 33.0f;      /* 0x54d63c "camera distance" */
static float cm_canter_weapon = 7.0f;  /* 0x54d640 "camera canter" */
static float cm_canter_unarmed = 8.0f; /* 0x54d644 "camera canter" */
static float cm_jello_radius = 12.0f;  /* 0x54d648 "bla bla" */
static int32_t cm_jello_amt = 20;      /* 0x54d650 */
static bool jello_on = true;           /* 0x54d64c, cm_jello and CArEnableJello */
static uint32_t jello_probes = 1;      /* 0x54d654: no script variable; at most 5 */

/* The state (0x6364c0). The original overlays the modes' fields from +0x04; they are kept apart here, as
   no mode reads another's (each entry sets what its update reads). */
typedef struct {
    int mode;                   /* +0x00 */
    /* CAMERA_ORBIT */
    float angle, stop, speed;   /* +0x04, +0x08, +0x0c */
    /* CAMERA_FOLLOW */
    float canter, distance;     /* +0x10: height over the target, +0x14 */
    bool snap;                  /* +0x18: straight to the goal, no smoothing (nothing sets it) */
    /* CAMERA_ANIMATE */
    ObanPlayer anim;            /* +0x04 */
    /* CAMERA_MANUAL */
    bool fly;                   /* +0x04 */
    /* CAMERA_INTERPOLATE */
    CameraView from, to;        /* +0x04, +0x28 */
    uint32_t tick, ticks;       /* +0x4c, +0x50 */
    bool has_target;            /* +0x54: the character */
    CameraTarget target;
    CameraView cur;             /* +0x58 location, +0x64 view, +0x70 up */
    bool busy;                  /* +0x80 */
} Camera;

static Camera cam;
static CameraView m3;           /* the Motoko camera's copy (0x63112c +0x08..+0x28) */
static bool m3_set;
static void *jello_env;
static void (*cut_hook)(void);
static uint32_t sl_now;         /* the game time at the last update, for cm_anim's start */

/* The jello's marks: the quads it flagged on this update and the one before. */
enum { JELLO_MAX = 0x200 };
static uint32_t jello_cur[JELLO_MAX], jello_old[JELLO_MAX];
static int n_jello_cur, n_jello_old;

/* ---- the maths (MU) ---- */

/* 0x433050: unit length; a vector within 1e-20 of zero is scaled by 1e20 first. */
static void normalize(float v[3])
{
    float len = sqrtf(v[2] * v[2] + v[1] * v[1] + v[0] * v[0]);
    if (v[0] > -1e-20f && v[0] < 1e-20f && v[1] > -1e-20f && v[1] < 1e-20f && v[2] > -1e-20f && v[2] < 1e-20f) {
        v[0] *= 1e20f, v[1] *= 1e20f, v[2] *= 1e20f;
        len = sqrtf(v[0] * v[0] + v[2] * v[2] + v[1] * v[1]);
    }
    float k = 1.0f / len;
    v[0] *= k, v[1] *= k, v[2] *= k;
}

/* MUrVector_CrossProduct 0x433b70: a x b. */
static void cross(const float a[3], const float b[3], float out[3])
{
    float x = b[2] * a[1] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - b[0] * a[1];
    out[0] = x, out[1] = y, out[2] = z;
}

/* MUrMatrix_MultiplyNormal 0x433ae0: the 3x3 part (by columns) times v, made unit. */
static void mul_normal(const float m[12], const float v[3], float out[3])
{
    float x = v[0] * m[0] + v[1] * m[3] + v[2] * m[6];
    float y = v[0] * m[1] + v[1] * m[4] + v[2] * m[7];
    float z = v[0] * m[2] + v[1] * m[5] + v[2] * m[8];
    out[0] = x, out[1] = y, out[2] = z;
    normalize(out);
}

/* 0x433ff0: t b + (1 - t) a. */
static void lerp(const float a[3], const float b[3], float t, float out[3])
{
    float u = 1.0f - t;
    for (int i = 0; i < 3; i++) out[i] = t * b[i] + u * a[i];
}

static float wrap(float a)
{
    if (a < 0.0f) return a + TWO_PI;
    if (a >= TWO_PI) return a - TWO_PI;
    return a;
}

/* MUrPoint_RotateAboutAxis 0x432f70: the rotation matrix about a (unit) axis (0x4345f0), times p. */
static void rotate_about_axis(const float p[3], const float axis[3], float angle, float out[3])
{
    double c = cos(angle), s = sin(angle), t = 1.0 - c, x = axis[0], y = axis[1], z = axis[2];
    float m[9] = {
        (float)(t * x * x + c), (float)(s * z + t * x * y), (float)(t * x * z) - (float)(s * y),
        (float)(t * x * y) - (float)(s * z), (float)(t * y * y + c), (float)(s * x + t * y * z),
        (float)(s * y) + (float)(t * x * z), (float)(t * y * z - s * x), (float)(t * z * z + c),
    };
    float r[3];
    for (int i = 0; i < 3; i++) r[i] = m[i] * p[0] + m[3 + i] * p[1] + m[6 + i] * p[2];
    memcpy(out, r, sizeof r);
}

/* CArBuildUpVector 0x4d59b0, as every mode builds it inline: the up that keeps the side level,
   view x (view.z, 0, -view.x), made unit. */
static void build_up(const float view[3], float up[3])
{
    float side[3] = { view[2], 0.0f, -view[0] };
    cross(view, side, up);
    normalize(up);
}

/* ---- the jello ---- */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

static uint8_t *env_array(const void *env, unsigned field, uint32_t *count)
{
    uint8_t *a = tm_link((const uint8_t *)env + field);
    *count = a ? rd32(a + 0x14) : 0;
    return a ? a + 0x18 : NULL;
}

/* The quads of the last collision's hits (the list at 0x6a9080, its count at 0x6a906c), after those
   found already, while there is room (0x4d6d97). */
static uint32_t take_hits(const AkevHit *h, unsigned nh, uint32_t *found, uint32_t n)
{
    for (unsigned i = 0; i < nh && n < JELLO_MAX; i++) found[n++] = h[i].quad;
    return n;
}

static bool jello_marked(const uint32_t *list, int n, uint32_t q)
{
    for (int i = 0; i < n; i++)
        if (list[i] == q) return true;
    return false;
}

/* CAiTrackJello 0x4d6b10: the quads between the camera and its target become see-through (flag 0x20, and
   cm_jello_amt in the quad's AGQR entry +2). */
static void track_jello(void)
{
    void *env = jello_env;
    if (!jello_on || !env || !cam.has_target) return;
    const CameraTarget *c = &cam.target;
    uint32_t found[JELLO_MAX];
    uint32_t n = 0;
    static AkevHit hits[AKEV_HITS_MAX];
    /* the probes: off to the side (up x view, times 3, its y raised by 2 first), around a point 12 over
       the feet; only the first runs (the count at 0x54d654 is 1) */
    float side[3];
    cross(cam.cur.up, cam.cur.view, side);
    side[0] *= 3.0f;
    side[1] = (side[1] + 2.0f) * 3.0f;
    side[2] *= 3.0f;
    float head[3] = { c->pos[0], c->pos[1] + 12.0f, c->pos[2] };
    float reach = c->bottom + 1.5f;
    if (jello_probes > 5) jello_probes = 5;
    for (uint32_t i = 0; i < jello_probes; i++) {
        if (i == 1 || i == 3) side[1] = -side[1];
        float to[3];
        if (i == 2) side[0] = -side[0], side[1] = -side[1], side[2] = -side[2];
        if (i < 4) {
            for (int k = 0; k < 3; k++) to[k] = head[k] + side[k];
        } else {
            memcpy(to, head, sizeof to);
        }
        float d[3] = { to[0] - cam.cur.location[0], to[1] - cam.cur.location[1], to[2] - cam.cur.location[2] };
        if (cm_jello_radius > 0.0f) {
            /* a sphere of the radius from the camera, stopping the radius short (at least 1 unit along) */
            float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - cm_jello_radius;
            if (len <= 1.0f) len = 1.0f;
            float s[3] = { d[0], d[1], d[2] }, k = len / sqrtf(s[2] * s[2] + s[1] * s[1] + s[0] * s[0]);
            s[0] *= k, s[1] *= k, s[2] *= k;     /* 0x433d60 */
            /* AKrCollision_Sphere 0x441cb0, skipping 0x42000, listing as many as there is room for */
            float sph[4] = { cam.cur.location[0], cam.cur.location[1], cam.cur.location[2], cm_jello_radius };
            n = take_hits(hits, akev_collide_sphere(env, sph, s, AKEV_QUAD_NO_DRAW, JELLO_MAX - n, hits), found, n);
        } else {
            /* AKrCollision_Point 0x441c80 */
            n = take_hits(hits, akev_collide_point(env, cam.cur.location, d, AKEV_QUAD_NO_DRAW, JELLO_MAX - n, hits), found, n);
        }
        /* the level's objects with a physics context the same line meets: marked for their draw's jello */
        object_jello_line(cam.cur.location, d);
    }
    uint32_t nr, nq, npl, np;
    uint8_t *render = env_array(env, AKEV_QUAD_RENDER, &nr);
    const uint8_t *col = env_array(env, AKEV_QUAD_COLLISION, &nq);
    const uint8_t *planes = akev_planes(env, &npl), *pts = akev_points(env, &np);
    for (uint32_t i = 0; i < n; i++) {
        AkevQuad g;
        if (!akev_quad(env, found[i], &g)) continue;
        /* the highest corner: the quad must reach above the body's bottom + 1.5 */
        float top = -INFINITY;
        for (uint32_t k = 0; k < g.corners; k++) top = fmaxf(top, rdf(pts + 12 * g.point[k] + 4));
        if (top < reach) continue;
        if (cm_plane_test && col && found[i] < nq) {
            /* The plane index is used as stored: its bit 31 (the plane negated) falls out of the index
               times 16, so a negated plane is taken unnegated. Kept as the original has it. */
            uint32_t pi = rd32(col + 28u * found[i]) & 0x0fffffffu;
            if (pi < npl) {
                const uint8_t *pl = planes + 16 * pi;
                float a = rdf(pl), b = rdf(pl + 4), cc = rdf(pl + 8), dd = rdf(pl + 12);
                bool cam_side = cam.cur.location[0] * a + cam.cur.location[2] * cc + cam.cur.location[1] * b + dd > 0.0f;
                bool feet = c->bottom * b + c->pos[0] * a + c->pos[2] * cc + dd > 0.0f;
                bool top_side = (c->pos[1] + 15.0f) * b + c->pos[0] * a + c->pos[2] * cc + dd > 0.0f;
                /* Left alone when camera and feet are both in front, or nothing is: the test is
                   (feet && camera) == (head || feet || camera). Kept as the original has it. */
                if ((feet && cam_side) == (top_side || feet || cam_side)) continue;
            }
        }
        if (!jello_marked(jello_cur, n_jello_cur, found[i]) && n_jello_cur < JELLO_MAX) {
            if (!jello_marked(jello_old, n_jello_old, found[i])) akev_quad_set_flags(env, found[i], AKEV_QUAD_JELLO, 0);
            jello_cur[n_jello_cur++] = found[i];
        }
        if (render && found[i] < nr) {
            uint8_t *amt = render + 4u * found[i] + 2;
            amt[0] = (uint8_t)cm_jello_amt, amt[1] = (uint8_t)((uint32_t)cm_jello_amt >> 8);
        }
    }
}

/* 0x41aab0, at the start of every update: the jello flag off again. The original clears it on the quads
   the last frame drew (Motoko's visible list); the port clears it on the quads it marked, once this
   update's marks are known, so that quads marked twice don't change. */
static void jello_begin(void)
{
    memcpy(jello_old, jello_cur, sizeof(uint32_t) * (size_t)n_jello_cur);
    n_jello_old = n_jello_cur;
    n_jello_cur = 0;
}

static void jello_end(void)
{
    for (int i = 0; i < n_jello_old; i++)
        if (jello_env && !jello_marked(jello_cur, n_jello_cur, jello_old[i]))
            akev_quad_set_flags(jello_env, jello_old[i], 0, AKEV_QUAD_JELLO);
    n_jello_old = 0;
}

/* ---- the modes ---- */

/* 0x4d6220: around the target, at the distance and height of the follow camera, the angle turning by the
   speed each tick and holding at the stop angle when it passes it. */
static bool orbit_update(uint32_t ticks)
{
    if (!ticks) return false;
    float old = cam.angle, step = (float)ticks * cam.speed;
    if (step < -TWO_PI) step = -TWO_PI;
    else if (step > TWO_PI) step = TWO_PI;
    float now = step + cam.angle, s = cam.stop;
    cam.angle = now;
    if (s >= 0.0f) {
        /* the stop, or the stop a turn either way, between the old and the new angle (ends included) */
        bool hit = false;
        float stops[3] = { s, s + TWO_PI, s - TWO_PI };
        for (int i = 0; i < 3; i++)
            hit |= (old <= stops[i] && now >= stops[i]) || (old >= stops[i] && now <= stops[i]);
        if (hit) cam.angle = s;
    }
    cam.angle = wrap(cam.angle);
    if (!cam.has_target) return false;
    const CameraTarget *c = &cam.target;
    float a = cam.angle;
    if (c->active) a = wrap(a + c->aim_lr);
    a = wrap(c->facing + a);
    /* (0, 0, 1) turned about y (0x433010), at the distance, lowered by the unarmed canter */
    float *v = cam.cur.view;
    v[0] = (float)sin(a), v[1] = 0.0f, v[2] = (float)cos(a);
    v[0] *= cm_distance, v[1] *= cm_distance, v[2] *= cm_distance;
    v[1] -= cm_canter_unarmed;
    cam.cur.location[0] = c->pos[0] - v[0];
    cam.cur.location[1] = c->pos[1] - v[1];
    cam.cur.location[2] = c->pos[2] - v[2];
    cam.cur.location[1] = cm_height + cam.cur.location[1];
    normalize(v);
    build_up(v, cam.cur.up);
    return true;
}

/* CArFollow_Get_GoalPosition 0x4d7110: behind the target along its aim, at the distance, raised by the
   height and the canter, never lower than half a unit over the feet. Aiming up past 0.9 rad bends the
   direction toward straight up (so the camera drops). */
static void follow_goal(float out[3])
{
    const CameraTarget *c = &cam.target;
    float off[3] = { 0.0f, 0.0f, 0.0f };
    if (c->active) {
        float dir[3] = { c->aim[0], c->aim[1], c->aim[2] };
        if (c->aim_ud > 0.9f) {
            float t = (c->aim_ud - 0.9f) * 0.6707963943481445f;   /* 0x52b1ac */
            if (t < 0.0f) t = 0.0f;
            else if (t > 1.0f) t = 1.0f;
            static const float up[3] = { 0.0f, 1.0f, 0.0f };
            lerp(dir, up, t, dir);
            normalize(dir);
        }
        float k = -cam.distance;
        off[0] = k * dir[0], off[1] = k * dir[1], off[2] = k * dir[2];
    }
    out[0] = off[0] + c->pos[0];
    float y = cm_height + cam.canter + (off[1] + c->pos[1]);
    float floor = c->pos[1] + 0.5f;
    out[1] = y > floor ? y : floor;
    out[2] = off[2] + c->pos[2];
}

/* 0x4d72b0: the divisor of a smoothing step, 8 less 0.85/2 ln(|d|^2 k), at least 2 (the constants are
   the caller's: base, k, slope). */
static double step_divisor(const float d[3], float base, float k, float slope)
{
    double x = ((double)d[2] * d[2] + (double)d[1] * d[1] + (double)d[0] * d[0]) * k;
    if (x <= 1.0) x = 1.0;
    double r = base - log(x) * slope * 0.5;
    return r <= 2.0 ? 2.0 : r;
}

/* 0x4d7320: p moved toward to by 1/divisor of the way. */
static void approach(float p[3], const float to[3], float base, float k, float slope)
{
    float d[3] = { to[0] - p[0], to[1] - p[1], to[2] - p[2] };
    double inv = 1.0 / step_divisor(d, base, k, slope);
    p[0] = (float)(inv * d[0]) + p[0];
    p[1] = (float)(inv * d[1]) + p[1];
    p[2] = (float)(inv * d[2] + p[2]);
}

/* 0x4d66a0, the follow camera: the canter and distance ease toward what the aim asks, the location
   toward the goal and the focus toward the target's head, a step each tick. Then the jello, on every
   call (also with no ticks). */
static bool follow_update(uint32_t ticks)
{
    bool moved = false;
    if (ticks && cam.has_target) {
        const CameraTarget *c = &cam.target;
        if (c->active) {
            bool armed = c->armed && (c->variant & 0x7800) != 0;
            float goal = armed ? cm_canter_weapon : cm_canter_unarmed, pitch = c->aim_ud;
            /* aiming up lowers the canter (to a fifth at 1.2 rad), aiming down with a weapon raises it */
            if (pitch >= 1.2f) goal = (1.2f - (1.2f - 0.5f) * 1.428571343421936f) * goal;
            else if (pitch > 0.5f) goal = (1.2f - (pitch - 0.5f) * 1.428571343421936f) * goal;
            else if (armed && pitch < -0.5f) goal = ((-pitch - 0.5f) * 1.428571343421936f + 1.0f) * goal;
            float cur = cam.canter;
            if (cur < goal) {
                cur = (float)ticks * 0.12f + cur;
                cam.canter = goal > cur ? cur : goal;
            } else {
                cur = cur - (float)ticks * 0.12f;
                cam.canter = goal > cur ? goal : cur;
            }
            if (!armed || pitch > -0.5f) {
                cam.distance = cm_distance;
            } else {
                /* aiming down with a weapon: nearer, to half the distance at -1 rad */
                float t = pitch + 1.0f;
                if (t < 0.0f) t = 0.0f;
                cam.distance = (t + 0.5f) * cm_distance;
            }
        }
        float goal[3], head[3] = { c->pos[0], cm_height + c->pos[1], c->pos[2] };
        follow_goal(goal);
        if (cam.snap) {
            memcpy(cam.cur.location, goal, sizeof goal);
            for (int i = 0; i < 3; i++) cam.cur.view[i] = head[i] - goal[i];
            normalize(cam.cur.view);
            build_up(cam.cur.view, cam.cur.up);
        } else {
            float p[3], focus[3];
            memcpy(p, cam.cur.location, sizeof p);
            for (int i = 0; i < 3; i++) focus[i] = cam.distance * cam.cur.view[i] + cam.cur.location[i];
            for (uint32_t t = ticks; t; t--) {
                float d[3] = { goal[0] - p[0], goal[1] - p[1], goal[2] - p[2] };
                double inv = 1.0 / step_divisor(d, 8.0f, 1.0f, 0.85f);
                p[0] = (float)(inv * d[0]) + p[0];
                p[1] = (float)(inv * d[1]) + p[1];
                p[2] = (float)(p[2] + inv * d[2]);
                approach(focus, head, 8.0f, 3.0f, 0.85f);
            }
            memcpy(cam.cur.location, p, sizeof p);
            for (int i = 0; i < 3; i++) cam.cur.view[i] = focus[i] - p[i];
            normalize(cam.cur.view);
            build_up(cam.cur.view, cam.cur.up);
        }
        moved = true;
    }
    track_jello();
    return moved;
}

/* CArAnimate_Update 0x4d6580: the OBAN a step each tick; the camera at its matrix, looking along -z,
   up +y. Busy until the last frame. */
static bool animate_update(uint32_t ticks, uint32_t now)
{
    if (!ticks) return false;
    for (uint32_t t = ticks; t; t--) oban_update(&cam.anim, now);  /* PHrPhysics_Update_Animation 0x43a620 */
    float m[12];
    /* OBrAnim_Matrix 0x4395f0: the context's frame, its state word as the flags */
    if (!oban_matrix(cam.anim.oban, cam.anim.frame, (cam.anim.state & 2) != 0, m)) return false;
    static const float back[3] = { 0.0f, 0.0f, -1.0f }, up[3] = { 0.0f, 1.0f, 0.0f };
    memcpy(cam.cur.location, m + 9, sizeof cam.cur.location);
    mul_normal(m, back, cam.cur.view);
    mul_normal(m, up, cam.cur.up);
    ObanInfo info;
    oban_info(cam.anim.oban, &info);
    cam.busy = cam.anim.frame < (int)info.frames - 1;
    return true;
}

/* 0x4d7590, the debug fly camera: each input record moves and turns it. */
static bool manual_update(int n, const CameraManualInput *in)
{
    if (n <= 0 || !in) return false;
    float *loc = cam.cur.location, *v = cam.cur.view, *up = cam.cur.up;
    for (int i = 0; i < n; i++, in++) {
        float fwd = 5.0f * in->forward, str = 5.0f * in->strafe;
        float yaw = in->turn_x * -0.10471976548433304f, pitch = in->turn_y * -0.10471976548433304f;
        if (cam.fly) {
            yaw = yaw - in->look_x * 0.01666666753590107f;
            pitch = pitch - in->look_y * 0.01666666753590107f;
        }
        float side[3];
        cross(up, v, side);
        float k = 1.0f / sqrtf(side[2] * side[2] + side[1] * side[1] + side[0] * side[0]);
        side[0] *= k, side[1] *= k, side[2] *= k;
        loc[0] = side[0] * str + fwd * v[0] + loc[0];
        loc[1] = in->rise + loc[1];
        if (cam.fly) loc[1] = fwd * v[1] + loc[1];   /* the forward motion climbs only in fly mode */
        loc[2] = side[2] * str + fwd * v[2] + loc[2];
        if (pitch < 0.0f) pitch += TWO_PI;
        if (yaw < 0.0f) yaw += TWO_PI;
        rotate_about_axis(v, up, yaw, v);
        rotate_about_axis(v, side, pitch, v);
        k = 1.0f / sqrtf(v[2] * v[2] + v[1] * v[1] + v[0] * v[0]);
        v[0] *= k, v[1] *= k, v[2] *= k;
        build_up(v, up);
    }
    return true;
}

/* 0x4d7980: from the start view to the target view on a sine ease, location and view each a straight
   blend. Not busy once it arrives. */
static bool interpolate_update(uint32_t ticks)
{
    if (!ticks) return false;
    cam.tick += ticks;
    if (cam.tick >= cam.ticks) {
        cam.tick = cam.ticks;
        cam.busy = false;
    }
    double a = ((double)cam.tick / (double)cam.ticks) * PI_F - HALF_PI;
    if (a < 0.0) a += TWO_PI;
    float t = (float)((sin(a) + 1.0) * 0.5);
    lerp(cam.from.location, cam.to.location, t, cam.cur.location);
    lerp(cam.from.view, cam.to.view, t, cam.cur.view);
    normalize(cam.cur.view);
    build_up(cam.cur.view, cam.cur.up);
    return true;
}

/* ---- the entries ---- */

void camera_init(void)
{
    memset(&cam, 0, sizeof cam);
    m3_set = false;
    n_jello_cur = n_jello_old = 0;
}

void camera_set_environment(void *env)
{
    jello_env = env;
    n_jello_cur = n_jello_old = 0;
}

void camera_set_target(const CameraTarget *t)
{
    cam.has_target = t != NULL;
    if (t) cam.target = *t;
}

void camera_set_cut_hook(void (*fn)(void)) { cut_hook = fn; }

void camera_reset(void)
{
    cam.angle = cam.stop = cam.speed = 0.0f;
    cam.snap = false;
    cam.canter = cam.has_target && cam.target.armed ? cm_canter_weapon : cm_canter_unarmed;
    cam.distance = cm_distance;
    cam.mode = CAMERA_FOLLOW;
    if (cam.has_target) {
        const CameraTarget *c = &cam.target;
        float head[3] = { c->pos[0], cm_height + c->pos[1], c->pos[2] }, goal[3];
        follow_goal(goal);
        memcpy(cam.cur.location, goal, sizeof goal);
        for (int i = 0; i < 3; i++) cam.cur.view[i] = head[i] - goal[i];
        normalize(cam.cur.view);
        build_up(cam.cur.view, cam.cur.up);
    }
    cam.busy = false;
}

void camera_orbit(float speed, float stop)
{
    cam.angle = 0.0f;
    cam.stop = stop;
    cam.speed = speed;
    cam.mode = CAMERA_ORBIT;
    cam.busy = false;   /* so cm_orbit_block never waits for an orbit that stops */
}

bool camera_animate(const void *oban, uint32_t now)
{
    if (!oban) return false;
    oban_player_init(&cam.anim, oban);
    /* 0x4394e0 then OBrAnim_Start 0x439580: both pick the start frame (a random one twice, for the
       animations that start anywhere) */
    oban_start(&cam.anim, now);
    oban_start(&cam.anim, now);
    cam.mode = CAMERA_ANIMATE;
    cam.busy = true;
    return true;
}

void camera_interpolate(const float m[12], uint32_t ticks)
{
    static const float back[3] = { 0.0f, 0.0f, -1.0f }, up[3] = { 0.0f, 1.0f, 0.0f };
    memcpy(cam.to.location, m + 9, sizeof cam.to.location);
    mul_normal(m, back, cam.to.view);
    mul_normal(m, up, cam.to.up);
    build_up(cam.to.view, cam.to.up);   /* the matrix's up is replaced by the level one */
    cam.from = cam.cur;
    cam.mode = CAMERA_INTERPOLATE;
    cam.tick = 0;
    cam.ticks = ticks;
    cam.busy = true;
}

void camera_interpolate_to(const CameraView *v, uint32_t ticks)
{
    cam.to = *v;
    cam.from = cam.cur;
    cam.mode = CAMERA_INTERPOLATE;
    cam.tick = 0;
    cam.ticks = ticks;
    cam.busy = true;
}

void camera_detach(void)
{
    /* an interpolation from where it is to where it is, one tick long */
    cam.to = cam.cur;
    cam.from = cam.cur;
    cam.mode = CAMERA_INTERPOLATE;
    cam.tick = 0;
    cam.ticks = 1;
    cam.busy = true;
}

void camera_manual(void)
{
    cam.fly = false;
    cam.mode = CAMERA_MANUAL;
    cam.busy = false;
}

void camera_manual_toggle(void)
{
    if (cam.mode != CAMERA_MANUAL) camera_manual();
    else if (cam.fly) camera_reset();
    else cam.fly = true;
}

void camera_enable_jello(bool on) { jello_on = on; }
bool camera_busy(void) { return cam.busy; }
int camera_mode(void) { return cam.mode; }

int camera_jello_quads(const uint32_t **quads)
{
    *quads = jello_cur;
    return n_jello_cur;
}

static bool run_mode(uint32_t ticks, int n, const CameraManualInput *in, uint32_t now)
{
    switch (cam.mode) {
    case CAMERA_ORBIT: return orbit_update(ticks);
    case CAMERA_FOLLOW: return follow_update(ticks);
    case CAMERA_ANIMATE: return animate_update(ticks, now);
    case CAMERA_MANUAL: return manual_update(n, in);
    case CAMERA_INTERPOLATE: return interpolate_update(ticks);
    }
    return false;
}

void camera_update(uint32_t ticks, int n, const CameraManualInput *inputs, uint32_t now)
{
    /* The original's update takes the ticks and the input records together; every mode but the manual
       one reads only the ticks, the manual one only the records. Recording and playing a camera film
       (ONrGameState +0xf8, +0xf9: camera_record, camera_play) is left out. */
    sl_now = now;
    jello_begin();
    if (run_mode(ticks, n, inputs, now)) {
        m3 = cam.cur;   /* 0x41b3f0 */
        m3_set = true;
    }
    jello_end();
}

bool camera_view(CameraView *v)
{
    if (m3_set) *v = m3;
    return m3_set;
}

bool camera_view_matrix(float m[16])
{
    if (!m3_set) return false;
    float f[3] = { m3.view[0], m3.view[1], m3.view[2] }, s[3], u[3];
    normalize(f);
    cross(f, m3.up, s);
    normalize(s);
    cross(s, f, u);
    const float *e = m3.location;
    float v[16] = { s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0,
                    -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]), -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]),
                    f[0] * e[0] + f[1] * e[1] + f[2] * e[2], 1 };
    memcpy(m, v, sizeof v);
    return true;
}

/* ---- the script commands ---- */

static float num(const SlValue *v) { return v->type == SL_FLOAT ? v->f : (float)v->i; }
static int32_t inum(const SlValue *v) { return v->type == SL_FLOAT ? (int32_t)v->f : v->i; }

/* The original reports "failed to load object animation %s." (0x54ac14) as a script error; the port's
   script engine has no error report for commands yet, so the command just ends. */
static void report(const char *name) { (void)name; }

/* 0x4bbfe0, 0x4bc060: the OBAN's frame 0, the ticks at least 1. The _block form waits until the camera
   is free first: it doesn't wait for its own move (cm_wait does). */
static bool cmd_interpolate(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    bool block = u != NULL;
    if (block && camera_busy()) return false;
    const char *name = argc > 0 && argv[0].type == SL_STRING ? argv[0].s : NULL;
    const void *oban = name ? tm_instance_get(OBAN_TAG, name) : NULL;   /* 0x423420 */
    if (!oban) { report(name); return true; }
    if (cut_hook) cut_hook();   /* 0x4f1f40 */
    float m[12];
    if (!oban_matrix(oban, 0, false, m)) return true;
    /* a missing count is read past the arguments in the original; 0 here, so 1 */
    int32_t ticks = argc > 1 ? inum(&argv[1]) : 0;
    camera_interpolate(m, ticks > 1 ? (uint32_t)ticks : 1u);
    return true;
}

/* 0x4bc100, 0x4bc160: the cam_spec ("move", "look", "both") is never read. */
static bool cmd_anim(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    bool block = u != NULL;
    if (block && camera_busy()) return false;
    const char *name = argc > 1 && argv[1].type == SL_STRING ? argv[1].s : NULL;
    const void *oban = name ? tm_instance_get(OBAN_TAG, name) : NULL;
    if (!oban) { report(name); return true; }   /* (an error result to the script) */
    camera_animate(oban, sl_now);
    return true;
}

/* 0x4bc210, 0x4bc250: degrees a tick, the stop angle in degrees (-1, none, if not given). */
static bool cmd_orbit(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    bool block = u != NULL;
    if (block && camera_busy()) return false;
    float speed = argc > 0 ? num(&argv[0]) : 0.0f, stop = argc > 1 ? num(&argv[1]) : -1.0f;
    camera_orbit(speed * DEG, stop * DEG);
    return true;
}

static bool cmd_detach(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u) { camera_detach(); return true; }      /* 0x4bbfd0 */
static bool cmd_wait(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u) { return !camera_busy(); }               /* 0x4bc1d0 */

/* 0x4bc200: the arguments ([maxspeed] [maxFocalAccel]) are never read. */
static bool cmd_reset(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    if (cut_hook) cut_hook();
    camera_reset();
    return true;
}

/* 0x4d5b30 */
static bool cmd_jello(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    jello_on = argc > 0 && inum(&argv[0]) > 0;
    return true;
}

void camera_register_commands(void)
{
    static int block = 1;
    sl_register_bool("cm_plane_test", &cm_plane_test);
    sl_register_float("cm_height", &cm_height);
    sl_register_float("cm_distance", &cm_distance);
    sl_register_float("cm_canter_weapon", &cm_canter_weapon);
    sl_register_float("cm_canter_unarmed", &cm_canter_unarmed);
    sl_register_float("cm_jello_radius", &cm_jello_radius);
    sl_register_int("cm_jello_amt", &cm_jello_amt);
    sl_register("cm_jello", cmd_jello, NULL);
    sl_register("cm_anim", cmd_anim, NULL);
    sl_register("cm_anim_block", cmd_anim, &block);
    sl_register("cm_interpolate", cmd_interpolate, NULL);
    sl_register("cm_interpolate_block", cmd_interpolate, &block);
    sl_register("cm_orbit", cmd_orbit, NULL);
    sl_register("cm_orbit_block", cmd_orbit, &block);
    sl_register("cm_detach", cmd_detach, NULL);
    sl_register("cm_wait", cmd_wait, NULL);
    sl_register("cm_reset", cmd_reset, NULL);
}
