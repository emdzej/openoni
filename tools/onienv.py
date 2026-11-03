#!/usr/bin/env python3
"""Reference reader for Oni's level geometry (the AKEV environment), standard library only.

    tools/onienv.py <game folder> [levelN ...]              summary per level and over all
    tools/onienv.py <game folder> level1 --obj out/env      Wavefront OBJ + MTL + the textures as PNGs
    tools/onienv.py <game folder> level1 --png out/env      a preview: the level from above and at an angle

The level is a set of "gunk quads" (AGQG): quads and triangles over one point array (PNTA) and one
texture coordinate array (TXCA), each with a baked shade per corner, flags, and a texture (AGQR, an index
into the TXMA texture array). Format: docs/formats/akev.md.

Each level's file is resolved after level0, as the game loads it: textures a level shares come from
level0. The hash is a CRC-32 over every quad (its corner count, flags, object id and texture index as
u32, then per corner the point, the texture coordinates and the shade as stored, then its texture's
name, "-" for none, and "\\n") and one line per level; akev_test prints the same.
The exports hold the quads the game draws (AKiEnvironment_ComputeVis 0x444030), grouped by texture.
"""
import os
import struct
import sys
import zlib

import onidat
import onitex

AKEV = 0x414B4556
F_POINTS, F_TEXCOORDS, F_QUADS, F_RENDER, F_TEXTURES, F_BOUNDS = 0x00, 0x08, 0x0C, 0x10, 0x1C, 0x44
ARRAY_COUNT, PNTA_COUNT = 0x14, 0x34
QUAD_SIZE = 0x38
TRIANGLE, NO_DRAW = 0x40, 0x42000


def drawn(flags):
    """AKiEnvironment_ComputeVis 0x444030: none of the low four flags (door frame, ghost, stairs) and
    none of 0x42000."""
    return flags & 0xF == 0 and flags & NO_DRAW == 0


class Env:
    def __init__(self, dat, level0=None):
        self.dat = dat
        idx = [i for i, x in enumerate(dat.inst) if x[0] == AKEV and x[2] is not None]
        if len(idx) != 1:
            raise ValueError(f"{len(idx)} environments")
        d, base = dat.d, dat.inst[idx[0]][2]

        def arr(field, count_at, size):
            (link,) = struct.unpack_from("<I", d, base + field)
            if not link:
                return b"", 0
            p = dat.inst[link >> 8][2]
            (n,) = struct.unpack_from("<I", d, p + count_at)
            return d[p + count_at + 4:p + count_at + 4 + n * size], n

        self.points, self.n_points = arr(F_POINTS, PNTA_COUNT, 12)
        self.uvs, self.n_uvs = arr(F_TEXCOORDS, ARRAY_COUNT, 8)
        quads, n = arr(F_QUADS, ARRAY_COUNT, QUAD_SIZE)
        render, nr = arr(F_RENDER, ARRAY_COUNT, 4)
        tex, nt = arr(F_TEXTURES, ARRAY_COUNT, 4)
        self.bounds = struct.unpack_from("<6f", d, base + F_BOUNDS)
        # the texture array's names; a placeholder only counts if level0 has the texture
        self.textures = []
        for k in range(nt):
            (link,) = struct.unpack_from("<I", tex, 4 * k)
            name = None
            if link:
                _, name, data, _, _ = dat.inst[link >> 8]
                if data is None and (level0 is None or onidat.find(level0, name) is None):
                    name = None
            self.textures.append(name)
        if nr != n:
            raise ValueError(f"{n} quads, {nr} render entries")
        self.quads = []
        for i in range(n):
            e = struct.unpack_from("<14I", quads, i * QUAD_SIZE)
            (t,) = struct.unpack_from("<H", render, 4 * i)
            if max(e[0:4]) >= self.n_points or max(e[4:8]) >= self.n_uvs:
                raise ValueError(f"quad {i}: index past its array")
            corners = 3 if e[12] & TRIANGLE else 4
            self.quads.append((corners, e[0:4], e[4:8], e[8:12], e[12], e[13], t))

    def texture_name(self, t):
        return self.textures[t] if t < len(self.textures) else None

    def hash(self, h=0):
        for corners, pts, uvs, shades, flags, obj, t in self.quads:
            b = bytearray(struct.pack("<4I", corners, flags, obj, t))
            for k in range(corners):
                b += self.points[pts[k] * 12:pts[k] * 12 + 12] + self.uvs[uvs[k] * 8:uvs[k] * 8 + 8]
                b += struct.pack("<I", shades[k])
            b += (self.texture_name(t) or "-").encode("latin-1") + b"\n"
            h = zlib.crc32(b, h)
        return h

    def xyz(self, i):
        return struct.unpack_from("<3f", self.points, i * 12)


