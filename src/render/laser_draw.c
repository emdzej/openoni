/* The laser triggers' beams: see laser_draw.h. */
#include "laser_draw.h"

#include "gl_dev.h"
#include "laser.h"

#include <math.h>

/* FXrDrawLaser 0x4f4720: the beam's side the cross of the eye-to-start line with the beam, set to 0.2
   (0x5531b8; at least 0.001 long before, 0x529dfc), the points' colour the trigger's RGB, its alpha the
   colour's top byte. Drawn through M3rContrail_Draw 0x41a8e0 without a texture, in a pushed draw state
   (0x41e750, popped by 0x41e7f0) with the depth write off (state 4 = 0, 0x4f473a) and the depth test
   kept; blended by the colour's alpha (inferred, as the contrails). */
void laser_draw(const float view[16])
{
    float eye[3];
    for (int k = 0; k < 3; k++) eye[k] = -(view[4 * k] * view[12] + view[4 * k + 1] * view[13] + view[4 * k + 2] * view[14]);
    gl_dev_cull(0);
    for (int i = 0; i < laser_count(); i++)
        for (int k = 0; k < laser_emitters(i); k++) {
            LaserBeam b;
            if (!laser_beam(i, k, &b)) continue;
            float a[3] = { b.from[0] - eye[0], b.from[1] - eye[1], b.from[2] - eye[2] };
            float d[3] = { b.to[0] - b.from[0], b.to[1] - b.from[1], b.to[2] - b.from[2] };
            float w[3] = { a[1] * d[2] - a[2] * d[1], a[2] * d[0] - a[0] * d[2], a[0] * d[1] - a[1] * d[0] };
            float l = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
            if (l <= 0.001f) l = 0.001f;
            l = 0.2f / l;
            w[0] *= l, w[1] *= l, w[2] *= l;
            uint8_t r = (uint8_t)(b.colour >> 16), g = (uint8_t)(b.colour >> 8), bl = (uint8_t)b.colour, al = (uint8_t)(b.colour >> 24);
            GlVertex v[4] = {
                { b.from[0] - w[0], b.from[1] - w[1], b.from[2] - w[2], 0, 0, r, g, bl, al },
                { b.from[0] + w[0], b.from[1] + w[1], b.from[2] + w[2], 0, 1, r, g, bl, al },
                { b.to[0] + w[0], b.to[1] + w[1], b.to[2] + w[2], 1, 1, r, g, bl, al },
                { b.to[0] - w[0], b.to[1] - w[1], b.to[2] - w[2], 1, 0, r, g, bl, al },
            };
            static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
            gl_dev_draw(v, 4, idx, 6, 0, GL_DEV_BLEND);
        }
}
