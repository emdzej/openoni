/* The inventory over time: see inventory.h. */
#include "inventory.h"

#include "tm.h"
#include "tram.h"

#include <math.h>
#include <string.h>

static float gs_float(unsigned off)     /* the game settings (ONGS "game_settings", 0x5ecbfc) */
{
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
    if (!gs) return 0.0f;
    float f;
    memcpy(&f, gs + off, 4);
    return f;
}

static uint32_t round_u(float f) { return (uint32_t)lrintf(f); }   /* 0x4340a0 */

/* 0x4e64a0: hit points added to the living (over the maximum if over). */
static void heal(Character *c, uint32_t n, bool over)
{
    if (c->flags & 1) return;
    c->hit_points += n;
    if (!over && c->hit_points > c->max_hit_points) c->hit_points = c->max_hit_points;
}

void inventory_update(Character *c)
{
    /* the wait; then the healing to come, a point at a time up to the settings' cap (+0: the maximum
       times it), waiting the class's heal delay (ONCC +0x5c) while under the maximum; without it, health
       over the maximum lost a point every 10 ticks (or, with the regeneration cheat 0x620134 for the
       player, a point a second gained: not ported) */
    if (c->heal_wait) {
        c->heal_wait--;
    } else {
        bool waited = false;
        if (c->hypo_heal) {
            uint32_t cap = round_u((float)c->max_hit_points * gs_float(0));
            if (c->hit_points < cap) {
                c->hypo_heal--;
                heal(c, 1, true);
                if (c->hit_points < c->max_hit_points) {
                    const uint8_t *k = (const uint8_t *)c->oncc + 0x5c;
                    c->heal_wait = (uint16_t)(k[0] | k[1] << 8);
                }
                waited = true;
            } else {
                c->hit_points = cap;
                c->hypo_heal = 0;
            }
        } else if (c->hit_points > c->max_hit_points) {
            c->hit_points--;
        } else {
            waited = true;
        }
        if (!waited) c->heal_wait = 10;
    }
    /* the shield's shown part (C+0x1b6) toward what is left, 2 a tick: with the shield's display */
    if (c->invisibility == 0) c->invisible_ticks = 0;
    else c->invisible_ticks++;
}

bool inventory_use_hypo(Character *c)
{
    uint32_t total = c->hypo_heal + c->hit_points, max = c->max_hit_points, to;
    if (!c->hypos || total > max) return false;
    if (total < max) {
        /* the settings' part of the maximum (+4); past the maximum, of the over part (+8) as much as the
           share of the dose that goes over */
        uint32_t dose = round_u((float)max * gs_float(4));
        to = dose + total;
        if (to > max) to = round_u((float)max * gs_float(8) * (1.0f - (float)(max - total) / (float)dose)) + max;
    } else {
        to = total + round_u((float)max * gs_float(8));
    }
    if (to > 0xffff) to = 0xffff;
    c->hypos--;
    c->heal_wait = 0;
    c->hypo_heal = (uint16_t)(to - c->hit_points);
    return true;
}

void inventory_invis_update(Character *c)
{
    uint16_t left = c->invisibility;
    bool count = true;
    if (left && !c->invisible_shown) {
        c->visibility = 1.0f;
        c->invisible_shown = true;
    } else if (c->invisible_shown) {
        /* toward none shown while more than 120 ticks are left, back to all after: 0.0085 a tick down,
           0.02 up (0x52b33c, 0x52b338); none shown with none left, it ends */
        float to = left > 120 ? 0.0f : 1.0f, v = c->visibility;
        if (v > to) {
            v -= 0.008500000461935997f;
            if (v < to) v = to;
        } else if (v < to) {
            v += 0.019999999552965164f;
            if (v > to) v = to;
        }
        c->visibility = v;
        if (v == 0.0f && left == 0) {
            c->invisible_shown = false;
            count = false;
        }
    }
    if (count && left && left != 0xffff) c->invisibility = (uint16_t)(left - 1);
    /* a teleport's first half (0xde) fades the body out from its action frame (TRAM +0x174) to its end,
       never above what was shown (A+0x2818 kept from before: 0 if never invisible) */
    if (c->type == 0xde && c->anim) {
        const uint8_t *t = (const uint8_t *)c->anim;
        uint16_t from = (uint16_t)(t[0x174] | t[0x175] << 8), dur = (uint16_t)(t[TRAM_DURATION] | t[TRAM_DURATION + 1] << 8);
        if (from <= c->frame) {
            float f = 1.0f - (float)((int)c->frame - (int)from + 1) / (float)((int)dur - (int)from);
            c->visibility = f >= 0.0f ? (c->visibility < f ? c->visibility : f) : 0.0f;
            c->flags |= 0x40000;
            return;
        }
    }
    c->flags &= ~0x40000u;
}

void inventory_reveal(Character *c, bool ticks)
{
    if (ticks) c->invisible_ticks = 0;
    if (c->invisible_shown && c->visibility < 0.6000000238418579f) c->visibility = 0.6000000238418579f;
}
