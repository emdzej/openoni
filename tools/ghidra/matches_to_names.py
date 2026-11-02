#!/usr/bin/env python3
"""Turns the reliable part of re/mac-matches.tsv (match_mac.py) into names.tsv lines.

    tools/ghidra/matches_to_names.py re/mac-matches.tsv >> tools/ghidra/names.tsv

Kept: string anchors and neighbourhood matches with similarity >= 0.75 (about 96% right when
match_mac.py --validate re-derives hidden string matches). The rest stay candidates in re/mac-matches.tsv.
Names already in names.tsv are not repeated. The module is Bungie's prefix (AI2rMelee_Update -> AI2).
"""
import os
import re
import sys

NAMES = os.path.join(os.path.dirname(__file__), "names.tsv")


def main(path):
    have = set()
    if os.path.exists(NAMES):
        have = {l.split("\t")[0] for l in open(NAMES) if l.strip() and not l.startswith("#")}
    for line in open(path):
        if line.startswith("#"):
            continue
        addr, name, method, _, _ = line.rstrip("\n").split("\t")
        if addr in have:
            continue
        ok = method.startswith("strings") or (method.startswith("neighbours") and float(method.split(":")[1]) >= 0.75)
        if not ok:
            continue
        m = re.match(r"([A-Z][A-Z0-9]*?)[ri][A-Z]", name)
        module = m.group(1) if m else "-"
        print(f"{addr}\t{name}\t{module}\tmed\tname from the Intel Mac build ({method})")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip())
    main(sys.argv[1])
