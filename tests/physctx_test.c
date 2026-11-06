/* The physics contexts against each other (physctx.c), on made-up contexts: a sphere moving into
   another gets one hit of kind 1 whose plane faces it, halfway between the spheres; one moving away or
   out of reach gets none; a sphere moving into a box meets the face it heads for, not those along the
   move (pi/2 from it: the double acos under the float constant), and a box of an animated context
   (type 3) is met by a still sphere only on the mover's first pass, with no face skipped. */
#include "physctx.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float spheres[2][4];
static PhysBox box;
static void receive(const PhysCtx *self, const PhysCtx *from, const float force[3]) { (void)self, (void)from, (void)force; }

/* One other context: the sphere 1, or the box with it as its sphere. */
static int mode;    /* 0 the sphere, 1 the box (type 0), 2 the box (type 3) */
static int count(void) { return 1; }
static bool get(int i, PhysCtx *out)
{
    (void)i;
    memset(out, 0, sizeof *out);
    out->kind = PHYS_KIND_OBJECT;
    out->flags = 2u | (mode ? PHYS_VOLUME : 0u);
    out->type = mode == 2 ? 3 : 0;
    out->tree.s = (const float (*)[4])spheres[1];
    out->box = &box;
    out->owner = &box;
    out->receive_force = receive;
    return true;
}

static bool skip_all(const PhysCtx *self, const PhysCtx *other) { (void)self, (void)other; return true; }

static PhysCtx mover(uint32_t extra)
{
    PhysCtx m;
    memset(&m, 0, sizeof m);
    m.kind = PHYS_KIND_CHARACTER;
    m.flags = 2u | extra;
    m.tree.s = (const float (*)[4])spheres[0];
    m.owner = spheres;
    return m;
}

static bool near(float a, float b) { return fabsf(a - b) < 1e-4f; }

int main(void)
{
    int failed = 0;
    physctx_add_source((PhysSource){ count, get });
    PhysHit hits[16];

    /* a sphere of 2 at x 0 moving +1 in x toward one of 2 at x 5 */
    const float a[4] = { 0, 0, 0, 2 }, b[4] = { 5, 0, 0, 2 };
    memcpy(spheres[0], a, sizeof a);
    memcpy(spheres[1], b, sizeof b);
    PhysCtx m = mover(0);
    float mv[3] = { 1.5f, 0, 0 };
    unsigned n = physctx_find(&m, mv, NULL, hits, 0, 16);
    bool ok = n == 1 && hits[0].kind == 1 && near(hits[0].plane[0], -1.0f) && near(hits[0].point[0], 2.5f) &&
              near(hits[0].plane[3], 2.5f) && hits[0].other.receive_force == receive;
    printf("physctx_test: sphere hit %u, plane %.2f %.2f %.2f %.2f, point %.2f\n", n, hits[0].plane[0], hits[0].plane[1],
           hits[0].plane[2], hits[0].plane[3], hits[0].point[0]);
    if (!ok) { fprintf(stderr, "physctx_test: the spheres' hit is wrong\n"); failed++; }
    mv[0] = 0.5f;
    if (physctx_find(&m, mv, NULL, hits, 0, 16) != 0) { fprintf(stderr, "physctx_test: a short move met it\n"); failed++; }

    /* a box from -1 to 1 about x 5 (its sphere the same as above): faces by hand, the normals out */
    mode = 1;
    memset(&box, 0, sizeof box);
    for (int i = 0; i < 8; i++) {
        box.local[i][0] = (i & 4) ? 6.0f : 4.0f;
        box.local[i][1] = (i & 2) ? 1.0f : -1.0f;
        box.local[i][2] = (i & 1) ? 1.0f : -1.0f;
    }
    static const int32_t faces[6][4] = { { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 } };
    static const float normals[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    memcpy(box.faces, faces, sizeof faces);
    memcpy(box.normals, normals, sizeof normals);
    static const float id[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
    physctx_box_place(&box, id);
    spheres[0][0] = 2.5f;   /* within 2 of the near face (x 4), moving toward it */
    mv[0] = 0.5f;
    n = physctx_find(&m, mv, NULL, hits, 0, 16);
    ok = n == 1 && near(hits[0].plane[0], -1.0f) && near(hits[0].plane[3], 4.0f) && near(hits[0].point[0], 4.0f);
    printf("physctx_test: box hits %u, plane %.2f %.2f %.2f %.2f\n", n, hits[0].plane[0], hits[0].plane[1], hits[0].plane[2], hits[0].plane[3]);
    if (!ok) { fprintf(stderr, "physctx_test: the box's hit is wrong\n"); failed++; }
    mv[0] = -0.5f;
    if (physctx_find(&m, mv, NULL, hits, 0, 16) != 0) { fprintf(stderr, "physctx_test: moving away met the box\n"); failed++; }

    /* still: a type 0 box isn't met; a type 3 one is, on the first pass */
    mv[0] = 0.0f;
    unsigned still0 = physctx_find(&m, mv, NULL, hits, 0, 16);
    mode = 2;
    unsigned still3 = physctx_find(&m, mv, NULL, hits, 0, 16);
    PhysCtx first = mover(PHYS_FIRST_PASS);
    unsigned still3f = physctx_find(&first, mv, NULL, hits, 0, 16);
    printf("physctx_test: still against a box: %u, animated %u, first pass %u\n", still0, still3, still3f);
    /* (no face skipped: the near one and the four whose edges the sphere reaches) */
    if (still0 || still3 || still3f != 5) { fprintf(stderr, "physctx_test: the still mover's box hits are wrong\n"); failed++; }

    /* a skip leaves it out */
    if (physctx_find(&first, mv, skip_all, hits, 0, 16) != 0) { fprintf(stderr, "physctx_test: the skip didn't\n"); failed++; }
    return failed ? 1 : 0;
}
