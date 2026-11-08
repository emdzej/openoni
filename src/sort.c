/* The original's sorts: see sort.h. */
#include "sort.h"

#include <string.h>

enum { STACK = 30, CUTOFF = 8 };

/* 0x51ea53: two elements exchanged */
static void swap(unsigned char *a, unsigned char *b, size_t width)
{
    if (a == b) return;
    while (width--) {
        unsigned char t = *a;
        *a++ = *b, *b++ = t;
    }
}

/* The shared shape. The partition's element is the middle one, moved to the bottom; from there one
   pointer climbs while its elements don't go after it and one falls while theirs do (the C library's:
   while they don't go before it); the two swapped, until they cross. The smaller side is sorted first,
   the other kept on the stack. Up to CUTOFF elements: the largest moved to the top, again and again
   (0x51ea05; AUrQSort's in place). */
static void sort(unsigned char *base, size_t num, size_t width, bool crt, int (*comp)(const void *, const void *),
                 bool (*after)(const void *, const void *, void *), void *u)
{
    if (num < 2 || width == 0) return;
    unsigned char *lostk[STACK], *histk[STACK];
    int sp = 0;
    unsigned char *lo = base, *hi = base + (num - 1) * width;
#define GT(a, b) (crt ? comp(a, b) > 0 : after(a, b, u))
    for (;;) {
        size_t size = (size_t)(hi - lo) / width + 1;
        if (size <= CUTOFF) {
            for (unsigned char *top = hi; top > lo; top -= width) {
                unsigned char *max = lo;
                for (unsigned char *p = lo + width; p <= top; p += width)
                    if (GT(p, max)) max = p;
                swap(max, top, width);
            }
        } else {
            swap(lo + (size / 2) * width, lo, width);
            unsigned char *loguy = lo, *higuy = hi + width;
            for (;;) {
                do loguy += width;
                while (loguy <= hi && !GT(loguy, lo));
                if (crt) {
                    do higuy -= width;
                    while (higuy > lo && comp(higuy, lo) >= 0);
                } else {
                    do higuy -= width;
                    while (higuy > lo && after(higuy, lo, u));
                }
                if (higuy < loguy) break;
                swap(loguy, higuy, width);
            }
            swap(lo, higuy, width);
            /* (the sides compared in bytes, the lower one less one, as both originals do) */
            if ((ptrdiff_t)(higuy - lo) - 1 >= (ptrdiff_t)(hi - loguy)) {
                if (lo + width < higuy && sp < STACK) lostk[sp] = lo, histk[sp] = higuy - width, sp++;
                if (loguy < hi) {
                    lo = loguy;
                    continue;
                }
            } else {
                if (loguy < hi && sp < STACK) lostk[sp] = loguy, histk[sp] = hi, sp++;
                if (lo + width < higuy) {
                    hi = higuy - width;
                    continue;
                }
            }
        }
        if (--sp < 0) return;
        lo = lostk[sp], hi = histk[sp];
    }
#undef GT
}

void crt_qsort(void *base, size_t num, size_t width, int (*comp)(const void *a, const void *b))
{
    sort(base, num, width, true, comp, NULL, NULL);
}

void au_qsort(void *base, size_t num, size_t width, bool (*after)(const void *a, const void *b, void *u), void *u)
{
    sort(base, num, width, false, NULL, after, u);
}
