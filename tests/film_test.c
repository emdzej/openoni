/* The films' recording (OPENONI_DATA=<dir>, default ./game, for the exe's strings; skipped without it):
   start_record, a few ticks of keys and mouse, stop_record; the file saved_film000.dat (big-endian:
   the bound names, the header, the keys only where the input changed), then the recording played back
   gives the ticks' input again. */
#include "exe.h"
#include "film.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char saved_name[64];
static uint8_t *saved;
static size_t saved_size;

static uint8_t *load(const char *name, size_t *size)
{
    if (!saved || strcmp(name, saved_name)) return NULL;
    uint8_t *b = malloc(saved_size);
    memcpy(b, saved, saved_size);
    *size = saved_size;
    return b;
}
static bool store(const char *name, const void *data, size_t size)
{
    snprintf(saved_name, sizeof saved_name, "%s", name);
    free(saved);
    saved = malloc(size);
    memcpy(saved, data, size);
    saved_size = size;
    return true;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static float bef(const uint8_t *p) { uint32_t u = be32(p); float f; memcpy(&f, &u, 4); return f; }

enum { TICKS = 6 };
static const uint64_t held_at[TICKS] = { 0, 0x200000, 0x200000, 0x200000, 0, 0 };
static const float turn_at[TICKS] = { 0, 0, 0, 0.25f, 0, 0 };

static ChrInput tick_input(int t, uint64_t prev)
{
    ChrInput in = { turn_at[t], 0, held_at[t], ~held_at[t], held_at[t] & ~prev, prev & ~held_at[t] };
    return in;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("film_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err)) {
        fprintf(stderr, "film_test: %s\n", err);
        return 1;
    }
    film_register_commands(load, store);
    int failed = 0;
    static Character c;
    c.pos[0] = 10, c.pos[1] = 2, c.pos[2] = -30, c.facing = 1.5f, c.desired_facing = 1.25f;

    ChrInput start = { 0, 0, 0x400, ~(uint64_t)0x400, 0x400, 0 };
    film_update(&c, &start);
    if (!(c.film_flags & 2) || (start.held & 0x1c00) || (start.down & 0x1c00)) {
        fprintf(stderr, "film_test: the recording didn't start, or its key wasn't taken out\n");
        failed++;
    }
    uint64_t prev = 0;
    for (int t = 0; t < TICKS; t++) {
        ChrInput in = tick_input(t, prev);
        film_update(&c, &in);
        prev = held_at[t];
    }
    ChrInput stop = { 0, 0, 0x800, ~(uint64_t)0x800, 0x800, 0 };
    film_update(&c, &stop);
    if (c.film_flags & 2) {
        fprintf(stderr, "film_test: the recording didn't stop\n");
        failed++;
    }
    /* the start tick and the six: 7 (the stop's tick writes the film first, then isn't recorded) */
    uint32_t len = saved ? be32(saved + 0x100 + 0x1c) : 0, n = saved ? be32(saved + 0x100 + 0x3c) : 0;
    printf("film_test: %s, %zu bytes, length %u, keys %u, start %.0f %.0f %.0f facing %.2f\n", saved_name,
           saved_size, (unsigned)len, (unsigned)n, saved ? bef(saved + 0x100) : 0, saved ? bef(saved + 0x104) : 0,
           saved ? bef(saved + 0x108) : 0, saved ? bef(saved + 0x10c) : 0);
    if (!saved || strcmp(saved_name, "saved_film000.dat") || len != 7 || n != 4 || saved_size != 0x140 + 4 * 0x18 ||
        bef(saved + 0x100) != 10 || bef(saved + 0x108) != -30 || bef(saved + 0x10c) != 1.5f) {
        fprintf(stderr, "film_test: the saved film isn't as recorded\n");
        failed++;
    }
    /* the keys: the start tick's (none held), forward down (the second tick of the six), the mouse, forward up */
    static const struct { uint32_t tick; uint64_t held; float turn; } want[4] = {
        { 0, 0, 0 }, { 2, 0x200000, 0 }, { 4, 0x200000, 0.25f }, { 5, 0, 0 },
    };
    for (uint32_t k = 0; saved && k < n && k < 4; k++) {
        const uint8_t *e = saved + 0x140 + k * 0x18;
        uint64_t held = (uint64_t)be32(e + 8) << 32 | be32(e + 0xc);
        if (be32(e + 0x10) != want[k].tick || held != want[k].held || bef(e) != want[k].turn) {
            fprintf(stderr, "film_test: key %u: tick %u held %llx turn %.2f\n", (unsigned)k, (unsigned)be32(e + 0x10),
                    (unsigned long long)held, bef(e));
            failed++;
        }
    }
    /* the recording played back (as play_record does, here in place): each tick's input again */
    c.film_flags |= 1, c.film_frame = 0;
    prev = 0;
    for (int t = 0; t < 7; t++) {
        ChrInput in = { 0 };
        film_update(&c, &in);
        uint64_t want_held = t >= 2 && t <= 4 ? 0x200000 : 0;
        float want_turn = t == 4 ? 0.25f : 0;
        if (in.held != want_held || in.turn_x != want_turn || in.down != (want_held & ~prev)) {
            fprintf(stderr, "film_test: playback tick %d: held %llx down %llx turn %.2f\n", t,
                    (unsigned long long)in.held, (unsigned long long)in.down, in.turn_x);
            failed++;
        }
        prev = want_held;
    }
    ChrInput idle = { 0 };
    film_update(&c, &idle);
    if (c.film_flags & 1) {
        fprintf(stderr, "film_test: the playback didn't end\n");
        failed++;
    }
    /* a second save finds the first's name taken */
    ChrInput again = { 0, 0, 0x400, ~(uint64_t)0x400, 0x400, 0 };
    film_update(&c, &again);
    ChrInput stop2 = { 0, 0, 0x800, ~(uint64_t)0x800, 0x800, 0 };
    film_update(&c, &stop2);
    if (strcmp(saved_name, "saved_film001.dat")) {
        fprintf(stderr, "film_test: the second film saved as %s\n", saved_name);
        failed++;
    }
    printf(failed ? "film_test: FAILED\n" : "film_test: ok\n");
    return failed ? 1 : 0;
}
