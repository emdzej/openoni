/* The screen's 2D layer: see ui_draw.h. */
#include "ui_draw.h"

#include "character.h"
#include "chr_anim.h"
#include "cinematic.h"
#include "exe.h"
#include "font.h"
#include "gl_dev.h"
#include "igui.h"
#include "text.h"
#include "tm.h"
#include "txmp.h"
#include "ui.h"
#include "weapon.h"
#include "wm.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const uint8_t c[4], unsigned tex)
{
    GlVertex v[4] = {
        { x0, y0, 0, u0, v0, c[0], c[1], c[2], c[3] }, { x1, y0, 0, u1, v0, c[0], c[1], c[2], c[3] },
        { x1, y1, 0, u1, v1, c[0], c[1], c[2], c[3] }, { x0, y1, 0, u0, v1, c[0], c[1], c[2], c[3] },
    };
    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    gl_dev_draw(v, 4, idx, 6, tex, GL_DEV_SCREEN);
}

static void rect(float x0, float y0, float x1, float y1, const uint8_t c[4])
{
    quad(x0, y0, x1, y1, 0, 0, 1, 1, c, gl_dev_white());
}

/* The glyph's texture: its coverage (font_glyph_alpha), kept under the glyph's place in its array. */
static unsigned glyph_texture(const void *font, const FontGlyph *g, const void *key)
{
    size_t n = (size_t)g->width * g->height;
    uint8_t *a = n ? malloc(n) : NULL;
    unsigned t = a && font_glyph_alpha(font, g, a) ? gl_dev_texture_alpha(key, g->width, g->height, a) : gl_dev_white();
    free(a);
    return t;
}

/* The colour of a code letter, from the table at 0x533da0 in the exe (8 bytes an entry: the letter as a
   u16 in a u32, the ARGB colour; a 0 letter ends it). False if the letter has none. */
enum { COLOUR_CODES = 0x533da0 };
static bool code_colour(uint16_t ch, uint32_t *argb)
{
    for (uint32_t va = COLOUR_CODES;; va += 8) {
        const uint8_t *e = exe_data(va, 8);
        if (!e) return false;
        uint16_t k = (uint16_t)(e[0] | e[1] << 8);
        if (!k) return false;
        if (k == ch) {
            *argb = exe_u32(va + 4);
            return true;
        }
    }
}

static float walk(const void *font, float x, float y, float scale, const char *s, const uint8_t base[4], bool draw)
{
    /* TSiContext_DrawLine 0x42d860: the pen at the baseline; each glyph's bitmap at pen - (x, y offsets),
       the pen moved by the advance. With formatting (TSrContext_FormatString 0x42e210): "[" opens a code,
       in which a letter of the table sets the colour and "." closes it (both unseen; any other character
       closes it and is drawn), and "]" puts the colour back. */
    const char *p = s;
    uint16_t ch;
    bool code = false;
    uint8_t c[4];
    memcpy(c, base, 4);
    while ((ch = font_next_char(&p))) {
        if (ch == '[') { code = true; continue; }
        if (ch == ']') { memcpy(c, base, 3); continue; }
        if (code) {
            uint32_t argb;
            if (ch == '.') { code = false; continue; }
            if (code_colour(ch, &argb)) {
                c[0] = (uint8_t)(argb >> 16), c[1] = (uint8_t)(argb >> 8), c[2] = (uint8_t)argb;
                continue;
            }
            code = false;
        }
        FontGlyph g;
        if (!font_glyph(font, ch, &g)) continue;
        if (draw && g.width && g.height) {
            /* a key per font and character: the font's data plus the character (a font is far bigger) */
            const void *key = (const uint8_t *)font + 1 + ch;
            unsigned t = glyph_texture(font, &g, key);
            float gx = x + (float)(-g.x) * scale, gy = y + (float)(-g.y) * scale;
            quad(gx, gy, gx + g.width * scale, gy + g.height * scale, 0, 0, 1, 1, c, t);
        }
        x += g.advance * scale;
    }
    return x;
}

float ui_draw_text(const void *font, float x, float y, float scale, const char *s, const uint8_t c[4])
{
    return walk(font, x, y, scale, s, c, true);
}

static float wm_scale;
static void wm_glyphs(const void *font, float x, float y, const char *str, size_t n, uint32_t argb, const int16_t clip[4]);

/* A run of a text area's glyphs, unclipped. */
static void area_glyphs(const void *font, int16_t x, int16_t y, const char *s, uint16_t n, uint32_t argb, void *u)
{
    (void)u;
    static const int16_t all[4] = { -32768, -32768, 32767, 32767 };
    wm_glyphs(font, x, y, s, n, argb, all);
}

