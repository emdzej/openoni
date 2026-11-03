#!/usr/bin/env python3
"""Reference reader for Oni's character bodies (ONCC -> TRBS -> TRCM), standard library only.

    tools/onibody.py <game folder> level1 --list                              the character classes
    tools/onibody.py <game folder> level1 --obj out/body konoko_generic [ANIM FRAME]
                                             the body (the highest detail) as OBJ, in its rest pose or posed
    tools/onibody.py <game folder> level1 --png out/body konoko_generic [ANIM FRAME]   a preview, three views

A character class (ONCC) links a body set (TRBS, at 0xc34): five bodies (TRCM), lowest detail first, and
a texture per part (TRMA, at 0xc38). A body has a geometry (M3GM) per bone (TRGA), the bone's
translation from its parent (TRTA) and the hierarchy (TRIA: parent, first child, next sibling, as
bytes). iBuildMatriciesRecursive 0x427570 builds each bone's matrix as its parent's, translated by the
bone's offset (0x434c90), times the bone's rotation (MUrMatrixStack_Quaternion 0x434c00). Matrices are
3x4, by columns, the last column the translation. Format: docs/formats/oncc.md.
"""
import os
import struct
import sys

import onidat
import onigeom
import onitex
import onitram

ONCC = 0x4F4E4343
F_BODY_SET, F_TEXTURES = 0xC34, 0xC38
F_BONES, F_GEOMETRIES, F_TRANSLATIONS, F_HIERARCHY = 0x04, 0x54, 0x58, 0x5C


def var_array(dat, link, size):
    """The arrays with a u16 count at 0x16, elements from 0x18 (TRGA, TRTA, TRIA, TRMA)."""
    p = dat.inst[link >> 8][2]
    (n,) = struct.unpack_from("<H", dat.d, p + 0x16)
    return [dat.d[p + 0x18 + k * size:p + 0x18 + (k + 1) * size] for k in range(n)]


def quat_matrix(q):
    """MUrQuatToMatrix 0x4338c0, by columns."""
    x, y, z, w = q
    s = 2 / (x * x + y * y + z * z + w * w)
    ys, zs, xw, xx = s * y, s * z, s * x * w, s * x * x
    return [1 - (zs * z + ys * y), ys * x - zs * w, zs * x + ys * w,
            ys * x + zs * w, 1 - (zs * z + xx), zs * y - xw,
            zs * x - ys * w, zs * y + xw, 1 - (ys * y + xx)]


def mul(m, r):
    """3x4 matrix m times a 3x3 rotation r (both by columns): the translation stays."""
    out = [0.0] * 12
    for c in range(3):
        for row in range(3):
            out[3 * c + row] = sum(m[3 * k + row] * r[3 * c + k] for k in range(3))
    out[9:12] = m[9:12]
    return out


def apply(m, p):
    return tuple(m[row] * p[0] + m[3 + row] * p[1] + m[6 + row] * p[2] + m[9 + row] for row in range(3))


class Body:
    def __init__(self, dat, trcm):
        d, b = dat.d, dat.inst[trcm][2]
        (self.bones,) = struct.unpack_from("<H", d, b + F_BONES)
        geo, tra, hie = struct.unpack_from("<3I", d, b + F_GEOMETRIES)
        self.geoms = [struct.unpack("<I", e)[0] >> 8 for e in var_array(dat, geo, 4)]
        self.trans = [struct.unpack("<3f", e) for e in var_array(dat, tra, 12)]
        self.tree = [tuple(e) for e in var_array(dat, hie, 4)]

    def matrices(self, quats=None):
        """iBuildMatriciesRecursive 0x427570 from the root (bone 0) with the identity."""
        out = [None] * self.bones
        ident = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]

        def build(bone, parent):
            while True:
                _, child, sibling, _ = self.tree[bone]
                m = list(parent)
                t = self.trans[bone]
                m[9:12] = apply(parent, t)
                if quats:
                    m = mul(m, quat_matrix(quats[bone]))
                out[bone] = m
                if sibling:
                    build(sibling, parent)
                if not child:
                    return
                bone, parent = child, m
        build(0, ident)
        return out


