/* Every font and text instance of every level (OPENONI_DATA=<dir>, default ./game; skipped without it),
   read through font.h and text.h, with the same summary lines as tools/onifont.py: the FNV-1a hashes
   per kind (the line formats are in the tool's docstring), the string widths, the best font lookups and
   a subtitle and a message by name. Every glyph's bitmap must lie in its font, and its code must be its
   slot. OPENONI_FONT_SUMMARY=<hash> checks the "all" hash. */
#include "exe.h"
#include "font.h"
#include "text.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int levels[] = { 0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };
enum { N_LEVELS = sizeof levels / sizeof levels[0] };

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t fnv(uint32_t h, const void *data, size_t n)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

enum { K_TSFF, K_SUBT, K_IGST, K_IGSA, K_IGPG, K_IGPA, K_IGHH, K_STNA, N_KINDS };
static const char *const kind_names[N_KINDS] = { "TSFF", "SUBT", "IGSt", "IGSA", "IGPG", "IGPA", "IGHH", "StNA" };
static const char *const count_names[N_KINDS][3] = {
    { "families", "fonts", "glyphs" }, { "arrays", "entries" }, { "strings" }, { "arrays", "strings" },
    { "pages" }, { "arrays", "pages" }, { "overlays", "labels" }, { "arrays", "strings" },
};
static uint32_t hash[N_KINDS], counts[N_KINDS][3];
static int failed;

static void feed(int k, const void *data, size_t n) { hash[k] = fnv(hash[k], data, n); }

static void feedf(int k, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    feed(k, buf, (size_t)n);
}

/* As the tool prints a link: the target's name, "-" if unnamed, "0" if none. */
static const char *link_name(const void *field)
{
    uint32_t h = rd32(field);
    const TmInstance *t = h ? tm_handle_instance(h) : NULL;
    if (!t || !t->data) return "0";
    return t->name ? t->name : "-";
}

static long array_count(const void *a, uint32_t tag) { return a ? (long)text_array_count(a, tag) : -1; }

static void header(const uint8_t *p, char *out, size_t cap)
{
    TextIgst h;
    text_igst(p, &h);
    snprintf(out, cap, "%s %u %08x %u %x", link_name(p + IGST_FAMILY), (unsigned)h.style, (unsigned)h.color,
             h.size, h.flags);
}

static void family(int lv, uint32_t i, const char *name, const void *fam)
{
    int n = font_family_count(fam);
    counts[K_TSFF][0]++;
    feedf(K_TSFF, "%d %u %s %d\n", lv, (unsigned)i, name, n);
    for (int k = 0; k < n; k++) {
        const uint8_t *f = font_family_at(fam, k);
        if (!f) {
            feedf(K_TSFF, "-\n");
            continue;
        }
        FontInfo in;
        font_info(f, &in);
        counts[K_TSFF][1]++;
        feedf(K_TSFF, "%u %u %u %u %u %u %u\n", in.size, (unsigned)in.style, in.ascending, in.descending,
              in.leading, in.unknown_12, (unsigned)in.words);
        for (unsigned hi = 0; hi < 256; hi++) {
            if (!tm_link(f + TSFT_GLYPH_ARRAYS + 4 * hi)) continue;
            feedf(K_TSFF, "%x\n", hi);
            for (unsigned lo = 0; lo < 256; lo++) {
                FontGlyph g;
                font_glyph(f, (uint16_t)(hi << 8 | lo), &g);
                if (!(g.code | g.advance | g.width | g.height | g.x | g.y | g.bitmap)) continue;
                const uint8_t *bm = font_glyph_bitmap(f, &g);
                if (!bm || g.code != (hi << 8 | lo)) {
                    fprintf(stderr, "font_test: %s font %d glyph %x: bad\n", name, k, hi << 8 | lo);
                    failed++;
                    continue;
                }
                counts[K_TSFF][2]++;
                feedf(K_TSFF, "%x %d %u %u %d %d %u\n", g.code, g.advance, g.width, g.height, g.x, g.y,
                      (unsigned)g.bitmap);
                feed(K_TSFF, bm, (size_t)g.width * g.height);
            }
        }
    }
}

