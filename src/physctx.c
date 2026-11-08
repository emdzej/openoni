/* The physics contexts against each other: see physctx.h. */
#include "physctx.h"

#include "akev_collide.h"
#include "exe.h"
#include "m3gm.h"

#include <math.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

enum { SOURCES_MAX = 8 };
static PhysSource sources[SOURCES_MAX];
static int nsources;

void physctx_add_source(PhysSource src)
{
    for (int i = 0; i < nsources; i++)
        if (sources[i].count == src.count && sources[i].get == src.get) return;
    if (nsources < SOURCES_MAX) sources[nsources++] = src;
}

/* ---- the maths ---- */

static float dot(const float a[3], const float b[3]) { return a[2] * b[2] + a[1] * b[1] + a[0] * b[0]; }

static float dist2(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

/* 0x4340f0: unit length, unless within 2^-20 of zero (true then, the vector kept). */
static bool normalize_or_zero(float v[3])
{
    float l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (fabsf(l2) < 9.5367431640625e-07f) return true;      /* 0x529d00 */
    float k = 1.0f / sqrtf(l2);
    v[0] *= k, v[1] *= k, v[2] *= k;
    return false;
}

/* 0x433050 (camera.c's normalize): within 1e-20 of zero, scaled by 1e20 first. */
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

/* 0x433bd0: the normal of the triangle a, b, c ((b - a) x (c - a), unit; (1, 0, 0) for none). */
static void tri_normal(const float a[3], const float b[3], const float c[3], float n[3])
{
    float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    float x = v[2] * u[1] - u[2] * v[1], y = u[2] * v[0] - u[0] * v[2], z = u[0] * v[1] - v[0] * u[1];
    float l2 = x * x + y * y + z * z;
    if (l2 == 0.0f) {
        n[0] = 1.0f, n[1] = n[2] = 0.0f;
        return;
    }
    float k = 1.0f / sqrtf(l2);
    n[0] = x * k, n[1] = y * k, n[2] = z * k;
}

/* CLrSphere_Line 0x43c7a0: the segment a to b meets the sphere: an end inside it, or the nearest point
   strictly between them. */
static bool sphere_line(const float a[3], const float b[3], const float s[4])
{
    float r2 = s[3] * s[3];
    if (dist2(a, s) < r2 || dist2(b, s) < r2) return true;
    float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, p[3] = { s[0] - a[0], s[1] - a[1], s[2] - a[2] };
    float t = dot(p, d) / dot(d, d);    /* 0x432d70 */
    if (!(t > 0.0f) || !(t < 1.0f)) return false;
    float at[3] = { d[0] * t + a[0], d[1] * t + a[1], d[2] * t + a[2] };     /* 0x432df0 */
    return dist2(at, s) < r2;
}

/* 0x43f680: sphere a moving by mv meets sphere b: the move's line within b's radius of b's centre, or
   either end within the radii's sum. */
static bool spheres_meet(const float a[4], const float mv[3], const float b[4])
{
    float e[3] = { a[0] + mv[0], a[1] + mv[1], a[2] + mv[2] };
    if (sphere_line(a, e, b)) return true;
    float r = a[3] + b[3];
    r *= r;
    return dist2(a, b) < r || dist2(e, b) < r;
}

/* ---- the trees ---- */

static int child(const PhysTree *t, int i) { return t->links ? t->links[i][0] : -1; }
static int sibling(const PhysTree *t, int i) { return t->links ? t->links[i][1] : -1; }

/* PHrCollision_SphereTree_SphereTree 0x43a560: down a's tree first, then b's, to a pair of leaves that
   meet. */
static bool trees_meet(const PhysTree *a, int ia, const float mv[3], const PhysTree *b, int ib)
{
    if (!spheres_meet(a->s[ia], mv, b->s[ib])) return false;
    int k = child(a, ia);
    if (k >= 0) {
        for (; k >= 0; k = sibling(a, k))
            if (trees_meet(a, k, mv, b, ib)) return true;
        return false;
    }
    k = child(b, ib);
    if (k < 0) return true;
    for (; k >= 0; k = sibling(b, k))
        if (trees_meet(a, ia, mv, b, k)) return true;
    return false;
}

/* 0x43a240's descent: the node met by the segment, then a child of it that is (a leaf: itself). */
static int point_tree(const float p[3], const float mv[3], const PhysTree *t, int node)
{
    const float e[3] = { p[0] + mv[0], p[1] + mv[1], p[2] + mv[2] };
    if (!sphere_line(p, e, t->s[node])) return -1;
    int k = child(t, node);
    if (k < 0) return node;
    for (; k >= 0; k = sibling(t, k)) {
        int r = point_tree(p, mv, t, k);
        if (r >= 0) return r;
    }
    return -1;
}

/* 0x43a560's descent with a lone sphere: the node met by the sphere's move, then a child of it. */
static int sphere_tree(const float s[4], const float mv[3], const PhysTree *t, int node)
{
    if (!spheres_meet(s, mv, t->s[node])) return -1;
    int k = child(t, node);
    if (k < 0) return node;
    for (; k >= 0; k = sibling(t, k)) {
        int r = sphere_tree(s, mv, t, k);
        if (r >= 0) return r;
    }
    return -1;
}

/* 0x43db00: the sphere against a box's face (static): its plane within the radius, then a corner within
   it, the centre's projection inside the face, or an edge within it. The point: the projection. */
static bool face_sphere(const PhysBox *box, int f, const float s[4], float point[3])
{
    const float *pl = box->planes[f];
    float d = pl[2] * s[2] + pl[1] * s[1] + pl[0] * s[0] + pl[3];
    if (!(fabsf(d) <= s[3])) return false;
    for (int i = 0; i < 3; i++) point[i] = s[i] - d * pl[i];
    float c[4][3];
    for (int k = 0; k < 4; k++) memcpy(c[k], box->corners[box->faces[f][k]], sizeof c[k]);
    float r2 = s[3] * s[3];
    for (int k = 0; k < 4; k++)
        if (dist2(c[k], s) < r2) return true;
    if (akev_corners_inside(c, box->axis[f], point)) return true;
    for (int k = 0; k < 4; k++)
        if (sphere_line(c[k], c[(k + 1) & 3], s)) return true;
    return false;
}

/* PHrCollision_Volume_SphereTree 0x43a2d0: the tree's node against the box's faces; a face the move
   leaves (less than a right angle from its normal) skipped, unless still (flag: an animated box on the
   first pass, a move under 1/150). A face hit goes down to the children: one of them must hit too, or
   this node's hits go. */
static bool volume_tree(const PhysBox *box, const float mv[3], bool flag, const PhysTree *t, int node,
                        const PhysCtx *other, PhysHit *hits, unsigned *n, unsigned cap)
{
    float u[3] = { mv[0], mv[1], mv[2] };
    float s = sqrtf(u[2] * u[2] + u[1] * u[1] + u[0] * u[0]);
    bool check = !flag || !(fabsf(s) < 0.0066666667f);     /* 0x529da8 */
    if (check) {
        if (s == 0.0f) return false;
        float k = 1.0f / s;
        u[0] *= k, u[1] *= k, u[2] *= k;
    }
    unsigned entry = *n;
    bool any = false;
    for (int f = 0; f < 6; f++) {
        float p[3];
        if (!face_sphere(box, f, t->s[node], p)) continue;
        if (check) {
            float c = dot(u, box->planes[f]);   /* 0x433e00 */
            if (c < -1.0f) c = -1.0f;
            else if (c > 1.0f) c = 1.0f;
            /* (the CRT's acos 0x520210 in double: a face along the move, at pi/2, is under the float
               constant and skipped too) */
            if (acos((double)c) < (double)1.5707964f) continue;    /* 0x529cd0 */
        }
        any = true;
        if (*n < cap) {
            PhysHit *h = &hits[(*n)++];
            memset(h, 0, sizeof *h);
            h->kind = 1;
            h->other = *other;
            memcpy(h->plane, box->planes[f], sizeof h->plane);
            memcpy(h->point, p, sizeof h->point);
        }
    }
    if (!any) return false;
    int k = child(t, node);
    if (k < 0) return true;
    for (; k >= 0; k = sibling(t, k))
        if (volume_tree(box, mv, flag, t, k, other, hits, n, cap)) return true;
    *n = entry;
    return false;
}

/* PHrPhysics_Single_PhyCollision 0x43ba70: the mover a against b. */
static void single(const PhysCtx *a, const PhysCtx *b, const float mv[3], PhysHit *hits, unsigned *n, unsigned cap)
{
    if (!(b->flags & 2) || !(a->flags & 2)) return;
    if (a->owner == b->owner && a->kind == b->kind) return;
    if ((b->flags & PHYS_INACTIVE_OTHER) || (a->flags & 4) || (b->flags & 4)) return;
    bool flag = b->type == 3 && (a->flags & PHYS_FIRST_PASS);
    const float *ra = a->tree.s[a->tree.root], *rb = b->tree.s[b->tree.root];
    /* a's flag 0x20 (a point against b's tree, 0x43a240): set by nothing the port has */
    if (a->flags & PHYS_POINT) return;
    if (b->flags & PHYS_VOLUME) {
        float r = rb[3] + ra[3];
        if (r * r < dist2(ra, rb) || !b->box) return;
        volume_tree(b->box, mv, flag, &a->tree, a->tree.root, b, hits, n, cap);
        return;
    }
    if (!trees_meet(&a->tree, a->tree.root, mv, &b->tree, b->tree.root) || *n >= cap) return;
    PhysHit *h = &hits[(*n)++];
    memset(h, 0, sizeof *h);
    h->kind = 1;
    h->other = *b;
    float v[3] = { ra[0] - rb[0], ra[1] - rb[1], ra[2] - rb[2] };
    if (normalize_or_zero(v)) {
        h->plane[1] = 1.0f;
        h->plane[3] = -ra[1];
        memcpy(h->point, ra, sizeof h->point);
        return;
    }
    for (int i = 0; i < 3; i++) {
        h->plane[i] = v[i];
        h->point[i] = ((rb[i] + rb[3] * v[i]) + (ra[i] - ra[3] * v[i])) * 0.5f;    /* 0x529560 */
    }
    h->plane[3] = -dot(h->point, v);
    /* OBrNotifyPhysicsCollision (a kind 2 object with flag 0x80): no object of the data has it */
}

unsigned physctx_find(const PhysCtx *mover, const float mv[3], bool (*skip)(const PhysCtx *mover, const PhysCtx *other),
                      PhysHit *hits, unsigned n, unsigned cap)
{
    for (int s = 0; s < nsources; s++) {
        int count = sources[s].count();
        for (int i = 0; i < count; i++) {
            PhysCtx o;
            if (!sources[s].get(i, &o)) continue;
            if (!(o.flags & 2) || (o.owner == mover->owner && o.kind == mover->kind) || (o.flags & PHYS_INACTIVE_OTHER)) continue;
            if (skip && skip(mover, &o)) continue;
            single(mover, &o, mv, hits, &n, cap);
        }
    }
    return n;
}

/* ---- an object's box ---- */

void physctx_box_make(PhysBox *box, const void *const *geometries, int n, const float pos[3], float *radius)
{
    memset(box, 0, sizeof *box);
    /* the bounds of the geometries' points (their PNTA's own); a geometry of one face normal sets the
       low x to its plane's distance (0x41f0e0 makes a slab of it into the corners, which the bounds'
       corners then overwrite) */
    float b[6] = { 0, 0, 0, 0, 0, 0 };
    for (int g = 0; g < n; g++) {
        float gb[6], sph[4];
        if (!geometries[g] || !m3gm_bounds(geometries[g], gb, sph)) continue;
        if (!g) memcpy(b, gb, sizeof b);
        else
            for (int k = 0; k < 3; k++) {
                if (gb[k] <= b[k]) b[k] = gb[k];
                if (gb[3 + k] > b[3 + k]) b[3 + k] = gb[3 + k];
            }
        uint32_t nn = 0, np = 0;
        const uint8_t *fn = m3gm_face_normals(geometries[g], &nn), *pt = m3gm_points(geometries[g], &np);
        if (nn == 1 && fn && pt && np) b[0] = -(rdf(fn + 8) * rdf(pt + 8) + rdf(fn + 4) * rdf(pt + 4) + rdf(fn) * rdf(pt));
    }
    /* 0x41efc0: the corners, x slowest */
    for (int i = 0; i < 8; i++) {
        box->local[i][0] = (i & 4) ? b[3] : b[0];
        box->local[i][1] = (i & 2) ? b[4] : b[1];
        box->local[i][2] = (i & 1) ? b[5] : b[2];
    }
    /* the sphere's radius: of each point, the largest of its coordinates' distances from the position
       so far, but kept as the coordinate's own size (the original's), at least 1 */
    float r = -1e9f;    /* 0x52963c */
    for (int g = 0; g < n; g++) {
        uint32_t np = 0;
        const uint8_t *pt = geometries[g] ? m3gm_points(geometries[g], &np) : NULL;
        for (uint32_t i = 0; pt && i < np; i++)
            for (int k = 0; k < 3; k++) {
                float c = rdf(pt + 12 * i + 4 * k);
                if (r < fabsf(c - pos[k])) r = fabsf(c);
            }
    }
    *radius = 1.0f <= r ? r : 1.0f;
    /* 0x41f050: the faces (the exe's table, 0x529660) and their normals as made */
    const uint8_t *ft = exe_data(0x529660, sizeof box->faces);
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < 4; k++) box->faces[f][k] = ft ? (int32_t)rd32(ft + 16 * f + 4 * k) & 7 : 0;
    for (int f = 0; f < 6; f++)
        tri_normal(box->local[box->faces[f][0]], box->local[box->faces[f][1]], box->local[box->faces[f][2]], box->normals[f]);
}

