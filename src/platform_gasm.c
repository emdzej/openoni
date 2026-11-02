/* gasm platform backend for openoni.wasm (gasm ABI 0, gasm:gl, raw keyboard, pointer). The only file that
   includes gasm.h. The game keeps the original's own loops (gasm_loop.h): gasm_loop_init mounts the data,
   gasm_main runs app_main, and plat_wait_frame returns to the runner between frames (stack switching
   where the runner has it, Asyncify otherwise; both modules come from one build, see CMakeLists.txt).

   Game data: the installed game folder (Oni.exe, GameDataFolder/...) as assets named by their paths
   (gasm-run --asset-dir game; openoni-extract or visextract make it from the CD). Names are tried as the
   game spells them; the runner matches folder entries case-insensitively. The original opens paths like
   "GameDataFolder\\level0_Final.dat": '\\' -> '/', ".." resolved. */
#include "app.h"
#include "platform.h"
#include "vfs.h"
#include <gasm.h>
#include <gasm_loop.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The window title (gasm 0.6 custom section, also readable by launchers without running the module). */
GASM_TITLE("Oni");

_Static_assert(PAD_A == GASM_BTN_A && PAD_B == GASM_BTN_B && PAD_X == GASM_BTN_X && PAD_Y == GASM_BTN_Y &&
               PAD_L == GASM_BTN_L && PAD_R == GASM_BTN_R && PAD_SELECT == GASM_BTN_SELECT &&
               PAD_START == GASM_BTN_START && PAD_UP == GASM_BTN_UP && PAD_DOWN == GASM_BTN_DOWN &&
               PAD_LEFT == GASM_BTN_LEFT && PAD_RIGHT == GASM_BTN_RIGHT, "PAD_* must be the gasm button bits");

static uint32_t pads[4];
static double frame_hz = APP_FRAME_HZ;
static uint64_t time_us;          /* the frame clock: microseconds at the start of this frame */
static double time_frac;
static uint32_t frames;
static uint8_t pointer[GASM_POINTER_BYTES];
static bool have_pointer;
static uint32_t input_flags = GASM_INPUT_KEYS_RAW;

void plat_log(const char *msg) { gasm_log(msg, (uint32_t)strlen(msg)); }
uint32_t plat_pad(int player) { return player >= 0 && player < 4 ? pads[player] : 0; }
bool plat_param(const char *name, char *dst, size_t cap) { return gasm_param_str(name, dst, (uint32_t)cap); }
void plat_set_frame_rate(double hz)
{
    frame_hz = hz;
    gasm_set_frame_rate(hz);
}
uint64_t plat_time_us(void) { return time_us; }
uint32_t plat_frames(void) { return frames; }
void plat_display_size(int *w, int *h)
{
    *w = (int)gasm_gl_width();
    *h = (int)gasm_gl_height();
}
bool plat_frame_shown(void) { return gasm_gl_frame_shown() != 0; }

