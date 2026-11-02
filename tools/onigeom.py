#!/usr/bin/env python3
"""Reference reader for Oni's geometry (M3GM instances and the arrays they link to), standard library only.

    tools/onigeom.py <game folder> [levelN ...]                  summary per level and over all
    tools/onigeom.py <game folder> level0 --list                 every geometry: index, points, triangles, name
    tools/onigeom.py <game folder> level0 --obj out/geom NAME..  Wavefront OBJ (positions, UVs, normals)
    tools/onigeom.py <game folder> level0 --png out/geom NAME..  a flat-shaded preview, three views

A geometry is points (PNTA), vertex normals and face normals (VCRA), texture coordinates (TXCA), triangle
strips and a face normal index per triangle (IDXA), and a texture (TXMP). Format: docs/formats/m3gm.md.

The hash is a CRC-32 over one line per geometry ("index name points tris crc08x\\n", the CRC over the
points, UVs and vertex normals as stored, then the triangles as decoded and each one's face normal)
and per level; m3gm_test prints the same.
"""
import math
import struct
import sys
import zlib

import onidat

M3GM = 0x4D33474D
ARRAY_COUNT = 0x14          # VCRA, TXCA, IDXA: 20 bytes of padding, then the count and the elements
PNTA_COUNT = 0x34           # PNTA: padding, bounding box (6 floats), bounding sphere (4), then the count


def array(dat, link, count_at, elem_size):
    if not link:
        return b"", 0
    data = dat.inst[link >> 8][2]
    (n,) = struct.unpack_from("<I", dat.d, data + count_at)
    start = data + count_at + 4
    return dat.d[start:start + n * elem_size], n


def strips(idx):
    """MSrGeomContext_Method_Geometry_Draw 0x410930: a value with bit 31 set starts a strip with it
    and the next two; every other value makes a triangle with the two before it, in that order (no
    flip)."""
    tris, k = [], 0
    a = b = 0
    while k < len(idx):
        v = idx[k]
        if v & 0x80000000:
            a, b, c = v & 0x7FFFFFFF, idx[k + 1], idx[k + 2]
            k += 3
        else:
            c = v
            k += 1
        tris.append((a, b, c))
        a, b = b, c
    return tris


class Geometry:
    def __init__(self, dat, index):
        self.name = dat.inst[index][1]
        data = dat.inst[index][2]
        (self.flags, pnta, vn, fn, txca, st, fidx, txmp, _) = struct.unpack_from("<9I", dat.d, data)
        self.points, self.n_points = array(dat, pnta, PNTA_COUNT, 12)
        self.vnormals, _ = array(dat, vn, ARRAY_COUNT, 12)
        self.fnormals, self.n_fnormals = array(dat, fn, ARRAY_COUNT, 12)
        self.uvs, _ = array(dat, txca, ARRAY_COUNT, 8)
        raw, n = array(dat, st, ARRAY_COUNT, 4)
        self.tris = strips(struct.unpack(f"<{n}I", raw))
        raw, n = array(dat, fidx, ARRAY_COUNT, 4)
        self.face_normal = struct.unpack(f"<{n}I", raw)
        self.texture = dat.inst[txmp >> 8][1] if txmp else None
        if len(self.face_normal) != len(self.tris):
            raise ValueError(f"{self.name}: {len(self.tris)} triangles, {len(self.face_normal)} face normals")
        for t in self.tris:
            if max(t) >= self.n_points:
                raise ValueError(f"{self.name}: index {max(t)} of {self.n_points} points")

    def crc(self):
        c = zlib.crc32(self.points)
        c = zlib.crc32(self.uvs, c)
        c = zlib.crc32(self.vnormals, c)
        for t, f in zip(self.tris, self.face_normal):
            c = zlib.crc32(struct.pack("<4I", *t, f), c)
        return c

    def xyz(self):
        return [struct.unpack_from("<3f", self.points, i * 12) for i in range(self.n_points)]


