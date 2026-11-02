/* The instance files of the game folder (OPENONI_DATA=<dir>, default ./game; skipped without it):
   - every level file walks by its layouts, with the same summary lines as tools/onidat.py;
   - every level loads after level 0, with the same link counts as `tools/onidat.py <game> --load`;
   - lookups by name and handles find the instances back.
   OPENONI_DAT_SUMMARY=<hash> checks the "all" hash (the tool's last line). */
#include "crc32.h"
#include "exe.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int levels[] = { 0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };
enum { N_LEVELS = sizeof levels / sizeof levels[0] };

static int failed;

static void check(bool ok, const char *what, const char *err)
{
    if (!ok) {
        fprintf(stderr, "dat_test: %s: %s\n", what, err ? err : "failed");
        failed++;
    }
}

/* Every instance with data is found again by its handle, and the named ones by name. */
static void check_lookups(void)
{
    for (int s = 0; s < tm_file_slots(); s++) {
        TmFile *f = tm_file_at(s);
        if (!f) continue;
        for (uint32_t i = 0; i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (!in->data) continue;
            if (tm_handle_instance(tm_handle_of(in->data)) != in) {
                check(false, tm_file_name(f), "handle doesn't find the instance");
                return;
            }
            /* by name: this file's, unless an earlier file has the same name (it wins) */
            if (in->name) {
                void *d = tm_instance_get(in->tmpl->tag, in->name + 4);
                const TmInstance *got = d ? tm_handle_instance(tm_handle_of(d)) : NULL;
                if (!got || strcmp(got->name, in->name)) {
                    check(false, in->name, "not found by name");
                    return;
                }
            }
        }
    }
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("dat_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err)) { fprintf(stderr, "dat_test: %s\n", err); return 1; }
    if (!tm_initialize(err, sizeof err)) { fprintf(stderr, "dat_test: %s\n", err); return 1; }
    printf("dat_test: %d templates\n", tm_template_count());

    uint32_t all = 0;
    for (int k = 0; k < N_LEVELS; k++) {
        char path[64], name[16];
        snprintf(name, sizeof name, "level%d", levels[k]);
        snprintf(path, sizeof path, "GameDataFolder/%s_Final.dat", name);
        check(tm_level_valid(levels[k]), name, "not a valid level");
        TmSummary s;
        if (!tm_file_summary(path, &s, err, sizeof err)) { check(false, name, err); continue; }
        printf("%s: instances=%u placeholders=%u links=%u slack=%u hash=%08x\n", name, (unsigned)s.instances,
               (unsigned)s.placeholders, (unsigned)s.links, (unsigned)s.slack, (unsigned)s.hash);
        char line[64];
        snprintf(line, sizeof line, "%s %08x\n", name, (unsigned)s.hash);
        all = crc32_update(all, line, strlen(line));
    }
    printf("all: hash=%08x\n", (unsigned)all);
    const char *want = getenv("OPENONI_DAT_SUMMARY");
    if (want && *want && strtoul(want, NULL, 16) != all) check(false, "summary", "the all hash differs from OPENONI_DAT_SUMMARY");

    for (int k = 0; k < N_LEVELS; k++) {
        char name[16];
        snprintf(name, sizeof name, "level%d", levels[k]);
        if (!tm_initialize(err, sizeof err)) { check(false, name, err); break; }
        bool ok = tm_level_load(0, err, sizeof err) && (levels[k] == 0 || tm_level_load(levels[k], err, sizeof err));
        check(ok, name, err);
        if (!ok) continue;
        TmLoadStats st = tm_load_stats();
        printf("%s loaded after level0: links=%u cross=%u unresolved=%u\n", name, (unsigned)st.links,
               (unsigned)st.cross, (unsigned)st.unresolved);
        check_lookups();
        if (levels[k] != 0) {
            tm_level_unload(levels[k]);
            check(tm_file_slots() == 3, name, "unload left files behind");
        }
    }
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("dat_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
