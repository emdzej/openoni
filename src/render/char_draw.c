/* Character drawing: see char_draw.h. */
#include "char_draw.h"

#include "akev_collide.h"
#include "body.h"
#include "character.h"
#include "chars.h"
#include "decal.h"
#include "exe.h"
#include "geom_draw.h"
#include "gl_dev.h"
#include "m3gm.h"
#include "particle.h"
#include "persist.h"
#include "tm.h"
#include "tram.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

const void *char_find_animation(const void *oncc, unsigned type)
{
    const uint8_t *c = tm_link((const uint8_t *)oncc + ONCC_ANIMATIONS);
    for (int depth = 0; c && depth < 8; depth++) {
        uint16_t n = rd16(c + 0x16);
        for (uint16_t i = 0; i < n; i++) {
            const uint8_t *t = tm_link(c + 0x18 + 12 * i + 8);
            if (t && rd16(t + TRAM_TYPE) == type && rd16(t + TRAM_VARIENT) == 0) return t;
        }
        c = tm_link(c + 0x10);      /* the collection it falls back to */
    }
    return NULL;
}

/* The body under its root, posed (the rest pose without quats). */
static void draw_root(const void *oncc, const BodyMatrix root, const float *quats)
{
    const void *body = body_of_class(oncc, 4);
    BodyMatrix m[BODY_MAX_BONES];
    if (!body || body_bones(body) > BODY_MAX_BONES || !body_matrices(body, root, quats, m)) return;
    for (uint32_t b = 0; b < body_bones(body); b++) geom_draw(body_geometry(body, b), m[b], body_texture(oncc, b));
}

void char_draw_pose(const void *oncc, const float pos[3], float facing, float height, const float *quats, uint32_t bones_posed)
{
    const void *body = body_of_class(oncc, 4);
    if (!body) return;
    uint32_t bones = body_bones(body);
    if (bones > BODY_MAX_BONES) return;
    if (quats && bones_posed != bones) quats = NULL, height = 0;
    float c = cosf(facing), s = sinf(facing);
    BodyMatrix root = { c, 0, -s, 0, 1, 0, s, 0, c, pos[0], pos[1] + height, pos[2] };
    draw_root(oncc, root, quats);
}

void char_draw(const void *oncc, const float pos[3], float facing, const void *tram, uint32_t frame)
{
    float q[4 * BODY_MAX_BONES], h = 0;
    uint32_t bones = 0;
    if (tram) {
        TramInfo in;
        tram_info(tram, &in);
        uint32_t f = frame % (in.frames ? in.frames : 1);
        if (in.bones <= BODY_MAX_BONES && tram_rotations(tram, f, q)) bones = in.bones, tram_height(tram, f, &h);
    }
    char_draw_pose(oncc, pos, facing, h, bones ? q : NULL, bones);
}

static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

/* The shadows' decals, by the characters' slots (A+0x27e8, its block from 0x4fdc50; A+0x27fe set while one
   is up). */
static uint32_t shadows[CHARS_MAX];
static bool shadows_on = true;          /* 0x54e18d, chr_shadows ("Enables character shadows") */

void char_shadows_clear(void)
{
    /* The characters' display 0x4e1540: every character's shadow goes before the bodies are drawn */
    for (int i = 0; i < CHARS_MAX; i++)
        if (shadows[i]) decal_delete(shadows[i]), shadows[i] = 0;
}

/* The end of ONiCharacter_Display_Body 0x4e1d70: the shadow, a decal (flags 0x1e: into the character's
   block of 0x800 bytes, facing quads, planar, unshaded) of the class's texture (ONCC+0x20, else
   "notfoundtex" 0x53b8c0) under the pelvis. On the ground it lies on the floor's quad at the floor's
   height (A+0xe8), of size ONCC+0x2c and alpha +0x38; in the air (or with no floor quad) a sphere of that
   size from the body's bottom less it (C+0x190, or the floor's height) falls by the size plus +0x24
   (AKrCollision_Sphere 0x441cb0, skipping 0x46800, the nearest): at a height h over the hit up to +0x28
   the size goes from +0x2c to +0x30 and the alpha from +0x38 to +0x3a, then to +0x24 from +0x30 to +0x34
   and the alpha from +0x3a to none; higher, none. The fade below 1 scales the alpha. */
