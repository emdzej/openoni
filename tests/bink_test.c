/* The movies (OPENONI_DATA=<dir>, default ./game; skipped without it): the first frames of intro.bik and
   outro.bik decoded (OPENONI_BINK_FRAMES, default 60), a CRC-32 of each frame's planes and of its audio
   chained into one line per movie. OPENONI_BINK_DUMP=<frame> writes that frame of the intro to
   out/bink_<frame>.ppm (BT.601, a look only: the player's conversion is its own). */
#include "bink.h"
#include "crc32.h"
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <stdlib.h>

static void dump(const BinkMovie *m, uint32_t frame)
{
    char path[64];
    snprintf(path, sizeof path, "out/bink_%u.ppm", frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t w = bink_width(m), h = bink_height(m), ys, us, vs;
    const uint8_t *y = bink_plane(m, 0, &ys), *u = bink_plane(m, 1, &us), *v = bink_plane(m, 2, &vs);
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++) {
            float Y = 1.164f * (y[j * ys + i] - 16), U = u[(j / 2) * us + i / 2] - 128.0f, V = v[(j / 2) * vs + i / 2] - 128.0f;
            float rgb[3] = { Y + 1.596f * V, Y - 0.391f * U - 0.813f * V, Y + 2.018f * U };
            for (int k = 0; k < 3; k++) fputc(rgb[k] < 0 ? 0 : rgb[k] > 255 ? 255 : (int)rgb[k], f);
        }
    fclose(f);
    printf("bink_test: wrote %s\n", path);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(DLL_PATH)) { printf("bink_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!dll_init(err, sizeof err)) {
        fprintf(stderr, "bink_test: %s\n", err);
        return 1;
    }
    const char *e = getenv("OPENONI_BINK_FRAMES"), *d = getenv("OPENONI_BINK_DUMP");
    uint32_t want = e ? (uint32_t)atoi(e) : 60, dump_at = d ? (uint32_t)atoi(d) : 0xffffffffu;
    static const char *movies[] = { "GameDataFolder/intro.bik", "GameDataFolder/outro.bik" };
    int failed = 0;
    for (int k = 0; k < 2; k++) {
        BinkMovie *m = bink_open(movies[k], err, sizeof err);
        if (!m) {
            fprintf(stderr, "bink_test: %s\n", err);
            failed++;
            continue;
        }
        uint32_t n = want < bink_frames(m) ? want : bink_frames(m), hv = 0, ha = 0, samples = 0;
        /* OPENONI_BINK_RAW=1: the intro's samples to out/intro.raw (16-bit, interleaved) */
        FILE *wav = k == 0 && getenv("OPENONI_BINK_RAW") ? fopen("out/intro.raw", "wb") : NULL;
        for (uint32_t i = 0; i < n; i++) {
            if (!bink_do_frame(m)) {
                fprintf(stderr, "bink_test: %s frame %u: can't decode\n", movies[k], i);
                failed++;
                break;
            }
            for (int p = 0; p < 3; p++) {
                uint32_t s;
                const uint8_t *pl = bink_plane(m, p, &s);
                uint32_t rows = p ? (bink_height(m) + 1) / 2 : bink_height(m), cols = p ? (bink_width(m) + 1) / 2 : bink_width(m);
                for (uint32_t r = 0; r < rows; r++) hv = crc32_update(hv, pl + (size_t)r * s, cols);
            }
            uint32_t c;
            const int16_t *a = bink_audio(m, &c);
            if (a && c) ha = crc32_update(ha, (const uint8_t *)a, 2 * (size_t)c);
            if (a && c && wav && k == 0) fwrite(a, 2, c, wav);
            samples += c;
            if (k == 0 && i == dump_at) dump(m, i);
            bink_next_frame(m);
        }
        if (wav) fclose(wav);
        uint32_t num, den;
        bink_rate(m, &num, &den);
        printf("bink_test: %s %ux%u %u frames at %u/%u, audio %u Hz x%u: %u frames video=%08x audio=%08x samples=%u\n",
               movies[k], bink_width(m), bink_height(m), bink_frames(m), num, den, bink_audio_rate(m),
               bink_audio_channels(m), n, hv, ha, samples);
        bink_close(m);
    }
    dll_free();
    return failed ? 1 : 0;
}