static float lef32(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static uint32_t leu32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- mouse ---- */

_Static_assert(MOUSE_LEFT == GASM_MOUSE_LEFT && MOUSE_RIGHT == GASM_MOUSE_RIGHT && MOUSE_MIDDLE == GASM_MOUSE_MIDDLE &&
               MOUSE_BACK == GASM_MOUSE_BACK && MOUSE_FORWARD == GASM_MOUSE_FORWARD, "MOUSE_* must be the gasm bits");

bool plat_mouse(PlatMouse *m)
{
    memset(m, 0, sizeof *m);
    if (!have_pointer) return false;
    const uint8_t *p = pointer;
    m->x = lef32(p + GASM_POINTER_OFF_X);
    m->y = lef32(p + GASM_POINTER_OFF_Y);
    m->dx = lef32(p + GASM_POINTER_OFF_DX);
    m->dy = lef32(p + GASM_POINTER_OFF_DY);
    m->wheel = lef32(p + GASM_POINTER_OFF_WHEEL_Y);
    m->buttons = leu32(p + GASM_POINTER_OFF_BUTTONS);
    m->pressed = leu32(p + GASM_POINTER_OFF_PRESSED);
    m->released = leu32(p + GASM_POINTER_OFF_RELEASED);
    uint32_t flags = leu32(p + GASM_POINTER_OFF_FLAGS);
    m->inside = flags & GASM_POINTER_INSIDE;
    m->captured = flags & GASM_POINTER_IS_LOCKED;
    return true;
}

void plat_mouse_capture(bool on)
{
    uint32_t f = on ? input_flags | GASM_INPUT_POINTER_LOCKED | GASM_INPUT_POINTER_HIDDEN
                    : input_flags & ~(uint32_t)(GASM_INPUT_POINTER_LOCKED | GASM_INPUT_POINTER_HIDDEN);
    if (f != input_flags) gasm_input_mode(input_flags = f);
}

/* ---- raw keyboard (gasm 0.5) ---- */

/* GASM_KEY_* (W3C KeyboardEvent.code) -> DirectInput code (DIK_*, from OpenBallance): the set-1 scan
   code with 0x80 for extended keys, what the game reads (see platform.h). */
static const struct { uint8_t key, dik; } DIK_OF[] = {
    {GASM_KEY_ESCAPE, 0x01}, {GASM_KEY_DIGIT1, 0x02}, {GASM_KEY_DIGIT2, 0x03}, {GASM_KEY_DIGIT3, 0x04},
    {GASM_KEY_DIGIT4, 0x05}, {GASM_KEY_DIGIT5, 0x06}, {GASM_KEY_DIGIT6, 0x07}, {GASM_KEY_DIGIT7, 0x08},
    {GASM_KEY_DIGIT8, 0x09}, {GASM_KEY_DIGIT9, 0x0a}, {GASM_KEY_DIGIT0, 0x0b}, {GASM_KEY_MINUS, 0x0c},
    {GASM_KEY_EQUAL, 0x0d}, {GASM_KEY_BACKSPACE, 0x0e}, {GASM_KEY_TAB, 0x0f}, {GASM_KEY_KEY_Q, 0x10},
    {GASM_KEY_KEY_W, 0x11}, {GASM_KEY_KEY_E, 0x12}, {GASM_KEY_KEY_R, 0x13}, {GASM_KEY_KEY_T, 0x14},
    {GASM_KEY_KEY_Y, 0x15}, {GASM_KEY_KEY_U, 0x16}, {GASM_KEY_KEY_I, 0x17}, {GASM_KEY_KEY_O, 0x18},
    {GASM_KEY_KEY_P, 0x19}, {GASM_KEY_BRACKET_LEFT, 0x1a}, {GASM_KEY_BRACKET_RIGHT, 0x1b},
    {GASM_KEY_ENTER, 0x1c}, {GASM_KEY_CONTROL_LEFT, 0x1d}, {GASM_KEY_KEY_A, 0x1e}, {GASM_KEY_KEY_S, 0x1f},
    {GASM_KEY_KEY_D, 0x20}, {GASM_KEY_KEY_F, 0x21}, {GASM_KEY_KEY_G, 0x22}, {GASM_KEY_KEY_H, 0x23},
    {GASM_KEY_KEY_J, 0x24}, {GASM_KEY_KEY_K, 0x25}, {GASM_KEY_KEY_L, 0x26}, {GASM_KEY_SEMICOLON, 0x27},
    {GASM_KEY_QUOTE, 0x28}, {GASM_KEY_BACKQUOTE, 0x29}, {GASM_KEY_SHIFT_LEFT, 0x2a}, {GASM_KEY_BACKSLASH, 0x2b},
    {GASM_KEY_KEY_Z, 0x2c}, {GASM_KEY_KEY_X, 0x2d}, {GASM_KEY_KEY_C, 0x2e}, {GASM_KEY_KEY_V, 0x2f},
    {GASM_KEY_KEY_B, 0x30}, {GASM_KEY_KEY_N, 0x31}, {GASM_KEY_KEY_M, 0x32}, {GASM_KEY_COMMA, 0x33},
    {GASM_KEY_PERIOD, 0x34}, {GASM_KEY_SLASH, 0x35}, {GASM_KEY_SHIFT_RIGHT, 0x36}, {GASM_KEY_NUMPAD_MULTIPLY, 0x37},
    {GASM_KEY_ALT_LEFT, 0x38}, {GASM_KEY_SPACE, 0x39}, {GASM_KEY_CAPS_LOCK, 0x3a}, {GASM_KEY_F1, 0x3b},
    {GASM_KEY_F2, 0x3c}, {GASM_KEY_F3, 0x3d}, {GASM_KEY_F4, 0x3e}, {GASM_KEY_F5, 0x3f}, {GASM_KEY_F6, 0x40},
    {GASM_KEY_F7, 0x41}, {GASM_KEY_F8, 0x42}, {GASM_KEY_F9, 0x43}, {GASM_KEY_F10, 0x44}, {GASM_KEY_NUM_LOCK, 0x45},
    {GASM_KEY_SCROLL_LOCK, 0x46}, {GASM_KEY_NUMPAD7, 0x47}, {GASM_KEY_NUMPAD8, 0x48}, {GASM_KEY_NUMPAD9, 0x49},
    {GASM_KEY_NUMPAD_SUBTRACT, 0x4a}, {GASM_KEY_NUMPAD4, 0x4b}, {GASM_KEY_NUMPAD5, 0x4c}, {GASM_KEY_NUMPAD6, 0x4d},
    {GASM_KEY_NUMPAD_ADD, 0x4e}, {GASM_KEY_NUMPAD1, 0x4f}, {GASM_KEY_NUMPAD2, 0x50}, {GASM_KEY_NUMPAD3, 0x51},
    {GASM_KEY_NUMPAD0, 0x52}, {GASM_KEY_NUMPAD_DECIMAL, 0x53}, {GASM_KEY_INTL_BACKSLASH, 0x56}, {GASM_KEY_F11, 0x57},
    {GASM_KEY_F12, 0x58}, {GASM_KEY_F13, 0x64}, {GASM_KEY_F14, 0x65}, {GASM_KEY_F15, 0x66}, {GASM_KEY_INTL_RO, 0x73},
    {GASM_KEY_INTL_YEN, 0x7d}, {GASM_KEY_NUMPAD_EQUAL, 0x8d}, {GASM_KEY_NUMPAD_ENTER, 0x9c},
    {GASM_KEY_CONTROL_RIGHT, 0x9d}, {GASM_KEY_NUMPAD_COMMA, 0xb3}, {GASM_KEY_NUMPAD_DIVIDE, 0xb5},
    {GASM_KEY_PRINT_SCREEN, 0xb7}, {GASM_KEY_ALT_RIGHT, 0xb8}, {GASM_KEY_PAUSE, 0xc5}, {GASM_KEY_HOME, 0xc7},
    {GASM_KEY_ARROW_UP, 0xc8}, {GASM_KEY_PAGE_UP, 0xc9}, {GASM_KEY_ARROW_LEFT, 0xcb}, {GASM_KEY_ARROW_RIGHT, 0xcd},
    {GASM_KEY_END, 0xcf}, {GASM_KEY_ARROW_DOWN, 0xd0}, {GASM_KEY_PAGE_DOWN, 0xd1}, {GASM_KEY_INSERT, 0xd2},
    {GASM_KEY_DELETE, 0xd3}, {GASM_KEY_META_LEFT, 0xdb}, {GASM_KEY_META_RIGHT, 0xdc}, {GASM_KEY_CONTEXT_MENU, 0xdd},
};

static uint8_t scan_of[256];   /* GASM_KEY_* -> DIK code, 0 = none */

static void init_keys(void)
{
    for (size_t i = 0; i < sizeof DIK_OF / sizeof *DIK_OF; i++) scan_of[DIK_OF[i].key] = DIK_OF[i].dik;
}

bool plat_keys(uint8_t keys[KEY_COUNT])
{
    uint8_t bits[GASM_KEY_STATE_BYTES];
    memset(keys, 0, KEY_COUNT);
    if (gasm_key_state(bits, sizeof bits) < 0) return false;
    for (int k = 0; k < 256; k++)
        if (scan_of[k] && (bits[k >> 3] & (1u << (k & 7)))) keys[scan_of[k]] = 1;
    return true;
}

static uint8_t presses[64];
static int npresses;

static void poll_key_events(void)
{
    uint8_t ev[4 * 64];
    npresses = 0;
    int32_t n = gasm_key_events(ev, sizeof ev);
    if (n < 0 || n > (int32_t)sizeof ev) return;   /* none, or more than a frame's worth: dropped */
    for (int32_t i = 0; i + 4 <= n; i += 4) {
        uint16_t key = (uint16_t)(ev[i] | ev[i + 1] << 8);
        if (ev[i + 2] && key < 256 && scan_of[key] && npresses < 64) presses[npresses++] = scan_of[key];
    }
}

int plat_key_presses(uint8_t *codes, int cap)
{
    int n = npresses < cap ? npresses : cap;
    memcpy(codes, presses, (size_t)n * sizeof *codes);
    return n;
}

/* ---- storage ---- */

/* A file name as a storage key (1-128 of [A-Za-z0-9._-]): the separators '\\' and '/' become '_',
   letters, digits and '.' stay, any other byte (so '-' and '_' too) becomes "-XX" in hex; reversible.
   0 when too long. */
static uint32_t storage_key(const char *name, char *key, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        unsigned char c = *p;
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.';
        if (c == '\\' || c == '/') {
            if (n + 1 > cap) return 0;
            key[n++] = '_';
        } else if (keep) {
            if (n + 1 > cap) return 0;
            key[n++] = (char)c;
        } else {
            if (n + 3 > cap) return 0;
            key[n++] = '-', key[n++] = hex[c >> 4], key[n++] = hex[c & 15];
        }
    }
    return n <= 128 ? (uint32_t)n : 0;
}

