/* Films: film.h. */
#include "film.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camera.h"
#include "chars.h"
#include "chr_anim.h"
#include "exe.h"
#include "physics.h"
#include "sl.h"
#include "tm.h"
#include "ui.h"

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static void wrf(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); wr32(p, u); }
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v; }

enum { FILM_SIZE = 0x13c6d0, FILM_KEYS_MAX = (FILM_SIZE - 0x38) / 0x18 };

/* sc_bind_f2, sc_bind_f3 (0x6311e0, 0x6311a0): the animations a recording's F2 and F3 play */
static char bind_f2[0x80], bind_f3[0x80];
static int32_t save_counter;    /* 0x5ecbf0: the last saved_film number found free */
static uint8_t *(*user_load)(const char *name, size_t *size);
static bool (*user_store)(const char *name, const void *data, size_t size);

/* ONiSetFocus 0x4f3eb0: sc_focus chr_index; the player number set and the camera put behind it
   (0x636514, the character's pointer kept, is never read) */
static bool cmd_sc_focus(SlThread *t, int n, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    chars_set_focus((uint16_t)(n > 0 ? v[0].i : 0));
    camera_reset();
    return true;
}

void film_register_commands(uint8_t *(*load)(const char *name, size_t *size),
                            bool (*store)(const char *name, const void *data, size_t size))
{
    user_load = load, user_store = store;
    bind_f2[0] = bind_f3[0] = 0;    /* ONrFilm_Initialize 0x4f3e00 */
    sl_register("sc_focus", cmd_sc_focus, NULL);
    sl_register_string("sc_bind_f2", bind_f2);
    sl_register_string("sc_bind_f3", bind_f3);
}

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

/* ONrFilm_AppendFrame 0x4f3fd0: a key when the mouse moved or the bits held changed, then a tick more.
   The original writes past the film's 0x13c6d0 bytes after 54,001 keys; the port keeps no more. */
static void append_frame(uint8_t *f, const ChrInput *in)
{
    uint32_t n = rd32(f + 0x34), len = rd32(f + 0x1c);
    if (n == 0 || in->turn_y != 0.0f || in->turn_x != 0.0f || rd64(f + 0x38 + (n - 1) * 0x18 + 8) != in->held) {
        if (n < FILM_KEYS_MAX) {
            uint8_t *k = f + 0x38 + n * 0x18;
            wrf(k, in->turn_x), wrf(k + 4, in->turn_y);
            wr32(k + 8, (uint32_t)in->held), wr32(k + 0xc, (uint32_t)(in->held >> 32));
            wr32(k + 0x10, len);
            wr32(f + 0x34, n + 1);
        }
    }
    wr32(f + 0x1c, len + 1);
}

/* ONrFilm_WriteToDisk 0x4f44c0: saved_filmNNN.dat, the first number from the last one free
   (BFrFileRef_MakeUnique 0x401870, up to 999), big-endian (0x4f4220): the two bound animations' names
   (0x80 bytes each), the header (0x40: the start, facing, aim, length, zeros, the key count at 0x3c),
   the keys. */
static void write_to_disk(const uint8_t *f)
{
    char name[64];
    if (!user_load || !user_store) return;
    int32_t i = save_counter;
    for (;; i++) {
        if (i > 999) { save_counter = i; return; }
        const char *pre = exe_str(0x553180), *ext = exe_str(0x532038);     /* "%s%s%s", 3 digits */
        snprintf(name, sizeof name, "%s%03d%s", pre ? pre : "", (int)i, ext ? ext : "");
        size_t sz;
        uint8_t *old = user_load(name, &sz);
        if (!old) break;
        free(old);
    }
    save_counter = i;
    ui_console_printf(exe_str(0x553170), name);
    uint32_t n = rd32(f + 0x34);
    size_t size = 0x140 + (size_t)n * 0x18;
    uint8_t *b = calloc(1, size);
    if (!b) return;
    memcpy(b, bind_f2, 0x80);
    memcpy(b + 0x80, bind_f3, 0x80);
    uint8_t *h = b + 0x100;
    for (int k = 0; k < 8; k++) be32(h + 4 * k, rd32(f + 4 * k));
    be32(h + 0x3c, n);
    for (uint32_t j = 0; j < n; j++) {
        const uint8_t *k = f + 0x38 + j * 0x18;
        uint8_t *o = h + 0x40 + j * 0x18;
        be32(o, rd32(k)), be32(o + 4, rd32(k + 4));
        be32(o + 8, rd32(k + 0xc)), be32(o + 0xc, rd32(k + 8));    /* the u64 as one big-endian */
        be32(o + 0x10, rd32(k + 0x10));
        memcpy(o + 0x14, k + 0x14, 4);
    }
    if (!user_store(name, b, size)) ui_console_printf(exe_str(0x553144));
    free(b);
}

