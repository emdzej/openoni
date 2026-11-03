/* The sound system (OPENONI_DATA=<dir>, default ./game; skipped without it), level 1 over level 0: the
   sound databases build from the OSBD instances, every ambient, dialog, music and impulse name the level's
   scripts give resolves (but "pause", which no level has: the original's lookup fails as well), and a
   script plays music and two dialogs, the second waiting for the first (sound_dialog_play_block), for 15 s of
   ticks with an impulse and a music stop on the way. Prints the databases' numbers, when things happened
   and an FNV-1a hash of the mix (s16). */
#include "exe.h"
#include "sl.h"
#include "sound.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int seen, missing, failed; char dir[128]; } Scan;

/* Every "sound_<command> <name>" of a script file, outside comments. */
static void scan_text(Scan *s, const char *text, size_t n)
{
    const char *p = text, *end = text + n;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        if (!eol) eol = end;
        const char *hash = memchr(p, '#', (size_t)(eol - p));
        const char *stop = hash ? hash : eol;
        for (const char *q = p; q + 6 < stop; q++) {
            if (strncmp(q, "sound_", 6) || (q > p && (isalnum((unsigned char)q[-1]) || q[-1] == '_'))) continue;
            char cmd[48] = "", name[48] = "";
            int k = 0;
            const char *r = q;
            while (r < stop && (isalnum((unsigned char)*r) || *r == '_') && k < 47) cmd[k++] = *r++;
            cmd[k] = 0;
            while (r < stop && (*r == ' ' || *r == '\t' || *r == '(' || *r == '"')) r++;
            k = 0;
            while (r < stop && (isalnum((unsigned char)*r) || *r == '_') && k < 47) name[k++] = *r++;
            name[k] = 0;
            q = r;
            bool imp = !strcmp(cmd, "sound_impulse_play");
            bool amb = !strncmp(cmd, "sound_music_", 12) || !strncmp(cmd, "sound_ambient_", 14) ||
                       !strncmp(cmd, "sound_dialog_play", 17);
            if (!(imp || amb) || !strcmp(cmd, "sound_dialog_play_pause") || !*name) continue;
            s->seen++;
            if (imp ? sound_has_impulse(name) : sound_has_ambient(name)) continue;
            s->missing++;
            if (strcmp(name, "pause")) {
                fprintf(stderr, "sound_test: %s %s doesn't resolve\n", cmd, name);
                s->failed++;
            }
        }
        p = eol + 1;
    }
}

static void scan_file(const char *name, bool is_dir, void *user)
{
    Scan *s = user;
    size_t n = strlen(name);
    if (is_dir || n < 4 || strcmp(name + n - 4, ".bsl")) return;
    char path[300];
    snprintf(path, sizeof path, "%s/%s", s->dir, name);
    size_t size;
    uint8_t *text = vfs_read_all(path, &size);
    if (text) scan_text(s, (const char *)text, size);
    free(text);
}

static int marks[8], nmarks;
static uint32_t tick;
static bool mark(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)argc, (void)argv, (void)r, (void)u;
    if (nmarks < 8) marks[nmarks++] = (int)tick;
    return true;
}

static const char script[] =
    "func void main(void)\n"
    "{\n"
    "\tsound_music_start mus_chase 0.8\n"
    "\tsound_dialog_play_block c00_01_74shinatama\n"
    "\ttest_mark\n"
    "\tsound_dialog_play_block c00_01_75shinatama\n"
    "\ttest_mark\n"
    "\tsound_dialog_play_block pause\n"
    "\ttest_mark\n"
    "}\n";

static int logged;
static void log_some(const char *m) { if (logged++ < 8) printf("  %s\n", m); }

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("sound_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err) ||
        !tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "sound_test: %s\n", err);
        return 1;
    }
    sound_set_log(log_some);
    sound_initialize();
    int failed = 0;
    if (!sound_level_load(0) || !sound_level_load(1)) { fprintf(stderr, "sound_test: no sound data\n"); failed++; }
    SoundStats st = sound_stats();
    printf("databases: groups=%u ambients=%u impulses=%u permutations=%u unresolved=%u\n", st.groups, st.ambients,
           st.impulses, st.permutations, st.unresolved);

    /* The level's folder, from its ONLV (as app.c finds it). */
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    const char *lv = NULL;
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TM_TAG("ONLV") && in->data) lv = (const char *)in->data;
    }
    Scan scan = { 0 };
    snprintf(scan.dir, sizeof scan.dir, "GameDataFolder/IGMD/%.63s", lv ? lv : "");
    if (!lv || !vfs_list(scan.dir, scan_file, &scan) || !scan.seen) { fprintf(stderr, "sound_test: no level scripts\n"); failed++; }
    printf("level1 scripts: %d sound names, %d missing\n", scan.seen, scan.missing);
    failed += scan.failed;

    sl_reset();
    sound_register_commands();
    sl_register("test_mark", mark, NULL);
    if (!sl_load_text("sound_test", script, err, sizeof err) || !sl_call("main", 0, NULL)) {
        fprintf(stderr, "sound_test: script: %s\n", err);
        failed++;
    }
    enum { TICKS = 900, PER_TICK = SOUND_RATE / 60 };
    static float buf[PER_TICK * 2];
    uint32_t h = 0x811c9dc5u;
    double sq = 0;
    int busy_max = 0, dialog_ticks = 0;
    for (tick = 1; tick <= TICKS; tick++) {
        sl_update();
        if (tick == 30) sound_impulse_play("kon_land_con", 1.0f);
        if (tick == 300) sound_music_volume("mus_chase", 0.3f, 2.0f);
        if (tick == 600) sound_music_stop("mus_chase");
        sound_update();
        sound_mix(buf, PER_TICK);
        for (int i = 0; i < PER_TICK * 2; i++) {
            int32_t v = (int32_t)lrintf(buf[i] * 32767.0f);
            for (int b = 0; b < 2; b++) h = (h ^ (uint8_t)(v >> (8 * b))) * 0x01000193u;
            sq += (double)buf[i] * buf[i];
        }
        int n = sound_channels_playing();
        if (n > busy_max) busy_max = n;
        dialog_ticks += sound_dialog_playing();
    }
    printf("marks:");
    for (int i = 0; i < nmarks; i++) printf(" %d", marks[i]);
    printf("\nchannels: up to %d; dialog: %d ticks; at the end: %d playing\n", busy_max, dialog_ticks, sound_channels_playing());
    double rms = sqrt(sq / (TICKS * PER_TICK * 2.0));
    printf("mix: %d frames, rms=%.5f hash=%08x\n", TICKS * PER_TICK, rms, h);
    /* The first block form finds no dialog playing and goes on at once; the second waits for the first
       dialog's 433 ticks; "pause" waits for the second. */
    if (nmarks != 3 || marks[0] != 1 || marks[1] < 400 || marks[2] <= marks[1] || rms < 1e-4) {
        fprintf(stderr, "sound_test: the dialogs didn't play as they should\n");
        failed++;
    }
    sound_terminate();
    sl_reset();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("sound_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
