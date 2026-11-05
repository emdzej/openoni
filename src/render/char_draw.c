/* Character drawing: see char_draw.h. */
#include "char_draw.h"

#include "body.h"
#include "character.h"
#include "exe.h"
#include "geom_draw.h"
#include "gl_dev.h"
#include "m3gm.h"
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

/* ONiCharacter_Display_Body 0x4e1d70's alpha: the body at A+0x2818 while invisible or a teleport fades it
   (C+4 0x40000; none drawn at 0), blended below 1 (255 times it); invisible, the INVIS texture (0x550e78)
   over the body scaled 1.4 about each bone (TRrBody_DrawMagic 0x427770) at (1 - it) x 56.1 (0x52b300). */
void char_draw_character(const Character *c)
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
}
