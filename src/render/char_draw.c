/* Character drawing: see char_draw.h. */
#include "char_draw.h"

#include "body.h"
#include "character.h"
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

void char_draw_pose(const void *oncc, const float pos[3], float facing, float height, const float *quats, uint32_t bones_posed)
{
    const void *body = body_of_class(oncc, 4);
    if (!body) return;
    uint32_t bones = body_bones(body);
    if (bones > BODY_MAX_BONES) return;
    if (quats && bones_posed != bones) quats = NULL, height = 0;
    float c = cosf(facing), s = sinf(facing);
    BodyMatrix root = { c, 0, -s, 0, 1, 0, s, 0, c, pos[0], pos[1] + height, pos[2] }, m[BODY_MAX_BONES];
    if (!body_matrices(body, root, quats, m)) return;
    for (uint32_t b = 0; b < bones; b++) {
        const void *g = body_geometry(body, b);
        geom_draw(g, m[b], body_texture(oncc, b));
    }
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

void char_draw_character(const Character *c)
{
    char_draw_pose(c->oncc, c->pos, c->facing, c->pelvis_height, c->aimed, CHR_BONES);
}
