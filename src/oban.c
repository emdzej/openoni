/* Object animations: see oban.h. */
#include "oban.h"

#include "body.h"
#include "trac.h"
#include "tram.h"

#include <math.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

bool oban_info(const void *oban, ObanInfo *info)
{
    const uint8_t *d = oban;
    if (!d) return false;
    info->flags = rd32(d + OBAN_FLAGS);
    info->ticks = rd16(d + OBAN_TICKS);
    info->frames = rd16(d + OBAN_FRAMES);
    info->door_open = rd16(d + OBAN_DOOR_OPEN);
    info->keys = rd16(d + OBAN_KEY_COUNT);
    return true;
}

static void key(const uint8_t *k, float q[4], float p[3])
{
    for (int i = 0; i < 4; i++) q[i] = rdf(k + 4 * i);
    for (int i = 0; i < 3; i++) p[i] = rdf(k + 16 + 4 * i);
}

/* MUrMatrix_Multiply 0x4341c0: a b, 3x4 by columns. */
static void mul(const float a[12], const float b[12], float o[12])
{
    float t[12];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 3; r++)
            t[3 * c + r] = a[r] * b[3 * c] + a[3 + r] * b[3 * c + 1] + a[6 + r] * b[3 * c + 2] + (c == 3 ? a[9 + r] : 0.0f);
    memcpy(o, t, sizeof t);
}

bool oban_matrix(const void *oban, int frame, bool no_rotation, float out[12])
{
    const uint8_t *d = oban;
    uint16_t n = rd16(d + OBAN_KEY_COUNT);
    if (!n) return false;
    /* The key at the frame, else the two around it. Past the last key (or before the first, which is at
       frame 0 in every animation on the CD) the original reads outside the keys; the port holds the end. */
    float q[4], p[3];
    const uint8_t *keys = d + OBAN_KEYS, *k = NULL;
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t *e = keys + OBAN_KEY_SIZE * i;
        int32_t f = (int32_t)rd32(e + 0x1c);
        if (f == frame) { k = e; break; }
        if (frame < f) {
            if (!i) { k = e; break; }
            const uint8_t *a = e - OBAN_KEY_SIZE;
            int32_t fa = (int32_t)rd32(a + 0x1c);
            float t = (float)(frame - fa) / (float)(f - fa);
            float qa[4], pa[3], qb[4], pb[3];
            key(a, qa, pa);
            key(e, qb, pb);
            /* 0x433ff0 and MUrQuat_Lerp 0x433360 */
            for (int c = 0; c < 3; c++) p[c] = t * pb[c] + (1.0f - t) * pa[c];
            tram_quat_lerp(qa, qb, t, q);
            goto have;
        }
    }
    if (!k) k = keys + OBAN_KEY_SIZE * (n - 1);
    key(k, q, p);
have:;
    float r[12] = { 0 }, t[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, p[0], p[1], p[2] }, base[12];
    body_quat_to_matrix(q, r);
    for (int i = 0; i < 12; i++) base[i] = rdf(d + OBAN_BASE + 4 * i);
    if (!no_rotation) mul(t, r, t);
    mul(t, base, out);
    if (rd32(d + OBAN_FLAGS) & OBAN_Z_UP) {
        /* 0x434570: a quarter turn about x (the double pi/2 at 0x529d40), in front */
        float c = (float)cos(1.5707963705062866), s = (float)sin(1.5707963705062866);
        float x[12] = { 1, 0, 0, 0, c, s, 0, -s, c, 0, 0, 0 };
        mul(x, out, out);
    }
    return true;
}

void oban_player_init(ObanPlayer *p, const void *oban)
{
    memset(p, 0, sizeof *p);
    p->oban = oban;
}

void oban_start(ObanPlayer *p, uint32_t now)
{
    p->state |= OBAN_PLAYING;
    const uint8_t *d = p->oban;
    if (d && (rd32(d + OBAN_FLAGS) & OBAN_RANDOM_START) && rd16(d + OBAN_FRAMES))
        p->frame = (int16_t)(((uint32_t)trac_random() * rd16(d + OBAN_FRAMES)) >> 16);
    else
        p->frame = 0;
    p->last = 0;     /* (and a u16 at +0x10 of the context, which nothing here reads; the stop frame stays) */
    p->step = 1;
    /* 0x43a600: a paused animation takes the time and goes on */
    if (p->state & OBAN_PAUSED) {
        p->state &= (uint16_t)~OBAN_PAUSED;
        p->last = now;
    }
}

/* 0x439540: stopped, the frame kept within the animation. */
static void stop(ObanPlayer *p)
{
    uint16_t frames = rd16((const uint8_t *)p->oban + OBAN_FRAMES);
    if (p->frame >= (int)frames) p->frame = (int16_t)(frames - 1);
    if (p->frame < 0) p->frame = 0;
    p->state &= (uint16_t)~OBAN_PLAYING;
}

bool oban_update(ObanPlayer *p, uint32_t now)
{
    const uint8_t *d = p->oban;
    if (!d || (p->state & OBAN_PAUSED)) return false;
    uint32_t flags = rd32(d + OBAN_FLAGS);
    if (!(p->state & OBAN_PLAYING) && (flags & OBAN_AUTOSTART)) oban_start(p, now);
    bool stopped = false;
    /* The test is the last step's time against the ticks field plus now, so it holds on every tick: the
       animations step once a tick whatever the field says. Kept as the original has it. */
    if ((p->state & OBAN_PLAYING) && p->last < (uint32_t)rd16(d + OBAN_TICKS) + now) {
        p->last = now;
        p->frame = (int16_t)(p->frame + p->step);
        uint16_t frames = rd16(d + OBAN_FRAMES);
        if (p->frame >= (int)frames || (p->stop && p->frame >= p->stop)) {
            if (flags & OBAN_PINGPONG) {
                p->step = -1;
                p->frame = (int16_t)(frames - 1);
            } else if (flags & OBAN_LOOP) {
                p->frame = 0;
            } else {
                stop(p);
                stopped = true;
            }
        }
        if (p->frame < 0) {
            if (flags & OBAN_PINGPONG) {
                p->frame = 0;
                p->step = 1;
                return stopped;
            }
            stop(p);
            stopped = true;
        }
    }
    return stopped;
}