uint8_t *plat_load_user_file(const char *name, size_t *size)
{
    char key[128];
    uint32_t k = storage_key(name, key, sizeof key);
    if (!k) return NULL;
    int32_t n = gasm_storage_get(key, k, NULL, 0);
    if (n < 0) return NULL;
    uint8_t *b = malloc(n ? (size_t)n : 1);
    if (gasm_storage_get(key, k, b, (uint32_t)n) != n) { free(b); return NULL; }
    *size = (size_t)n;
    return b;
}

bool plat_save_user_file(const char *name, const void *data, size_t size)
{
    char key[128];
    uint32_t k = storage_key(name, key, sizeof key);
    return k && gasm_storage_set(key, k, data, (uint32_t)size) == 0;
}

/* ---- data: the game's files as assets ---- */

typedef struct { int64_t size; char name[]; } FileAsset;

/* Game path -> asset name: '\\' -> '/', "." and ".." resolved; leading ".." components are dropped. */
static void norm_path(const char *rel, char *out, size_t cap)
{
    size_t n = 0;
    while (*rel && n + 1 < cap) {
        while (*rel == '/' || *rel == '\\') rel++;
        const char *seg = rel;
        while (*rel && *rel != '/' && *rel != '\\') rel++;
        size_t len = (size_t)(rel - seg);
        if (!len || (len == 1 && seg[0] == '.')) continue;
        if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            while (n && out[n - 1] != '/') n--;
            if (n) n--;
            continue;
        }
        if (n && n + 1 < cap) out[n++] = '/';
        for (size_t k = 0; k < len && n + 1 < cap; k++) out[n++] = seg[k];
    }
    out[n] = 0;
}

