/* The particles' draw: see particle_draw.h. */
#include "particle_draw.h"

#include "geom_draw.h"
#include "gl_dev.h"
#include "particle.h"
#include "tm.h"
#include "weapon.h"
#include "txmp.h"
#include "ftol.h"

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

/* The alpha a draw takes (P3rDisplayClass_Sprite 0x4617cc, _Vector 0x461056, _Contrail 0x462316;
   _Geometry 0x460b21, whose alpha the port's geometry doesn't take), false when nothing is drawn: a
   lens flare faded in and out is then faded by the fog (the point's factor, gl_dev_fog_factor, when
   above 0: times it, truncated, at most 255; none left draws nothing); a fading particle's then takes
   its place. Only those flares: the fog of the rest is the draw engine's. */
static bool draw_alpha(const P3Draw *d, int32_t *alpha)
{
    int32_t a = d->alpha;
    if (d->fogged) {
        float f = gl_dev_fog_factor(d->pos);
        if (f > 0.0f) {
            a = ftol((double)(a & 0xffff) * (double)f);
            if ((uint16_t)a > 0xff) a = 0xff;
        }
        if (!(a & 0xffff)) return false;
    }
    if (d->fading) a = d->fade_alpha;
    *alpha = a;
    return true;
}

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
    int32_t alpha;
    if (!draw_alpha(d, &alpha)) return;
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
        if (c < d->edge_max)    /* rounded (0x4340a0) */
            alpha = (int32_t)lrintf((c - d->edge_min) * (float)(alpha & 0xffff) / (d->edge_max - d->edge_min));
    }
no_fade:
    if (d->rotation != 0.0f) {
        /* by the rotation (degrees) about the sprite's normal, wrapped as the original wraps it: the
           sine from the cosine, negative past a half turn */
        float r = d->rotation * 0.017453292f;
        int32_t n = (int16_t)ftol(r * 0.15915494f);     /* 0x52954c, 0x411fba: 16 bits */
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
    int a8 = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
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
    int mode = d->flare ? (additive ? GL_DEV_SCREEN_ADD : GL_DEV_SCREEN) : additive ? GL_DEV_ADD : GL_DEV_BLEND;
    gl_dev_draw(q, 4, idx, 6, gl_dev_texture(t), mode);
}

void sprite_draw_flat(const void *txmp, const float pos[3], const float m[12], float sx, float sy, uint8_t alpha,
                      uint32_t now)
{
    if (!txmp) return;
    View v;
    memset(&v, 0, sizeof v);
    v.now = now;
    P3Draw d;
    memset(&d, 0, sizeof d);
    memcpy(d.pos, pos, sizeof d.pos);
    d.orient = m;
    d.scale = sx, d.yscale = sy;
    d.alpha = alpha;
    d.tint = 0xffffff;
    d.texture = txmp;
    d.mode = 4;
    sprite(&v, &d);
}

static void geometry(const P3Draw *d)
{
    if (!d->texture) return;
    float m[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, d->pos[0], d->pos[1], d->pos[2] };
    if (d->orient) memcpy(m, d->orient, 36);
    for (int k = 0; k < 9; k++) m[k] *= d->scale;
    geom_draw(d->texture, m, NULL);
}

/* The contrail's quad (the draw engine's method +0x38, 0x412860): each end's place less and plus its
   half width, u 0 and 1 across, v the end's along; the ends' colours. */
static void contrail(const P3Contrail *s, void *user)
{
    const View *v = user;
    GlVertex q[4];
    const float *pos[4] = { s->a, s->a, s->b, s->b }, *w[4] = { s->wa, s->wa, s->wb, s->wb };
    const float sg[4] = { 1, -1, 1, -1 }, us[4] = { 0, 1, 0, 1 }, vs[4] = { s->va, s->va, s->vb, s->vb };
    for (int i = 0; i < 4; i++) {
        uint32_t c = i < 2 ? s->argb_a : s->argb_b;
        q[i].x = pos[i][0] + sg[i] * w[i][0];
        q[i].y = pos[i][1] + sg[i] * w[i][1];
        q[i].z = pos[i][2] + sg[i] * w[i][2];
        q[i].u = us[i], q[i].v = vs[i];
        q[i].r = (uint8_t)(c >> 16), q[i].g = (uint8_t)(c >> 8), q[i].b = (uint8_t)c, q[i].a = (uint8_t)(c >> 24);
    }
    static const uint16_t idx[6] = { 0, 1, 3, 0, 3, 2 };
    const void *t = txmp_resolve(s->texture, v->now - s->tex_time, s->tex_index);
    const uint8_t *tb = t;
    bool additive = tb && (tb[0x81] & 0x04);
    gl_dev_draw(q, 4, idx, 6, gl_dev_texture(t), additive ? GL_DEV_ADD : GL_DEV_BLEND);
}

