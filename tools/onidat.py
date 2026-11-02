#!/usr/bin/env python3
"""Reference reader for Oni's instance files (levelN_Final.dat + .raw), standard library only.

    tools/onidat.py <game folder> [levelN ...]          summary per level and over all
    tools/onidat.py <game folder> level0 --list         every instance: index, tag, name, size, flags
    tools/onidat.py <game folder> level0 --templates    the file's template table
    tools/onidat.py <game folder> --load                 also: links as loading resolves them, each
                                                         level loaded after level0 (cross: resolved by
                                                         name into level0; unresolved: no file has it)

The instance layouts (which fields are links, arrays, raw offsets) are not in the data files: they are
compiled into Oni.exe. This tool reads them from <game folder>/Oni.exe at run time, as src/tm.c does, so
nothing of the exe is in the source. Format: docs/formats/dat.md.

The summary walks every instance of every file by its template's layout and checks that the walk stays
within the instance's size (normally the walk's end padded to 32 bytes; "slack" counts the bigger ones),
and that every link points at an instance of the tag the layout says.
The hash is a CRC-32 over one line per instance ("index tag size links crc08x\n", the CRC over its data
with links replaced by the target's index) and must equal the C one (dat_test).
"""
import struct
import sys
import zlib

EXE_SIZE, EXE_CRC = 1527808, 0xA8223704
TEMPLATE_COUNT_VA = 0x558CF0      # count of the template table (TMrTemplate_Register 0x4200f0 reads it)
TEMPLATE_SETUP_VA = 0x51D1C0      # stores each template's layout pointer, then the table's base at 0x620138
TEMPLATE_TABLE_PTR = 0x620138
TEMPLATE_STRIDE = 0x30
LEVELS = [0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19]

# Layout opcodes (TMiGame_InstanceFileRef_LoadLevel 0x421c50 walks them).
OP_8, OP_4, OP_2, OP_1, OP_ARRAY, OP_ARRAY_END, OP_VARARRAY, OP_VARARRAY_END, OP_LINK, OP_RAW = range(1, 11)


def tag_str(t):
    return struct.pack(">I", t).decode("latin-1")


class Exe:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        if len(self.d) != EXE_SIZE or zlib.crc32(self.d) != EXE_CRC:
            sys.exit(f"{path}: not Oni.exe v1.0 ({EXE_SIZE} bytes, CRC-32 {EXE_CRC:08x})")
        pe = struct.unpack_from("<I", self.d, 0x3C)[0]
        n = struct.unpack_from("<H", self.d, pe + 6)[0]
        opt = struct.unpack_from("<H", self.d, pe + 20)[0]
        self.secs = [struct.unpack_from("<8sIIII", self.d, pe + 24 + opt + i * 40)[1:5] for i in range(n)]

    def off(self, va):
        rva = va - 0x400000
        for vsize, vaddr, rsize, roff in self.secs:
            if vaddr <= rva < vaddr + min(vsize, rsize):
                return roff + rva - vaddr
        raise ValueError(f"{va:#x} outside the exe's sections")

    def u32(self, va):
        return struct.unpack_from("<I", self.d, self.off(va))[0]

    def cstr(self, va):
        o = self.off(va)
        return self.d[o:self.d.index(b"\0", o)].decode("latin-1")


def templates(exe):
    """tag -> (name, layout VA). The layout pointers are the immediates of the mov [mem], imm32
    instructions in 0x51d1c0; the last one stores the table's base."""
    layout_at = {}
    pc = exe.off(TEMPLATE_SETUP_VA)
    while True:
        if exe.d[pc:pc + 2] != b"\xc7\x05":
            sys.exit(f"unexpected instruction at {pc:#x} in the template setup")
        addr, imm = struct.unpack_from("<II", exe.d, pc + 2)
        pc += 10
        if addr == TEMPLATE_TABLE_PTR:
            base = imm
            break
        layout_at[addr] = imm
    out = {}
    for i in range(exe.u32(TEMPLATE_COUNT_VA)):
        e = base + i * TEMPLATE_STRIDE
        tag = exe.u32(e + 8)
        out[tag] = (exe.cstr(exe.u32(e + 0xC)), layout_at.get(e + 0x10), exe.u32(e + 0x14))
    return out


NO_LINKS, NO_ARRAY_LINKS = 2, 4     # template flags (TMrLevel_Load skips these)