static void *files_open(void *ctx, const char *rel, uint64_t *size)
{
    char name[512];
    norm_path(rel, name, sizeof name);
    int64_t n = gasm_asset_size64(name, (uint32_t)strlen(name));
    if (n < 0) return NULL;
    size_t l = strlen(name);
    FileAsset *f = malloc(sizeof *f + l + 1);
    if (!f) return NULL;
    f->size = n;
    memcpy(f->name, name, l + 1);
    *size = (uint64_t)n;
    return f;
}

static int64_t files_read_at(void *ctx, void *file, uint64_t off, void *dst, size_t len)
{
    const FileAsset *f = file;
    size_t done = 0;
    while (done < len) {
        uint32_t want = len - done > (1u << 30) ? 1u << 30 : (uint32_t)(len - done);
        int32_t k = gasm_asset_read_at64(f->name, (uint32_t)strlen(f->name), off + done, (uint8_t *)dst + done, want);
        if (k < 0) return done ? (int64_t)done : -1;
        if (k == 0) break;
        done += (size_t)k;
    }
    return (int64_t)done;
}

static void files_close(void *ctx, void *file) { free(file); }

/* Entries directly under dir, from the asset names (folder entries are relative paths; the game lists
   directories case-insensitively, e.g. the level files and the IGMD folders). Directories are reported once
   per name, at their first file. */