static void level(int lv, const TmFile *f)
{
    char h[256];
    for (uint32_t i = 0; i < tm_file_instance_count(f); i++) {
        const TmInstance *ti = tm_file_instance(f, i);
        const uint8_t *p = ti->data;
        const char *name = ti->name ? ti->name : "-";
        if (!p) continue;
        switch (ti->tmpl->tag) {
        case TSFF_TAG:
            family(lv, i, name, p);
            break;
        case SUBT_TAG: {
            uint32_t n = text_subt_count(p);
            counts[K_SUBT][0]++;
            counts[K_SUBT][1] += n;
            feedf(K_SUBT, "%d %u %s %u\n", lv, (unsigned)i, name, (unsigned)n);
            for (uint32_t k = 0; k < n; k++) {
                const char *e, *t;
                if (!text_subt_entry(p, k, &e, &t)) {
                    fprintf(stderr, "font_test: level%d %s entry %u: past the .raw\n", lv, name, (unsigned)k);
                    failed++;
                    continue;
                }
                feed(K_SUBT, e, strlen(e));
                feed(K_SUBT, "\t", 1);
                feed(K_SUBT, t, strlen(t));
                feed(K_SUBT, "\n", 1);
            }
            break;
        }
        case IGST_TAG: {
            TextIgst s;
            text_igst(p, &s);
            header(p, h, sizeof h);
            counts[K_IGST][0]++;
            feedf(K_IGST, "%d %u %s ", lv, (unsigned)i, h);
            feed(K_IGST, s.text, s.length);
            feed(K_IGST, "\n", 1);
            break;
        }
        case IGSA_TAG: {
            uint32_t n = text_array_count(p, IGSA_TAG);
            counts[K_IGSA][0]++;
            counts[K_IGSA][1] += n;
            feedf(K_IGSA, "%d %u %s %u\n", lv, (unsigned)i, name, (unsigned)n);
            for (uint32_t k = 0; k < n; k++) {
                const void *s = text_array_at(p, IGSA_TAG, k);
                TextIgst t = { 0 };
                if (s) text_igst(s, &t);
                if (s) feed(K_IGSA, t.text, t.length);
                else feed(K_IGSA, "-", 1);
                feed(K_IGSA, "\n", 1);
            }
            break;
        }
        case IGPG_TAG: {
            TextIgpg g;
            text_igpg(p, &g);
            header(p, h, sizeof h);
            counts[K_IGPG][0]++;
            feedf(K_IGPG, "%d %u %s %s %ld %ld\n", lv, (unsigned)i, h, link_name(p + IGPG_IMAGE),
                  array_count(g.text, IGSA_TAG), array_count(g.hint, IGSA_TAG));
            break;
        }
        case IGPA_TAG: {
            uint32_t n = text_array_count(p, IGPA_TAG);
            counts[K_IGPA][0]++;
            counts[K_IGPA][1] += n;
            feedf(K_IGPA, "%d %u %s %u", lv, (unsigned)i, name, (unsigned)n);
            for (uint32_t k = 0; k < n; k++) {
                const void *pg = text_array_at(p, IGPA_TAG, k);
                TextIgpg g;
                if (!pg) {
                    feedf(K_IGPA, " -");
                    continue;
                }
                text_igpg(pg, &g);
                feedf(K_IGPA, " %ld/%ld", array_count(g.text, IGSA_TAG), array_count(g.hint, IGSA_TAG));
            }
            feed(K_IGPA, "\n", 1);
            break;
        }
        case IGHH_TAG: {
            TextIghh o;
            text_ighh(p, &o);
            counts[K_IGHH][0]++;
            counts[K_IGHH][1] += o.count;
            feedf(K_IGHH, "%d %u %s %s %s %d %d %d %d %u %u %u\n", lv, (unsigned)i, name,
                  link_name(p + IGHH_LEFT_TEXTURE), link_name(p + IGHH_RIGHT_TEXTURE), o.x[0], o.y[0], o.x[1],
                  o.y[1], (unsigned)o.left_count, (unsigned)o.right_count, (unsigned)o.count);
            for (uint32_t k = 0; k < o.count; k++) {
                const char *t;
                size_t len;
                int16_t x, y;
                text_ighh_label(p, k, &t, &len, &x, &y);
                feed(K_IGHH, t, len);
                feedf(K_IGHH, " %d %d\n", x, y);
            }
            break;
        }
        case STNA_TAG: {
            uint32_t n = text_array_count(p, STNA_TAG);
            counts[K_STNA][0]++;
            counts[K_STNA][1] += n;
            feedf(K_STNA, "%d %u %s %u\n", lv, (unsigned)i, name, (unsigned)n);
            for (uint32_t k = 0; k < n; k++) {
                size_t len;
                const char *t = text_string_array_at(p, k, &len);
                if (t) feed(K_STNA, t, len);
                else feed(K_STNA, "-", 1);
                feed(K_STNA, "\n", 1);
            }
            break;
        }
        }
    }
}

