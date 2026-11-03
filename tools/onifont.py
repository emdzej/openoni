#!/usr/bin/env python3
"""Reference reader for Oni's fonts (TSFF, TSFT, TSGA) and text instances, standard library only.

    tools/onifont.py <game folder> [--summary]                  summary per kind and over all
    tools/onifont.py <game folder> --png FAMILY SIZE OUT.png [STYLE]   a sheet of the font's glyphs
    tools/onifont.py <game folder> --text [levelN ...]          the subtitles and strings, as text

A font family (TSFF) links its fonts (TSFT), one per size and style. A font links 256 glyph arrays
(TSGA), by a character's high byte, and holds its glyphs' bitmaps: a byte per pixel, 0 where inked,
255 where clear. A glyph array is 256 glyphs of 0x14 bytes. Text instances: SUBT (name and text pairs
in the .raw), IGSt (a string with its font), IGSA (an array of IGSt), IGPG (a page: font, image, two
IGSA), IGPA (an array of IGPG), IGHH (the HUD help labels), StNA (an array of TStr). Formats:
docs/formats/font.md, docs/formats/text.md.

The hashes are FNV-1a (32 bits) over lines, one kind each, over every level; font_test prints the same:

    TSFF  "lv index name fonts\\n", per font "size style asc desc leading f12 words\\n" ("-\\n" for an
          empty link), per glyph array "hi\\n" (hex), per glyph that isn't all 0 "code adv w h x y word\\n"
          (code hex) followed by its w * h bitmap bytes
    SUBT  "lv index name count\\n", per entry name, a tab, the text, "\\n"
    IGSt  "lv index family style color08x size flags " + text + "\\n"
    IGSA  "lv index name count\\n", per link the IGSt's text (or "-") + "\\n"
    IGPG  "lv index family style color08x size flags image text hint\\n" (text, hint: the IGSAs' counts,
          -1 for none)
    IGPA  "lv index name count" + per page " text/hint" + "\\n"
    IGHH  "lv index name left right lx ly rx ry nleft nright count\\n", per label text + " x y\\n"
    StNA  "lv index name count\\n", per TStr its text + "\\n"

A link prints as its target's name, "-" if unnamed, "0" if none (or a placeholder no loaded file has,
as the loader leaves it). "widths" are TSrContext_GetStringRect's widths of a fixed string in every
font of Tahoma, "best" TSiContext_GetBestFont for sizes 0..20 in each style, then two lookups. "all" is
the hash over those lines.
"""
import struct
import sys

import onidat
import onitex

TSFF, TSFT, TSGA = 0x54534646, 0x54534654, 0x54534741
SUBT, IGST, IGSA, IGPG, IGPA, IGHH, STNA = (struct.unpack(">I", t)[0] for t in
                                             (b"SUBT", b"IGSt", b"IGSA", b"IGPG", b"IGPA", b"IGHH", b"StNA"))
SAMPLE = b"The quick brown fox, Konoko!"
FNV_BASIS, FNV_PRIME = 0x811C9DC5, 0x01000193


def fnv(data, h=FNV_BASIS):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
    return h


def u16(d, o): return struct.unpack_from("<H", d, o)[0]
def u32(d, o): return struct.unpack_from("<I", d, o)[0]


class Level:
    def __init__(self, folder, lv, level0=None):
        self.lv = lv
        self.dat = onidat.Dat(f"{folder}/GameDataFolder/level{lv}_Final.dat")
        self.raw = open(f"{folder}/GameDataFolder/level{lv}_Final.raw", "rb").read()
        self.level0 = level0 or self

    def link(self, off):
        """The instance a link field points at, as (level, index), resolved as the loader does: a
        placeholder by name in level 0 (or here); None for none or unresolved."""
        v = u32(self.dat.d, off)
        if not v:
            return None
        i = v >> 8
        tag, name, data, _, _ = self.dat.inst[i]
        if data is not None:
            return self, i
        for lvl in (self.level0, self):
            j = onidat.find(lvl.dat, name)
            if j is not None:
                return lvl, j
        return None

    def data(self, ref):
        lvl, i = ref
        return lvl.dat.d, lvl.dat.inst[i][2]

    def link_name(self, off):
        r = self.link(off)
        if r is None:
            return b"0"
        name = r[0].dat.inst[r[1]][1]
        return name.encode("latin-1") if name is not None else b"-"

    def of(self, tag):
        for i, (t, name, data, _, _) in enumerate(self.dat.inst):
            if t == tag and data is not None:
                yield i, (name or "-").encode("latin-1"), data


def cstr(d, o, n):
    s = d[o:o + n]
    z = s.find(b"\0")
    return s if z < 0 else s[:z]


