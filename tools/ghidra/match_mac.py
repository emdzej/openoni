#!/usr/bin/env python3
"""Carries Bungie's function names from the Intel Mac build (oni-mac) to the Windows v1.0 exe (oni.exe).

    tools/ghidra/match_mac.py re/meta.tsv re/mac-meta.tsv > re/mac-matches.tsv

Inputs are DumpMeta.java dumps of both programs (addr, name, size, callers, callees, imports, strings).
The matches are evidence for names.tsv, not names.tsv itself: every name still gets a look before it goes
in (the Mac build is a later version with Feral's changes, compiled by GCC; the Windows one by MSVC, which
inlines).

1. Strings: a string (source file names reduced to their base name) that exactly one function references
   in each program votes for that pair. A pair is taken when each is the other's best and the votes agree.
2. Call graph: around matched pairs, an unmatched callee (or caller) that is the only one left on both
   sides is matched too.
3. Layout: compilers emit a source file's functions in order and linkers keep object files in order, so
   between two consecutive matches that are in order in both programs (the longest increasing run of
   matches; outliers are dropped from the anchors), a gap with the same number of unmatched functions on
   both sides is matched in order, if every pair's sizes are within a factor of 4.
4. Neighbourhood: an unmatched Windows function whose matched callees and callers (at least 2) map onto
   those of one unmatched Mac function (similarity >= 0.6, mutual best, clearly ahead of the runner-up)
   is matched.
Steps 2-4 repeat until nothing changes.

Output: win_addr, mac_name, method (strings:N / callee / caller), win_size, mac_size, sorted by address.
"""
import collections
import re
import sys


def load(path):
    funcs = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 7:
            f += [""] * (7 - len(f))
        addr, name, size, _, callees, imports, strings = f[:7]
        if not re.fullmatch(r"[0-9a-f]{8}", addr) or not size.isdigit():
            continue  # the rest of a string that held a newline
        funcs[addr] = {
            "name": name,
            "size": int(size or 0),
            "callees": [c for c in callees.split(",") if c],
            "imports": [i for i in imports.split(",") if i],
            "strings": {norm(s) for s in strings.split("|") if len(s) >= 4},
        }
    callers = collections.defaultdict(list)
    for a, fn in funcs.items():
        for c in fn["callees"]:
            if c in funcs:
                callers[c].append(a)
    for a in funcs:
        funcs[a]["callers"] = callers[a]
    return funcs


def norm(s):
    s = s.strip()
    if re.search(r"[\\/][^\\/]+\.(c|cpp|h)\b", s, re.I):  # source paths differ: F:\Bungie... vs /tmp/OniSource...
        s = re.split(r"[\\/]", s)[-1]
    return s.lower()


def by_string(funcs):
    idx = collections.defaultdict(set)
    for a, fn in funcs.items():
        for s in fn["strings"]:
            idx[s].add(a)
    return idx


SEED = 1


