#!/usr/bin/env python3
"""Reference reader for MindVision Installer VISE archives (docs/formats/vise.md), stdlib only.

    tools/vise.py --summary <cd>          file count, bytes, hash (the same line as openoni-extract --summary)
    tools/vise.py --list <cd>             the items
    tools/vise.py <cd> <member> [out]     one file (checked against its CRC-32), to out or out/<name>

<cd> is a folder with ONISETUP.EXE and ONISETUP.002, or the ISO image. Meant for checking the C reader
(src/vise.c) and for looking at single files; openoni-extract makes the game folder.
"""
import os
import struct
import sys
import zlib

MAGIC = 0x56495345  # "ESIV"
ANCHOR = b"\xff" * 32
HDR_TO_ANCHOR = 0x46

# ---- input: a folder or an ISO 9660 image (root directory only: the setup files are there) ----


class Cd:
    def __init__(self, path):
        self.path = path
        self.iso = None
        if os.path.isfile(path):
            self.iso = open(path, "rb")
            self.iso.seek(16 * 2048)
            pvd = self.iso.read(2048)
            if pvd[1:6] != b"CD001":
                raise SystemExit(f"{path}: not an ISO 9660 image")
            root = pvd[156:190]
            self.root = (struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0])

    def _iso_find(self, name):
        lba, size = self.root
        self.iso.seek(lba * 2048)
        d = self.iso.read(size)
        p = 0
        while p < len(d):
            n = d[p]
            if n == 0:
                p = (p // 2048 + 1) * 2048
                continue
            fid = d[p + 33:p + 33 + d[p + 32]].decode("latin-1").split(";")[0]
            if fid.upper() == name.upper():
                return struct.unpack_from("<I", d, p + 2)[0] * 2048, struct.unpack_from("<I", d, p + 10)[0]
            p += n
        raise SystemExit(f"{self.path}: {name} missing")

    def read(self, name, off=0, size=None):
        if self.iso:
            base, total = self._iso_find(name)
            size = total - off if size is None else size
            self.iso.seek(base + off)
            return self.iso.read(size)
        for f in os.listdir(self.path):
            if f.upper() == name.upper():
                with open(os.path.join(self.path, f), "rb") as fp:
                    fp.seek(off)
                    return fp.read() if size is None else fp.read(size)
        raise SystemExit(f"{self.path}: {name} missing")


# ---- compression: plain deflate after swapping the bytes of every 16-bit word ----


def inflate_vise(src):
    b = bytearray(src) + (b"\0" if len(src) & 1 else b"")
    b[0::2], b[1::2] = b[1::2], b[0::2]
    return zlib.decompressobj(-15).decompress(bytes(b))


# ---- the archive ----


def items(cd, setup="ONISETUP.EXE"):
    d = cd.read(setup)
    if struct.unpack_from("<I", d, len(d) - 8)[0] != MAGIC:
        raise SystemExit(f"{setup}: not an Installer VISE setup program")
    seg = struct.unpack_from("<I", d, len(d) - 4)[0]
    magic, _, segno, seglen = struct.unpack_from("<4I", d, seg)
    assert magic == MAGIC and segno == 1
    hi = min(seg + seglen, len(d))
    p = seg + 16  # three strings, then the setup program's own support files
    p += 1 + d[p]
    p += 1 + (d[p] & 0x3F)
    p += 1 + d[p]
    nsup = struct.unpack_from("<H", d, p)[0]
    p += 2
    for _ in range(nsup):
        p += 1 + d[p] + 12
        p += 4 + struct.unpack_from("<I", d, p)[0]
    paths = {0: ""}
    p = d.find(ANCHOR, p + HDR_TO_ANCHOR)
    while 0 <= p < hi:
        q = p + 32
        while q < hi and d[q] == 0xFF:
            q += 1
        n = struct.unpack_from("<H", d, q + 1)[0] if q + 3 <= hi else 0
        name = d[q + 3:q + 3 + n]
        if q - p == 32 and d[q] == 0 and 0 < n < 256 and all(0x20 <= c < 0x7F for c in name):
            kind, ident, parent = struct.unpack_from("<3I", d, p - HDR_TO_ANCHOR)
            if kind in (1, 2):
                path = (paths[parent] + "/" if paths[parent] else "") + name.decode("latin-1")
                if kind == 1:
                    paths[ident] = path
                    yield {"path": path, "dir": True}
                else:
                    b = q + 3 + n
                    size, csize, segment, off = struct.unpack_from("<4I", d, b + 4)
                    yield {"path": path, "dir": False, "size": size, "csize": csize, "segment": segment,
                           "offset": off, "crc": struct.unpack_from("<I", d, b + 0x49)[0]}
            p = q + 3 + n
        else:
            p = q
        p = d.find(ANCHOR, p)


def extract(cd, it, setup="ONISETUP.EXE"):
    seg = setup.rsplit(".", 1)[0] + ".%03d" % it["segment"]
    data = inflate_vise(cd.read(seg, it["offset"], it["csize"])) if it["size"] else b""
    if len(data) != it["size"] or zlib.crc32(data) != it["crc"]:
        raise SystemExit(f"{it['path']}: damaged (size or CRC mismatch)")
    return data


def main(argv):
    if len(argv) == 2 and argv[0] in ("--summary", "--list"):
        cd = Cd(argv[1])
        if argv[0] == "--list":
            for it in items(cd):
                if it["dir"]:
                    print(f"{'':10s}  {'':10s}  {'':8s}  {it['path']}/")
                else:
                    print(f"{it['size']:10d}  {it['csize']:10d}  {it['crc']:08x}  {it['path']}")
            return 0
        h = total = files = 0
        for it in items(cd):
            if not it["dir"]:
                h = zlib.crc32(f"{it['path']} {it['size']} {it['crc']:08x}\n".encode("latin-1"), h)
                total += it["size"]
                files += 1
        print(f"files={files} bytes={total} hash={h:08x}")
        return 0
    if len(argv) in (2, 3) and not argv[0].startswith("-"):
        cd = Cd(argv[0])
        for it in items(cd):
            if not it["dir"] and it["path"].lower() == argv[1].lower():
                data = extract(cd, it)
                out = argv[2] if len(argv) == 3 else os.path.basename(it["path"])
                if os.path.isdir(out):
                    out = os.path.join(out, os.path.basename(it["path"]))
                with open(out, "wb") as f:
                    f.write(data)
                print(f"{it['path']}: {len(data)} bytes -> {out}")
                return 0
        raise SystemExit(f"{argv[1]}: not in the archive")
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