def next_char(s, k):
    """TSiString_GetNextCharacter: (character, next index). Bit 7 and not 0xff: two bytes, high first.
    At a terminator in the second byte the walk stops there (the original reads on)."""
    b = s[k] if k < len(s) else 0
    if not b & 0x80 or b == 0xFF:
        return b, k + (1 if b else 0)
    b2 = s[k + 1] if k + 1 < len(s) else 0
    return b << 8 | b2, k + (2 if b2 else 1)


class Font:
    def __init__(self, level, ref):
        self.level = level
        self.d, self.b = level.data(ref)
        (self.size,) = struct.unpack_from("<H", self.d, self.b + 6)
        (self.style,) = struct.unpack_from("<I", self.d, self.b + 8)
        self.asc, self.desc, self.lead, self.f12 = struct.unpack_from("<4H", self.d, self.b + 0xC)
        (self.words,) = struct.unpack_from("<I", self.d, self.b + 0x414)
        self.arrays = {}
        for hi in range(256):
            r = ref[0].link(self.b + 0x14 + 4 * hi)
            if r:
                self.arrays[hi] = ref[0].data(r)

    def glyph(self, ch):
        a = self.arrays.get(ch >> 8)
        if a is None:
            return None
        return struct.unpack_from("<HhHHhhI", a[0], a[1] + 0x14 * (ch & 0xFF))

    def bitmap(self, g):
        _, _, w, h, _, _, word = g
        if 4 * word + w * h > 4 * self.words:
            raise ValueError(f"glyph {g[0]:#x}: bitmap past the font's")
        o = self.b + 0x418 + 4 * word
        return self.d[o:o + w * h]

    def width(self, s):
        """TSrContext_GetStringRect 0x42e920: the advances, in 16 bits."""
        w, k = 0, 0
        while True:
            ch, k = next_char(s, k)
            if not ch:
                break
            g = self.glyph(ch)
            if g:
                w = (w + g[1]) & 0xFFFF
        return w - 0x10000 if w & 0x8000 else w


class Family:
    def __init__(self, level, index):
        self.level = level
        d, b = level.dat.d, level.dat.inst[index][2]
        self.fonts = []
        for k in range(u32(d, b + 0x14)):
            r = level.link(b + 0x18 + 4 * k)
            self.fonts.append(Font(level, r) if r else None)

    def font(self, size, style):
        for f in self.fonts:
            if f and f.size == size and f.style == style:
                return f
        return None

    def best(self, size, style):
        """TSiContext_GetBestFont 0x42ebd0: (font, size, style), font None if none."""
        s, up = size, False
        while True:
            f = self.font(s, style)
            if not f and self.font(s, 0):
                f, style = self.font(s, 0), 0
            if f:
                return f, s, style
            if not up:
                if s > 1:
                    s -= 1
                    continue
                up, s = True, size
            if s > 17:
                return None, size, style
            s += 1


def fold(c):
    c = c - 256 if c > 127 else c
    return c - 0x20 if 0x60 < c < 0x7B else c


def compare_nocase(a, b, n=None):
    """0x4266d0, and 0x426700 with n: zero if equal."""
    k = 0
    while True:
        x = fold(a[k]) if k < len(a) else 0
        y = fold(b[k]) if k < len(b) else 0
        k += 1
        if x != y or not x:
            return x - y
        if n is not None and k == n:
            return 0


def subt_entries(level, data):
    d = level.dat.d
    base, n = u32(d, data + 0x10), u32(d, data + 0x14)
    out = []
    for k in range(n):
        p = base + u32(d, data + 0x18 + 4 * k)
        z = level.raw.index(b"\0", p)
        name = level.raw[p:z]
        text = level.raw[z + 1:level.raw.index(b"\0", z + 1)]
        out.append((name, text))
    return out


def subt_find(level0, array, name, prefix):
    """SSrSubtitle_Find 0x47f460 (prefix, a digit entry skipping the name's first byte) and
    SSrMessage_Find 0x47f550."""
    i = onidat.find(level0.dat, "SUBT" + array)
    if i is None:
        return None
    for e, t in subt_entries(level0, level0.dat.inst[i][2]):
        if prefix:
            key = name[1:] if 0x30 <= e[0] <= 0x39 else name
            if not compare_nocase(e, key, len(e)):
                return t
        elif not compare_nocase(e, name):
            return t
    return None


def header(level, d, b):
    style, color, size, flags = struct.unpack_from("<IIHH", d, b + 4)
    return b"%s %d %08x %d %x" % (level.link_name(b), style, color, size, flags)


def array_count(level, off):
    r = level.link(off)
    if r is None:
        return -1
    d, b = level.data(r)
    return u32(d, b + 0x14)


