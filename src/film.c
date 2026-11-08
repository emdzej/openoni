/* Films: film.h. */
#include "film.h"

#include <string.h>

#include "chr_anim.h"
#include "physics.h"
#include "tm.h"

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

bool film_start(Character *c, const void *film, int mode, uint32_t ticks)
{
    const uint8_t *f = film;
    chr_make_active(c);             /* ONrFilm_Start 0x4f4060 */
    c->varient &= 0xfeff;           /* 0x4ece70(c, 0) */
    /* A+0x2190, the time of the last key press: not kept */
    c->film = film;
    c->film_frame = 0;
    c->facing = rdf(f + 0xc);
    c->desired_facing = rdf(f + 0x10);
    c->aim_lr = rdf(f + 0x14);
    c->aim_ud = rdf(f + 0x18);
    if (mode == FILM_START_HERE_PLACED) {
        const float pos[3] = { rdf(f), rdf(f + 4), rdf(f + 8) };
        physics_place(c, pos);      /* 0x4e9210(c, pos, 0) */
        if (c->air_ticks) {
            c->air_ticks = 0;
            chr_force_stand(c);
        }
    } else if (mode == FILM_START_INTERP) {
        if (ticks < 2) ticks = 1;
        c->film_interp = ticks;
        float d2 = 0.0f;
        for (int k = 0; k < 3; k++) {
            c->film_delta[k] = rdf(f + 4 * k) - c->pos[k];
            d2 += c->film_delta[k] * c->film_delta[k];
        }
        /* too far (80 squared, 0x52a9a4): refused, the film and its pose kept as set above */
        if (d2 > 6400.0f) return false;
        for (int k = 0; k < 3; k++) c->film_delta[k] *= 1.0f / (float)ticks;
        c->film_flags |= 4;
    }
    c->film_flags |= 1;
    return true;
}

bool film_stop(Character *c)
{
    if (!(c->film_flags & 1)) return false;
    c->film_flags = 0;
    return true;
}

bool film_playing(const Character *c) { return (c->film_flags & 1) != 0; }

/* 0x4f3ee0: the input of the film's current tick. The key in force is the last that starts at or before
   it; on the tick a key starts, its mouse and the bits gone down and up since the key before (the key
   before the first is the FILM's +0x20 field as the original reads it: the unused +0x28 bits, "dead"
   filler in the data); between keys, the bits held and no mouse. */
static void frame_input(const Character *c, ChrInput *in)
{
    const uint8_t *f = c->film;
    uint32_t n = rd32(f + 0x34), i = 0;
    const uint8_t *k = f + 0x38;
    if (n == 0) {
        /* no keys (no film has none): the original takes the playback state's words as the input */
        in->turn_x = in->turn_y = 0.0f;
        in->held = in->down = in->up = 0, in->not_held = ~(uint64_t)0;
        return;
    }
    while (i < n && c->film_frame >= rd32(k + 0x10)) i++, k += 0x18;
    k -= 0x18;      /* the last key at or before the tick (i == 0: the one before the first) */
    uint64_t held = rd64(k + 8);
    if (i != 0 && rd32(k + 0x10) == c->film_frame) {
        uint64_t before = rd64(k - 0x10);
        in->turn_x = rdf(k), in->turn_y = rdf(k + 4);
        in->held = held, in->not_held = ~held;
        in->down = ~before & held;
        in->up = ~held & before;
        return;
    }
    /* before the first key (never: the films start with a key at 0) the original takes the mouse from
       the TRAM links at +0x20: none here */
    in->turn_x = in->turn_y = 0.0f;
    in->held = held, in->not_held = ~held;
    in->down = in->up = 0;
}

void film_update(Character *c, ChrInput *in)
{
    /* the recording keys (0x400 record, 0x800 stop and save, 0x1000 play the recording): the
       developers' (ONrFilm_Create, WriteToDisk), not ported; their bits are taken out */
    in->held &= ~(uint64_t)0x1c00, in->not_held |= 0x1c00;
    in->down &= ~(uint64_t)0x1c00, in->up &= ~(uint64_t)0x1c00;
    if (!(c->film_flags & 1)) return;
    if (c->film_frame < rd32((const uint8_t *)c->film + 0x1c)) {
        frame_input(c, in);
        c->film_frame++;
    } else
        film_stop(c);
    /* a debug switch (0x5ecba4) replays a film that ended: never set */
}

const void *film_special_animation(const Character *c, const ChrInput *in)
{
    if (!(c->film_flags & 1)) return NULL;
    /* the bit 8 down: the +0x20 link, else 0x10: the +0x24; a missing one only logged ("film playback
       failed to find animation") */
    const uint8_t *f = c->film;
    if (in->down & 8) return tm_link(f + 0x20);
    if (in->down & 0x10) return tm_link(f + 0x24);
    return NULL;
}

void film_interp_step(Character *c)
{
    if (!(c->film_flags & 1) || !(c->film_flags & 4)) return;
    float *p = c->has_physics ? c->phys_pos : c->pos;
    for (int k = 0; k < 3; k++) p[k] += c->film_delta[k];
    if (--c->film_interp == 0) c->film_flags &= ~4u;
}