void film_update(Character *c, ChrInput *in)
{
    /* DoFilmUpdate 0x4ed420: start_record (0x400), play_record (0x1000), stop_record (0x800) */
    if (in->down & 0x400) {
        if (c->film_flags & 2) ui_console_printf(exe_str(0x5516b0));
        else {
            ui_console_printf(exe_str(0x55169c));
            /* ONrFilm_Create 0x4f4620: a new film each time, the last one never freed (as the original) */
            uint8_t *f = malloc(FILM_SIZE);
            if (!f) ui_console_printf(exe_str(0x55311c));
            else {
                wr32(f + 0x1c, 0), wr32(f + 0x20, 0), wr32(f + 0x24, 0), wr32(f + 0x34, 0);
                for (int k = 0; k < 3; k++) wrf(f + 4 * k, c->pos[k]);
                wrf(f + 0xc, c->facing), wrf(f + 0x10, c->desired_facing);
                wrf(f + 0x14, c->aim_lr), wrf(f + 0x18, c->aim_ud);
                c->film = f;
                c->film_flags |= 2;
            }
        }
    }
    if (in->down & 0x1000) {
        if (c->film_flags & 1) ui_console_printf(exe_str(0x55168c));
        else if (!c->film) ui_console_printf(exe_str(0x551678));
        else {
            ui_console_printf(exe_str(0x551664));
            film_start(c, c->film, FILM_START_HERE_PLACED, 0);
        }
    }
    if (in->down & 0x800) {
        ui_console_printf(exe_str(0x55165c));
        if (!(c->film_flags & 2)) ui_console_printf(exe_str(0x55164c));
        else {
            write_to_disk(c->film);
            c->film_flags &= ~2u;
        }
    }
    in->held &= ~(uint64_t)0x1c00, in->not_held |= 0x1c00;
    in->down &= ~(uint64_t)0x1c00, in->up &= ~(uint64_t)0x1c00;
    if (c->film_flags & 2) {
        append_frame((uint8_t *)c->film, in);
        return;
    }
    if (!(c->film_flags & 1)) return;
    if (c->film_frame < rd32((const uint8_t *)c->film + 0x1c)) {
        frame_input(c, in);
        c->film_frame++;
    } else
        film_stop(c);
    /* a debug switch (0x5ecba4) replays a film that ended: never set */
}

const void *film_special_animation(Character *c, const ChrInput *in)
{
    const uint8_t *f = c->film;
    if (c->film_flags & 1) {
        /* the bit 8 down: the +0x20 link, else 0x10: the +0x24; a missing one only logged ("film
           playback failed to find animation") */
        if (in->down & 8) return tm_link(f + 0x20);
        if (in->down & 0x10) return tm_link(f + 0x24);
        return NULL;
    }
    if (!(c->film_flags & 2)) return NULL;
    /* recording (0x4f719e): F2 or F3 down plays the animation bound to it, kept in the film */
    const char *name = (in->down & 8) ? bind_f2 : (in->down & 0x10) ? bind_f3 : NULL;
    if (!name) return NULL;
    const void *a = tm_instance_get(TM_TAG("TRAM"), name);
    wr32((uint8_t *)f + ((in->down & 8) ? 0x20 : 0x24), a ? tm_handle_of(a) : 0);
    if (!a) ui_console_printf((in->down & 8) ? exe_str(0x553f1c)
                                             : exe_str(0x553ef0), name);
    return a;
}

void film_interp_step(Character *c)
{
    if (!(c->film_flags & 1) || !(c->film_flags & 4)) return;
    float *p = c->has_physics ? c->phys_pos : c->pos;
    for (int k = 0; k < 3; k++) p[k] += c->film_delta[k];
    if (--c->film_interp == 0) c->film_flags &= ~4u;
}