def obj(g, path):
    with open(path, "w") as f:
        f.write(f"# {g.name} (texture {g.texture})\n")
        for p in g.xyz():
            f.write("v %.6f %.6f %.6f\n" % p)
        for i in range(g.n_points):
            u, v = struct.unpack_from("<2f", g.uvs, i * 8)
            f.write("vt %.6f %.6f\n" % (u, 1 - v))
        for i in range(g.n_points):
            f.write("vn %.6f %.6f %.6f\n" % struct.unpack_from("<3f", g.vnormals, i * 12))
        for t in g.tris:
            f.write("f " + " ".join(f"{i + 1}/{i + 1}/{i + 1}" for i in t) + "\n")


def preview(g, path, size=256):
    """Front, side and top views side by side, flat-shaded by the face normal, painter's order."""
    import onitex
    pts = g.xyz()
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    span = max(hi[k] - lo[k] for k in range(3)) or 1
    img = bytearray(b"\x30\x30\x30\xff" * (size * 3) * size)
    views = [(0, 1, 2), (2, 1, 0), (0, 2, 1)]   # (horizontal axis, vertical axis, depth axis)
    light = (0.3, 0.8, 0.5)
    for vi, (ax, ay, az) in enumerate(views):
        def proj(p):
            x = (p[ax] - lo[ax]) / span * (size - 8) + 4 + vi * size
            y = size - 4 - (p[ay] - lo[ay]) / span * (size - 8)
            return x, y, p[az]
        order = sorted(range(len(g.tris)), key=lambda i: sum(proj(pts[j])[2] for j in g.tris[i]))
        for i in order:
            a, b, c = (proj(pts[j]) for j in g.tris[i])
            n = struct.unpack_from("<3f", g.fnormals, g.face_normal[i] * 12)
            shade = int(60 + 180 * abs(sum(n[k] * light[k] for k in range(3))))
            xmin, xmax = int(max(vi * size, min(a[0], b[0], c[0]))), int(min(vi * size + size - 1, max(a[0], b[0], c[0])))
            ymin, ymax = int(max(0, min(a[1], b[1], c[1]))), int(min(size - 1, max(a[1], b[1], c[1])))
            den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if abs(den) < 1e-12:
                continue
            for y in range(ymin, ymax + 1):
                for x in range(xmin, xmax + 1):
                    w0 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / den
                    w1 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / den
                    if w0 >= 0 and w1 >= 0 and w0 + w1 <= 1:
                        o = (y * size * 3 + x) * 4
                        img[o:o + 3] = bytes((shade, shade, min(255, shade + 20)))
    onitex.png(path, size * 3, size, img)


def geometries(dat):
    for i, (tag, name, data, size, flags) in enumerate(dat.inst):
        if tag == M3GM and data is not None:
            yield i, Geometry(dat, i)


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.exit(__doc__.strip())
    folder, rest = argv[0], argv[1:]
    names = [a for a in rest if a.startswith("level")] or [f"level{n}" for n in onidat.LEVELS]
    opts = [a for a in rest if a.startswith("--")]
    total = 0
    for lv in names:
        dat = onidat.Dat(f"{folder}/GameDataFolder/{lv}_Final.dat")
        if "--list" in opts:
            for i, g in geometries(dat):
                print(f"{i:6d} {g.n_points:6d} {len(g.tris):6d} {g.name or '-'} {g.texture or '-'}")
            continue
        for mode in ("--obj", "--png"):
            if mode in opts:
                import os
                k = rest.index(mode)
                outdir, want = rest[k + 1], set(rest[k + 2:])
                os.makedirs(outdir, exist_ok=True)
                for i, g in geometries(dat):
                    label = g.name or f"M3GM{lv}_{i}"
                    if want and label not in want and label[4:] not in want:
                        continue
                    (obj if mode == "--obj" else preview)(g, f"{outdir}/{label}.{mode[2:]}")
        if "--obj" in opts or "--png" in opts:
            continue
        h, count = 0, 0
        for i, g in geometries(dat):
            line = f"{i} {g.name or '-'} {g.n_points} {len(g.tris)} {g.crc():08x}\n"
            h = zlib.crc32(line.encode("latin-1"), h)
            count += 1
        print(f"{lv}: geometries={count} hash={h:08x}")
        total = zlib.crc32(f"{lv} {h:08x}\n".encode(), total)
    if not opts:
        print(f"all: hash={total:08x}")


if __name__ == "__main__":
    main(sys.argv[1:])