static void shadow(const Character *c, int slot, float fade)
{
    if (persist_quality() < 1 || !shadows_on || (c->flags & 4) || fade <= 0.0f || (c->flags & 0x1000000)) return;
    if (slot < 0 || slot >= CHARS_MAX) return;
    const uint8_t *oc = c->oncc;
    const void *tex = tm_link(oc + 0x20);
    if (!tex) tex = tm_instance_get(TM_TAG("TXMP"), exe_str(0x53b8c0));
    if (!tex) return;
    float pos[3] = { c->bone_world[0][9], c->bone_world[0][10], c->bone_world[0][11] };
    const float normal[3] = { 0, 1, 0 }, b[3] = { 0, 0, 1 }, a[3] = { 1, 0, 0 };
    float far = rdf(oc + 0x24), fadeh = rdf(oc + 0x28), size = rdf(oc + 0x2c);
    int32_t quad = -1;
    float alpha;
    if (c->air_ticks == 0 && c->ground_quad != -1) {
        quad = c->ground_quad;
        pos[1] = c->ground_point[1];
        alpha = rd16(oc + 0x38);
    } else {
        float y = c->air_ticks == 0 ? c->ground_point[1] : c->bottom;
        const void *env = p3_environment();
        float sphere[4] = { pos[0], y - size, pos[2], size }, move[3] = { 0, -(size + far), 0 };
        static AkevHit hit[AKEV_HITS_MAX];
        if (!env || !akev_collide_sphere(env, sphere, move, 0x46800, 1, hit)) return;
        float h = y - hit[0].point[1];
        if (h < 0.0f) h = 0.0f;
        memcpy(pos, hit[0].point, sizeof pos);
        if (h < fadeh) {
            float t = h / fadeh;
            size = (rdf(oc + 0x30) - size) * t + size;
            alpha = (float)lrintf((float)(rd16(oc + 0x3a) - rd16(oc + 0x38)) * t + (float)rd16(oc + 0x38));
        } else if (h < far) {
            float t = (far - h) / (far - fadeh);
            size = (rdf(oc + 0x30) - rdf(oc + 0x34)) * t + rdf(oc + 0x34);
            alpha = (float)lrintf((float)rd16(oc + 0x3a) * t);
        } else return;
        quad = (int32_t)hit[0].quad;
    }
    if (fade < 1.0f) alpha = (float)lrintf(alpha * fade);
    /* (the wrap 0x3fb2b8c2, the tint white) */
    shadows[slot] = decal_create_into(tex, quad, 0x1e, 1.3962633609771729f, NULL, pos, normal, b, a, size, size,
                                      (uint16_t)alpha, 0xffffffffu, 0x800);
}

/* ONiCharacter_Display_Body 0x4e1d70's alpha: the body at A+0x2818 while invisible or a teleport fades it
   (C+4 0x40000; none drawn at 0), blended below 1 (255 times it); invisible, the INVIS texture (0x550e78)
   over the body scaled 1.4 about each bone (TRrBody_DrawMagic 0x427770) at (1 - it) x 56.1 (0x52b300). */
void char_draw_character(const Character *c, int slot)
{
    const void *body = body_of_class(c->oncc, 4);
    if (!body || body_bones(body) != CHR_BONES) {
        char_draw_pose(c->oncc, c->pos, c->facing, c->pelvis_height, c->aimed, CHR_BONES);
        return;
    }
    BodyMatrix m[BODY_MAX_BONES];
    if (!chr_pose_matrices(c, m)) return;     /* an environment animation's root, if one plays */
    float a = (c->invisible_shown || (c->flags & 0x40000)) ? c->visibility : 1.0f;
    if (a > 0.0f) {
        uint8_t alpha = a < 1.0f ? (uint8_t)lrintf(a * 255.0f) : 255;
        for (uint32_t b = 0; b < CHR_BONES; b++) geom_draw_alpha(body_geometry(body, b), m[b], body_texture(c->oncc, b), alpha);
    }
    if (c->invisible_shown) {
        const void *tex = tm_instance_get(TM_TAG("TXMP"), exe_str(0x550e78));
        uint8_t alpha = (uint8_t)lrintf((1.0f - c->visibility) * 56.099998474121094f);
        if (tex && alpha) {
            for (uint32_t b = 0; b < CHR_BONES; b++) {
                BodyMatrix s;
                memcpy(s, m[b], sizeof s);
                for (int k = 0; k < 9; k++) s[k] *= 1.4f;
                geom_draw_alpha(body_geometry(body, b), s, tex, alpha);
            }
        }
    }
    shadow(c, slot, a);
}
