/* The scripts of every level (OPENONI_DATA=<dir>, default ./game; skipped without it): IGMD/global and
   each level's folder lex and compile, and the warehouse's main runs for ten seconds of ticks without a
   thread going wrong. Prints the database's numbers and a CRC of the tokens. */
#include "sl.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <string.h>

static const char *folders[] = { "Airport", "Airport_III", "compound", "dream_lab", "EnvWarehouse", "lab", "manplant",
                                 "neuro", "power", "power_II", "roof", "state", "tctf", "tctf_ii" };

static int calls;
static bool dprint(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)argc, (void)argv, (void)r, (void)u;
    calls++;
    return true;
}
static int said;
static void quiet(const char *m) { if (said++ < 12) printf("  %s\n", m); }

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("GameDataFolder/IGMD/global/global.bsl")) {
        if (!vfs_mounted() || !vfs_exists("Oni.exe")) { printf("sl_test: no game folder, skipped\n"); return 0; }
    }
    int failed = 0;
    char err[300];
    for (size_t i = 0; i < sizeof folders / sizeof *folders; i++) {
        sl_reset();
        char dir[128];
        snprintf(dir, sizeof dir, "GameDataFolder/IGMD/%s", folders[i]);
        bool ok = sl_load_folder("GameDataFolder/IGMD/global", err, sizeof err) && sl_load_folder(dir, err, sizeof err);
        SlStats s = sl_stats();
        printf("%s: files=%u functions=%u globals=%u tokens=%u code=%u hash=%08x%s%s\n", folders[i], s.files, s.functions,
               s.globals, s.tokens, s.code, s.hash, ok ? "" : " FAILED: ", ok ? "" : err);
        failed += !ok;
    }
    sl_reset();
    sl_set_log(quiet);
    sl_register("dprint", dprint, NULL);
    if (sl_load_folder("GameDataFolder/IGMD/global", err, sizeof err) &&
        sl_load_folder("GameDataFolder/IGMD/EnvWarehouse", err, sizeof err) && sl_call("main", 0, NULL)) {
        for (int k = 0; k < 600; k++) sl_update();
        printf("EnvWarehouse main: %d threads after 600 ticks, %d dprints\n", sl_threads(), calls);
    } else {
        printf("EnvWarehouse main doesn't start\n");
        failed++;
    }
    /* a string engine variable: set from a script (63 characters kept), read back into another */
    sl_reset();
    static char bound[0x80], copy[0x80];
    sl_register_string("sc_bind_f2", bound);
    sl_register_string("copy_of", copy);
    if (sl_load_text("t.bsl", "func main { sc_bind_f2 = \"KONOKOpunch_heavy\"; copy_of = sc_bind_f2; }\n"
                     "func longer { sc_bind_f2 = \"0123456789012345678901234567890123456789012345678901234567890123456789\"; }\n",
                     err, sizeof err) && sl_call("main", 0, NULL)) {
        for (int k = 0; k < 2; k++) sl_update();
        sl_call("longer", 0, NULL);
        for (int k = 0; k < 2; k++) sl_update();
        printf("string variables: copy \"%s\", %zu characters kept\n", copy, strlen(bound));
        if (strcmp(copy, "KONOKOpunch_heavy") || strlen(bound) != 63) failed++;
    } else {
        printf("string variables: %s\n", err);
        failed++;
    }
    sl_reset();
    vfs_unmount();
    printf("sl_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