class Dat:
    def __init__(self, path):
        d = self.d = open(path, "rb").read()
        if d[8:12] != b"13RV":
            sys.exit(f"{path}: not an instance file")
        sizes = struct.unpack_from("<4H", d, 0xC)
        if sizes != (0x40, 0x14, 0x10, 0x08):
            sys.exit(f"{path}: record sizes {sizes}")
        (self.checksum,) = struct.unpack_from("<Q", d, 0)
        (self.n_inst, self.n_names, self.n_tmpl, self.data_off, self.data_size,
         self.names_off, self.names_size) = struct.unpack_from("<7I", d, 0x14)
        self.inst = []
        for i in range(self.n_inst):
            tag, doff, noff, size, flags = struct.unpack_from("<5I", d, 0x40 + i * 0x14)
            name = None if flags & 1 else d[self.names_off + noff:d.index(b"\0", self.names_off + noff)].decode("latin-1")
            data = None if flags & 2 else self.data_off + doff
            self.inst.append((tag, name, data, size, flags))
        o = 0x40 + self.n_inst * 0x14
        self.names = [struct.unpack_from("<2I", d, o + i * 8) for i in range(self.n_names)]
        o += self.n_names * 8
        self.tmpl = [struct.unpack_from("<QII", d, o + i * 0x10) for i in range(self.n_tmpl)]


class Walker:
    """Walks one instance's data by its layout: counts links, checks their targets, and builds the
    bytes the hash runs over (links replaced by the target's index, -1 for none)."""

    def __init__(self, exe, dat, tmpl):
        self.exe, self.dat, self.tmpl = exe, dat, tmpl

    def walk(self, index):
        tag, _, data, size, _ = self.dat.inst[index]
        layout = self.tmpl[tag][1]
        self.buf = bytearray(self.dat.d[data - 8:data - 8 + size])
        self.links = self.raws = 0
        self.pos = 0
        pc = self.exe.off(layout)
        self.code = self.exe.d
        pc = self.fields(pc, False)
        if self.pos > size:
            raise ValueError(f"{index} {tag_str(tag)}: layout ends at {self.pos:#x}, size {size:#x}")
        self.slack = (self.pos + 31) & ~31 != size   # normally padded to 32 bytes; some are bigger
        return self.buf

    stop_at_array = False
    on_link = None

    def fields(self, pc, in_array):
        code = self.code
        while True:
            op = code[pc]
            pc += 1
            if op in (OP_8, OP_4, OP_2, OP_1):
                self.pos += {OP_8: 8, OP_4: 4, OP_2: 2, OP_1: 1}[op]
            elif op == OP_RAW:
                self.raws += 1
                self.pos += 4
            elif op == OP_ARRAY:
                count = code[pc]
                pc += 1
                start = pc
                for _ in range(count):
                    pc = self.fields(start, True)
                if count == 0:
                    pc = self.skip(start, OP_ARRAY_END)
            elif op in (OP_ARRAY_END, OP_VARARRAY_END):
                return pc
            elif op == OP_VARARRAY:
                if self.stop_at_array:
                    return pc
                kind = code[pc]
                pc += 1
                if kind == 1:
                    count = struct.unpack_from("<I", self.buf, self.pos)[0]; self.pos += 8
                elif kind == 2:
                    count = struct.unpack_from("<I", self.buf, self.pos)[0]; self.pos += 4
                elif kind == 3:
                    count = struct.unpack_from("<H", self.buf, self.pos)[0]; self.pos += 2
                else:
                    raise ValueError(f"variable array kind {kind}")
                start = pc
                for _ in range(count):
                    pc = self.fields(start, True)
                if count == 0:
                    pc = self.skip(start, OP_VARARRAY_END)
            elif op == OP_LINK:
                want = struct.unpack_from("<I", code, pc)[0]
                pc += 4
                self.link(want)
            else:
                raise ValueError(f"layout opcode {op} at {pc - 1:#x}")

    def skip(self, pc, end):
        """Past the elements' layout of an empty array, to just after its end opcode."""
        code, depth = self.code, 0
        while True:
            op = code[pc]
            pc += 1
            if op == OP_LINK:
                pc += 4
            elif op == OP_ARRAY:
                pc += 1; depth += 1
            elif op == OP_VARARRAY:
                pc += 1; depth += 1
            elif op in (OP_ARRAY_END, OP_VARARRAY_END):
                if depth == 0:
                    return pc
                depth -= 1

    def link(self, want):
        v = struct.unpack_from("<I", self.buf, self.pos)[0]
        target = 0xFFFFFFFF
        if v:
            if not v & 1:
                raise ValueError(f"link {v:#x} at {self.pos:#x} isn't an instance id")
            target = v >> 8
            tag = self.dat.inst[target][0]
            if want and tag != want:
                raise ValueError(f"link to {tag_str(tag)}, layout wants {tag_str(want)}")
            self.links += 1
            if self.on_link:
                self.on_link(target)
        struct.pack_into("<I", self.buf, self.pos, target)
        self.pos += 4


