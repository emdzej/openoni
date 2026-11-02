/* Oni.exe from the game folder (OPENONI_DATA=<dir>, default ./game; skipped without it): the size and
   CRC check, and reads through the section table: two strings the game logs at start-up (in .data, at
   the addresses ONiMain 0x4d3280 passes to UUrStartupMessage), checked by length and CRC-32 so the text
   stays out of the source, and nothing outside the sections (the PE headers included). */
#include "crc32.h"
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int check_str(uint32_t va, size_t len, uint32_t crc)
{
    const char *s = exe_str(va);
    if (s && strlen(s) == len && crc32(s, len) == crc) return 0;
    fprintf(stderr, "exe_test: string at 0x%08x is \"%s\"\n", (unsigned)va, s ? s : "(none)");
    return 1;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("exe_test: no game folder, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { fprintf(stderr, "exe_test: %s\n", err); return 1; }
    int failed = check_str(0x54ca7c, 30, 0xf56bfb7du) + check_str(0x54ca3c, 27, 0x3587e3f5u);
    if (exe_data(0x400000, 2) || exe_data(0x10000000, 4)) { fprintf(stderr, "exe_test: read outside the image\n"); failed++; }
    printf("exe_test: %s\n", failed ? "FAILED" : "ok");
    exe_free();
    vfs_unmount();
    return failed != 0;
}
