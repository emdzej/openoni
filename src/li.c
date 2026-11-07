/* The input layer's keys: see li.h. */
#include "li.h"

#include "exe.h"
#include "platform.h"

/* DirectInput's set-1 scan codes as a US keyboard's characters, unshifted and shifted (0: none). */
static const char plain[0x3b] = "\0\x1b" "1234567890-=\b\0qwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0\0\0 ";
static const char shifted[0x3b] = "\0\x1b" "!@#$%^&*()_+\b\0QWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0\0\0 ";

/* The keys WM_KEYDOWN gives the LI (0x50f7a0's list), as virtual keys. */
static uint8_t virtual_key(uint8_t dik)
{
    switch (dik) {
    case 0x0f: return 0x09;                         /* Tab */
    case 0xc9: return 0x21; case 0xd1: return 0x22; /* Page Up, Down */
    case 0xcf: return 0x23; case 0xc7: return 0x24; /* End, Home */
    case 0xcb: return 0x25; case 0xc8: return 0x26; /* the arrows */
    case 0xcd: return 0x27; case 0xd0: return 0x28;
    case 0xd2: return 0x2d; case 0xd3: return 0x2e; /* Insert, Delete */
    case 0x57: return 0x7a; case 0x58: return 0x7b; /* F11, F12 */
    }
    if (dik >= 0x3b && dik <= 0x44) return (uint8_t)(0x70 + dik - 0x3b);    /* F1 to F10 */
    return 0;
}

int li_key_presses(uint8_t *codes, int cap)
{
    uint8_t dik[64], keys[KEY_COUNT] = { 0 };
    int n = plat_key_presses(dik, (int)sizeof dik), out = 0;
    plat_keys(keys);
    bool shift = keys[0x2a] || keys[0x36];
    for (int i = 0; i < n && out < cap; i++) {
        uint8_t vk = virtual_key(dik[i]), c = 0;
        if (vk) c = exe_data(0x5293b8 + vk, 1) ? *exe_data(0x5293b8 + vk, 1) : 0;
        else if (dik[i] == 0x9c) c = '\r';          /* the keypad's Enter */
        else if (dik[i] < sizeof plain - 1) c = (uint8_t)(shift ? shifted : plain)[dik[i]];
        if (c) codes[out++] = c;
    }
    return out;
}

/* ---- The bindings ---- */

#include <stdlib.h>
#include <string.h>

enum {
    BINDINGS = 100,
    ACTIONS = 0x52db18, ACTION_SIZE = 0x24,     /* u16 type (1 a button, 2 to 5 an axis), u16 index, name */
    KEY_NAMES = 0x52e220, KEY_NAME_SIZE = 0x24, /* name[32], u32 code */
    DIK_TO_LI = 0x5292b8,                       /* 256 bytes: DirectInput's codes as LI codes */
};
static struct { uint32_t key; const uint8_t *action; } bindings[BINDINGS];  /* 0x55fa20 */

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* UUrString_Compare_NoCase 0x4266d0 (a to z folded) */
static bool same(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) a++, b++;
    return lower((unsigned char)*a) == lower((unsigned char)*b);
}

uint32_t li_key_code(char *name)
{
    for (char *p = name; *p; p++) *p = (char)lower((unsigned char)*p);
    if (name[0] && !name[1]) return (uint32_t)(int)(signed char)name[0];
    for (uint32_t at = KEY_NAMES;; at += KEY_NAME_SIZE) {
        const uint8_t *e = exe_data(at, KEY_NAME_SIZE);
        if (!e || !e[0]) return 0;
        if (!strncmp((const char *)e, name, 32)) return e[0x20] | e[0x21] << 8 | e[0x22] << 16 | (uint32_t)e[0x23] << 24;
    }
}

static const uint8_t *action_named(const char *name)
{
    for (uint32_t at = ACTIONS;; at += ACTION_SIZE) {
        const uint8_t *e = exe_data(at, ACTION_SIZE);
        if (!e || !e[4]) return NULL;
        char n[33];
        memcpy(n, e + 4, 32), n[32] = 0;
        if (same(n, name)) return e;
    }
}

int li_bind(uint32_t key, const char *action)
{
    if (!key) return 0;
    int at = -1;
    for (int i = 0; i < BINDINGS && at < 0; i++)
        if (bindings[i].key == key) at = i;
    const uint8_t *a = action_named(action);
    if (!a) return 0;
    if (at >= 0 && key != 0x1b) {
        bindings[at].action = a;
        return 0;
    }
    for (int i = 0; i < BINDINGS; i++)
        if (!bindings[i].key) {
            bindings[i].key = key, bindings[i].action = a;
            return 0;
        }
    return 2;
}

void li_unbind_all(void)
{
    memset(bindings, 0, sizeof bindings);
    const char *e = exe_str(0x52ecf8);     /* "escape" */
    if (e) li_bind(0x1b, e);
}

/* strtok on " \t" (0x54d150), as 0x5205b2 */
static char *word(char **p)
{
    char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return *p = s, NULL;
    char *w = s;
    while (*s && *s != ' ' && *s != '\t') s++;
    if (*s) *s++ = 0;
    *p = s;
    return w;
}