def summary(folder):
    level0 = Level(folder, 0)
    kinds = {k: [FNV_BASIS, 0, 0, 0] for k in ("TSFF", "SUBT", "IGSt", "IGSA", "IGPG", "IGPA", "IGHH", "StNA")}

    def feed(k, data):
        kinds[k][0] = fnv(data, kinds[k][0])

    for lv in onidat.LEVELS:
        L = level0 if lv == 0 else Level(folder, lv, level0)
        d = L.dat.d
        for i, name, b in L.of(TSFF):
            fam = Family(L, i)
            kinds["TSFF"][1] += 1
            feed("TSFF", b"%d %d %s %d\n" % (lv, i, name, len(fam.fonts)))
            for f in fam.fonts:
                if f is None:
                    feed("TSFF", b"-\n")
                    continue
                kinds["TSFF"][2] += 1
                feed("TSFF", b"%d %d %d %d %d %d %d\n" % (f.size, f.style, f.asc, f.desc, f.lead, f.f12, f.words))
                for hi in sorted(f.arrays):
                    feed("TSFF", b"%x\n" % hi)
                    for lo in range(256):
                        g = f.glyph(hi << 8 | lo)
                        if not any(g):
                            continue
                        kinds["TSFF"][3] += 1
                        feed("TSFF", b"%x %d %d %d %d %d %d\n" % g + f.bitmap(g))
        for i, name, b in L.of(SUBT):
            es = subt_entries(L, b)
            kinds["SUBT"][1] += 1
            kinds["SUBT"][2] += len(es)
            feed("SUBT", b"%d %d %s %d\n" % (lv, i, name, len(es)))
            for e, t in es:
                feed("SUBT", e + b"\t" + t + b"\n")
        for i, name, b in L.of(IGST):
            kinds["IGSt"][1] += 1
            feed("IGSt", b"%d %d %s " % (lv, i, header(L, d, b)) + cstr(d, b + 0x10, 384) + b"\n")
        for i, name, b in L.of(IGSA):
            n = u32(d, b + 0x14)
            kinds["IGSA"][1] += 1
            kinds["IGSA"][2] += n
            feed("IGSA", b"%d %d %s %d\n" % (lv, i, name, n))
            for k in range(n):
                r = L.link(b + 0x18 + 4 * k)
                feed("IGSA", (cstr(*_at(L, r, 0x10), 384) if r else b"-") + b"\n")
        for i, name, b in L.of(IGPG):
            kinds["IGPG"][1] += 1
            feed("IGPG", b"%d %d %s %s %d %d\n" % (lv, i, header(L, d, b), L.link_name(b + 0x10),
                                                  array_count(L, b + 0x14), array_count(L, b + 0x18)))
        for i, name, b in L.of(IGPA):
            n = u32(d, b + 0x14)
            kinds["IGPA"][1] += 1
            kinds["IGPA"][2] += n
            line = b"%d %d %s %d" % (lv, i, name, n)
            for k in range(n):
                r = L.link(b + 0x18 + 4 * k)
                if r is None:
                    line += b" -"
                else:
                    pl, pb = r[0], r[0].dat.inst[r[1]][2]
                    line += b" %d/%d" % (array_count(pl, pb + 0x14), array_count(pl, pb + 0x18))
            feed("IGPA", line + b"\n")
        for i, name, b in L.of(IGHH):
            lx, ly, rx, ry, nl, nr, n = struct.unpack_from("<4h3I", d, b + 0x24)
            kinds["IGHH"][1] += 1
            kinds["IGHH"][2] += n
            feed("IGHH", b"%d %d %s %s %s %d %d %d %d %d %d %d\n" % (lv, i, name, L.link_name(b + 0x1C),
                                                                 L.link_name(b + 0x20), lx, ly, rx, ry, nl, nr, n))
            for k in range(n):
                e = b + 0x38 + 0x44 * k
                feed("IGHH", cstr(d, e, 64) + b" %d %d\n" % struct.unpack_from("<hh", d, e + 64))
        for i, name, b in L.of(STNA):
            n = u16(d, b + 0x16)
            kinds["StNA"][1] += 1
            kinds["StNA"][2] += n
            feed("StNA", b"%d %d %s %d\n" % (lv, i, name, n))
            for k in range(n):
                r = L.link(b + 0x18 + 4 * k)
                feed("StNA", (cstr(*_at(L, r, 0), 128) if r else b"-") + b"\n")

    labels = {"TSFF": ("families", "fonts", "glyphs"), "SUBT": ("arrays", "entries"), "IGSt": ("strings",),
              "IGSA": ("arrays", "strings"), "IGPG": ("pages",), "IGPA": ("arrays", "pages"),
              "IGHH": ("overlays", "labels"), "StNA": ("arrays", "strings")}
    lines = []
    for k, (h, *counts) in kinds.items():
        lines.append(f"{k}: " + " ".join(f"{n}={c}" for n, c in zip(labels[k], counts)) + f" hash={h:08x}")
    fam = Family(level0, onidat.find(level0.dat, "TSFFTahoma"))
    lines.append("widths: " + " ".join(f"{f.size}/{f.style}={f.width(SAMPLE)}" for f in fam.fonts if f))
    best = []
    for style in range(3):
        for size in range(21):
            f, s, st = fam.best(size, style)
            best.append(f"{s}/{st}" if f else "-")
    lines.append("best: " + " ".join(best))
    for array, key, prefix in (("subtitles", b"c01_01_01", True), ("messages", b"XWELCOME", False)):
        t = subt_find(level0, array, key, prefix)
        lines.append(f"{array} {key.decode()}: " + (t.decode("latin-1") if t is not None else "-"))
    h = FNV_BASIS
    for line in lines:
        print(line)
        h = fnv(line.encode("latin-1") + b"\n", h)
    print(f"all: hash={h:08x}")