void physctx_box_bounds(PhysBox *box, const float b[6], const float pos[3])
{
    memset(box, 0, sizeof *box);
    for (int i = 0; i < 8; i++) {      /* 0x41efc0, then moved (0x433b40) */
        box->local[i][0] = (i & 4) ? b[3] : b[0];
        box->local[i][1] = (i & 2) ? b[4] : b[1];
        box->local[i][2] = (i & 1) ? b[5] : b[2];
        for (int k = 0; k < 3; k++) box->corners[i][k] = box->local[i][k] + pos[k];
    }
    const uint8_t *ft = exe_data(0x529660, sizeof box->faces);
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < 4; k++) box->faces[f][k] = ft ? (int32_t)rd32(ft + 16 * f + 4 * k) & 7 : 0;
    for (int f = 0; f < 6; f++) {
        tri_normal(box->corners[box->faces[f][0]], box->corners[box->faces[f][1]], box->corners[box->faces[f][2]], box->normals[f]);
        float *pl = box->planes[f];
        memcpy(pl, box->normals[f], 3 * sizeof *pl);
        const float *q = box->corners[box->faces[f][0]];
        pl[3] = -(pl[0] * q[0] + pl[1] * q[1] + pl[2] * q[2]);
        float c[4][3];
        for (int k = 0; k < 4; k++) memcpy(c[k], box->corners[box->faces[f][k]], sizeof c[k]);
        box->axis[f] = (uint16_t)akev_corners_axis(c);
    }
}