void ui_draw_letterbox(int w, int h)
{
    gl_dev_screen(w, h);
    /* The original draws at the display's own pixels; the port draws as at 640x480, scaled. */
    float lb = ui_letterbox() * ((float)h / 480.0f);
    if (lb > 0) {
        static const uint8_t black[4] = { 0, 0, 0, 255 };
        rect(0, 0, (float)w, lb, black);
        rect(0, (float)h - lb, (float)w, (float)h, black);
    }
}

void ui_draw(int w, int h)
{
    gl_dev_screen(w, h);
    float scale = (float)h / 480.0f;
    /* the console's text areas (ONrGameState_Display: 0x430400, 0x4303e0), at 640 by 480 */
    wm_scale = scale;
    ui_area_display((int16_t)((float)w / scale), area_glyphs, NULL);
    /* the fade over everything (ONrBlanketScreen) */
    float f[4];
    if (ui_fade(f)) {
        uint8_t c[4] = { (uint8_t)(255 * f[0]), (uint8_t)(255 * f[1]), (uint8_t)(255 * f[2]), (uint8_t)(255 * f[3]) };
        rect(0, 0, (float)w, (float)h, c);
    }
    /* the cinematic inserts, after the fade (OCiCinematic_Draw 0x4f2910): the 1024 by 728 screen
       scaled to the display (0x529cc8, 1/1024, and 1/728), mirrored by the texture coordinates */
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    for (int i = 0; i < cinematic_count(); i++) {
        CinematicInsert ci;
        if (!cinematic_get(i, &ci)) continue;
        float sx = (float)w / 1024.0f, sy = (float)h / 728.0f;
        float x0 = floorf(ci.x * sx), y0 = floorf(ci.y * sy), x1 = floorf((ci.x + ci.w) * sx), y1 = floorf((ci.y + ci.h) * sy);
        quad(x0, y0, x1, y1, ci.mirror ? 1.0f : 0.0f, 0.0f, ci.mirror ? 0.0f : 1.0f, 1.0f, white, gl_dev_texture(ci.txmp));
    }
}

/* ---- the splash screens ---- */

static uint16_t tx16(const void *t, unsigned off);