static bool files_list(void *ctx, const char *dir, VfsListFn fn, void *user)
{
    char d[512], name[512], last[256] = "";
    norm_path(dir, d, sizeof d);
    size_t dl = strlen(d);
    bool any = false;
    for (uint32_t i = 0, n = gasm_asset_count(); i < n; i++) {
        int32_t l = gasm_asset_name(i, name, sizeof name - 1);
        if (l < 0 || l >= (int32_t)sizeof name - 1) continue;
        name[l] = 0;
        const char *rest = name;
        if (dl) {
            if ((size_t)l <= dl || strncasecmp(name, d, dl) != 0 || name[dl] != '/') continue;
            rest = name + dl + 1;
        }
        const char *slash = strchr(rest, '/');
        size_t el = slash ? (size_t)(slash - rest) : strlen(rest);
        if (!el || el >= sizeof last) continue;
        any = true;
        if (slash) {
            if (strlen(last) == el && !strncasecmp(last, rest, el)) continue;
            memcpy(last, rest, el);
            last[el] = 0;
            fn(last, true, user);
        } else {
            char e[256];
            memcpy(e, rest, el);
            e[el] = 0;
            fn(e, false, user);
        }
    }
    return any;
}

static bool has_game(void) { return vfs_exists("Oni.exe"); }

static bool mount_game(void)
{
    VfsBackend b = {NULL, files_open, files_read_at, files_close, files_list, NULL};
    vfs_mount(&b, "gasm assets");
    if (has_game()) return true;
    vfs_unmount();
    return false;
}

/* ---- the loop ---- */

static void sample_input(void)
{
    for (uint32_t p = 0; p < 4; p++) pads[p] = gasm_input_pad(p);
    poll_key_events();
    have_pointer = gasm_pointer(pointer, sizeof pointer) == GASM_POINTER_BYTES;
}

static double audio_acc;   /* fractional audio frames carried between frames */

void plat_wait_frame(void)
{
    static float audio_buf[4096 * 2];
    audio_acc += APP_AUDIO_RATE / frame_hz;
    unsigned n = (unsigned)audio_acc;
    if (n > 4096) n = 4096;
    audio_acc -= n;
    app_audio(audio_buf, n);
    gasm_audio_push(audio_buf, n);
    gasm_wait_frame();
    /* The next frame: advance the clock by the period in effect, then sample input. */
    time_frac += 1e6 / frame_hz;
    uint64_t whole = (uint64_t)time_frac;
    time_us += whole;
    time_frac -= (double)whole;
    frames++;
    sample_input();
}

int gasm_loop_init(void)
{
    gasm_set_frame_rate(APP_FRAME_HZ);
    gasm_audio_config(APP_AUDIO_RATE, 2);
    gasm_input_mode(input_flags);
    init_keys();
    if (!mount_game()) {
        plat_log("OpenOni: game data not found. Pass the game folder: gasm-run openoni.wasm --asset-dir <folder "
                 "with Oni.exe and GameDataFolder> (make it from the CD with openoni-extract or visextract)");
        return 1;
    }
    char m[640];
    snprintf(m, sizeof m, "OpenOni: game data from %s", vfs_describe());
    plat_log(m);
    return 0;
}

int gasm_main(void)
{
    sample_input();
    return app_main();
}

void gasm_loop_exit(void) { app_exit(); }