bool physctx_box_ray(const PhysBox *box, const float p[3], const float v[3], float hit[3])
{
    /* the box convex: the crossing from the front that a segment from outside enters by is the last
       of the front faces' crossings within the back faces' */
    float t0 = 0.0f, t1 = 1.0f;
    int entered = -1;
    for (int f = 0; f < 6; f++) {
        const float *pl = box->planes[f];
        float dn = pl[0] * v[0] + pl[1] * v[1] + pl[2] * v[2];
        float dist = pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3];
        if (dn == 0.0f) {
            if (dist > 0.0f) return false;
            continue;
        }
        float t = -dist / dn;
        if (dn < 0.0f) {
            if (dist > 0.0f && t >= t0) t0 = t, entered = f;
        } else if (t < t1) {
            t1 = t;
        }
        if (t0 > t1) return false;
    }
    if (entered < 0) return false;
    for (int k = 0; k < 3; k++) hit[k] = p[k] + v[k] * t0;
    return true;
}

bool physctx_box_ray_normal(const PhysBox *box, const float p[3], const float v[3], float hit[3], float normal[3])
{
    float t0 = 0.0f, t1 = 1.0f;
    int entered = -1;
    for (int f = 0; f < 6; f++) {
        const float *pl = box->planes[f];
        float dn = pl[0] * v[0] + pl[1] * v[1] + pl[2] * v[2];
        float dist = pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3];
        if (dn == 0.0f) {
            if (dist > 0.0f) return false;
            continue;
        }
        float t = -dist / dn;
        if (dn < 0.0f) {
            if (dist > 0.0f && t >= t0) t0 = t, entered = f;
        } else if (t < t1) {
            t1 = t;
        }
        if (t0 > t1) return false;
    }
    if (entered < 0) return false;
    for (int k = 0; k < 3; k++) hit[k] = p[k] + v[k] * t0, normal[k] = box->planes[entered][k];
    return true;
}

