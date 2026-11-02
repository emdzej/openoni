/* openoni-extract: makes the game folder OpenOni plays from out of the Oni CD, without running the Windows
   installer. The CD can be the ISO image or a folder with its files.

     openoni-extract <cd> <outdir>     extract (Oni.exe, binkw32.dll, readme.txt, GameDataFolder/...)
     openoni-extract --list <cd>       the archive's folders and files
     openoni-extract --summary <cd>    file count, bytes and one hash over names, sizes and CRCs

   Every file is checked against the CRC-32 the archive keeps for it. Files are written as <name>.part and
   renamed when complete, so an interrupted run leaves no truncated file under the real name. */
#include "crc32.h"
#include "vfs.h"
#include "vfs_host.h"
#include "vise.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <sys/utime.h>
#define mkdir(p, m) _mkdir(p)
#else
#include <utime.h>
#endif

static const char SETUP[] = "ONISETUP.EXE";

static int usage(void)
{
    fprintf(stderr, "usage: openoni-extract <cd folder | Oni.iso> <outdir>\n"
                    "       openoni-extract --list <cd>\n"
                    "       openoni-extract --summary <cd>\n");
    return 2;
}

static bool mkdirs(const char *path)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) return false;
            *p = '/';
        }
    return mkdir(buf, 0755) == 0 || errno == EEXIST;
}

/* FILETIME (100 ns since 1601, UTC) to Unix seconds. */
static long long unix_time(uint64_t ft) { return (long long)(ft / 10000000u) - 11644473600LL; }

static int extract(ViseArchive *a, const char *outdir)
{
    if (!mkdirs(outdir)) { fprintf(stderr, "%s: %s\n", outdir, strerror(errno)); return 1; }
    uint64_t total = 0;
    int files = 0;
    for (int i = 0; i < vise_count(a); i++) {
        const ViseEntry *e = vise_entry(a, i);
        char path[1100];
        snprintf(path, sizeof path, "%s/%s", outdir, e->path);
        if (e->dir) {
            if (!mkdirs(path)) { fprintf(stderr, "%s: %s\n", path, strerror(errno)); return 1; }
            continue;
        }
        char err[600];
        uint8_t *data = vise_extract(a, i, err, sizeof err);
        if (!data) { fprintf(stderr, "%s\n", err); return 1; }
        char part[1110];
        snprintf(part, sizeof part, "%s.part", path);
        FILE *f = fopen(part, "wb");
        bool ok = f && fwrite(data, 1, e->size, f) == e->size;
        if (f && fclose(f) != 0) ok = false;
        free(data);
        if (!ok || rename(part, path) != 0) {
            fprintf(stderr, "%s: %s\n", path, strerror(errno));
            remove(part);
            return 1;
        }
        struct utimbuf t = {(time_t)unix_time(e->mtime), (time_t)unix_time(e->mtime)};
        utime(path, &t);
        printf("%10u  %s\n", e->size, e->path);
        fflush(stdout);
        total += e->size;
        files++;
    }
    printf("%d files, %llu bytes in %s\n", files, (unsigned long long)total, outdir);
    return 0;
}

static void list(const ViseArchive *a)
{
    for (int i = 0; i < vise_count(a); i++) {
        const ViseEntry *e = vise_entry(a, i);
        if (e->dir) printf("%10s  %10s  %8s  %s/\n", "", "", "", e->path);
        else printf("%10u  %10u  %08x  %s\n", e->size, e->csize, e->crc, e->path);
    }
}

/* Hash over "path size crc\n" for every file, in archive order: the same as tools/vise.py --summary. */
static void summary(const ViseArchive *a)
{
    uint32_t h = 0;
    uint64_t total = 0;
    int files = 0;
    for (int i = 0; i < vise_count(a); i++) {
        const ViseEntry *e = vise_entry(a, i);
        if (e->dir) continue;
        char line[600];
        int n = snprintf(line, sizeof line, "%s %u %08x\n", e->path, e->size, e->crc);
        h = crc32_update(h, line, (size_t)n);
        total += e->size;
        files++;
    }
    printf("files=%d bytes=%llu hash=%08x\n", files, (unsigned long long)total, h);
}

int main(int argc, char **argv)
{
    const char *mode = NULL;
    int arg = 1;
    if (argc > 1 && argv[1][0] == '-') mode = argv[arg++];
    if (mode && strcmp(mode, "--list") && strcmp(mode, "--summary")) return usage();
    if (mode ? argc - arg != 1 : argc - arg != 2) return usage();
    const char *cd = argv[arg];
    if (!vfs_mount_path(cd)) { fprintf(stderr, "%s: not a folder or a disc image\n", cd); return 1; }
    char err[600];
    ViseArchive *a = vise_open(SETUP, err, sizeof err);
    if (!a) {
        fprintf(stderr, "%s: %s (is this the Oni CD?)\n", cd, err);
        return 1;
    }
    int rc = 0;
    if (!mode) rc = extract(a, argv[arg + 1]);
    else if (!strcmp(mode, "--list")) list(a);
    else summary(a);
    vise_close(a);
    vfs_unmount();
    return rc;
}