void li_config_run(const char *text, size_t n)
{
    char line[256];
    size_t i = 0;
    while (i < n) {
        size_t k = 0;
        while (i < n && text[i] != '\n') {
            if (k < sizeof line - 1) line[k++] = text[i];
            i++;
        }
        i++;
        line[k] = 0;
        if (k && line[k - 1] == '\r') line[--k] = 0;     /* BFrTextFile's lines */
        /* 0, '\n', '\v', '\r' and '#' first: skipped */
        if (!line[0] || line[0] == '\v' || line[0] == '\r' || line[0] == '#') continue;
        char *p = line, *w = word(&p);
        if (!w) continue;
        if (same(w, "unbindall")) {
            li_unbind_all();
            continue;
        }
        if (!same(w, "bind")) continue;
        char *key = word(&p), *to = key ? word(&p) : NULL, *act = to ? word(&p) : NULL;
        if (key && to && act && same(to, "to")) {
            li_bind(li_key_code(key), act);
            continue;
        }
        const char *m1 = exe_str(0x54d12c), *m2 = exe_str(0x54d114);  /* COrConsole_Printf */
        if (m1) plat_log(m1);
        if (m2) plat_log(m2);
    }
}

/* 0x4d4a50's default lines, in the order it writes them: the strings' addresses (0, an empty line) */
static const uint32_t default_lines[] = {
    0x52ede0, 0, 0x54d458, 0x54d444, 0x54d430, 0x54d41c, 0x54d40c, 0x54d3fc, 0x54d3ec, 0x54d3dc, 0,
    0x54d3c8, 0x54d3ac, 0x54d390, 0x54d374, 0x54d358, 0x54d33c, 0x54d320, 0x54d30c, 0, 0x54d2f0,
    0x54d2d4, 0, 0x54d2bc, 0x54d2a0, 0, 0x54d284, 0x54d270, 0x54d25c, 0x54d238, 0x54d21c, 0, 0x54d208,
    0x54d1f4, 0x54d1d4, 0x54d1b8, 0x54d1a8, 0x54d18c, 0x54d17c, 0x54d168, 0x54d154, 0,
};

void li_config_load(uint8_t *(*load)(const char *name, size_t *size),
                    bool (*store)(const char *name, const void *data, size_t size),
                    uint8_t *(*game)(const char *name, size_t *size))
{
    const char *name = exe_str(0x54d46c);      /* "key_config.txt" */
    if (!name) return;
    size_t n = 0;
    uint8_t *f = load ? load(name, &n) : NULL;
    if (!f && game) f = game(name, &n);
    if (!f) {
        /* fopen "w" and each line written "%s\n" */
        size_t cap = 2048, len = 0;
        char *t = malloc(cap);
        if (!t) return;
        for (size_t i = 0; i < sizeof default_lines / sizeof *default_lines; i++) {
            const char *s = default_lines[i] ? exe_str(default_lines[i]) : "";
            size_t l = s ? strlen(s) : 0;
            if (len + l + 1 > cap) break;
            memcpy(t + len, s, l), len += l;
            t[len++] = '\n';
        }
        if (store) store(name, t, len);
        f = (uint8_t *)t, n = len;
    }
    li_config_run((const char *)f, n);
    free(f);
}

/* 0x403b30: an event (a key's code, the axis's value) into the frame's actions */
static void apply(LiActions *a, uint32_t key, float value, bool down)
{
    for (int i = 0; i < BINDINGS; i++) {
        if (bindings[i].key != key || !bindings[i].action) continue;
        const uint8_t *e = bindings[i].action;
        unsigned type = e[0] | e[1] << 8, index = e[2] | e[3] << 8;
        if (type == 1) {
            if (down) a->down |= (uint64_t)1 << index;
            else a->held |= (uint64_t)1 << index;
        } else if (!down && index == 7 && type >= 2 && type <= 4) a->aim_lr += value;
        else if (!down && index == 8 && type >= 2 && type <= 4) a->aim_ud += value;
        return;     /* the first slot with the key */
    }
}

void li_actions(LiActions *a, const PlatMouse *mouse, bool invert)
{
    static uint64_t last;
    memset(a, 0, sizeof *a);
    const uint8_t *map = exe_data(DIK_TO_LI, 256);
    if (!map) return;
    if (mouse) {
        /* DIMOUSESTATE's buttons as codes 1 to 4, then the axes 5 (x), 6 (y), 7 (the wheel) */
        static const uint32_t bit[4] = { MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE, MOUSE_BACK };
        for (int b = 0; b < 4; b++) {
            if (mouse->buttons & bit[b]) apply(a, (uint32_t)b + 1, 1.0f, false);
            if (mouse->pressed & bit[b]) apply(a, (uint32_t)b + 1, 1.0f, true);
        }
        apply(a, 5, mouse->dx * 2.0f * 0.125f, false);
        apply(a, 6, mouse->dy * 2.0f * (invert ? -0.125f : 0.125f), false);
        apply(a, 7, mouse->wheel * 2.0f * 0.125f, false);
    }
    uint8_t keys[KEY_COUNT] = { 0 }, dik[64];
    plat_keys(keys);
    for (int k = 0; k < 256; k++)
        if (keys[k] && map[k]) apply(a, map[k], 1.0f, false);
    int n = plat_key_presses(dik, (int)sizeof dik);
    for (int i = 0; i < n; i++)
        if (map[dik[i]]) apply(a, map[dik[i]], 1.0f, true);
    a->down |= a->held & ~last;
    last = a->held;
}