class TextureSource:
    """A texture's pixels by name: from the level's own file, or level0's for the shared ones."""

    def __init__(self, folder, lv, exe):
        self.dec = onitex.Decoder(exe)
        self.files = []
        for name in dict.fromkeys(["level0", lv]):
            dat = onidat.Dat(f"{folder}/GameDataFolder/{name}_Final.dat")
            self.files.insert(0, (dat, f"{folder}/GameDataFolder/{name}_Final.raw"))
        self.raws = {}

    def top_level(self, name):
        for dat, raw_path in self.files:
            i = onidat.find(dat, name)
            if i is None:
                continue
            data = dat.inst[i][2]
            f, w, h, ty = struct.unpack_from("<IHHI", dat.d, data + onitex.F_FLAGS)
            (off,) = struct.unpack_from("<I", dat.d, data + onitex.F_RAW)
            raw = self.raws.setdefault(raw_path, open(raw_path, "rb").read())
            n = self.dec.size(ty, w, h, f & onitex.FLAG_MIPMAPS)
            return self.dec.decode(ty, w, h, f & onitex.FLAG_MIPMAPS, raw[off:off + n])[0]
        return None


def export_obj(env, src, outdir, lv):
    os.makedirs(f"{outdir}/{lv}", exist_ok=True)
    groups = {}
    for q in env.quads:
        if drawn(q[4]):
            groups.setdefault(q[6], []).append(q)
    with open(f"{outdir}/{lv}.mtl", "w") as m:
        for t in sorted(groups):
            name = env.texture_name(t) or f"none{t}"
            m.write(f"newmtl {name}\nKd 1 1 1\n")
            img = src.top_level(name) if env.texture_name(t) else None
            if img:
                file = name.replace("/", "_")   # some names have a /
                onitex.png(f"{outdir}/{lv}/{file}.png", *img)
                m.write(f"map_Kd {lv}/{file}.png\n")
            m.write("\n")
    with open(f"{outdir}/{lv}.obj", "w") as f:
        f.write(f"# {lv}: the quads the game draws, by texture\nmtllib {lv}.mtl\n")
        for i in range(env.n_points):
            f.write("v %.6f %.6f %.6f\n" % env.xyz(i))
        for i in range(env.n_uvs):
            u, v = struct.unpack_from("<2f", env.uvs, i * 8)
            f.write("vt %.6f %.6f\n" % (u, 1 - v))
        for t in sorted(groups):
            f.write(f"usemtl {env.texture_name(t) or f'none{t}'}\n")
            for corners, pts, uvs, *_ in groups[t]:
                f.write("f " + " ".join(f"{pts[k] + 1}/{uvs[k] + 1}" for k in range(corners)) + "\n")