static uint16_t rd16b(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* TXMB (instance data): +0x08 width, +0x0a height, +0x10 the tiles across, +0x12 down, +0x14 their
   count, +0x18 the TXMPs, by rows. */
void ui_draw_movie(int w, int h, unsigned tex, int mw, int mh)
{
    gl_dev_begin(w, h, 0.0f, 0.0f, 0.0f);
    gl_dev_screen(w, h);
    if (!tex) return;
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    float scale = (float)h / 480.0f, bw = (float)mw * scale, bh = (float)mh * scale;
    float x0 = ((float)w - bw) / 2, y0 = ((float)h - bh) / 2;
    quad(x0, y0, x0 + bw, y0 + bh, 0, 0, 1, 1, white, tex);
}

void ui_draw_splash(int w, int h, const char *name, float blanket)
{
    gl_dev_begin(w, h, 0.0f, 0.0f, 0.0f);
    gl_dev_screen(w, h);
    const uint8_t *b = tm_instance_get(TM_TAG("TXMB"), name);
    float scale = (float)h / 480.0f;
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    if (b) {
        float bw = (float)rd16b(b + 0x08) * scale, bh = (float)rd16b(b + 0x0a) * scale;
        float x0 = ((float)w - bw) / 2, y0 = ((float)h - bh) / 2, y = y0;
        uint16_t across = rd16b(b + 0x10), down = rd16b(b + 0x12);
        for (uint16_t r = 0, k = 0; r < down; r++) {
            float x = x0, th = 0;
            for (uint16_t c = 0; c < across; c++, k++) {
                const void *t = tm_link(b + 0x18 + 4 * k);
                if (!t) continue;
                float pw = (float)tx16(t, TXMP_WIDTH), ph = (float)tx16(t, TXMP_HEIGHT);
                float tw = pw * scale, du = pw > 0 ? 0.5f / pw : 0, dv = ph > 0 ? 0.5f / ph : 0;
                th = ph * scale;
                /* half a texel in from the edges, so the filtering doesn't wrap round the tile */
                quad(x, y, x + tw, y + th, du, dv, 1 - du, 1 - dv, white, gl_dev_texture(t));
                x += tw;
            }
            y += th;
        }
    }
    if (blanket > 0.0f) {
        uint8_t c[4] = { 0, 0, 0, (uint8_t)(255.0f * (blanket > 1.0f ? 1.0f : blanket)) };
        rect(0, 0, (float)w, (float)h, c);
    }
}

/* ---- the HUD ---- */

static uint16_t tx16(const void *t, unsigned off) { const uint8_t *p = (const uint8_t *)t + off; return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32p(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdfp(const uint8_t *p) { uint32_t v = rd32p(p); float f; memcpy(&f, &v, 4); return f; }

/* The HUD's blend: a texture with flag 0x400 draws added (the OpenGL back end's 0x40d640), else by its
   alpha. */
static unsigned screen_mode(const void *tex, unsigned mode)
{
    return mode == GL_DEV_SCREEN && tex && (tx16(tex, TXMP_FLAGS) & 0x400) ? GL_DEV_SCREEN_ADD : mode;
}

/* An arc (0x5045a0, 0x5007d0): segments + 1 points from angle a0 to a1 at radii ro and ri around the
   centre, the texture's coordinates the same points on the ring texture (its middle the centre, its
   half width the scale); drawn (0x500640) as a quad per segment. */
static void arc(float cx, float cy, float a0, float a1, float ro, float ri, int segments, const void *ring,
                float scale, uint32_t argb, uint8_t alpha)
{
    enum { MAX_SEG = 32 };
    if (!ring || segments > MAX_SEG) return;
    float half = (float)(tx16(ring, TXMP_WIDTH) >> 1);
    GlVertex v[2 * (MAX_SEG + 1)];
    uint16_t idx[6 * MAX_SEG];
    uint8_t r = (uint8_t)(argb >> 16), g = (uint8_t)(argb >> 8), b = (uint8_t)argb;
    for (int i = 0; i <= segments; i++) {
        float a = (float)i * ((a1 - a0) / (float)segments) + a0;
        while (a < 0.0f) a += 6.2831855f;
        while (a > 6.2831855f) a -= 6.2831855f;
        float co = cosf(a), si = sinf(a);
        v[2 * i] = (GlVertex){ cx + co * ro * scale, cy + si * ro * scale, 0, co * 0.5f * (ro / half) + 0.5f,
                               si * 0.5f * (ro / half) + 0.5f, r, g, b, alpha };
        v[2 * i + 1] = (GlVertex){ cx + co * ri * scale, cy + si * ri * scale, 0, co * 0.5f * (ri / half) + 0.5f,
                                   si * 0.5f * (ri / half) + 0.5f, r, g, b, alpha };
    }
    for (int i = 0; i < segments; i++) {
        uint16_t o = (uint16_t)(2 * i);
        uint16_t q[6] = { o, (uint16_t)(o + 2), (uint16_t)(o + 1), (uint16_t)(o + 1), (uint16_t)(o + 2), (uint16_t)(o + 3) };
        memcpy(idx + 6 * i, q, sizeof q);
    }
    gl_dev_draw(v, (uint32_t)(2 * (segments + 1)), idx, (uint32_t)(6 * segments), gl_dev_texture(ring),
                screen_mode(ring, GL_DEV_SCREEN));
}

/* A sprite (M3rDraw_Sprite) from (x0, y0) to (x1, y1), flipped upside down or not. */
static void sprite(float x0, float y0, float x1, float y1, bool flip, const void *tex, uint8_t alpha, unsigned mode)
{
    float v0 = flip ? 1.0f : 0.0f, v1 = 1.0f - v0;
    GlVertex v[4] = {
        { x0, y0, 0, 0, v0, 255, 255, 255, alpha },
        { x1, y0, 0, 1, v0, 255, 255, 255, alpha },
        { x1, y1, 0, 1, v1, 255, 255, 255, alpha },
        { x0, y1, 0, 0, v1, 255, 255, 255, alpha },
    };
    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    gl_dev_draw(v, 4, idx, 6, gl_dev_texture(tex), screen_mode(tex, mode));
}

/* The left element (ONrInGameUI_Display 0x4fff50, while ui_show_element left is on): the "left"
   picture, its ring's arcs (ONrInGameUI_SetArcPoints 0x504270, untinted) for the ballistic clips (from pi
   to 3 pi / 2, radii 64 and 51.5, a segment a clip, 6 at most) and the energy cells (from pi back to
   pi / 2), and the compass toward target_set's flag: an arc (radii 50 and 46, 32 segments) centred on
   the flag's bearing from the player's facing, pi either side within the target's radius, else
   narrowing with the distance from pi / 2 (as far as when it was set) to 0.349; then the "arrow" up
   (the flag more than 10 above) or upside down (more than 10 below), 36.5 right of the centre.
   Each flashes by its ui_flash_element bit (ballistic, energy, compass, arrow). Then the weapon's
   displays (draw_weapon). ui_fill_element's bits (0x5ed09c) draw the clips, the cells, the compass and
   the ammo's ring full and the arrow up. */
static void draw_compass(const Character *c, float cx, float cy, const void *ring, const void *arrow, float scale);

/* At the left element's centre: the ammo (0x500ea0) of the hand's weapon, else of the stowed one
   (WPrSlot_FindLastStowed 0x51bc70): its class's icon (+0x50) at its size and, with ammo, the ring
   (class +0x54; radii 44 and 30, 24 segments, from pi / 12 (0x52a75c) on by the ammo's share of 5.76,
   0x52b40c), flashing by the ammo bit; then (0x501240) the hand's weapon's picture (class +0x4c), 54
   square from 27 up and left of the centre, flashing by the weapon bit. An owner of unlimited ammo
   (C+0x04 0x40) shows it full: the original's getter (0x517850) fills it there. */
static void draw_weapon(const Character *c, const void *left, float cx, float cy, float scale)
{
    const Weapon *w = c->weapon ? c->weapon : chr_slot(c, 1) ? chr_slot(c, 1) : chr_slot(c, 2);
    const void *icon = w ? tm_link(w->cls + 0x50) : NULL;
    if (icon) {
        /* at the "left" picture's size, not its own (0x500f3d: 0x5ed020's +0x84, +0x86) */
        float lw = (float)tx16(left, TXMP_WIDTH), lh = (float)tx16(left, TXMP_HEIGHT);
        float x0 = cx - (float)((int)lw >> 1) * scale, y0 = cy - (float)((int)lh >> 1) * scale;
        sprite(x0, y0, x0 + lw * scale, y0 + lh * scale, false, icon, 255, GL_DEV_SCREEN);
        unsigned full = (unsigned)(w->cls[0xc0] | w->cls[0xc1] << 8);
        unsigned ammo = (ui_filled(0x40) || (w->owner && (w->owner->flags & 0x40))) ? full : w->ammo;
        const void *ring = tm_link(w->cls + 0x54);
        if (ammo && full && ring)
            arc(cx, cy, 0.2617994f, (float)ammo / (float)full * 5.7595863f + 0.2617994f, 44.0f, 30.0f, 24, ring, scale,
                0xffffffffu, ui_flash_alpha(0x40));
    }
    const void *pic = c->weapon ? tm_link(c->weapon->cls + 0x4c) : NULL;
    if (pic) {
        float x0 = cx - 27.0f * scale, y0 = cy - 27.0f * scale;     /* 0x52b41c, 0x52b418 */
        sprite(x0, y0, x0 + 54.0f * scale, y0 + 54.0f * scale, false, pic, ui_flash_alpha(0x20), GL_DEV_SCREEN);
    }
}

static void draw_left(int w, int h, const Character *c, float scale)
{
    (void)w;
    const void *left = tm_instance_get(TM_TAG("TXMP"), "left"), *ring = tm_instance_get(TM_TAG("TXMP"), "leftRing");
    const void *arrow = tm_instance_get(TM_TAG("TXMP"), "arrow");
    if (!left) return;
    float lw = (float)tx16(left, TXMP_WIDTH), lh = (float)tx16(left, TXMP_HEIGHT);
    float cx = (10.0f + (float)((int)lw >> 1)) * scale, cy = (float)h - (float)((int)lh >> 1) * scale;
    sprite(cx - lw * 0.5f * scale, cy - lh * 0.5f * scale, cx + lw * 0.5f * scale, cy + lh * 0.5f * scale, false,
           left, 255, GL_DEV_SCREEN_ADD);
    unsigned clips = ui_filled(0x200) || c->ammo > 6 ? 6 : c->ammo, cells = ui_filled(0x400) || c->cells > 6 ? 6 : c->cells;
    if (clips)
        arc(cx, cy, 3.1415927f, 3.1415927f + (4.712389f - 3.1415927f) * (float)clips / 6.0f, 64.0f, 51.5f,
            (int)clips, ring, scale, 0xffffffffu, ui_flash_alpha(0x200));
    if (cells)
        arc(cx, cy, 3.1415927f, 3.1415927f + (1.5707964f - 3.1415927f) * (float)cells / 6.0f, 64.0f, 51.5f,
            (int)cells, ring, scale, 0xffffffffu, ui_flash_alpha(0x400));
    draw_compass(c, cx, cy, ring, arrow, scale);
    draw_weapon(c, left, cx, cy, scale);
}

static void draw_compass(const Character *c, float cx, float cy, const void *ring, const void *arrow, float scale)
{
    float tp[3], r2, d0, dy = 0.0f;
    bool on = ui_target(tp, &r2, &d0);
    if (on) dy = tp[1] - c->pos[1];
    /* flashing by its bit or while a new objective is unread (0x5ed098); whole when filled, or without
       a target while the objective is unread */
    uint8_t alpha = ui_flashing(0x80) || ui_objective_unread() ? ui_flash_value() : 255;
    if (ui_filled(0x80) || (!on && ui_objective_unread())) {
        arc(cx, cy, 0.0f, 6.2831855f, 50.0f, 46.0f, 32, ring, scale, 0xffffffffu, alpha);
    } else if (on) {
        float dx = tp[0] - c->pos[0], dz = tp[2] - c->pos[2];
        float d2 = dx * dx + dy * dy + dz * dz, half;
        if (d2 < r2) half = 3.1415927f;
        else if (d2 <= d0) {
            half = 1.5707964f - d2 / d0 * 1.5707964f;
            if (half < 0.34906584f) half = 0.34906584f;
            else if (half > 3.1415927f) half = 3.1415927f;
        } else half = 0.34906584f;
        /* the facing (ONrCharacter_GetFacing 0x4e33a0: the desired facing less the run's turn, in
           [0, 2 pi)) less the bearing */
        float facing = c->desired_facing - c->facing_offset;
        if (facing < 0.0f) facing += 6.2831855f;
        else if (facing >= 6.2831855f) facing -= 6.2831855f;
        float rel = facing - atan2f(dx, dz);
        if (rel < -3.1415927f) rel += 6.2831855f;
        else if (rel > 3.1415927f) rel -= 6.2831855f;
        arc(cx, cy, rel - half + 4.712389f, rel + half + 4.712389f, 50.0f, 46.0f, 32, ring, scale, 0xffffffffu,
            alpha);
    }
    /* the arrow: up for a flag more than 10 above, upside down more than 10 below; filled, up but for
       the one below */
    bool down = on && dy < -10.0f, up = (on && dy > 10.0f) || (ui_filled(0x100) && !down);
    if (arrow && (up || down)) {
        float x0 = cx + (36.5f - 6.0f) * scale, y0 = cy - 6.0f * scale;
        sprite(x0, y0, x0 + 12.0f * scale, y0 + 12.0f * scale, down, arrow, ui_flash_alpha(0x100), GL_DEV_SCREEN);
    }
}

/* The right element after the health (ONrInGameUI_Display 0x4fff50): the invisibility's arc (radii 63
   and 51, 8 segments, from -0.0349 (0x52b3e0) by its share of 1800 ticks of -1.518, 0x52b3b8 and
   0x52b3e4; the invis bit flashing); the shield's (ONiInGameUI_DisplayShield 0x501080: igui_hud's two,
   radii 63 and 51, 8 segments, the eased part at 0x7f; flashing by the shield bit, the eased part then
   at the flash squared over 127, cut to a byte, as the original); the hypos (ONiInGameUI_DisplayHypos
   0x500e30: one arc of a segment each, radii 63 and 51, up to 6, their angles the exe's table at
   0x555480; the hypo bit); the hits' (0x500db0: a side's arc at its fading C+0x166c, radii 37 and 30,
   4 segments, the angles the exe's table at 0x5554b0; the hits bit); the LSI's icon
   (ONiInGameUI_DisplayLSIIcon 0x501330: "lsi_icon", 54 square from 27 up and left of the centre, at
   0x55 while the timer runs (0x4fd360), the lsi bit). ui_fill_element's bits draw the invisibility's
   arc and the hypos full and the hit arcs at 255. */
static void draw_inventory(const Character *c, const IguiHud *hud, float cx, float cy, const void *ring, float scale)
{
    if (ui_filled(0x4))
        arc(cx, cy, -0.034906585f, -1.553343f, 63.0f, 51.0f, 8, ring, scale, 0xffffffffu, ui_flash_alpha(0x4));
    else if (c->invisibility)
        arc(cx, cy, -0.034906585f, (float)c->invisibility * 0.00055555557f * -1.5184364f - 0.034906585f, 63.0f, 51.0f,
            8, ring, scale, 0xffffffffu, ui_flash_alpha(0x4));
    unsigned s1 = 0xff, s2 = 0x7f;
    if (ui_flashing(0x8)) {
        unsigned fl = ui_flash_alpha(0x8);
        s1 = fl, s2 = (fl * fl / 127u) & 0xffu;
    }
    for (int i = 0; i < 2; i++)
        if (hud->shield[i].on)
            arc(cx, cy, hud->shield[i].a0, hud->shield[i].a1, 63.0f, 51.0f, 8, ring, scale, 0xffffffffu,
                (uint8_t)(i ? s2 : s1));
    unsigned hypos = ui_filled(0x10) || c->hypos > 6 ? 6 : c->hypos;
    for (unsigned i = 0; i < hypos; i++) {
        const uint8_t *t = exe_data(0x555480 + 8 * i, 8);
        if (t) arc(cx, cy, rdfp(t), rdfp(t + 4), 63.0f, 51.0f, 1, ring, scale, 0xffffffffu, ui_flash_alpha(0x10));
    }
    for (unsigned i = 0; i < 4; i++) {
        unsigned a = ui_filled(0x2) ? 0xffu : c->hit_flash[i];
        const uint8_t *t = exe_data(0x5554b0 + 8 * i, 8);
        if (!a || !t) continue;
        if (ui_flashing(0x2)) a = a * ui_flash_alpha(0x2) / 255u;
        arc(cx, cy, rdfp(t), rdfp(t + 4), 37.0f, 30.0f, 4, ring, scale, 0xffffffffu, (uint8_t)a);
    }
    const void *lsi = c->lsi ? tm_instance_get(TM_TAG("TXMP"), exe_str(0x555c18)) : NULL;
    if (lsi) {
        unsigned a = ui_timer_running() ? 0x55 : 0xff;
        a = a * ui_flash_alpha(0x800) / 255u;
        float x0 = cx - 27.0f * scale, y0 = cy - 27.0f * scale;
        sprite(x0, y0, x0 + 54.0f * scale, y0 + 54.0f * scale, false, lsi, (uint8_t)a, GL_DEV_SCREEN);
    }
}

static void draw_right(int w, int h, const Character *c, const void *right, const void *ring, uint32_t time, float scale);
static void draw_help(int w, float scale);

void ui_draw_hud(int w, int h, const Character *c, uint32_t time)
{
    const void *right = tm_instance_get(TM_TAG("TXMP"), "right"), *ring = tm_instance_get(TM_TAG("TXMP"), "rightRing");
    uint32_t max = c->max_hit_points;
    gl_dev_screen(w, h);
    /* the original draws at the display's pixels; the port as at 640x480, scaled */
    float scale = (float)h / 480.0f;
    if (ui_element_shown(0)) draw_left(w, h, c, scale);
    if (right && max && ui_element_shown(1)) draw_right(w, h, c, right, ring, time, scale);
    draw_help(w, scale);
}

/* The help overlays (ONiInGameUI_DisplayHelp 0x501440), while the pause screen's help page is up
   (0x5ed099, or ui_show_help): the IGHH "hud_help_info"'s two pictures, each where its element is shown,
   at its element's centre (rounded, 0x4340a0) plus the IGHH's offset, less half the picture's size (the
   HUD's own picture's if the IGHH has none, else 64); then its labels in Tahoma 7 (format 1), green
   (0xff00ff00), the left element's first ones left-aligned 3 right of the left picture's corner, the
   others right-aligned 3 left of the right's, each centred on its point's height, under a shadow 1 right
   and down in 0xff3f3f3f. */
static void draw_help(int w, float scale)
{
    const void *ighh = igui_help_shown() ? tm_instance_get(TM_TAG("IGHH"), exe_str(0x555c08)) : NULL;
    if (!ighh) return;
    TextIghh hh;
    text_ighh(ighh, &hh);
    float dw = (float)w / scale;
    const void *own[2] = { tm_instance_get(TM_TAG("TXMP"), "left"), tm_instance_get(TM_TAG("TXMP"), "right") };
    int16_t pt[2][2];
    for (int k = 0; k < 2; k++) {
        /* the elements' centres (ONrInGameUI_SetArcPoints 0x504270) */
        float half_w = own[k] ? (float)(tx16(own[k], TXMP_WIDTH) >> 1) : 0.0f;
        float half_h = own[k] ? (float)(tx16(own[k], TXMP_HEIGHT) >> 1) : 0.0f;
        float cx = k ? dw - (10.0f + half_w) : 10.0f + half_w, cy = 480.0f - half_h;
        pt[k][0] = (int16_t)((int16_t)lrintf(cx) + hh.x[k]);
        pt[k][1] = (int16_t)((int16_t)lrintf(cy) + hh.y[k]);
        const void *t = hh.texture[k] ? hh.texture[k] : own[k];
        if (t) {
            pt[k][0] = (int16_t)(pt[k][0] - (tx16(t, TXMP_WIDTH) >> 1));
            pt[k][1] = (int16_t)(pt[k][1] - (tx16(t, TXMP_HEIGHT) >> 1));
        } else pt[k][0] = (int16_t)(pt[k][0] - 64), pt[k][1] = (int16_t)(pt[k][1] - 64);
        if (hh.texture[k] && ui_element_shown(k)) {
            float x0 = pt[k][0] * scale, y0 = pt[k][1] * scale;
            /* under the HUD's draw states, the constant colour white (state 7, -1); left_help and
               right_help have the flag 0x400, drawn added */
            sprite(x0, y0, x0 + tx16(hh.texture[k], TXMP_WIDTH) * scale, y0 + tx16(hh.texture[k], TXMP_HEIGHT) * scale,
                   false, hh.texture[k], 255, GL_DEV_SCREEN);
        }
    }
    const void *family = font_family_get(exe_str(0x534354));
    uint16_t size = 7;
    uint32_t style = FONT_STYLE_PLAIN;
    const void *font = family ? font_family_best_font(family, &size, &style) : NULL;
    if (!font) return;
    wm_scale = scale;
    const int16_t rect[4] = { 0, 0, (int16_t)dw, 480 };
    static const int16_t all[4] = { -32768, -32768, 32767, 32767 };
    FontInfo fi;
    font_info(font, &fi);
    for (uint32_t i = 0; i < hh.count; i++) {
        bool is_right = i >= hh.left_count;
        if (!ui_element_shown(is_right)) continue;
        const char *text;
        size_t len;
        int16_t lx, ly;
        if (!text_ighh_label(ighh, i, &text, &len, &lx, &ly)) continue;
        char buf[65];
        if (len > 64) len = 64;
        memcpy(buf, text, len), buf[len] = 0;
        int16_t pen[2] = { (int16_t)(lx + pt[is_right][0] + (is_right ? -3 : 3)), (int16_t)(ly + pt[is_right][1]) };
        FontRun runs[FONT_RUNS];
        int16_t lh;
        int n = font_format_text(family, font, 0xff00ff00u, 1, buf, rect, pen, runs, &lh);
        if (!n) continue;
        /* the text's box: from the first run's top (its baseline less the ascending height) to the
           last's bottom (a line height lower) */
        int top = pen[1] - fi.ascending, bottom = pen[1] + runs[n - 1].line * lh - fi.ascending + lh;
        int dx = pen[0] - (is_right ? runs[0].x + runs[0].w : runs[0].x);
        int dy = pen[1] - (top + bottom) / 2;
        for (int pass = 0; pass < 2; pass++)
            for (int r = 0; r < n; r++)
                wm_glyphs(font, (float)(runs[r].x + dx + !pass), (float)(pen[1] + runs[r].line * lh + dy + !pass),
                          runs[r].s, runs[r].n, pass ? runs[r].colour : 0xff3f3f3fu, all);
    }
}

/* ONrGameState_Timer_Display 0x4fd500 (from 0x50061f, while the right element is shown): the text in
   the context of ONrGameState_Timer_LevelBegin 0x4fd270, Tahoma 7 bold, shaded white, centred on the
   right ring's centre (0x5ed010): the pen at the centre chopped (0x4264e0) less half the string's width
   (TSrContext_GetStringRect 0x42e920), and lower by half the font's ascending height (0x42eec0). */
static void draw_timer(float cx, float cy, float scale)
{
    char tt[32];
    const void *family = font_family_get(exe_str(0x534354));
    if (!family || !ui_timer_text(tt)) return;
    uint16_t size = 7;
    uint32_t style = FONT_STYLE_BOLD;
    const void *font = font_family_best_font(family, &size, &style);
    if (!font) return;
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    FontInfo fi;
    font_info(font, &fi);
    uint16_t tw = (uint16_t)walk(font, 0, 0, 1.0f, tt, white, false);
    int x = (int)cx - (tw >> 1), y = (int)cy + ((uint16_t)fi.ascending >> 1);
    ui_draw_text(font, (float)x * scale, (float)y * scale, scale, tt, white);
}

/* The right element (0x4fff50 on): its picture, the health and the inventory. */
static void draw_right(int w, int h, const Character *c, const void *right, const void *ring, uint32_t time, float scale)
{
    uint32_t hp = c->hit_points, max = c->max_hit_points;
    float rw = (float)tx16(right, TXMP_WIDTH), rh = (float)tx16(right, TXMP_HEIGHT);
    /* the right element's centre (ONrInGameUI_SetArcPoints 0x504270): 10 from the right edge, on the
       bottom */
    float cx = (float)w - (10.0f + (float)((int)rw >> 1)) * scale, cy = (float)h - (float)((int)rh >> 1) * scale;
    /* the element: an RGB555 picture on black with the texture flag 0x400, drawn added (0x40d640; the
       HUD's draw states, 0x41e170 with 0 = 3 textured, 2 = 2 filled polygons, 9 = 255, 7 = -1) */
    GlVertex bg[4] = {
        { cx - rw * 0.5f * scale, cy - rh * 0.5f * scale, 0, 0, 0, 255, 255, 255, 255 },
        { cx + rw * 0.5f * scale, cy - rh * 0.5f * scale, 0, 1, 0, 255, 255, 255, 255 },
        { cx + rw * 0.5f * scale, cy + rh * 0.5f * scale, 0, 1, 1, 255, 255, 255, 255 },
        { cx - rw * 0.5f * scale, cy + rh * 0.5f * scale, 0, 0, 1, 255, 255, 255, 255 },
    };
    static const uint16_t bi[6] = { 0, 1, 2, 0, 2, 3 };
    gl_dev_draw(bg, 4, bi, 6, gl_dev_texture(right), GL_DEV_SCREEN_ADD);
    /* DisplayHealth 0x500970: igui_hud's arcs (radii 50 and 40, the overhealth's 55, 24 segments) in
       the health's colour (0x4ef450, of the part shown while it falls), the eased parts at 0x50 (0x7f
       falling); unless rising, under 30% (3.33, 0x52b3fc) the health's arcs pulse. All by the health's
       flash bit. */
    const IguiHud *hud = igui_hud(c);
    const IguiBar *hb = &hud->health_bar;
    uint32_t v = hb->down ? (uint32_t)lrintf(hb->shown) : hp;
    uint32_t col = chr_health_colour((float)v / (float)max);
    unsigned a = ui_flash_alpha(0x1), a2 = (hb->down ? 0x7fu : 0x50u) * a / 255u;
    if (!hb->up) {
        float low = (float)hp / (float)max * 3.3333333f;
        if (low < 1.0f) {
            float period = low * 35.0f + 40.0f;
            float t = fmodf((float)time, low * 100.0f + 40.0f);
            if (t < period) a = (unsigned)lrintf((1.0f - (0.5f - low * 0.2f) * sinf(t * 3.1415927f / period)) * (float)a);
        }
    }
    for (int i = 0; i < 4; i++)
        if (hud->health[i].on)
            arc(cx, cy, hud->health[i].a0, hud->health[i].a1, (i & 1) ? 55.0f : 50.0f, 40.0f, 24, ring, scale, col,
                (uint8_t)(i < 2 ? a : a2));
    draw_inventory(c, hud, cx, cy, ring, scale);
    draw_timer(cx / scale, cy / scale, scale);
}

/* ---- the window manager's drawing ---- */

/* The WM's desktop is as tall as 480 (as the HUD): its pixels scaled to the display's. */
static float wm_scale = 1.0f;

static void clip_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                      const float clip[4], const uint8_t c[4], unsigned tex)
{
    if (x0 >= clip[2] || x1 <= clip[0] || y0 >= clip[3] || y1 <= clip[1] || x1 <= x0 || y1 <= y0) return;
    if (x0 < clip[0]) u0 += (u1 - u0) * (clip[0] - x0) / (x1 - x0), x0 = clip[0];
    if (y0 < clip[1]) v0 += (v1 - v0) * (clip[1] - y0) / (y1 - y0), y0 = clip[1];
    if (x1 > clip[2]) u1 -= (u1 - u0) * (x1 - clip[2]) / (x1 - x0), x1 = clip[2];
    if (y1 > clip[3]) v1 -= (v1 - v0) * (y1 - clip[3]) / (y1 - y0), y1 = clip[3];
    quad(x0, y0, x1, y1, u0, v0, u1, v1, c, tex);
}

static void argb_rgba(uint32_t argb, uint8_t c[4])
{
    c[0] = (uint8_t)(argb >> 16), c[1] = (uint8_t)(argb >> 8), c[2] = (uint8_t)argb, c[3] = (uint8_t)(argb >> 24);
}

static void wm_sprite(const void *txmp, const float r[4], const float uv[4], uint32_t argb)
{
    uint8_t c[4];
    argb_rgba(argb, c);
    float s = wm_scale;
    quad(r[0] * s, r[1] * s, r[2] * s, r[3] * s, uv[0], uv[1], uv[2], uv[3], c, gl_dev_texture(txmp));
}

/* TSiContext_DrawLine 0x42d860 for a run of n bytes, clipped to the rectangle. */
static void wm_glyphs(const void *font, float x, float y, const char *str, size_t n, uint32_t argb, const int16_t clip[4])
{
    uint8_t c[4];
    argb_rgba(argb, c);
    float s = wm_scale, cl[4] = { clip[0] * s, clip[1] * s, clip[2] * s, clip[3] * s };
    const char *p = str, *end = str + n;
    x *= s, y *= s;
    while (p < end) {
        uint16_t ch = font_next_char(&p);
        if (!ch) break;
        FontGlyph g;
        if (!font_glyph(font, ch, &g)) continue;
        if (g.width && g.height) {
            const void *key = (const uint8_t *)font + 1 + ch;
            unsigned t = glyph_texture(font, &g, key);
            float gx = x + (float)(-g.x) * s, gy = y + (float)(-g.y) * s;
            clip_quad(gx, gy, gx + g.width * s, gy + g.height * s, 0, 0, 1, 1, cl, c, t);
        }
        x += g.advance * s;
    }
}

static const WmDrawOps wm_ops = { wm_sprite, wm_glyphs };

void ui_draw_wm_begin(int w, int h, int *desk_w, int *desk_h)
{
    wm_scale = (float)h / 480.0f;
    *desk_w = (int)((float)w / wm_scale);
    *desk_h = 480;
    wm_set_draw_ops(&wm_ops);
    gl_dev_screen(w, h);
}