bool physctx_box_sphere(const PhysBox *box, const float s[4], const float mv[3], bool flag, float point[3], float normal[3])
{
    static const int8_t none[1][2] = { { -1, -1 } };
    const float (*one)[4] = (const float (*)[4])s;
    PhysTree t = { one, none, 0 };
    PhysCtx other;
    memset(&other, 0, sizeof other);
    PhysHit h[6];
    unsigned n = 0;
    if (!volume_tree(box, mv, flag, &t, 0, &other, h, &n, 6) || !n) return false;
    memcpy(point, h[0].point, sizeof h[0].point);
    memcpy(normal, h[0].plane, 3 * sizeof(float));
    return true;
}

int physctx_point_tree(const float p[3], const float mv[3], const PhysTree *t)
{
    return point_tree(p, mv, t, t->root);
}

int physctx_sphere_tree(const float s[4], const float mv[3], const PhysTree *t)
{
    return sphere_tree(s, mv, t, t->root);
}

int physctx_collect(PhysCtx *out, int cap)
{
    int n = 0;
    for (int s = 0; s < nsources; s++) {
        int count = sources[s].count();
        for (int i = 0; i < count && n < cap; i++)
            if (sources[s].get(i, &out[n])) n++;
    }
    return n;
}

bool physctx_box_contains(const PhysBox *box, const float p[3])
{
    for (int f = 0; f < 6; f++) {
        const float *pl = box->planes[f];
        if (p[1] * pl[1] + pl[0] * p[0] + pl[2] * p[2] + pl[3] > 0.0f) return false;
    }
    return true;
}

void physctx_box_place(PhysBox *box, const float m[12])
{
    for (int i = 0; i < 8; i++)
        for (int r = 0; r < 3; r++)
            box->corners[i][r] = m[r] * box->local[i][0] + m[3 + r] * box->local[i][1] + m[6 + r] * box->local[i][2] + m[9 + r];
    for (int f = 0; f < 6; f++) {
        float *pl = box->planes[f];
        const float *n = box->normals[f];
        for (int r = 0; r < 3; r++) pl[r] = m[r] * n[0] + m[3 + r] * n[1] + m[6 + r] * n[2];    /* 0x433ae0 */
        normalize(pl);
        pl[3] = -dot(pl, box->corners[box->faces[f][0]]);
        float c[4][3];
        for (int k = 0; k < 4; k++) memcpy(c[k], box->corners[box->faces[f][k]], sizeof c[k]);
        box->axis[f] = (uint16_t)akev_corners_axis(c);     /* 0x43e3a0 */
    }
}