def preview(env, src, path, size=640):
    """Three views side by side, painter's order, back faces culled (a quad faces along (b-a)x(c-a), which
    its collision plane agrees with): from above, from above at an angle, and the plan (from above, cut
    at a fifth of the height). Each quad is filled with its texture's mean colour times its mean shade."""
    import math
    mean = {}
    for t in range(len(env.textures)):
        img = src.top_level(env.textures[t]) if env.textures[t] else None
        if img:
            w, h, px = img
            mean[t] = tuple(sum(px[k::4]) / (w * h) / 255 for k in range(3))
    lo, hi = env.bounds[:3], env.bounds[3:]
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]
    img = bytearray(b"\x20\x20\x28\xff" * (size * 3) * size)

    def cross(u, v):
        return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])

    def dot(u, v):
        return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]

    ya, el = math.radians(35), math.radians(50)
    cams = [((0, -1, 0), (1, 0, 0)),
            ((math.cos(el) * math.sin(ya), -math.sin(el), math.cos(el) * math.cos(ya)), (math.cos(ya), 0, -math.sin(ya))),
            ((0, -1, 0), (1, 0, 0))]
    cut = lo[1] + (hi[1] - lo[1]) / 5
    quads = [q for q in env.quads if drawn(q[4]) and not q[4] & 0x80]
    pts = [env.xyz(i) for i in range(env.n_points)]
    for vi, (f, r) in enumerate(cams):
        u = cross(f, r)

        def view(p):
            d = (p[0] - c[0], p[1] - c[1], p[2] - c[2])
            return dot(d, r), -dot(d, u), dot(d, f)
        # scaled to the stored bounds: points no quad uses lie far out (-2097152)
        box = [view((x, y, z)) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
        s = (size - 8) / (2 * max(max(abs(p[0]) for p in box), max(abs(p[1]) for p in box)) or 1)
        faces = []
        for corners, idx, uvs, shades, flags, obj, t in quads:
            if vi == 2 and min(pts[idx[k]][1] for k in range(corners)) > cut:
                continue
            a, b, cc = (pts[idx[k]] for k in range(3))
            n = cross([b[k] - a[k] for k in range(3)], [cc[k] - a[k] for k in range(3)])
            if dot(n, f) >= 0:
                continue
            poly = [view(pts[idx[k]]) for k in range(corners)]
            depth = sum(p[2] for p in poly) / corners
            col = mean.get(t, (0.6, 0.6, 0.6))
            sh = [sum((sv >> bit) & 0xFF for sv in shades[:corners]) / corners / 255 for bit in (16, 8, 0)]
            rgb = bytes(min(255, int(col[k] * sh[k] * 255 * 1.6)) for k in range(3))
            scr = [(p[0] * s + size / 2 + vi * size, p[1] * s + size / 2, p[2]) for p in poly]
            faces.append((depth, scr, rgb))
        faces.sort(key=lambda x: -x[0])
        for depth, scr, rgb in faces:
            for tri in ((0, 1, 2), (0, 2, 3))[:len(scr) - 2]:
                fill(img, size * 3, size, vi * size, [scr[k] for k in tri], rgb)
    onitex.png(path, size * 3, size, img)


def fill(img, w, h, x0, tri, rgb):
    (ax, ay, _), (bx, by, _), (cx, cy, _) = tri
    den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if abs(den) < 1e-9:
        return
    xmin, xmax = max(x0, int(min(ax, bx, cx))), min(x0 + h - 1, int(max(ax, bx, cx)) + 1)
    ymin, ymax = max(0, int(min(ay, by, cy))), min(h - 1, int(max(ay, by, cy)) + 1)
    for y in range(ymin, ymax + 1):
        row = y * w
        for x in range(xmin, xmax + 1):
            w0 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den
            w1 = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / den
            if w0 >= -1e-6 and w1 >= -1e-6 and w0 + w1 <= 1 + 1e-6:
                o = (row + x) * 4
                img[o:o + 3] = rgb


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.exit(__doc__.strip())
    folder, rest = argv[0], argv[1:]
    names = [a for a in rest if a.startswith("level")] or [f"level{n}" for n in onidat.LEVELS if n]
    opts = [a for a in rest if a.startswith("--")]
    level0 = onidat.Dat(f"{folder}/GameDataFolder/level0_Final.dat")
    total = 0
    for lv in names:
        dat = onidat.Dat(f"{folder}/GameDataFolder/{lv}_Final.dat")
        env = Env(dat, level0)
        for mode in ("--obj", "--png"):
            if mode in opts:
                outdir = rest[rest.index(mode) + 1]
                os.makedirs(outdir, exist_ok=True)
                src = TextureSource(folder, lv, onidat.Exe(f"{folder}/Oni.exe"))
                if mode == "--obj":
                    export_obj(env, src, outdir, lv)
                else:
                    preview(env, src, f"{outdir}/{lv}.png")
        if opts:
            continue
        h = env.hash()
        n_drawn = sum(drawn(q[4]) for q in env.quads)
        print(f"{lv}: points={env.n_points} quads={len(env.quads)} drawn={n_drawn} "
              f"textures={len(env.textures)} hash={h:08x}")
        total = zlib.crc32(f"{lv} {h:08x}\n".encode(), total)
    if not opts:
        print(f"all: hash={total:08x}")


if __name__ == "__main__":
    main(sys.argv[1:])
