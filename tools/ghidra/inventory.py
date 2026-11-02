#!/usr/bin/env python3
"""Code size per module of oni.exe, from re/meta.tsv (apply-names.sh) and Bungie's name prefixes.

    tools/ghidra/inventory.py re/meta.tsv

A named function's module is its prefix (AI2rMelee_Update -> AI2). An unnamed one between two named
functions of the same module is counted with them (the linker keeps each source file's functions
together); the rest is "unassigned". Prints a Markdown table for docs/re/inventory.md.
"""
import re
import sys


def module(name):
    m = re.match(r"([A-Z][A-Z0-9]*?)[ri][A-Z]", name)
    return m.group(1) if m else None


def main(path):
    funcs = []
    for line in open(path, encoding="utf-8", errors="replace"):
        f = line.split("\t")
        if len(f) >= 3 and re.fullmatch(r"[0-9a-f]{8}", f[0]) and f[2].isdigit():
            funcs.append((int(f[0], 16), f[1], int(f[2])))
    funcs.sort()
    mods = [module(n) for _, n, _ in funcs]
    inferred = list(mods)
    i = 0
    while i < len(funcs):
        if mods[i] is None:
            j = i
            while j < len(funcs) and mods[j] is None:
                j += 1
            left = mods[i - 1] if i else None
            right = mods[j] if j < len(funcs) else None
            if left and left == right:
                for k in range(i, j):
                    inferred[k] = left
            i = j
        else:
            i += 1
    stats = {}
    for (addr, name, size), m, inf in zip(funcs, mods, inferred):
        key = inf or "unassigned"
        s = stats.setdefault(key, [0, 0, 0, 0, addr, addr])  # funcs, named, bytes, named bytes, lo, hi
        s[0] += 1
        s[2] += size
        if m:
            s[1] += 1
            s[3] += size
        s[4], s[5] = min(s[4], addr), max(s[5], addr)
    total = sum(s[2] for s in stats.values())
    named = sum(s[3] for s in stats.values())
    print(f"{len(funcs)} functions, {total / 1024:.0f} KB of code; named {sum(1 for m in mods if m)} "
          f"({named / total:.0%} of the bytes)\n")
    print("| Module | Functions | Named | KB | Range |")
    print("|---|---|---|---|---|")
    for k, s in sorted(stats.items(), key=lambda kv: -kv[1][2]):
        print(f"| {k} | {s[0]} | {s[1]} | {s[2] / 1024:.1f} | 0x{s[4]:x}-0x{s[5]:x} |")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "re/meta.tsv")
