#!/usr/bin/env python3
"""Reference decoder for Oni's textures (TXMP instances), standard library only.

    tools/onitex.py <game folder> [levelN ...]              summary per level and over all
    tools/onitex.py <game folder> level0 --list             every texture: index, size, type, flags, name
    tools/onitex.py <game folder> level0 --png out/tex [NAME ...]   write PNGs (all, or the named ones)

Decodes every mip level of every texture to RGBA bytes the way Oni.exe converts to its RGBA byte type
(IMrImage_ConvertPixelType 0x44ac20 and its converters), quirks included: RGB555 is widened by a shift
only, DXT1 end points too, and DXT1's 3-colour mode gives opaque black, not transparency. The bytes per
pixel come from the exe's pixel type table, read at run time. Format: docs/formats/txmp.md.

The hash is a CRC-32 over one line per texture ("index name w h type flags crc08x\\n", name "-" if
unnamed, the CRC over all
its levels' RGBA bytes) and per level ("levelN hash\\n"); txmp_test prints the same.
"""
import struct
import sys
import zlib

import onidat

TXMP = 0x54584D50
PIXEL_TYPES_VA = 0x529E78      # IMrImage_ComputeSize 0x4478d0 reads byte +5 (bytes per pixel)
PIXEL_TYPE_STRIDE = 0x18
DXT1 = 9

# TXMP fields, from the instance data (past the 8-byte header); M3rTextureMap_New 0x41eb00 fills them.
F_FLAGS, F_WIDTH, F_HEIGHT, F_TYPE, F_RAW = 0x80, 0x84, 0x86, 0x88, 0x94
FLAG_MIPMAPS = 0x1


def levels(w, h, mipmaps):
    """The mip chain: halve both, clamp to 1, until both were 1 (IMrImage_ComputeSize)."""
    out = []
    while w or h:
        out.append((w, h))
        if not mipmaps:
            break
        w, h = w >> 1, h >> 1
        if w or h:
            w, h = max(w, 1), max(h, 1)
    return out


def level_size(bpp, ty, w, h):
    """IMrImage_ComputeSize 0x4478d0 for one level."""
    if not w or not h:
        return 0
    if bpp == 0:
        if ty == 4:
            return ((w + 15) >> 3) * h
        if ty == DXT1:
            return ((w + 3) & ~3) * ((h + 3) & ~3) * 2 >> 2
        return 0
    if bpp == 1:
        return ((w + 1) & ~1) * h
    return w * h * bpp


def rgba_16(ty, src, n):
    """0x44b020 (ARGB4444), 0x44afa0 (RGB555), 0x44b0b0 (ARGB1555): n pixels to RGBA bytes."""
    out = bytearray(n * 4)
    px = struct.unpack_from(f"<{n}H", src)
    for i, p in enumerate(px):
        o = i * 4
        if ty == 0:
            a, r, g, b = p >> 12, (p >> 8) & 15, (p >> 4) & 15, p & 15
            out[o:o + 4] = bytes((r * 17, g * 17, b * 17, a * 17))
        elif ty == 1:
            out[o:o + 4] = bytes(((p >> 7) & 0xF8, (p >> 2) & 0xF8, (p << 3) & 0xF8, 0xFF))
        else:
            r, g, b = (p >> 10) & 31, (p >> 5) & 31, p & 31
            out[o:o + 4] = bytes(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 0xFF if p & 0x8000 else 0))
    return out


def rgba_32(src, n):
    """0x44b250 (xRGB8888 to RGB bytes), alpha 0xff."""
    out = bytearray(n * 4)
    for i in range(n):
        b, g, r, _ = src[i * 4:i * 4 + 4]
        out[i * 4:i * 4 + 4] = bytes((r, g, b, 0xFF))
    return out


