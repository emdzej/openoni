#!/usr/bin/env python3
"""Reference reader for Oni's animations (TRAM instances), standard library only.

    tools/onitram.py <game folder> [levelN ...]               summary per level and over all
    tools/onitram.py <game folder> level0 --list              every animation: index, frames, type, states, name
    tools/onitram.py <game folder> level0 --dump NAME FRAME   the bones' rotations at a frame, and the root's motion

An animation keeps, per bone, a stream of keyframes in the .raw: keys of 6 bytes (three u16 angles) or
16 (a float quaternion), each but the last followed by a byte, the frames to the next. Between keys the
rotation is lerped. Format: docs/formats/tram.md.

The hash is a CRC-32 over one line per animation ("index name frames bones keysize type from to
crc08x\\n", the CRC over every bone's stream as walked, then the velocities and heights, frames of
each) and per level; tram_test prints the same. The decoded rotations aren't hashed (the exe's x87
math isn't reproduced bit for bit); --dump and tram_test print the same values to 5 decimals.
"""
import math
import struct
import sys
import zlib

import onidat

TRAM = 0x5452414D
F_HEIGHTS, F_VELOCITIES, F_ROTATIONS = 0x04, 0x08, 0x2C
ANGLE = struct.unpack("<f", struct.pack("<f", 9.5875264e-05))[0]   # 2 pi / 65535, as the float at 0x5297f0


class Anim:
    def __init__(self, dat, raw, index):
        d, b = dat.d, dat.inst[index][2]
        self.name = dat.inst[index][1]
        self.raw = raw
        (self.fps, self.keysize, self.type, self.aim, self.frm, self.to, self.bones, self.frames,
         self.duration, self.varient) = struct.unpack_from("<10H", d, b + 0x156)
        self.heights, self.velocities, self.rotations = (struct.unpack_from("<I", d, b + f)[0]
                                                         for f in (F_HEIGHTS, F_VELOCITIES, F_ROTATIONS))

    def streams(self):
        """Per bone: [(key bytes, frames to the next)], the last with None."""
        r, base, ks = self.raw, self.rotations, self.keysize
        out = []
        for b in range(self.bones):
            (o,) = struct.unpack_from("<H", r, base + 2 * b)
            p, t, keys = base + o, 0, []
            while t < self.frames - 1:
                keys.append((r[p:p + ks], r[p + ks]))
                t += r[p + ks]
                p += ks + 1
            keys.append((r[p:p + ks], None))
            out.append(keys)
        return out

    def crc(self):
        c = 0
        for keys in self.streams():
            for k, dur in keys:
                c = zlib.crc32(k + (bytes([dur]) if dur is not None else b""), c)
        if self.velocities:
            c = zlib.crc32(self.raw[self.velocities:self.velocities + 8 * self.frames], c)
        if self.heights:
            c = zlib.crc32(self.raw[self.heights:self.heights + 4 * self.frames], c)
        return c

    def rotations_at(self, frame):
        """0x427820: the key before the frame, the key after, or the lerp of the two."""
        out = []
        for keys in self.streams():
            t, i = 0, 0
            while True:
                dur = keys[i][1]
                t += dur
                if not frame > t:
                    break
                i += 1
            off = frame - t + dur
            q0, q1 = key_quat(keys[i][0]), key_quat(keys[i + 1][0])
            out.append(q0 if off == 0 else q1 if off == dur else lerp(q0, q1, off / dur))
        return out


def f32(v):
    return struct.unpack("<f", struct.pack("<f", v))[0]


def key_quat(k):
    if len(k) == 16:
        return struct.unpack("<4f", k)
    x, y, z = (f32(v * ANGLE) for v in struct.unpack("<3H", k))
    return euler(x, y, z)


def euler(x, y, z):
    """0x437050: y enters as 2 pi less its half angle. The x87 keeps the sines and cosines of x and z at
    full precision; what it stores is rounded to floats, as here."""
    ya = f32(6.2831854820251465 - f32(y * 0.5))
    cz, sz, cx, sx = math.cos(z * 0.5), math.sin(z * 0.5), math.cos(x * 0.5), math.sin(x * 0.5)
    cy, sy = f32(math.cos(ya)), f32(math.sin(ya))
    a, b, c = f32(f32(cx) * f32(cz)), f32(sx * f32(cz)), f32(sz * f32(cx))
    return (f32(f32(b * cy) - f32(c * sy)), f32(-(sx * sz * cy) - a * sy), f32(f32(c * cy) - f32(b * sy)),
            f32(-(a * cy + sx * sz * sy)))


def lerp(a, b, t):
    """MUrQuat_Lerp 0x433360, in floats."""
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


def anims(dat, raw):
    for i, x in enumerate(dat.inst):
        if x[0] == TRAM and x[2] is not None:
            yield i, Anim(dat, raw, i)


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.exit(__doc__.strip())
    folder, rest = argv[0], argv[1:]
    names = [a for a in rest if a.startswith("level")] or [f"level{n}" for n in onidat.LEVELS]
    opts = [a for a in rest if a.startswith("--")]
    total = 0
    for lv in names:
        dat = onidat.Dat(f"{folder}/GameDataFolder/{lv}_Final.dat")
        raw = open(f"{folder}/GameDataFolder/{lv}_Final.raw", "rb").read()
        if "--list" in opts:
            for i, a in anims(dat, raw):
                print(f"{i:6d} {a.frames:4d} type {a.type:3d} {a.frm:3d}->{a.to:<3d} key {a.keysize:2d} {a.name}")
            continue
        if "--dump" in opts:
            k = rest.index("--dump")
            name, frame = rest[k + 1], int(rest[k + 2])
            i = onidat.find(dat, name if name.startswith("TRAM") else "TRAM" + name)
            a = Anim(dat, raw, i)
            for b, q in enumerate(a.rotations_at(frame)):
                print(f"bone {b:2d}: " + " ".join(f"{v:9.5f}" for v in q))
            if a.velocities:
                print("velocity: %.5f %.5f" % struct.unpack_from("<2f", raw, a.velocities + 8 * frame))
            if a.heights:
                print("height: %.5f" % struct.unpack_from("<f", raw, a.heights + 4 * frame))
            continue
        h, count = 0, 0
        for i, a in anims(dat, raw):
            line = f"{i} {a.name or '-'} {a.frames} {a.bones} {a.keysize} {a.type} {a.frm} {a.to} {a.crc():08x}\n"
            h = zlib.crc32(line.encode("latin-1"), h)
            count += 1
        print(f"{lv}: animations={count} hash={h:08x}")
        total = zlib.crc32(f"{lv} {h:08x}\n".encode(), total)
    if not opts:
        print(f"all: hash={total:08x}")


if __name__ == "__main__":
    main(sys.argv[1:])