class Mesh:
    """The posed body as one geometry, for onigeom.preview: face normals from the corners."""

    def __init__(self, dat, body, mats):
        self.pts, self.tris = [], []
        for bone in range(body.bones):
            g = onigeom.Geometry(dat, body.geoms[bone])
            base = len(self.pts)
            self.pts += [apply(mats[bone], p) for p in g.xyz()]
            self.tris += [tuple(base + i for i in t) for t in g.tris]
        self.n_points = len(self.pts)
        fn = bytearray()
        for a, b, c in self.tris:
            a, b, c = self.pts[a], self.pts[b], self.pts[c]
            u = [b[k] - a[k] for k in range(3)]
            v = [c[k] - a[k] for k in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            ln = sum(x * x for x in n) ** 0.5 or 1
            fn += struct.pack("<3f", *(x / ln for x in n))
        self.fnormals, self.face_normal = bytes(fn), list(range(len(self.tris)))

    def xyz(self):
        return self.pts


def body_of(dat, oncc):
    d, b = dat.d, dat.inst[oncc][2]
    trbs, trma = struct.unpack_from("<2I", d, b + F_BODY_SET)
    bodies = [struct.unpack_from("<I", d, dat.inst[trbs >> 8][2] + 4 * k)[0] >> 8 for k in range(5)]
    textures = [struct.unpack("<I", e)[0] for e in var_array(dat, trma, 4)] if trma else []
    return bodies, textures


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.exit(__doc__.strip())
    folder, lv, rest = argv[0], argv[1], argv[2:]
    dat = onidat.Dat(f"{folder}/GameDataFolder/{lv}_Final.dat")
    if "--list" in rest:
        for i, x in enumerate(dat.inst):
            if x[0] == ONCC and x[2] is not None:
                bodies, tex = body_of(dat, i)
                print(f"{i:6d} {x[1]} bodies {[dat.inst[k][1] or k for k in bodies]}")
        return
    if "--matrices" in rest:    # tools/onibody.py <game> level1 --matrices NAME ANIM FRAME: as body_test prints
        k = rest.index("--matrices")
        outdir, name, pose = None, rest[k + 1], rest[k + 2:k + 4]
        mode = "--matrices"
    else:
        mode = "--png" if "--png" in rest else "--obj"
        k = rest.index(mode)
        outdir, name = rest[k + 1], rest[k + 2]
        pose = rest[k + 3:k + 5]
    oncc = onidat.find(dat, "ONCC" + name)
    bodies, textures = body_of(dat, oncc)
    body = Body(dat, bodies[4])
    quats = None
    if pose:
        raw = open(f"{folder}/GameDataFolder/{lv}_Final.raw", "rb").read()
        anim_dat, anim_raw = dat, raw
        t = onidat.find(dat, "TRAM" + pose[0])
        if t is None:   # most animations are level 0's
            anim_dat = onidat.Dat(f"{folder}/GameDataFolder/level0_Final.dat")
            anim_raw = open(f"{folder}/GameDataFolder/level0_Final.raw", "rb").read()
            t = onidat.find(anim_dat, "TRAM" + pose[0])
        quats = onitram.Anim(anim_dat, anim_raw, t).rotations_at(int(pose[1]))
    mats = body.matrices(quats)
    if mode == "--matrices":
        for bone, m in enumerate(mats):
            print(f"bone {bone:2d}: " + " ".join(f"{v:8.3f}" for v in m))
        return
    os.makedirs(outdir, exist_ok=True)
    label = name + (f"_{pose[0]}_{pose[1]}" if pose else "")
    if mode == "--png":
        onigeom.preview(Mesh(dat, body, mats), f"{outdir}/{label}.png")
        print(f"{outdir}/{label}.png")
        return
    with open(f"{outdir}/{label}.obj", "w") as f:
        f.write(f"# {name}, {dat.inst[bodies[4]][1] or 'body'}, {'posed ' + ' '.join(pose) if pose else 'rest pose'}\n")
        base = 1
        for bone in range(body.bones):
            g = onigeom.Geometry(dat, body.geoms[bone])
            f.write(f"o bone{bone}\n")
            for p in g.xyz():
                f.write("v %.5f %.5f %.5f\n" % apply(mats[bone], p))
            for i in range(g.n_points):
                u, v = struct.unpack_from("<2f", g.uvs, i * 8)
                f.write("vt %.5f %.5f\n" % (u, 1 - v))
            for t in g.tris:
                f.write("f " + " ".join(f"{base + i}/{base + i}" for i in t) + "\n")
            base += g.n_points
    print(f"{outdir}/{label}.obj: {body.bones} bones")


if __name__ == "__main__":
    main(sys.argv[1:])
