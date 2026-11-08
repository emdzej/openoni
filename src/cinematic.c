/* Cinematic inserts: see cinematic.h. */
#include "cinematic.h"

#include "exe.h"
#include "sl.h"
#include "tm.h"
#include "txmp.h"
#include "ftol.h"

#include <math.h>
#include <string.h>

/* An insert (0x30 bytes in the original's array): flags (1 stopping, 2 to delete, 4 arrived, 0x10
   mirrored), the texture, its size, the place, the step, the ticks left, the start and end slots. */
enum { STOPPING = 1, DELETE = 2, ARRIVED = 4, MIRROR = 0x10, MAX_INSERTS = 16, SLOTS = 26 };
typedef struct {
    uint32_t flags;
    const void *txmp;
    int16_t w, h;
    float x, y, step[3];
    int32_t ticks;
    uint8_t from, to;
} Insert;

static Insert ins[MAX_INSERTS];
static int n_ins;
static float x_offset = 20.0f, y_offset = 65.0f;    /* OCrInitialize's defaults */

void cinematic_reset(void)
{
    n_ins = 0;
    x_offset = 20.0f;
    y_offset = 65.0f;
}

/* The place of a slot (the table of 26 at 0x551848 in the exe, 16 bytes each: the x and y edge, 0 none,
   1 the offset, 2 less the offset; whether at the far edge, 1024 or 728; the alignment, y in bits 0-2 (2
   centred, 4 above) and x in bits 4-6 (0x20 centred, 0x40 left of it)), for an insert of that size. */
enum { SLOT_TABLE = 0x551848 };
static void slot_place(uint8_t slot, int16_t w, int16_t h, float *px, float *py)
{
    uint32_t va = SLOT_TABLE + 16u * slot;
    int32_t xm = (int32_t)exe_u32(va), ym = (int32_t)exe_u32(va + 4);
    const uint8_t *b = exe_data(va + 8, 8);
    float x = xm == 1 ? x_offset : xm == 2 ? -x_offset : 0.0f;
    float y = ym == 1 ? y_offset : ym == 2 ? -y_offset : 0.0f;
    if (b && b[0]) x += 1024.0f;
    if (b && b[1]) y += 728.0f;
    uint32_t align = exe_u32(va + 12);
    if ((align & 7) == 2) y = (float)(728 - h) * 0.5f;
    else if ((align & 7) == 4) y -= (float)h;
    if ((align & 0x70) == 0x20) x = (float)(1024 - w) * 0.5f;
    else if ((align & 0x70) == 0x40) x -= (float)w;
    *px = x, *py = y;
}

/* The move from where it is to the slot: the step is the direction at the velocity, for (distance /
   velocity) ticks (truncated, 0x4264e0). */
static void move_to(Insert *e, uint8_t slot, float velocity)
{
    float tx, ty;
    slot_place(slot, e->w, e->h, &tx, &ty);
    float dx = tx - e->x, dy = ty - e->y, len = sqrtf(dx * dx + dy * dy);
    e->to = slot;
    if (len <= 0.0f || velocity <= 0.0f) {
        e->step[0] = e->step[1] = e->step[2] = 0;
        e->ticks = 0;
        return;
    }
    e->step[0] = dx / len * velocity, e->step[1] = dy / len * velocity, e->step[2] = 0;
    e->ticks = ftol(len / velocity);
}

/* OCrCinematic_Start 0x4f2be0 */
static void start(const char *name, int16_t w, int16_t h, uint8_t from, uint8_t to, float velocity, bool mirror)
{
    if (!name || !*name || from >= SLOTS || to >= SLOTS || w < -1 || h < -1) return;
    if (from != to && velocity < 0.0009765625f) return;
    const void *t = tm_instance_get(TM_TAG("TXMP"), name);
    if (!t || n_ins == MAX_INSERTS) return;
    TxmpInfo in;
    txmp_info(t, &in);
    Insert *e = &ins[n_ins++];
    memset(e, 0, sizeof *e);
    e->flags = mirror ? MIRROR : 0;
    e->txmp = t;
    e->w = w == -1 ? (int16_t)in.width : w;
    e->h = h == -1 ? (int16_t)in.height : h;
    e->from = from;
    slot_place(from, e->w, e->h, &e->x, &e->y);
    if (from == to) { e->to = to; return; }
    move_to(e, to, velocity);
}

/* 0x4f30c0: the insert of that texture goes to another slot, and is deleted there. */
static void stop(const char *name, uint8_t to, float velocity)
{
    const void *t = tm_instance_get(TM_TAG("TXMP"), name);
    for (int i = 0; t && i < n_ins; i++) {
        if (ins[i].txmp != t) continue;
        ins[i].flags = (ins[i].flags & MIRROR) | STOPPING;
        ins[i].from = ins[i].to;
        move_to(&ins[i], to, velocity);
        return;
    }
}

/* 0x4f3420: moving inserts step; one that is done goes exactly to its slot, and is marked for deletion
   if it was stopping; the marked ones go. */
void cinematic_update(void)
{
    for (int i = 0; i < n_ins; i++) {
        Insert *e = &ins[i];
        if (e->ticks) {
            e->ticks--;
            e->x += e->step[0];
            e->y += e->step[1];
        } else if (!(e->flags & ARRIVED)) {
            slot_place(e->to, e->w, e->h, &e->x, &e->y);
            e->flags |= e->flags & STOPPING ? ARRIVED | DELETE : ARRIVED;
        }
    }
    for (int i = n_ins - 1; i >= 0; i--)
        if (ins[i].flags & DELETE) memmove(&ins[i], &ins[i + 1], sizeof *ins * (size_t)(--n_ins - i));
}

int cinematic_count(void) { return n_ins; }

bool cinematic_get(int i, CinematicInsert *o)
{
    if (i < 0 || i >= n_ins) return false;
    o->txmp = ins[i].txmp;
    o->x = ins[i].x, o->y = ins[i].y, o->w = ins[i].w, o->h = ins[i].h;
    o->mirror = ins[i].flags & MIRROR;
    return true;
}

static int32_t num(const SlValue *v) { return v->type == SL_FLOAT ? (int32_t)v->f : v->i; }
static float fnum(const SlValue *v) { return v->type == SL_FLOAT ? v->f : (float)v->i; }

/* cinematic_start bitmap draw_width draw_height start end velocity mirror (0x4f3830) */
static bool cmd_start(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c != 7 || v[0].type != SL_STRING) return true;
    start(v[0].s, (int16_t)num(&v[1]), (int16_t)num(&v[2]), (uint8_t)num(&v[3]), (uint8_t)num(&v[4]), fnum(&v[5]), num(&v[6]) != 0);
    return true;
}

/* cinematic_stop bitmap end velocity (0x4f38e0) */
static bool cmd_stop(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c != 3 || v[0].type != SL_STRING) return true;
    stop(v[0].s, (uint8_t)num(&v[1]), fnum(&v[2]));
    return true;
}

void cinematic_register_commands(void)
{
    sl_register("cinematic_start", cmd_start, NULL);
    sl_register("cinematic_stop", cmd_stop, NULL);
    sl_register_float("cinematic_xoffset", &x_offset);
    sl_register_float("cinematic_yoffset", &y_offset);
}
