/* The characters' levels of detail: see chr_lod.h. */
#include "chr_lod.h"
#include "sort.h"

#include "akev_vis.h"
#include "body.h"
#include "m3gm.h"
#include "persist.h"
#include "ui.h"

#include <math.h>
#include <string.h>

int32_t chr_lod_forced = -1;

static float oncc_float(const Character *c, unsigned off)
{
    const uint8_t *p = (const uint8_t *)c->oncc + off;
    uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    float f;
    memcpy(&f, &v, 4);
    return f;
}

bool chr_lod_visible(Character *c, const LodCamera *cam)
{
    float d[3] = { c->pos[0] - cam->pos[0], c->pos[1] - cam->pos[1], c->pos[2] - cam->pos[2] };
    c->camera_d2 = d[2] * d[2] + d[1] * d[1] + d[0] * d[0];     /* 0x4e1540 */
    /* 0x433050 */
    float len = sqrtf(d[2] * d[2] + d[1] * d[1] + d[0] * d[0]);
    if (d[0] > -1e-20f && d[0] < 1e-20f && d[1] > -1e-20f && d[1] < 1e-20f && d[2] > -1e-20f && d[2] < 1e-20f) {
        d[0] *= 1e20f, d[1] *= 1e20f, d[2] *= 1e20f;
        len = sqrtf(d[0] * d[0] + d[2] * d[2] + d[1] * d[1]);
    }
    float k = 1.0f / len;
    d[0] *= k, d[1] *= k, d[2] *= k;
    float cs = d[2] * cam->view[2] + d[1] * cam->view[1] + d[0] * cam->view[0];     /* 0x433e00 */
    cs = cs < -1.0f ? -1.0f : cs > 1.0f ? 1.0f : cs;
    /* (chr_draw_all_characters, 0x5ecba2, would skip the test.) In the cone and the range, the body's box
       (C+0x1648: the last one it had while active) must be in a leaf the octree shows (0x440bb0) */
    if (!(acos((double)cs) > (double)cam->fov + (double)0.39269909f) && !(cam->far * cam->far < c->camera_d2) &&   /* 0x52b304 */
        vis_box(c->box)) {
        c->unseen = 0;
        return true;
    }
    c->unseen++;
    return c->unseen < 4 && !c->inactive && c->lod_shown != 6;
}

/* The triangles of a class's body at a detail (its parts' face normals). */
static int triangles(const Character *c, int lod)
{
    lod = lod < 0 ? 0 : lod > 4 ? 4 : lod;
    const void *body = body_of_class(c->oncc, lod);
    int n = 0;
    for (uint32_t b = 0; body && b < body_bones(body); b++) {
        uint32_t k = 0;
        const void *g = body_geometry(body, b);
        if (g && m3gm_face_normals(g, &k)) n += (int)k;
    }
    return n;
}

static int total(Character **list, int n, int step)
{
    int t = 0;
    for (int i = 0; i < n; i++) t += triangles(list[i], list[i]->lod_base + step);
    return t;
}

/* The budget (0x5083f0), by the quality 0..4. */
static int budget(void)
{
    static const int most[5] = { 1500, 2200, 3000, 3400, 4000 };
    int q = persist_quality();
    return most[q < 0 ? 0 : q > 4 ? 4 : q];
}

/* 0x4e2e70, the sort's order (AUrQSort_32 0x437f40, from 0x4e298a): the player before all, then the
   nearer. */
static bool after(const void *pa, const void *pb, void *u)
{
    const Character *a = *(Character *const *)pa, *b = *(Character *const *)pb, *player = u;
    if (a == player) return false;
    if (b == player) return true;
    return a->camera_d2 > b->camera_d2;
}

void chr_lod_compute(Character **list, int n, const Character *player)
{
    if (chr_lod_forced >= 0 && chr_lod_forced <= 4) {
        for (int i = 0; i < n; i++) list[i]->lod_shown = list[i]->lod_wanted = chr_lod_forced, list[i]->lod_frames = 0;
        return;
    }
    au_qsort(list, (size_t)n, sizeof *list, after, (void *)player);
    /* each one's detail by the class's squared distances (+0x60, farthest first) */
    for (int i = 0; i < n; i++) {
        int k = 0;
        while (k < 4 && !(oncc_float(list[i], 0x60 + 4 * (unsigned)k) < list[i]->camera_d2)) k++;
        list[i]->lod_base = k;
    }
    /* all stepped down until within the budget, or up while within it */
    int most = budget(), step, t = total(list, n, 0);
    if (most < t) {
        for (step = -1; step > -5; step--)
            if ((t = total(list, n, step)) <= most) goto apply;
    } else {
        for (step = 1; step < 5; step++)
            if (most < (t = total(list, n, step))) break;
    }
    if (most < t) step--;
apply:
    for (int i = 0; i < n; i++) {
        Character *c = list[i];
        int k = c->lod_base + step;
        /* one more for the nearest first, while the budget takes it */
        int more = triangles(c, k + 1) - triangles(c, k) + t;
        if (more < most) k++, t = more;
        k = k < 0 ? 0 : k > 4 ? 4 : k;
        if (i == 0 && k < 3) k = 2;         /* the first (the player) at least 2, the next at least 1 */
        else if (i == 1 && k < 2) k = 1;
        if (k == c->lod_shown) c->lod_frames = 0;
        else c->lod_frames++;
        c->lod_wanted = k;
        int shown = c->lod_shown;
        if (shown == 6) c->lod_shown = k;
        else {
            /* held while being thrown or getting up from a fall (the animation's type 6 or 0x11 and its
               states 7, 0x12, 0x13, 0x1f or 0x32) */
            bool held = false;
            if (c->type == 6 || c->type == 0x11)
                switch (c->state) {
                case 7: case 0x12: case 0x13: case 0x1f: case 0x32: held = true; break;
                default: break;
                }
            if (!held && ((k < shown && c->lod_frames > 6) || (shown < k && c->lod_frames > 9))) c->lod_shown = k;
        }
        if (ui_cutscene() && c->lod_shown < 2) c->lod_shown = 1;    /* G+0x3c */
    }
}

int chr_lod_draw(const Character *c, float alpha)
{
    int k = c->lod_shown;
    if (alpha < 1.0f) k--;
    k = k < 0 ? 0 : k > 4 ? 4 : k;
    if (persist_quality() >= 4 && k <= 1) k = 1;    /* 0x508560 */
    return k;
}
