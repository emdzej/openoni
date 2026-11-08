/* The original's two sorts, for the orders they leave among equal keys: the C library's qsort (MSVC's,
   statically linked) and Bungie's own copies for arrays of 16- and 32-bit values (AUrQSort_16 and _32).
   Both are a quicksort on an explicit stack, with a selection sort under nine elements; neither is
   stable, and they differ in one comparison, so each port's sort uses the one its original does. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* qsort 0x51e8b1: comp < 0, 0, > 0 as the C library's. */
void crt_qsort(void *base, size_t num, size_t width, int (*comp)(const void *a, const void *b));

/* AUrQSort_16 0x437d90 and AUrQSort_32 0x437f40: after(a, b, u) true when a goes after b. */
void au_qsort(void *base, size_t num, size_t width, bool (*after)(const void *a, const void *b, void *u), void *u);
