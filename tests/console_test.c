/* Consoles and the action key (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1 the 21
   consoles load with their markers; Konoko, 5 in front of the training's console 10, presses the action
   key: she walks to the marker (ONrCharacter_ConsoleAction_Update 0x4ee220), plays the console animation
   (type 0xc4) and at its action frame the console is used (OBJrConsole_OnActivate 0x4c0880). An inactive
   console isn't used; console_reset makes it unused again. */
#include "action.h"
#include "akev.h"
#include "chars.h"
#include "console.h"
#include "door.h"
#include "exe.h"
#include "physics.h"
#include "sl.h"
#include "tm.h"
#include "trac.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>

static void prepare(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TRAC_TAG && in->data) trac_prepare(in->data);
    }
}

static void *env;

static void run(int n, int press_at, uint16_t *seen_type)
{
    for (int t = 0; t < n; t++) {
        CharacterInput in = { 0 };
        in.action = t == press_at;
        chars_update(&in, env);
        door_update();
        if (seen_type && chars_player()->c.type == 0xc4) *seen_type = 0xc4;
    }
}

static int find(uint16_t id)
{
    for (int i = 0; i < console_count(); i++) if (console_id(i) == id) return i;
    return -1;
}

/* Konoko 5 in front of console i's marker, facing it. */
static void place(Chr *p, int i)
{
    const ActionMarker *m = console_marker(i);
    float at[3] = { m->pos[0] - sinf(m->facing) * 5, m->floor + 1, m->pos[2] - cosf(m->facing) * 5 };
    physics_place(&p->c, at);
    p->c.facing = p->c.desired_facing = m->facing;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("console_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "console_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "console_test: %s\n", err);
        return 1;
    }
    prepare();
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    chars_load();
    Chr *p = chars_player();
    door_load(env);
    int n = console_load(), failed = 0;
    action_install();
    sl_reset();
    console_register_commands();
    printf("consoles=%d\n", n);
    if (!p || n != 21) failed |= 1;

    /* console 10, inactive at the start: the action key does nothing to it */
    int c = find(10);
    place(p, c);
    run(30, -1, NULL);
    run(200, 0, NULL);
    bool inactive = (console_flags(c) & (CONSOLE_ACTIVE | CONSOLE_USED)) == 0;
    /* active: used */
    console_activate(10);
    place(p, c);
    uint16_t seen = 0;
    run(30, -1, NULL);
    run(200, 0, &seen);
    const ActionMarker *m = console_marker(c);
    float dx = p->c.pos[0] - m->pos[0], dz = p->c.pos[2] - m->pos[2];
    bool used = (console_flags(c) & CONSOLE_USED) && seen == 0xc4 && dx * dx + dz * dz < 4.0f && p->c.console_marker < 0;
    printf("console 10: inactive untouched %d, used %d (at %.1f from the marker)\n", inactive, used, sqrtf(dx * dx + dz * dz));
    if (!inactive || !used) failed |= 2;
    const char *src = "func void go(void)\n{\n\tconsole_reset 10\n}\n";
    if (!sl_load_text("test.bsl", src, err, sizeof err) || !sl_call("go", 0, NULL)) failed |= 4;
    sl_update();
    bool reset = (console_flags(c) & (CONSOLE_ACTIVE | CONSOLE_USED)) == 0;
    printf("console_reset 10: back to inactive %d\n", reset);
    if (!reset) failed |= 4;
    console_unload();
    door_unload();
    tm_terminate();
    return failed;
}