def _at(level, ref, off):
    d, b = level.data(ref)
    return d, b + off


def sheet(folder, family, size, out, style=0):
    """The glyphs of high byte 0 in a 16 x 16 grid, ink black on white, cells split by grey lines."""
    level0 = Level(folder, 0)
    i = onidat.find(level0.dat, "TSFF" + family)
    if i is None:
        sys.exit(f"no font family {family}")
    f = Family(level0, i).font(size, style)
    if f is None:
        sys.exit(f"{family} has no font of size {size}, style {style}")
    glyphs = [f.glyph(c) for c in range(256)]
    cw = max([g[2] for g in glyphs if g] + [1]) + 1
    ch = max([g[3] for g in glyphs if g] + [1]) + 1
    W, H = 16 * cw + 1, 16 * ch + 1
    px = bytearray(b"\xc0\xc0\xc0\xff" * W * H)
    for c, g in enumerate(glyphs):
        ox, oy = 1 + (c % 16) * cw, 1 + (c // 16) * ch
        for y in range(ch - 1):
            for x in range(cw - 1):
                o = ((oy + y) * W + ox + x) * 4
                px[o:o + 4] = b"\xff\xff\xff\xff"
        if not g or not any(g):
            continue
        bm = f.bitmap(g)
        for y in range(g[3]):
            for x in range(g[2]):
                v = bm[y * g[2] + x]
                o = ((oy + y) * W + ox + x) * 4
                px[o:o + 4] = bytes((v, v, v, 255))
    onitex.png(out, W, H, px)
    print(f"{out}: {family} {size}/{style}, {sum(1 for g in glyphs if g and any(g))} glyphs, {W}x{H}")


def text(folder, names):
    level0 = Level(folder, 0)
    out = sys.stdout.buffer
    for lv in names:
        L = level0 if lv == 0 else Level(folder, lv, level0)
        d = L.dat.d
        for i, name, b in L.of(SUBT):
            for e, t in subt_entries(L, b):
                out.write(b"level%d %s %s\t%s\n" % (lv, name, e, t))
        for i, name, b in L.of(IGSA):
            for k in range(u32(d, b + 0x14)):
                r = L.link(b + 0x18 + 4 * k)
                if r:
                    out.write(b"level%d IGSA %d.%d\t%s\n" % (lv, i, k, cstr(*_at(L, r, 0x10), 384)))
        for i, name, b in L.of(IGHH):
            for k in range(u32(d, b + 0x34)):
                out.write(b"level%d %s %d\t%s\n" % (lv, name, k, cstr(d, b + 0x38 + 0x44 * k, 64)))
        for i, name, b in L.of(STNA):
            for k in range(u16(d, b + 0x16)):
                r = L.link(b + 0x18 + 4 * k)
                out.write(b"level%d %s %d\t%s\n" % (lv, name, k, cstr(*_at(L, r, 0), 128) if r else b"-"))


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.exit(__doc__.strip())
    folder, rest = argv[0], argv[1:]
    if "--png" in rest:
        k = rest.index("--png")
        a = rest[k + 1:]
        sheet(folder, a[0], int(a[1]), a[2], int(a[3]) if len(a) > 3 else 0)
    elif "--text" in rest:
        names = [int(a[5:]) for a in rest if a.startswith("level")] or onidat.LEVELS
        text(folder, names)
    else:
        summary(folder)


if __name__ == "__main__":
    main(sys.argv[1:])