/* P3rDisplayClass_Vector 0x460cf0: a line in the tint, at the alpha (the draw context's, 0x41e720). */
static void vector(const P3Draw *d)
{
    int32_t alpha;
    if (!draw_alpha(d, &alpha)) return;
    int a8 = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
    uint8_t r = (uint8_t)(d->tint >> 16), g = (uint8_t)(d->tint >> 8), b = (uint8_t)d->tint;
    GlVertex q[2] = {
        { d->pos[0], d->pos[1], d->pos[2], 0, 0, r, g, b, (uint8_t)a8 },
        { d->end[0], d->end[1], d->end[2], 0, 0, r, g, b, (uint8_t)a8 },
    };
    gl_dev_lines(q, 2, d->flare ? GL_DEV_SCREEN : GL_DEV_BLEND);
}

static void each(const P3Draw *d, void *user)
{
    const View *v = user;
    if (d->sky) gl_dev_fog_enable(false);
    if (d->geometry) geometry(d);
    else if (d->vector) vector(d);
    else sprite(v, d);
    if (d->sky) gl_dev_fog_enable(true);
}

static void view_of(View *v, const float view[16], uint32_t now)
{
    /* the camera's axes and eye from the view matrix (rows of its rotation; eye = -R^T t) */
    for (int k = 0; k < 3; k++) v->right[k] = view[4 * k], v->up[k] = view[4 * k + 1];
    for (int k = 0; k < 3; k++) v->eye[k] = -(view[4 * k] * view[12] + view[4 * k + 1] * view[13] + view[4 * k + 2] * view[14]);
    v->now = now;
}

void particle_draw(const float view[16], uint32_t now)
{
    View v;
    view_of(&v, view, now);
    gl_dev_cull(0);
    p3_each_drawn(each, &v);
    p3_each_contrail(v.eye, now, contrail, &v);
}

void particle_draw_flares(const float view[16], float near, uint32_t now)
{
    View v;
    view_of(&v, view, now);
    const float fwd[3] = { -view[2], -view[6], -view[10] };    /* the camera looks down its -z */
    gl_dev_cull(0);
    p3_each_flare(v.eye, fwd, near, now, each, &v);
}

/* WPrDrawSprite 0x4e1420: the sprite of a weapon class ({TXMP, colour, scale}) at pos, facing the camera
   (M3rSimpleSprite_Draw 0x41a7e0: the sprite mode 0), its colour, opaque, without writing the depth (the
   draw state 4 off); its size the scale times the mode's: 0 five, 1 the camera's distance to the 0.3
   (0x51f2c0) times 0.7, 2 the distance times 0.03 (as big on the screen at any distance). through: no
   depth test either. */
static void weapon_sprite(const View *v, const uint8_t *spr, const float pos[3], int mode, bool through)
{
    const void *tex = tm_link(spr);
    if (!tex) return;
    float e[3] = { v->eye[0] - pos[0], v->eye[1] - pos[1], v->eye[2] - pos[2] };
    float dist = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]), f;
    if (mode == 0) f = 5.0f;                                    /* 0x52956c */
    else if (mode == 1) f = powf(dist, 0.3f) * 0.7f;            /* 0x54e150, 0x52b2fc */
    else f = dist * 0.03f;                                      /* 0x52b2f0 */
    float scale;
    memcpy(&scale, spr + 8, 4);
    float size = 1.0f * scale * f;                              /* 0x54e14c */
    uint32_t colour;
    memcpy(&colour, spr + 4, 4);
    P3Draw d;
    memset(&d, 0, sizeof d);
    memcpy(d.pos, pos, sizeof d.pos);
    d.scale = d.yscale = size;
    d.alpha = 0xff;
    d.tint = colour & 0xffffff;
    d.texture = tex;
    d.mode = 0;
    d.flare = through;      /* (the screen modes: no depth test) */
    sprite(v, &d);
}

