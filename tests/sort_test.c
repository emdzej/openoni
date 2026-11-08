/* The original's sorts (sort.c): ordered results, the elements kept, and the orders they leave among
   equal keys, worked out by hand from the disassembly (qsort 0x51e8b1, AUrQSort_32 0x437f40). */
#include "sort.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { int key, tag; } Item;

static int cmp(const void *a, const void *b)
{
    int x = ((const Item *)a)->key, y = ((const Item *)b)->key;
    return x < y ? -1 : x > y;
}

static bool after(const void *a, const void *b, void *u)
{
    (void)u;
    return ((const Item *)a)->key > ((const Item *)b)->key;
}

static int check(const char *what, const Item *v, int n, const int *want_tags)
{
    int bad = 0;
    for (int i = 1; i < n; i++) bad |= v[i - 1].key > v[i].key;
    if (want_tags)
        for (int i = 0; i < n; i++) bad |= v[i].tag != want_tags[i];
    if (bad) {
        fprintf(stderr, "sort_test: %s:", what);
        for (int i = 0; i < n; i++) fprintf(stderr, " %d/%d", v[i].key, v[i].tag);
        fprintf(stderr, "\n");
    }
    return bad;
}

int main(void)
{
    int failed = 0;
    /* the selection sort under nine: the largest to the top, the first of equals kept as the largest,
       so two equal keys come out reversed */
    Item a[3] = { { 1, 0 }, { 1, 1 }, { 0, 2 } };
    static const int want_a[3] = { 2, 1, 0 };
    crt_qsort(a, 3, sizeof *a, cmp);
    failed += check("short", a, 3, want_a);
    Item b[3] = { { 1, 0 }, { 1, 1 }, { 0, 2 } };
    au_qsort(b, 3, sizeof *b, after, NULL);
    failed += check("short (AUrQSort)", b, 3, want_a);
    /* ten equal keys and a smaller one: the partition, where the two differ. The C library's falling
       pointer passes over the equals, AUrQSort's stops at them. */
    Item c[11], d[11];
    for (int i = 0; i < 11; i++) c[i] = d[i] = (Item){ i == 10 ? 0 : 5, i };
    crt_qsort(c, 11, sizeof *c, cmp);
    au_qsort(d, 11, sizeof *d, after, NULL);
    failed += check("equal keys", c, 11, NULL) + check("equal keys (AUrQSort)", d, 11, NULL);
    int same = 1;
    for (int i = 0; i < 11; i++) same &= c[i].tag == d[i].tag;
    if (same) {
        fprintf(stderr, "sort_test: the two sorts left the equal keys in the same order\n");
        failed++;
    }
    /* many random keys: sorted, every element still there */
    enum { N = 5000 };
    static Item e[N];
    uint32_t seed = 1;
    long sum = 0;
    for (int i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        e[i] = (Item){ (int)(seed >> 16) % 300, i };
        sum += i;
    }
    for (int pass = 0; pass < 2; pass++) {
        if (pass) au_qsort(e, N, sizeof *e, after, NULL);
        else crt_qsort(e, N, sizeof *e, cmp);
        long s = 0;
        for (int i = 0; i < N; i++) s += e[i].tag;
        failed += check(pass ? "random (AUrQSort)" : "random", e, N, NULL) + (s != sum);
    }
    printf(failed ? "sort_test: FAILED\n" : "sort_test: ok\n");
    return failed ? 1 : 0;
}
