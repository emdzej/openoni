/* The installer archive on the CD (OPENONI_CD=<folder | Oni.iso>; skipped without it): the item list
   parses, every file decompresses to its size and CRC-32, and the summary hash equals the reference
   reader's (OPENONI_VISE_SUMMARY = the hash from `tools/vise.py --summary`, checked when set). */
#include "crc32.h"
#include "vfs.h"
#include "vfs_host.h"
#include "vise.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *cd = getenv("OPENONI_CD");
    if (!cd || !*cd) { printf("vise_test: OPENONI_CD not set, skipped\n"); return 0; }
    if (!vfs_mount_path(cd)) { fprintf(stderr, "vise_test: can't mount %s\n", cd); return 1; }
    char err[600];
    ViseArchive *a = vise_open("ONISETUP.EXE", err, sizeof err);
    if (!a) { fprintf(stderr, "vise_test: %s\n", err); return 1; }
    int files = 0, failed = 0;
    uint32_t h = 0;
    for (int i = 0; i < vise_count(a); i++) {
        const ViseEntry *e = vise_entry(a, i);
        if (e->dir) continue;
        uint8_t *d = vise_extract(a, i, err, sizeof err);
        if (!d) { fprintf(stderr, "vise_test: %s\n", err); failed++; continue; }
        free(d);
        char line[600];
        int n = snprintf(line, sizeof line, "%s %u %08x\n", e->path, e->size, e->crc);
        h = crc32_update(h, line, (size_t)n);
        files++;
    }
    if (vise_find(a, "oni.exe") < 0 || vise_find(a, "GameDataFolder/level0_Final.dat") < 0) {
        fprintf(stderr, "vise_test: Oni.exe or level0_Final.dat missing\n");
        failed++;
    }
    char hash[16];
    snprintf(hash, sizeof hash, "%08x", h);
    const char *want = getenv("OPENONI_VISE_SUMMARY");
    if (want && *want && strcmp(want, hash)) { fprintf(stderr, "vise_test: hash %s, want %s\n", hash, want); failed++; }
    printf("vise_test: %d files, hash %s%s\n", files, hash, failed ? ", FAILED" : "");
    vise_close(a);
    vfs_unmount();
    return failed != 0;
}
