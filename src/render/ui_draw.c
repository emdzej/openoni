/* The screen's 2D layer: see ui_draw.h. */
#include "ui_draw.h"

#include "cinematic.h"
#include "exe.h"
#include "font.h"
#include "gl_dev.h"
#include "tm.h"
#include "txmp.h"
#include "ui.h"

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

void ui_draw(int w, int h)
{
    gl_dev_screen(w, h);
    /* The original draws at the display's own pixels; the port draws as at 640x480, scaled. */
    float scale = (float)h / 480.0f;
    /* letterbox: two bars (ONrGameState_LetterBox_Display 0x4fceb0) */
    float lb = ui_letterbox() * scale;
    if (lb > 0) {
        static const uint8_t black[4] = { 0, 0, 0, 255 };
        rect(0, 0, (float)w, lb, black);
        rect(0, (float)h - lb, (float)w, (float)h, black);
    }
    /* the text areas: messages in the middle of the lower half, subtitles at the bottom (their places
       are inferred until COrTextArea's layout is ported) */
    const void *family = font_family_get("Tahoma");
    uint16_t size = 12;
    uint32_t style = FONT_STYLE_PLAIN;
    const void *font = family ? font_family_best_font(family, &size, &style) : NULL;
    if (font) {
        float lh = font_line_height(font) * scale;
        for (int area = 0; area < UI_AREAS; area++) {
            int n = ui_area_count(area);
            float y = area == UI_AREA_MESSAGE ? (float)h * 0.62f : (float)h - lb - lh * (float)(n + 1);
            for (int i = 0; i < n; i++) {
                float alpha;
                const UiLine *l = ui_area_line(area, i, &alpha);
                uint8_t c[4] = { (uint8_t)(l->colour >> 16), (uint8_t)(l->colour >> 8), (uint8_t)l->colour, (uint8_t)(255 * alpha) };
                static const uint8_t shadow_rgb[3] = { 0, 0, 0 };
                uint8_t sh[4] = { shadow_rgb[0], shadow_rgb[1], shadow_rgb[2], c[3] };
                float tw = walk(font, 0, 0, scale, l->text, c, false), x = ((float)w - tw) / 2;
                y += lh;
                ui_draw_text(font, x + scale, y + scale, scale, l->text, sh);
                ui_draw_text(font, x, y, scale, l->text, c);
            }
        }
    }
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

/* ---- the HUD ---- */

static uint16_t tx16(const void *t, unsigned off) { const uint8_t *p = (const uint8_t *)t + off; return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32p(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdfp(const uint8_t *p) { uint32_t v = rd32p(p); float f; memcpy(&f, &v, 4); return f; }

/* The health's colour (0x4ef450, which the Mac build's names call ONrCharacter_GetPartLocation): the
   game settings' (ONGS "game_settings", 0x4f4920) thresholds at +0x18 (their count at +0x14) and
   colours at +0x58, the colour lerped (0x447c10) between the two around the fraction. */
static uint32_t health_colour(const uint8_t *gs, float f)
{
    if (!gs) return 0xff00ff00u;
    if (f <= rdfp(gs + 0x18)) return rd32p(gs + 0x58);
    uint32_t n = rd32p(gs + 0x14);
    if (n > 16) n = 16;
    if (rdfp(gs + 0x14 + n * 4) <= f) return rd32p(gs + 0x54 + n * 4);
    uint32_t i = 1;
    while (i < n && !(f < rdfp(gs + 0x18 + (i - 1) * 4 + 4))) i++;
    float lo = rdfp(gs + 0x14 + i * 4), hi = rdfp(gs + 0x18 + i * 4);
    float t = hi > lo ? (f - lo) / (hi - lo) : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    uint32_t a = rd32p(gs + 0x54 + i * 4), b = rd32p(gs + 0x58 + i * 4), c = 0;
    for (int k = 0; k < 32; k += 8) {
        float x = (float)(a >> k & 0xff), y = (float)(b >> k & 0xff);
        c |= (uint32_t)lrintf(x + (y - x) * t) << k;
    }
    return c;
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
    gl_dev_draw(v, (uint32_t)(2 * (segments + 1)), idx, (uint32_t)(6 * segments), gl_dev_texture(ring), GL_DEV_SCREEN);
}

void ui_draw_hud(int w, int h, uint32_t hp, uint32_t max, uint32_t time)
{
    const void *right = tm_instance_get(TM_TAG("TXMP"), "right"), *ring = tm_instance_get(TM_TAG("TXMP"), "rightRing");
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
    if (!right || !max) return;
    gl_dev_screen(w, h);
    /* the original draws at the display's pixels; the port as at 640x480, scaled */
    float scale = (float)h / 480.0f;
    float rw = (float)tx16(right, TXMP_WIDTH), rh = (float)tx16(right, TXMP_HEIGHT);
    /* the right element's centre (ONrInGameUI_SetArcPoints 0x504270): 10 from the right edge, on the
       bottom */
    float cx = (float)w - (10.0f + (float)((int)rw >> 1)) * scale, cy = (float)h - (float)((int)rh >> 1) * scale;
    /* the element: an RGB555 picture on black, drawn added (inferred: its draw states, 0x41e170 with
       0 = 3, 1 = 0, 2 = 2, 9 = 255, 7 = -1, aren't decoded yet; drawn opaque its corners would show) */
    GlVertex bg[4] = {
        { cx - rw * 0.5f * scale, cy - rh * 0.5f * scale, 0, 0, 0, 255, 255, 255, 255 },
        { cx + rw * 0.5f * scale, cy - rh * 0.5f * scale, 0, 1, 0, 255, 255, 255, 255 },
        { cx + rw * 0.5f * scale, cy + rh * 0.5f * scale, 0, 1, 1, 255, 255, 255, 255 },
        { cx - rw * 0.5f * scale, cy + rh * 0.5f * scale, 0, 0, 1, 255, 255, 255, 255 },
    };
    static const uint16_t bi[6] = { 0, 1, 2, 0, 2, 3 };
    gl_dev_draw(bg, 4, bi, 6, gl_dev_texture(right), GL_DEV_SCREEN_ADD);
    /* DisplayHealth: the health from pi round to -pi/2 when full (-3 pi / 2 of the fraction, 0x52b408),
       the overhealth on from -pi/2 by 1.396 a maximum (0x52b404), up to the settings' cap (ONGS+0).
       Under 30% (3.33, 0x52b3fc) the arcs pulse. Not ported: the displayed health's easing toward the
       real one (0x630fc4) and the damage arcs (0x630fcc, 0x630fcd). */
    float f = (float)hp / (float)max, cap = gs ? rdfp(gs) : 2.0f;
    uint32_t col = health_colour(gs, (float)hp / (float)max);
    float alpha = 255.0f;
    float low = f * 3.3333333f;
    if (low < 1.0f) {
        float period = low * 35.0f + 40.0f;
        float t = fmodf((float)time, low * 100.0f + 40.0f);
        if (t < period) alpha *= 1.0f - (0.5f - low * 0.2f) * sinf(t * 3.1415927f / period);
    }
    uint8_t a = (uint8_t)lrintf(alpha);
    if (hp) arc(cx, cy, 3.1415927f, (f < 1.0f ? f : 1.0f) * -4.712389f + 3.1415927f, 50.0f, 40.0f, 24, ring, scale, col, a);
    if (f > 1.0f) {
        float over = (f < cap ? f : cap) - 1.0f;
        arc(cx, cy, -1.5707964f, over * -1.3962632f - 1.5707964f, 55.0f, 40.0f, 24, ring, scale, col, a);
    }
}
