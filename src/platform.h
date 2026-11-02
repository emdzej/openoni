/* Platform contract: what the portable core needs from a host (backend: platform_gasm.c). The game data
   comes through the file layer (vfs.h); 3D goes to OpenGL ES 3.0 (<GLES3/gl3.h>), which only the renderer's
   GL back end will call (step 4 of docs/re/port-plan.md).

   The game keeps the original's own loops (docs/re/architecture.md): app_main (app.h) runs once, like
   ONiMain 0x4d3280, and every loop in it ends each frame with plat_wait_frame(), where the original swaps
   buffers. The host shows the frame, plays the frame's audio and returns at the start of the next frame
   with fresh input. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ends the frame: the frame is shown, app_audio fills the frame's audio, and the call returns in the next
   frame. Input (keys, mouse, pads) is sampled once per frame, so it is stable between two calls. */
void plat_wait_frame(void);
/* The frame clock in microseconds: the start of the current frame, advancing by the frame period. It
   replaces QueryPerformanceCounter / GetTickCount, so runs are reproducible. */
uint64_t plat_time_us(void);
/* Frames waited so far. */
uint32_t plat_frames(void);
/* Calls of plat_wait_frame per second from now on; the audio per frame follows. */
void plat_set_frame_rate(double hz);

void plat_log(const char *msg);
/* Launch parameter (gasm --param / URL query); false if unset. */
bool plat_param(const char *name, char *dst, size_t cap);

/* The drawable (default framebuffer) size in pixels; follows window resizes. */
void plat_display_size(int *w, int *h);
/* False during catch-up frames, whose drawing is never shown (a frame may skip drawing). */
bool plat_frame_shown(void);

/* Keys are DirectInput codes (DIK_*: set-1 scan code, 0x80 for extended keys), as the original reads the
   keyboard through DirectInput: Esc 0x01, Enter 0x1c, keypad Enter 0x9c, Up 0xc8. */
enum { KEY_COUNT = 256 };
/* Held keys: keys[code] = 1. False if the runner has no keyboard. */
bool plat_keys(uint8_t keys[KEY_COUNT]);
/* Key presses in this frame, in order (no auto-repeat); count, at most cap. */
int plat_key_presses(uint8_t *codes, int cap);

/* The mouse in this frame. Positions in drawable pixels; dx, dy: motion since the previous frame (also
   while captured); wheel: notches, > 0 = down. */
enum { MOUSE_LEFT = 1 << 0, MOUSE_RIGHT = 1 << 1, MOUSE_MIDDLE = 1 << 2, MOUSE_BACK = 1 << 3, MOUSE_FORWARD = 1 << 4 };
typedef struct {
    float x, y, dx, dy, wheel;
    uint32_t buttons, pressed, released;   /* MOUSE_* */
    bool inside, captured;
} PlatMouse;
bool plat_mouse(PlatMouse *m);           /* false if the runner has no pointer */
/* Captures the pointer for relative motion (mouselook) and hides the cursor, or releases it. Best effort:
   a browser captures on the next click; PlatMouse.captured says what happened. */
void plat_mouse_capture(bool on);

/* Virtual pad buttons, the gasm ABI layout (SNES positions: A east, B south, X north, Y west). */
enum {
    PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3, PAD_L = 1 << 4, PAD_R = 1 << 5,
    PAD_SELECT = 1 << 6, PAD_START = 1 << 7, PAD_UP = 1 << 8, PAD_DOWN = 1 << 9, PAD_LEFT = 1 << 10,
    PAD_RIGHT = 1 << 11,
};
uint32_t plat_pad(int player);

/* Persistent user files (persist.dat, key_config.txt, ...). */
uint8_t *plat_load_user_file(const char *name, size_t *size);   /* malloc'd; NULL if missing */
bool plat_save_user_file(const char *name, const void *data, size_t size);