/* The lines after the kinds, from level 0 (the tool's "widths", "best" and the two lookups). */
static char extra[4][4096];

static void level0_lines(void)
{
    const void *fam = font_family_get("Tahoma");
    if (!fam) {
        fprintf(stderr, "font_test: no TSFFTahoma\n");
        failed++;
        return;
    }
    char *w = extra[0];
    w += sprintf(w, "widths:");
    for (int k = 0; k < font_family_count(fam); k++) {
        const void *f = font_family_at(fam, k);
        FontInfo in;
        if (!f) continue;
        font_info(f, &in);
        w += sprintf(w, " %u/%u=%d", in.size, (unsigned)in.style, font_string_width(f, "The quick brown fox, Konoko!"));
    }
    char *b = extra[1];
    b += sprintf(b, "best:");
    for (uint32_t style = 0; style < 3; style++)
        for (uint16_t size = 0; size <= 20; size++) {
            uint16_t s = size;
            uint32_t st = style;
            if (font_family_best_font(fam, &s, &st)) b += sprintf(b, " %u/%u", s, (unsigned)st);
            else b += sprintf(b, " -");
        }
    const char *t = text_subtitle("c01_01_01");
    snprintf(extra[2], sizeof extra[2], "subtitles c01_01_01: %s", t ? t : "-");
    t = text_message("XWELCOME");
    snprintf(extra[3], sizeof extra[3], "messages XWELCOME: %s", t ? t : "-");
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("font_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err)) {
        fprintf(stderr, "font_test: %s\n", err);
        return 1;
    }
    for (int k = 0; k < N_KINDS; k++) hash[k] = 0x811c9dc5u;
    for (int k = 0; k < N_LEVELS; k++) {
        int lv = levels[k];
        /* the game loads level 0 under the others: their links to the fonts resolve into it */
        if (!tm_initialize(err, sizeof err) || (lv && !tm_level_load(0, err, sizeof err)) ||
            !tm_level_load(lv, err, sizeof err)) {
            fprintf(stderr, "font_test: level%d: %s\n", lv, err);
            failed++;
            continue;
        }
        level(lv, tm_file_at(tm_file_slots() - 1));
        if (lv == 0) level0_lines();
        tm_terminate();
    }
    uint32_t all = 0x811c9dc5u;
    char line[8192];
    for (int k = 0; k < N_KINDS + 4; k++) {
        if (k < N_KINDS) {
            int n = snprintf(line, sizeof line, "%s:", kind_names[k]);
            for (int c = 0; c < 3 && count_names[k][c]; c++)
                n += snprintf(line + n, sizeof line - (size_t)n, " %s=%u", count_names[k][c], (unsigned)counts[k][c]);
            snprintf(line + n, sizeof line - (size_t)n, " hash=%08x", (unsigned)hash[k]);
        } else {
            snprintf(line, sizeof line, "%s", extra[k - N_KINDS]);
        }
        printf("%s\n", line);
        all = fnv(all, line, strlen(line));
        all = fnv(all, "\n", 1);
    }
    printf("all: hash=%08x\n", (unsigned)all);
    const char *want = getenv("OPENONI_FONT_SUMMARY");
    if (want && *want && strtoul(want, NULL, 16) != all) {
        fprintf(stderr, "font_test: the all hash differs from OPENONI_FONT_SUMMARY\n");
        failed++;
    }
    exe_free();
    vfs_unmount();
    printf("font_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
