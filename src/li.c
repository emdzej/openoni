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
