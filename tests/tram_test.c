/* Every animation of every level (OPENONI_DATA=<dir>, default ./game; skipped without it): the keyframe
   streams walked, with the same summary lines as tools/onitram.py, and every frame of every one decoded
   to unit quaternions. Prints KONOKOidle_spec3 at frame 37 as `onitram.py game level0 --dump` does.
   OPENONI_TRAM_SUMMARY=<hash> checks the "all" hash. */
#include "crc32.h"
#include "exe.h"
#include "tm.h"
#include "tram.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int levels[] = { 0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };
enum { N_LEVELS = sizeof levels / sizeof levels[0] };

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* The CRC of the tool: every bone's stream as walked, then velocities and heights. -1 if out of bounds. */
static int64_t anim_crc(const uint8_t *t, const TramInfo *in)
{
    size_t size;
    const uint8_t *raw = tm_raw(t, &size);
    if (!raw) return -1;
    uint32_t rot = rd32(t + TRAM_ROTATIONS), c = 0, ks = in->rotation_size;
    for (unsigned b = 0; b < in->bones; b++) {
        size_t p = rot + (size_t)(raw[rot + 2 * b] | raw[rot + 2 * b + 1] << 8), q = p;
        int sum = 0;
        while (sum < in->frames - 1) {
            if (q + ks + 1 > size) return -1;
            sum += raw[q + ks];
            q += ks + 1;
        }
        if (q + ks > size) return -1;
        c = crc32_update(c, raw + p, q + ks - p);
    }
    uint32_t v = rd32(t + TRAM_VELOCITIES), h = rd32(t + TRAM_HEIGHTS);
    if (v) c = crc32_update(c, raw + v, 8u * in->frames);
    if (h) c = crc32_update(c, raw + h, 4u * in->frames);
    return c;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("tram_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err)) {
        fprintf(stderr, "tram_test: %s\n", err);
        return 1;
    }
    int failed = 0;
    uint32_t all = 0, frames = 0;
    float q[4 * 64];
    for (int k = 0; k < N_LEVELS; k++) {
        int lv = levels[k];
        if (!tm_initialize(err, sizeof err) || !tm_level_load(lv, err, sizeof err)) {
            fprintf(stderr, "tram_test: level%d: %s\n", lv, err);
            failed++;
            continue;
        }
        TmFile *f = tm_file_at(tm_file_slots() - 1);
        uint32_t h = 0, count = 0;
        for (uint32_t i = 0; i < tm_file_instance_count(f); i++) {
            const TmInstance *ti = tm_file_instance(f, i);
            if (ti->tmpl->tag != TRAM_TAG || !ti->data) continue;
            TramInfo in;
            tram_info(ti->data, &in);
            int64_t c = in.bones <= 64 ? anim_crc(ti->data, &in) : -1;
            if (c < 0) {
                fprintf(stderr, "tram_test: level%d %s: bad streams\n", lv, ti->name ? ti->name : "-");
                failed++;
                continue;
            }
            for (uint32_t fr = 0; fr < in.frames; fr++, frames++) {
                bool ok = tram_rotations(ti->data, fr, q);
                for (unsigned b = 0; ok && b < in.bones; b++) {
                    float n = q[4 * b] * q[4 * b] + q[4 * b + 1] * q[4 * b + 1] + q[4 * b + 2] * q[4 * b + 2] + q[4 * b + 3] * q[4 * b + 3];
                    ok = fabsf(n - 1.0f) < 1e-3f;
                }
                if (!ok) {
                    fprintf(stderr, "tram_test: level%d %s frame %u: bad rotation\n", lv, ti->name, (unsigned)fr);
                    failed++;
                    break;
                }
            }
            char line[256];
            snprintf(line, sizeof line, "%u %s %u %u %u %u %u %u %08x\n", (unsigned)i, ti->name ? ti->name : "-",
                     in.frames, in.bones, in.rotation_size, in.type, in.from_state, in.to_state, (unsigned)c);
            h = crc32_update(h, line, strlen(line));
            count++;
        }
        printf("level%d: animations=%u hash=%08x\n", lv, (unsigned)count, (unsigned)h);
        char line[64];
        snprintf(line, sizeof line, "level%d %08x\n", lv, (unsigned)h);
        all = crc32_update(all, line, strlen(line));
        if (lv == 0) {
            const void *t = tm_instance_get(TRAM_TAG, "KONOKOidle_spec3");
            TramInfo in;
            float v[2], ht;
            if (t && (tram_info(t, &in), tram_rotations(t, 37, q)) && tram_velocity(t, 37, v) && tram_height(t, 37, &ht)) {
                for (unsigned b = 0; b < in.bones; b++)
                    printf("bone %2u: %9.5f %9.5f %9.5f %9.5f\n", b, q[4 * b], q[4 * b + 1], q[4 * b + 2], q[4 * b + 3]);
                printf("velocity: %.5f %.5f\nheight: %.5f\n", v[0], v[1], ht);
            } else {
                fprintf(stderr, "tram_test: KONOKOidle_spec3 doesn't decode\n");
                failed++;
            }
        }
        tm_terminate();
    }
    printf("all: hash=%08x (%u frames decoded)\n", (unsigned)all, (unsigned)frames);
    const char *want = getenv("OPENONI_TRAM_SUMMARY");
    if (want && *want && strtoul(want, NULL, 16) != all) {
        fprintf(stderr, "tram_test: the all hash differs from OPENONI_TRAM_SUMMARY\n");
        failed++;
    }
    exe_free();
    vfs_unmount();
    printf("tram_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
