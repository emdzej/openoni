#!/usr/bin/env python3
"""Reference reader for Oni's object animations (OBAN instances), standard library only.

    tools/onioban.py <game folder> [levelN ...]                 summary per level and over all
    tools/onioban.py <game folder> level1 --list                every animation: index, flags, frames, keys, name
    tools/onioban.py <game folder> level1 --matrix NAME FRAME   the object's matrix at a frame (3 decimals)

An animation is a start matrix, a base matrix and keys (a quaternion, a position and the frame it is
at); between keys both are lerped. The matrix is T(position) R(rotation) Base, turned a quarter about x
for the doors (flag 0x10). Format: docs/formats/oban.md.

The hash is a CRC-32 over one line per animation ("index name flags frames door keys crc08x\\n", the CRC
over the two matrices and the keys as stored) and per level; oban_test prints the same, and the same
--matrix values.
"""
import math
import struct
import sys
import zlib

import onidat

OBAN = 0x4F42414E
Z_UP = 0x10


def info(dat, i):
    d, b = dat.d, dat.inst[i][2]
    flags, = struct.unpack_from("<I", d, b + 0x0C)
    ticks, frames, door, keys = struct.unpack_from("<4H", d, b + 0x70)
    return flags, frames, door, keys


def crc(dat, i):
    d, b = dat.d, dat.inst[i][2]
    keys = struct.unpack_from("<H", d, b + 0x76)[0]
    return zlib.crc32(d[b + 0x10:b + 0x78 + 0x20 * keys])


def f32(v):
    return struct.unpack("<f", struct.pack("<f", v))[0]


def lerp_quat(a, b, t):
    """MUrQuat_Lerp 0x433360, as tram.c's port."""
    t = f32(t)
    if f32(sum(f32(a[k] * b[k]) for k in range(4))) < 0:
        b = [-v for v in b]
    u = f32(1 - t)
    o = [f32(f32(b[k] * t) + f32(a[k] * u)) for k in range(4)]
    n = f32(sum(f32(v * v) for v in o))
    if n <= 0:
        return [0, 0, 0, 1]
    k = f32(1 / f32(math.sqrt(n)))
    return [f32(v * k) for v in o]


def quat_matrix(q):
    """MUrQuatToMatrix 0x4338c0: 3x3 by columns, as body.c's port."""
    x, y, z, w = q
    s = 2.0 / (w * w + z * z + y * y + x * x)
    ys, zs, xw, xx = s * y, s * z, s * x * w, s * x * x
    return [1 - (zs * z + ys * y), ys * x - zs * w, zs * x + ys * w,
            ys * x + zs * w, 1 - (zs * z + xx), zs * y - xw,
            zs * x - ys * w, zs * y + xw, 1 - (ys * y + xx)]


def mul(a, b):
    """MUrMatrix_Multiply 0x4341c0: a b, 3x4 by columns."""
    o = []
    for c in range(4):
        for r in range(3):
            v = a[r] * b[3 * c] + a[3 + r] * b[3 * c + 1] + a[6 + r] * b[3 * c + 2]
            o.append(v + (a[9 + r] if c == 3 else 0))
    return o


def matrix(dat, i, frame):
    d, b = dat.d, dat.inst[i][2]
    flags, frames, door, n = info(dat, i)
    keys = [struct.unpack_from("<7fI", d, b + 0x78 + 0x20 * k) for k in range(n)]
    q = p = None
    for k, e in enumerate(keys):
        if e[7] == frame or (frame < e[7] and k == 0):
            q, p = e[0:4], e[4:7]
            break
        if frame < e[7]:
            a = keys[k - 1]
            t = f32((frame - a[7]) / (e[7] - a[7]))
            p = [t * e[4 + c] + (1 - t) * a[4 + c] for c in range(3)]
            q = lerp_quat(a[0:4], e[0:4], t)
            break
    if q is None:
        q, p = keys[-1][0:4], keys[-1][4:7]
    base = list(struct.unpack_from("<12f", d, b + 0x40))
    m = mul(mul([1, 0, 0, 0, 1, 0, 0, 0, 1] + list(p), quat_matrix(q) + [0, 0, 0]), base)
    if flags & Z_UP:
        c, s = math.cos(1.5707963705062866), math.sin(1.5707963705062866)
        m = mul([1, 0, 0, 0, c, s, 0, -s, c, 0, 0, 0], m)
    return m


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
            for i, x in enumerate(dat.inst):
                if x[0] == OBAN and x[2] is not None:
                    flags, frames, door, keys = info(dat, i)
                    print(f"{i:6d} flags {flags:#04x} frames {frames:4d} door {door:3d} keys {keys:4d} {x[1]}")
            continue
        if "--matrix" in opts:
            k = rest.index("--matrix")
            name, frame = rest[k + 1], int(rest[k + 2])
            i = onidat.find(dat, name if name.startswith("OBAN") else "OBAN" + name)
            m = matrix(dat, i, frame)
            print("matrix:" + "".join(f" {0.0 if abs(v) < 0.0005 else v:8.3f}" for v in m))   # no "-0.000"
            continue
        h, count = 0, 0
        for i, x in enumerate(dat.inst):
            if x[0] != OBAN or x[2] is None:
                continue
            flags, frames, door, keys = info(dat, i)
            line = f"{i} {x[1] or '-'} {flags} {frames} {door} {keys} {crc(dat, i):08x}\n"
            h = zlib.crc32(line.encode("latin-1"), h)
            count += 1
        print(f"{lv}: animations={count} hash={h:08x}")
        total = zlib.crc32(f"{lv} {h:08x}\n".encode(), total)
    if not opts:
        print(f"all: hash={total:08x}")


if __name__ == "__main__":
    main(sys.argv[1:])
