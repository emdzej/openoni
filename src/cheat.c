/* The cheat codes: see cheat.h. */
#include "cheat.h"

#include "chars.h"
#include "chr_hit.h"
#include "env.h"
#include "exe.h"
#include "inventory.h"
#include "knowledge.h"
#include "persist.h"
#include "tm.h"
#include "trac.h"
#include "ui.h"
#include "weapon.h"

#include <math.h>
#include <string.h>

bool cheat_change_characters, cheat_ultra_mode, cheat_slow_motion;

/* the code table at 0x5533f8: 16 bytes an entry (the code, the message when on, when off, the id), a
   null code at the end */
enum { TABLE = 0x5533f8, ENTRY = 16 };

static char typed[21];          /* 0x5ecea4: the last 20 letters, the newest last */
static const char *message;     /* 0x5ed00c */

bool cheat_shapeshift(Character *c, bool back)
{
    if (!cheat_change_characters || !c) return false;
    static void *all[512];
    int n = tm_instance_list(TM_TAG("ONCC"), all, 512);
    if (n <= 0) return false;
    /* the number of its class (the port finds it in the list: C+0x10 is kept as it) */
    int k = 0;
    while (k < n && all[k] != c->oncc) k++;
    if (k == n) k = 0;
    k = back ? (k + n - 1) % n : (k + 1) % n;
    return chr_set_class(c, all[k]);
}

const char *cheat_message(void) { return message; }
void cheat_message_clear(void) { message = NULL; }

void cheat_key(uint8_t code)
{
    /* ONrGameState_HandleCheats 0x4f6110: once the game is won (0x50f660), a typed letter (type 9, 'a'
       to 'z') onto the window; a code the window holds from some letter on switches its cheat, its
       message shown and the window emptied */
    if (!persist_won_game() || code < 'a' || code > 'z') return;
    memmove(typed, typed + 1, 19);
    typed[19] = (char)code;
    for (int i = 0; i < 20; i++)
        for (uint32_t e = TABLE; exe_u32(e); e += ENTRY) {
            const char *c = exe_str(exe_u32(e));
            if (!c || strncmp(typed + i, c, strlen(c))) continue;     /* UUrString_HasPrefix 0x4265d0 */
            uint32_t m = exe_u32(cheat_apply((int)exe_u32(e + 12)) ? e + 4 : e + 8);
            if (m) message = exe_str(m);
            memset(typed, 0, sizeof typed);
        }
}

static bool toggle(bool *b) { return *b = !*b; }

static float class_float(const Character *c, unsigned off)
{
    float f;
    memcpy(&f, (const uint8_t *)c->oncc + off, 4);
    return f;
}

/* "munitionfrenzy": every weapon class, in a circle of 8 about the player 10 up, each facing along the
   circle */
static void weapons_locker(const Character *c)
{
    static void *cls[256];
    int n = tm_instance_list(TM_TAG("ONWC"), cls, 256);
    for (int i = 0; i < n; i++) {
        float a = (float)i * 6.2831855f / (float)n;     /* 0x529548 */
        float pos[3] = { sinf(a) * 8.0f + c->pos[0], c->pos[1] + 10.0f, cosf(a) * 8.0f + c->pos[2] };   /* 0x52a740, 0x52a190 */
        float facing = a + 1.5707964f;                  /* 0x529cd0 */
        if (facing >= 6.2831855f) facing -= 6.2831855f;
        weapon_new(cls[i], pos, facing);
    }
}

bool cheat_apply(int id)
{
    Chr *p = chars_player();
    if (!p) return false;
    Character *c = &p->c;
    switch (id) {
    case 0: return toggle(&cheat_change_characters);
    case 1: return toggle(&chr_cheat_invincible);
    case 2: return toggle(&chr_cheat_omnipotent);
    case 3: return toggle(&chr_cheat_unstoppable);
    case 4:     /* "fatloot": six of each (C+0x1a2, 0x1a6, 0x1a4) */
        c->ammo = c->cells = c->hypos = 6;
        return true;
    case 5: return env_toggle_furniture_glass();
    case 6: ui_set_level_end(1); return true;
    case 7: ui_set_level_end(2); return true;
    case 8: return toggle(&chr_big_head);
    case 9:     /* "minime": the player at a quarter of the size (C+0x34), or under 0.75 back to 1 */
        if (c->scale < 0.75f) {
            c->scale = 1.0f;
            return false;
        }
        c->scale = 0.25f;
        return true;
    case 10:    /* "superammo": C+4 0x40, no ammo used */
        c->flags ^= 0x40u;
        return (c->flags & 0x40u) != 0;
    case 12: return toggle(&ai_last_man_standing);
    case 13: return toggle(&weapon_cheat_gatling);
    case 14:    /* "chenille": the Daodan's super value (C+0x1664) to 10 and C+4 0x40000000, or both off */
        if (c->super <= 1.0f) {
            c->super = 10.0f;
            c->flags |= 0x40000000u;
            return true;
        }
        c->super = 0.0f;
        c->flags &= ~0x40000000u;
        return false;
    case 15:    /* "behemoth": unthrowable (0x5ece90) at 1.8 the size; off, a size of the class's range again
                   (UUrRandom) */
        if (!toggle(&chr_cheat_unthrowable)) {
            float lo = class_float(c, 0xc58), hi = class_float(c, 0xc5c);
            c->scale = (hi - lo) * (float)trac_random() * 1.5259022e-05f + lo;
            return false;
        }
        c->scale = 1.8f;
        return true;
    case 16: return toggle(&inventory_cheat_regeneration);
    case 17:    /* "moonshadow": invisible for good (C+0x1ba 0xffff), or not at all */
        if (c->invisibility != 0xffff) {
            c->invisibility = 0xffff;
            return true;
        }
        c->invisibility = 0;
        return false;
    case 18:
        weapons_locker(c);
        return true;
    case 19: return toggle(&chr_cheat_fists);
    case 20: return toggle(&cheat_ultra_mode);
    case 21: return toggle(&cheat_slow_motion);
    default: return false;      /* 11, the developer keys: not in the table */
    }
}
