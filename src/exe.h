/* The original game program (and the Bink library), read at run time: tables, strings and other data the game needs are taken
   from Oni.exe of the user's copy rather than copied into the source tree. Only the supported build is
   accepted: Windows v1.0, 1,527,808 bytes, CRC-32 a8223704. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EXE_PATH "Oni.exe"
#define EXE_SIZE 1527808u
#define EXE_CRC32 0xa8223704u

/* Loads and checks the exe through the file layer. False (err says why) if missing or another build. */
bool exe_init(char *err, size_t errcap);
bool exe_loaded(void);
void exe_free(void);
/* len bytes at virtual address va (image base 0x400000), or NULL if the exe isn't loaded or the range
   isn't initialised data of one section (bytes beyond a section's raw size are zero-fill: NULL). */
const uint8_t *exe_data(uint32_t va, size_t len);
/* Little-endian reads at va; 0 if unavailable. */
uint32_t exe_u32(uint32_t va);
uint16_t exe_u16(uint32_t va);
/* The NUL-terminated string at va, or "" if unavailable. */
const char *exe_str(uint32_t va);

/* RAD's binkw32.dll from the game folder, the same way: the movies' tables (Huffman decode tables, scan
   order, quantizers, the audio's bands). Only the one Oni 1.0 ships: 286,208 bytes, CRC-32 6696be85;
   addresses with its image base, 0x10000000. */
#define DLL_PATH "binkw32.dll"
#define DLL_SIZE 286208u
#define DLL_CRC32 0x6696be85u
bool dll_init(char *err, size_t errcap);
bool dll_loaded(void);
void dll_free(void);
const uint8_t *dll_data(uint32_t va, size_t len);
uint32_t dll_u32(uint32_t va);