def summary(exe, tmpl, path):
    dat = Dat(path)
    w = Walker(exe, dat, tmpl)
    h = links = placeholders = slack = 0
    for i, (tag, name, data, size, flags) in enumerate(dat.inst):
        if data is None:
            placeholders += 1
            line = f"{i} {tag_str(tag)} - {name}\n"
        else:
            buf = w.walk(i)
            buf[0:8] = b"\0" * 8      # the instance id and file id: the loader rewrites them
            links += w.links
            slack += w.slack
            line = f"{i} {tag_str(tag)} {size} {w.links} {zlib.crc32(buf):08x}\n"
        h = zlib.crc32(line.encode("latin-1"), h)
    return dat, links, placeholders, slack, h


def find(dat, name):
    """0x423160: the instance with data of that name in one file (binary search of its name
    directory), or None."""
    lo, hi = 0, dat.n_names
    key = name.encode("latin-1")
    while lo < hi:
        mid = (lo + hi) // 2
        idx = dat.names[mid][0]
        n = dat.inst[idx][1].encode("latin-1")
        if key == n:
            return idx if dat.inst[idx][2] is not None else None
        if key > n:
            lo = mid + 1
        else:
            hi = mid
    return None


def load_stats(exe, tmpl, dats):
    """Links the way TMrLevel_Load resolves them with the files loaded one after the other (each file is
    resolved when it's loaded, against the files loaded so far): (links, links resolved by name in
    another file, named targets no file has)."""
    links = cross = unresolved = 0
    for k, dat in enumerate(dats):
        w = Walker(exe, dat, tmpl)

        def on_link(target):
            nonlocal cross, unresolved
            name = dat.inst[target][1]
            if name is None:
                return
            for j, other in enumerate(dats[:k + 1]):
                if find(other, name) is not None:
                    cross += j != k
                    return
            unresolved += 1
        w.on_link = on_link
        for i, (tag, name, data, size, flags) in enumerate(dat.inst):
            tf = tmpl[tag][2]
            if data is None or tf & NO_LINKS or flags & 4:
                continue
            w.stop_at_array = bool(tf & NO_ARRAY_LINKS)
            w.walk(i)
            links += w.links
    return links, cross, unresolved


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.exit(__doc__.strip())
    folder, rest = argv[0], argv[1:]
    exe = Exe(f"{folder}/Oni.exe")
    tmpl = templates(exe)
    opts = [a for a in rest if a.startswith("--")]
    names = [a for a in rest if not a.startswith("--")] or [f"level{n}" for n in LEVELS]
    total = 0
    for lv in names:
        path = f"{folder}/GameDataFolder/{lv}_Final.dat"
        if "--templates" in opts:
            dat = Dat(path)
            for checksum, tag, count in dat.tmpl:
                print(f"{tag_str(tag)} {count:6d} {checksum:016x} {tmpl.get(tag, ('?',))[0]}")
            continue
        if "--list" in opts:
            dat = Dat(path)
            for i, (tag, name, data, size, flags) in enumerate(dat.inst):
                print(f"{i:6d} {tag_str(tag)} {size:8d} {flags:08x} {name or ''}")
            continue
        dat, links, ph, slack, h = summary(exe, tmpl, path)
        print(f"{lv}: instances={dat.n_inst} placeholders={ph} links={links} slack={slack} hash={h:08x}")
        if "--load" in opts:
            files = [dat] if lv == "level0" else [Dat(f"{folder}/GameDataFolder/level0_Final.dat"), dat]
            ln, cross, unres = load_stats(exe, tmpl, files)
            print(f"{lv} loaded after level0: links={ln} cross={cross} unresolved={unres}")
        total = zlib.crc32(f"{lv} {h:08x}\n".encode(), total)
    if not any(o in opts for o in ("--list", "--templates")):
        print(f"all: hash={total:08x}")


if __name__ == "__main__":
    main(sys.argv[1:])
