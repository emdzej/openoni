/* The doors (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1 every door finds its
   panels; door 18 opens as Konoko comes near, hides its gunk while she walks through, stays open 180
   ticks and closes after (OBJrDoor_Update 0x4c44f0); the locked door 11 doesn't open for her until a
   script's door_unlock. Door 18's obstruction for the AI's grids is a line across the doorway shut, beside
   it open; the roll-up door 20's none open. */
#include "akev.h"
#include "chars.h"
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
#include <string.h>
#include "gametime.h"

/* A game tick: the characters' update, then the game time on (0x4fafb0). */
static void chars_tick(const CharacterInput *in, const void *env)
{
    chars_update(in, env);
    game_time_advance();
}

static void prepare(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TRAC_TAG && in->data) trac_prepare(in->data);
    }
}

static int find(uint16_t id)
{
    for (int i = 0; i < door_count(); i++) if (door_id(i) == id) return i;
    return -1;
}

static void *env;
static uint32_t ticks;

static void run(Chr *p, int n, bool forward)
{
    for (int t = 0; t < n; t++) {
        CharacterInput in = { 0 };
        in.forward = forward;
        sl_update();
        door_physics(++ticks);
        chars_tick(&in, env);
        door_update();
    }
    (void)p;
}

/* Whether the door's gunk is hidden: its first quad with the door's object id. */
static bool gunk_hidden(int d)
{
    uint32_t id = (door_object_id(d) & 0xffffff) | 0x3000000;
    for (uint32_t q = 0; q < akev_quad_count(env); q++) {
        AkevQuad a;
        if (akev_quad(env, q, &a) && a.object == id) return (a.flags & 0x2800) == 0x2800;
    }
    return false;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("door_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "door_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "door_test: %s\n", err);
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
    int n = door_load(env), failed = 0, panels = 0;
    for (int i = 0; i < n; i++) {
        door_open(i);
        float m[12];
        panels += door_panel(i, 0, m) != NULL;
        door_close(i);
    }
    door_load(env);     /* again: as at the level's start */
    printf("doors=%d with panels=%d\n", n, panels);
    /* OBJrDoor_ComputeObstruction: door 18 shut, its panel across the doorway (30 wide); open, beside
       it; the roll-up door 20, open, above the 15 that counts */
    {
        float p[4][3], c[3];
        uint16_t id;
        int d18 = find(18), d20 = find(20);
        door_position(d18, c);
        int shut = door_obstruction(d18, &id, p);
        bool across = shut == 2 && id == 18 && fabsf(p[0][0] - (c[0] - 15.0f)) < 0.1f && fabsf(p[1][0] - (c[0] + 15.0f)) < 0.1f;
        door_open(d18);
        int open = door_obstruction(d18, &id, p);
        bool aside = open == 2 && p[0][0] > c[0] + 14.0f && p[1][0] > c[0] + 44.0f;
        door_close(d18);
        door_open(d20);
        int up = door_obstruction(d20, &id, p);
        door_close(d20);
        door_load(env);
        printf("obstruction: door 18 shut %d across %d, open %d aside %d; door 20 open %d\n", shut, across, open, aside, up);
        if (!across || !aside || up != 0) failed++;
    }
    if (!p || n != 97 || panels != n) failed |= 1;

    /* door 18: Konoko 40 in front, facing it, walks through */
    int d = find(18);
    float at[3];
    door_position(d, at);
    at[2] += 40;
    at[1] += 2;
    physics_place(&p->c, at);
    p->c.facing = p->c.desired_facing = 3.14159265f;
    run(p, 30, false);
    bool closed_far = door_state(d) == 0 && !gunk_hidden(d);
    run(p, 40, true);
    bool opened = door_state(d) == 1 && gunk_hidden(d);
    run(p, 60, true);
    float z = p->c.pos[2];
    run(p, 100, false);
    bool open_still = door_state(d) == 1;
    run(p, 210, false);
    bool closed = door_state(d) == 0 && door_frame(d) == -1 && !gunk_hidden(d);
    printf("door 18: closed at 40 %d, opened %d, through to z %.1f, open after 100 %d, closed after 310 %d\n",
           closed_far, opened, z, open_still, closed);
    if (!closed_far || !opened || z > -620 || !open_still || !closed) failed |= 2;

    /* door 11 (locked from the start): Konoko stands at it; door_unlock 11 opens it */
    sl_reset();
    door_register_commands();
    d = find(11);
    door_position(d, at);
    at[2] += 15;
    at[1] += 2;
    physics_place(&p->c, at);
    run(p, 60, false);
    bool locked = door_state(d) == 0 && (door_flags(d) & DOOR_LOCKED);
    const char *src = "func void go(void)\n{\n\tdoor_unlock 11\n}\n";
    if (!sl_load_text("test.bsl", src, err, sizeof err) || !sl_call("go", 0, NULL)) {
        fprintf(stderr, "door_test: %s\n", err);
        failed |= 4;
    }
    run(p, 60, false);
    bool unlocked = door_state(d) == 1;
    printf("door 11: stays shut locked %d, opens unlocked %d\n", locked, unlocked);
    if (!locked || !unlocked) failed |= 4;
    door_unload();
    tm_terminate();
    return failed;
}