def main(win_path, mac_path, holdout=None):
    win, mac = load(win_path), load(mac_path)
    wi, mi = by_string(win), by_string(mac)
    votes = collections.Counter()
    for s, ws in wi.items():
        ms = mi.get(s)
        if ms and len(ws) == 1 and len(ms) == 1:
            votes[(next(iter(ws)), next(iter(ms)))] += 1
    best_w, best_m = {}, {}
    for (w, m), n in votes.items():
        if n > best_w.get(w, (0, None))[0]:
            best_w[w] = (n, m)
        if n > best_m.get(m, (0, None))[0]:
            best_m[m] = (n, w)
    match, method = {}, {}
    for w, (n, m) in best_w.items():
        rivals = [x for (ww, x), k in votes.items() if ww == w and x != m]
        if best_m[m][1] == w and not rivals:
            match[w] = m
            method[w] = f"strings:{n}"
    hidden = {}
    if holdout is not None:  # validation: hide a share of the string matches, see what propagation makes of them
        import random
        rng = random.Random(SEED)
        for w in sorted(match):
            if rng.random() < holdout:
                hidden[w] = match.pop(w)
                method.pop(w)
    used = set(match.values())

    def lis(pairs):
        """Longest run of (w, m) pairs increasing in m, pairs given sorted by w."""
        import bisect
        tails, idx, prev = [], [], [None] * len(pairs)
        for i, (_, m) in enumerate(pairs):
            k = bisect.bisect_left(tails, m)
            if k == len(tails):
                tails.append(m)
                idx.append(i)
            else:
                tails[k] = m
                idx[k] = i
            prev[i] = idx[k - 1] if k else None
        out, i = [], idx[-1] if idx else None
        while i is not None:
            out.append(pairs[i])
            i = prev[i]
        return out[::-1]

    def similar(w, m):
        """Sizes within a factor of 4 (MSVC's code is the smaller), callee counts close."""
        r = win[w]["size"] / max(1, mac[m]["size"])
        nw, nm = len(set(win[w]["callees"])), len(set(mac[m]["callees"]))
        return 0.2 <= r <= 2.5 and abs(nw - nm) <= max(2, nm // 2)

    worder = sorted(win, key=lambda a: int(a, 16))
    morder = sorted(mac, key=lambda a: int(a, 16))
    wpos = {a: i for i, a in enumerate(worder)}
    mpos = {a: i for i, a in enumerate(morder)}

    def layout():
        anchors = lis([(wpos[w], mpos[m]) for w, m in sorted(match.items(), key=lambda x: wpos[x[0]])])
        n = 0
        for (w1, m1), (w2, m2) in zip(anchors, anchors[1:]):
            wg = [worder[i] for i in range(w1 + 1, w2) if worder[i] not in match]
            mg = [morder[i] for i in range(m1 + 1, m2) if morder[i] not in used]
            if not wg or len(wg) != len(mg) or len(wg) > 12:
                continue
            if all(0.25 <= win[a]["size"] / max(1, mac[b]["size"]) <= 4 for a, b in zip(wg, mg)):
                for a, b in zip(wg, mg):
                    match[a] = b
                    used.add(b)
                    method[a] = "layout"
                    n += 1
        return n

    def neighbourhood():
        def nb(funcs, a):
            return set(funcs[a]["callees"]) | set(funcs[a]["callers"])
        mac_nb = {}
        cand = collections.defaultdict(set)    # mac neighbour -> unmatched mac functions next to it
        for m in mac:
            if m not in used:
                mac_nb[m] = {x for x in nb(mac, m) if x in used}
                for x in mac_nb[m]:
                    cand[x].add(m)
        best = {}
        for w in win:
            if w in match:
                continue
            mapped = {match[x] for x in nb(win, w) if x in match}
            if len(mapped) < 2:
                continue
            scores = []
            for m in set().union(*(cand[x] for x in mapped)):
                inter = len(mapped & mac_nb[m])
                if inter >= 2:
                    scores.append((inter / len(mapped | mac_nb[m]), m))
            scores.sort(reverse=True)
            if scores and scores[0][0] >= 0.6 and (len(scores) == 1 or scores[1][0] < scores[0][0] * 0.75):
                best[w] = scores[0]
        rev = collections.defaultdict(list)
        for w, (sc, m) in best.items():
            rev[m].append((sc, w))
        n = 0
        for m, lst in rev.items():
            lst.sort(reverse=True)
            if len(lst) == 1 or lst[1][0] < lst[0][0] * 0.75:
                w = lst[0][1]
                if 0.2 <= win[w]["size"] / max(1, mac[m]["size"]) <= 5:
                    match[w] = m
                    used.add(m)
                    method[w] = f"neighbours:{lst[0][0]:.2f}"
                    n += 1
        return n

    changed = True
    while changed:
        changed = layout() + neighbourhood() > 0
        for w, m in list(match.items()):
            for rel in ("callees", "callers"):
                wl = [x for x in dict.fromkeys(win[w][rel]) if x in win and x not in match]
                ml = [x for x in dict.fromkeys(mac[m][rel]) if x in mac and x not in used]
                if len(wl) == 1 and len(ml) == 1 and similar(wl[0], ml[0]):
                    match[wl[0]] = ml[0]
                    used.add(ml[0])
                    method[wl[0]] = rel[:-1]
                    changed = True

    if holdout is not None:
        stats = collections.defaultdict(lambda: [0, 0])
        for w, m in hidden.items():
            if w in match:
                meth = method[w]
                if meth.startswith("neighbours"):
                    sc = float(meth.split(":")[1])
                    meth = "neighbours>=0.9" if sc >= 0.9 else "neighbours>=0.75" if sc >= 0.75 else "neighbours<0.75"
                stats[meth][0 if match[w] == m else 1] += 1
        print(f"hidden {len(hidden)}, re-found {sum(1 for w in hidden if w in match)}")
        for k, (ok, bad) in sorted(stats.items()):
            print(f"  {k:18s} right {ok:4d}  wrong {bad:4d}  precision {ok / max(1, ok + bad):.2f}")
        return
    named = sum(1 for w in match if not mac[match[w]]["name"].startswith("FUN_"))
    print(f"# {len(match)} matched of {len(win)} Windows functions ({named} with a Mac name); "
          f"{sum(1 for v in method.values() if v.startswith('strings'))} from strings, "
          f"{sum(1 for v in method.values() if v == 'layout')} from layout, "
          f"{sum(1 for v in method.values() if v.startswith('neighbours'))} from neighbours", file=sys.stderr)
    print("# win_addr\tmac_name\tmethod\twin_size\tmac_size")
    for w in sorted(match, key=lambda a: int(a, 16)):
        m = match[w]
        print(f"{w}\t{mac[m]['name'].lstrip('_')}\t{method[w]}\t{win[w]['size']}\t{mac[m]['size']}")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[3].startswith("--validate="):
        main(sys.argv[1], sys.argv[2], float(sys.argv[3].split("=")[1]))
    elif len(sys.argv) == 3:
        main(sys.argv[1], sys.argv[2])
    else:
        sys.exit(__doc__.strip())
