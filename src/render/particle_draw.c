/* The particles' draw: see particle_draw.h. */
#include "particle_draw.h"

#include "geom_draw.h"
#include "gl_dev.h"
#include "particle.h"
#include "txmp.h"

#include <math.h>
#include <string.h>

typedef struct {
    float eye[3], right[3], up[3];
    uint32_t now;
} View;

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const float *a, const float *b, float *o)
{
    float r[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    memcpy(o, r, sizeof r);
}
static bool unit(float *v)
{
    float l = dot3(v, v);
    if (l < 1e-6f) return false;     /* 0x529558 */
    l = 1.0f / sqrtf(l);
    v[0] *= l, v[1] *= l, v[2] *= l;
    return true;
}
static const float *col(const float *m, int k) { return m + 3 * k; }

/* The sprite's two axes for its mode (MSrGeomContext_Method_Sprite_Draw's switch): 0 the camera's;
   1 the velocity (or the orientation's y) seen on the screen; 2 a beam along the velocity (or the
   orientation's y) turned to the camera; 3, 4 and 7 the orientation's columns (1 and 2, 0 and 2, 1 and
   0). False when the mode has nothing to orient it. */
static bool axes(const View *v, const P3Draw *d, float u[3], float w[3])
{
    switch (d->mode) {
    case 0:
        memcpy(u, v->right, 12), memcpy(w, v->up, 12);
        return true;
    case 1: {
        float a = 1.0f, b = 0.0f, dir[3];
        if (d->orient) memcpy(dir, col(d->orient, 1), 12);
        else if (d->velocity) memcpy(dir, d->velocity, 12);
        else return false;      /* "oriented particle supplied no orientation" */
        a = dot3(dir, v->right), b = dot3(dir, v->up);
        float l = a * a + b * b;
        if (l >= 1e-6f) l = sqrtf(l), a /= l, b /= l;
        else a = 1.0f, b = 0.0f;
        for (int k = 0; k < 3; k++) {
            u[k] = b * v->up[k] + a * v->right[k];
            w[k] = b * v->right[k] - a * v->up[k];
        }
        return true;
    }
    case 2: {
        if (d->orient) memcpy(u, col(d->orient, 1), 12);
        else if (d->velocity) {
            memcpy(u, d->velocity, 12);
            if (!unit(u)) u[0] = 0, u[1] = 1, u[2] = 0;
        } else return false;
        float to[3] = { d->pos[0] - v->eye[0], d->pos[1] - v->eye[1], d->pos[2] - v->eye[2] };
        cross3(u, to, w);
        return unit(w);
    }
    case 3: if (!d->orient) return false; memcpy(u, col(d->orient, 1), 12), memcpy(w, col(d->orient, 2), 12); return true;
    case 4: if (!d->orient) return false; memcpy(u, col(d->orient, 0), 12), memcpy(w, col(d->orient, 2), 12); return true;
    case 7: if (!d->orient) return false; memcpy(u, col(d->orient, 1), 12), memcpy(w, col(d->orient, 0), 12); return true;
    default: return false;     /* 5 and 6: contrails */
    }
}

static void sprite(const View *v, const P3Draw *d)
{
    const void *tex = d->texture;
    if (!tex) return;
    float u[3], w[3];
    if (!axes(v, d, u, w)) return;
    float alpha = d->alpha;
    /* the edge fade (flags2 0x4000000): by the angle to the eye, against the axis of the mode */
    if (d->edge_fade && d->mode != 5 && d->mode != 6) {
        float to[3] = { v->eye[0] - d->pos[0], v->eye[1] - d->pos[1], v->eye[2] - d->pos[2] }, ax[3];
        unit(to);
        if (d->mode <= 2 && !d->orient) {
            if (!d->velocity) goto no_fade;
            memcpy(ax, d->velocity, 12), unit(ax);
        } else if (d->orient) memcpy(ax, col(d->orient, d->mode == 3 ? 0 : d->mode == 4 ? 1 : 2), 12);
        else goto no_fade;
        float c = dot3(to, ax);
        if (!d->one_sided) c = fabsf(c);
        if (d->mode == 2) c = 1.0f - c;
        if (c < d->edge_min) return;
        if (c < d->edge_max) alpha *= (c - d->edge_min) / (d->edge_max - d->edge_min);
    }
no_fade:
    if (d->rotation != 0.0f) {
        /* by the rotation (degrees) about the sprite's normal, wrapped as the original wraps it: the
           sine from the cosine, negative past a half turn */
        float r = d->rotation * 0.017453292f;
        int32_t n = (int32_t)lrintf(r * 0.15915494f);
        r = r < 0.0f ? r + (float)(1 - n) * 6.2831855f : r - (float)n * 6.2831855f;
        float cs = cosf(r), sn = sqrtf(1.0f - cs * cs);
        if (r > 3.1415927f) sn = -sn;
        float nu[3], nw[3];
        for (int k = 0; k < 3; k++) nu[k] = cs * u[k] - sn * w[k], nw[k] = sn * u[k] + cs * w[k];
        memcpy(u, nu, 12), memcpy(w, nw, 12);
    }
    float sx = d->scale, sy = d->yscale;
    for (int k = 0; k < 3; k++) u[k] *= sx, w[k] *= sy;
    /* the x offset moves it along u, the shorten (and the chop over the life) cuts its far end */
    float cut = d->x_shorten, f1 = (1.0f - cut) * (d->x_offset - 1.0f), f2 = (1.0f - cut) * (d->x_offset + 1.0f);
    float u0 = (d->x_offset - 1.0f) * cut * -0.5f, u1 = 1.0f - (d->x_offset + 1.0f) * cut * 0.5f;
    if (d->chop != 0.0f) {
        f2 -= d->chop + d->chop;
        if (f2 <= f1) return;
        u1 -= d->chop;
    }
    GlVertex q[4];
    const float fs[4] = { f1, f2, f1, f2 }, ws[4] = { -1, -1, 1, 1 }, us[4] = { u0, u1, u0, u1 }, vs[4] = { 0, 0, 1, 1 };
    int a8 = (int)lrintf(alpha * 255.0f);
    a8 = a8 < 0 ? 0 : a8 > 255 ? 255 : a8;
    for (int i = 0; i < 4; i++) {
        q[i].x = d->pos[0] + fs[i] * u[0] + ws[i] * w[0];
        q[i].y = d->pos[1] + fs[i] * u[1] + ws[i] * w[1];
        q[i].z = d->pos[2] + fs[i] * u[2] + ws[i] * w[2];
        q[i].u = us[i], q[i].v = vs[i];
        q[i].r = (uint8_t)(d->tint >> 16), q[i].g = (uint8_t)(d->tint >> 8), q[i].b = (uint8_t)d->tint, q[i].a = (uint8_t)a8;
    }
    static const uint16_t idx[6] = { 0, 1, 3, 0, 3, 2 };
    const void *t = txmp_resolve(tex, v->now - d->tex_time, d->tex_index);
    const uint8_t *tb = t;
    bool additive = tb && (tb[0x81] & 0x04);   /* TXMP +0x80 0x400 (0x40d640) */
    gl_dev_draw(q, 4, idx, 6, gl_dev_texture(t), additive ? GL_DEV_ADD : GL_DEV_BLEND);
}

static void geometry(const P3Draw *d)
{
    if (!d->texture) return;
    float m[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, d->pos[0], d->pos[1], d->pos[2] };
    if (d->orient) memcpy(m, d->orient, 36);
    for (int k = 0; k < 9; k++) m[k] *= d->scale;
    geom_draw(d->texture, m, NULL);
}

static void each(const P3Draw *d, void *user)
{
    const View *v = user;
    if (d->geometry) geometry(d);
    else if (!d->vector) sprite(v, d);
}

void particle_draw(const float view[16], uint32_t now)
{
    View v;
    /* the camera's axes and eye from the view matrix (rows of its rotation; eye = -R^T t) */
    for (int k = 0; k < 3; k++) v.right[k] = view[4 * k], v.up[k] = view[4 * k + 1];
    for (int k = 0; k < 3; k++) v.eye[k] = -(view[4 * k] * view[12] + view[4 * k + 1] * view[13] + view[4 * k + 2] * view[14]);
    v.now = now;
    gl_dev_cull(0);
    p3_each_drawn(each, &v);
}