def dxt1_palette(c0, c1):
    """DecodeDXT1Block_To_RGBABytes 0x44a8a0: the block's four colours."""
    r0, g0, b0 = c0 >> 11, (c0 >> 5) & 63, c0 & 31
    r1, g1, b1 = c1 >> 11, (c1 >> 5) & 63, c1 & 31
    p = [(r0 << 3, g0 << 2, b0 << 3, 255), (r1 << 3, g1 << 2, b1 << 3, 255)]
    if c0 > c1:
        p.append((((r1 + 1 + 2 * r0) // 3) << 3, ((g1 + 2 * g0) // 3) << 2, ((b1 + 1 + 2 * b0) // 3) << 3, 255))
        p.append((((r0 + 1 + 2 * r1) // 3) << 3, ((g0 + 2 * g1) // 3) << 2, ((b0 + 1 + 2 * b1) // 3) << 3, 255))
    else:
        p.append((((r1 + 1 + r0) >> 1) << 3, ((g1 + g0) >> 1) << 2, ((b1 + 1 + b0) >> 1) << 3, 255))
        p.append((0, 0, 0, 255))
    return [bytes(c) for c in p]


def rgba_dxt1(src, w, h):
    """IMiConvert_DXT1_to_NByte 0x449650: 4x4 blocks left to right, top to bottom; blocks at the right
    and bottom edges of a level smaller than a block are cut."""
    out = bytearray(w * h * 4)
    bw, bh = (w + 3) >> 2, (h + 3) >> 2
    k = 0
    for by in range(bh):
        for bx in range(bw):
            c0, c1, bits = struct.unpack_from("<HHI", src, k)
            k += 8
            pal = dxt1_palette(c0, c1)
            for y in range(4):
                yy = by * 4 + y
                if yy >= h:
                    break
                for x in range(4):
                    xx = bx * 4 + x
                    if xx >= w:
                        break
                    o = (yy * w + xx) * 4
                    out[o:o + 4] = pal[(bits >> (2 * (y * 4 + x))) & 3]
    return out


class Decoder:
    def __init__(self, exe):
        self.bpp = {t: exe.d[exe.off(PIXEL_TYPES_VA + t * PIXEL_TYPE_STRIDE) + 5] for t in range(15)}
        self.cache = {}

    def size(self, ty, w, h, mipmaps):
        return sum(level_size(self.bpp[ty], ty, lw, lh) for lw, lh in levels(w, h, mipmaps))

    def decode(self, ty, w, h, mipmaps, src):
        """[(w, h, RGBA bytes)] per level."""
        key = (ty, w, h, mipmaps, zlib.crc32(src), len(src))
        if key in self.cache:
            return self.cache[key]
        out, k = [], 0
        for lw, lh in levels(w, h, mipmaps):
            n = level_size(self.bpp[ty], ty, lw, lh)
            part = src[k:k + n]
            k += n
            if ty == DXT1:
                px = rgba_dxt1(part, lw, lh)
            elif ty in (0, 1, 2):
                px = rgba_16(ty, part, lw * lh)
            elif ty == 8:
                px = rgba_32(part, lw * lh)
            else:
                raise ValueError(f"texel type {ty}")
            out.append((lw, lh, px))
        self.cache[key] = out
        return out


def textures(dat):
    for i, (tag, name, data, size, flags) in enumerate(dat.inst):
        if tag != TXMP or data is None:
            continue
        f, w, h, ty = struct.unpack_from("<IHHI", dat.d, data + F_FLAGS)
        (raw,) = struct.unpack_from("<I", dat.d, data + F_RAW)
        yield i, name, f, w, h, ty, raw


def png(path, w, h, rgba):
    rows = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.exit(__doc__.strip())
    folder, rest = argv[0], argv[1:]
    exe = onidat.Exe(f"{folder}/Oni.exe")
    dec = Decoder(exe)
    names = [a for a in rest if a.startswith("level")] or [f"level{n}" for n in onidat.LEVELS]
    opts = [a for a in rest if a.startswith("--")]
    total = 0
    for lv in names:
        dat = onidat.Dat(f"{folder}/GameDataFolder/{lv}_Final.dat")
        raw = open(f"{folder}/GameDataFolder/{lv}_Final.raw", "rb").read()
        if "--list" in opts:
            for i, name, f, w, h, ty, off in textures(dat):
                print(f"{i:6d} {w:4d}x{h:<4d} type {ty} flags {f:08x} raw {off:08x} {name}")
            continue
        if "--png" in opts:
            k = rest.index("--png")
            outdir, want = rest[k + 1], set(rest[k + 2:])
            import os
            os.makedirs(outdir, exist_ok=True)
            for i, name, f, w, h, ty, off in textures(dat):
                name = name or f"TXMP{lv}_{i}"      # unnamed textures
                if want and name not in want and name[4:] not in want:
                    continue
                n = dec.size(ty, w, h, f & FLAG_MIPMAPS)
                lw, lh, px = dec.decode(ty, w, h, f & FLAG_MIPMAPS, raw[off:off + n])[0]
                png(f"{outdir}/{name.replace('/', '_')}.png", lw, lh, px)   # some names have a /
            continue
        h_lv = count = 0
        for i, name, f, w, h, ty, off in textures(dat):
            n = dec.size(ty, w, h, f & FLAG_MIPMAPS)
            if off + n > len(raw):
                raise ValueError(f"{lv} {name}: pixels run past the .raw")
            crc = 0
            for lw, lh, px in dec.decode(ty, w, h, f & FLAG_MIPMAPS, raw[off:off + n]):
                crc = zlib.crc32(px, crc)
            line = f"{i} {name or '-'} {w} {h} {ty} {f:x} {crc:08x}\n"
            h_lv = zlib.crc32(line.encode("latin-1"), h_lv)
            count += 1
        print(f"{lv}: textures={count} hash={h_lv:08x}")
        total = zlib.crc32(f"{lv} {h_lv:08x}\n".encode(), total)
    if not opts:
        print(f"all: hash={total:08x}")


if __name__ == "__main__":
    main(sys.argv[1:])