void sight_draw(const float view[16], const WeaponSight *s, uint32_t now)
{
    View v;
    view_of(&v, view, now);
    gl_dev_cull(0);
    const uint8_t *k = s->cls;
    uint32_t opts;
    memcpy(&opts, k + 0xcc, 4);
    if (opts & 0x8000) {
        /* FXrDrawLaser 0x4f4720: a ribbon from the gun to the end, untextured, in the class's colour
           (+0x1c), opaque, without writing the depth; across it the cross of the way from the camera to
           its start with the beam, 0.2 (0x5531b8) each side (at least 0.001 long, 0x529dfc) */
        uint32_t c;
        memcpy(&c, k + 0x1c, 4);
        c |= 0xff000000u;
        float a[3] = { s->from[0] - v.eye[0], s->from[1] - v.eye[1], s->from[2] - v.eye[2] };
        float b[3] = { s->to[0] - s->from[0], s->to[1] - s->from[1], s->to[2] - s->from[2] }, w[3];
        cross3(a, b, w);
        float l = sqrtf(dot3(w, w));
        if (l <= 0.001f) l = 0.001f;
        l = 0.2f / l;
        w[0] *= l, w[1] *= l, w[2] *= l;
        GlVertex q[4];
        const float *pos[4] = { s->to, s->to, s->from, s->from };
        const float sg[4] = { 1, -1, 1, -1 };
        for (int i = 0; i < 4; i++) {
            q[i].x = pos[i][0] + sg[i] * w[0], q[i].y = pos[i][1] + sg[i] * w[1], q[i].z = pos[i][2] + sg[i] * w[2];
            q[i].u = (float)(i & 1), q[i].v = i < 2 ? 0.0f : 1.0f;
            q[i].r = (uint8_t)(c >> 16), q[i].g = (uint8_t)(c >> 8), q[i].b = (uint8_t)c, q[i].a = (uint8_t)(c >> 24);
        }
        static const uint16_t idx[6] = { 0, 1, 3, 0, 3, 2 };
        gl_dev_draw(q, 4, idx, 6, gl_dev_white(), GL_DEV_BLEND);
    }
    /* the tunnel (+0x38): +0x44 sprites along the beam, +0x48 apart, the first a quarter of that from
       the gun (0x5296c4), none past the end */
    const void *tun = tm_link(k + 0x38);
    if (!tun) return;
    uint32_t n;
    float step;
    memcpy(&n, k + 0x44, 4);
    memcpy(&step, k + 0x48, 4);
    float e[3] = { s->to[0] - s->from[0], s->to[1] - s->from[1], s->to[2] - s->from[2] };
    float len = sqrtf(dot3(e, e));
    if (len <= 0.0f) return;
    float st[3] = { e[0] * (step / len), e[1] * (step / len), e[2] * (step / len) };
    float at = step * 0.25f, p[3] = { s->from[0] + e[0] * (at / len), s->from[1] + e[1] * (at / len), s->from[2] + e[2] * (at / len) };
    for (uint32_t i = 0; i < n; i++) {
        if (!(at + step <= len)) return;
        weapon_sprite(&v, k + 0x38, p, 0, false);
        at += step;
        p[0] += st[0], p[1] += st[1], p[2] += st[2];
    }
}

void sight_draw_crosshair(const float view[16], const WeaponSight *s, uint32_t now)
{
    /* 0x4f1fa0: the crosshair (+0x20, the locked one +0x2c on a living character) at the end, its size's
       mode 2 with the class's 0x10000, else 1 with 0x20000, else 0; without the fog; with 0x40000 seen
       through walls */
    View v;
    view_of(&v, view, now);
    gl_dev_cull(0);
    const uint8_t *k = s->cls;
    uint32_t opts;
    memcpy(&opts, k + 0xcc, 4);
    int mode = (opts & 0x10000) ? 2 : (int)((opts >> 17) & 1);
    gl_dev_fog_enable(false);
    weapon_sprite(&v, k + (s->kind == 1 ? 0x2c : 0x20), s->to, mode, (opts & 0x40000) != 0);
    gl_dev_fog_enable(true);
}
