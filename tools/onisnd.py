#!/usr/bin/env python3
"""Reference decoder for Oni's sounds (SNDD instances), standard library only.

    tools/onisnd.py <game folder> [levelN ...] [--summary]    summary per level and over all
    tools/onisnd.py <game folder> level0 --list               every sound: index, format, length, name
    tools/onisnd.py <game folder> [levelN] --wav NAME OUT.wav  one sound decoded to a 16-bit WAVE file

A sound is a WAVEFORMATEX and sample data in the .raw. Every one on the CD is Microsoft ADPCM (format
tag 2), which Oni.exe hands to the Windows ACM; this decodes it from the published format, blocks of a
header per channel (predictor, step, two samples) and 4-bit deltas. The last block of a sound is short
and is decoded too. Format: docs/formats/sndd.md.

The hash is a CRC-32 over one line per sound ("index name flags format channels rate frames ticks
crc08x\\n", the CRC over its decoded samples as s16 little-endian, channels interleaved) and per level
("levelN hash\\n"); sndd_test prints the same.
"""
import os
import struct
import sys
import zlib

import onidat

SNDD = 0x534E4444
# SNDD fields, from the instance data (past the 8-byte header).
F_FLAGS, F_FORMAT, F_DURATION, F_SIZE, F_DATA = 0x00, 0x04, 0x36, 0x38, 0x3C
FLAG_PCM, FLAG_IMA, FLAG_MSADPCM = 0x3, 0x4, 0x8
HEADER = 7          # ADPCM block header bytes per channel
# The step adaptation per nibble, from the MS ADPCM specification (the exe leaves decoding to the ACM).
ADAPT = (230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230)


class Sound:
    def __init__(self, dat, index):
        d, b = dat.d, dat.inst[index][2]
        self.index, self.name = index, dat.inst[index][1]
        (self.flags,) = struct.unpack_from("<I", d, b + F_FLAGS)
        (self.format, self.channels, self.rate, self.avg, self.block_align, self.bits,
         self.extra) = struct.unpack_from("<HHIIHHH", d, b + F_FORMAT)
        self.spb, self.ncoef = struct.unpack_from("<HH", d, b + 0x16)
        self.coefs = struct.unpack_from(f"<{2 * min(self.ncoef, 7)}h", d, b + 0x1A)
        self.ticks, self.size, self.data = struct.unpack_from("<HII", d, b + F_DURATION)
        self.frames = self.count()
        if not self.ticks:      # SSrSoundData_GetDuration 0x47f790
            self.ticks = max(1, self.frames * 60 // self.rate) if self.rate else 1

    def block_frames(self, n):
        if n < HEADER * self.channels:
            return 0
        return min(2 + (n - HEADER * self.channels) * 2 // self.channels, self.spb)

    def count(self):
        if self.flags & FLAG_PCM:
            return self.size // (self.channels * (self.bits // 8)) if self.channels and self.bits in (8, 16) else 0
        if self.flags & FLAG_MSADPCM and self.block_align and self.channels in (1, 2):
            full, rest = divmod(self.size, self.block_align)
            return full * self.block_frames(self.block_align) + self.block_frames(rest)
        return 0

    def pcm(self, raw):
        """The samples as s16 little-endian bytes, channels interleaved."""
        d = raw[self.data:self.data + self.size]
        if self.flags & FLAG_PCM:
            if self.bits == 8:
                return struct.pack(f"<{len(d)}h", *((v - 128) << 8 for v in d))
            return d[:self.frames * self.channels * 2]
        if not self.flags & FLAG_MSADPCM:
            raise ValueError(f"{self.name}: flags {self.flags:#x} not decoded")
        out = []
        for at in range(0, self.size, self.block_align):
            blk = d[at:at + self.block_align]
            n = self.block_frames(len(blk))
            if n:
                out.extend(decode_block(blk, self.channels, self.coefs, n))
        return struct.pack(f"<{len(out)}h", *out)


def decode_block(b, ch, coefs, frames):
    """One MS ADPCM block to frames * ch samples. The header's fields are interleaved by channel:
    predictor (u8), step, then the second and the first sample (s16): those two come out first."""
    pred = b[:ch]
    step = list(struct.unpack_from(f"<{ch}h", b, ch))
    s1 = list(struct.unpack_from(f"<{ch}h", b, 3 * ch))
    s2 = list(struct.unpack_from(f"<{ch}h", b, 5 * ch))
    if max(pred) * 2 >= len(coefs):
        raise ValueError("bad predictor")
    c1 = [coefs[2 * p] for p in pred]
    c2 = [coefs[2 * p + 1] for p in pred]
    out = s2[:] + s1[:]
    out = out[:frames * ch]
    need = frames * ch - len(out)
    c = 0
    for byte in b[HEADER * ch:]:
        for nib in (byte >> 4, byte & 15):
            if need <= 0:
                return out
            p = (s1[c] * c1[c] + s2[c] * c2[c]) >> 8       # arithmetic shift: floor
            v = p + (nib - 16 if nib >= 8 else nib) * step[c]
            v = 32767 if v > 32767 else -32768 if v < -32768 else v
            out.append(v)
            s2[c], s1[c] = s1[c], v
            st = (step[c] * ADAPT[nib]) >> 8
            step[c] = st if st >= 16 else 16
            need -= 1
            c = c + 1 if c + 1 < ch else 0
    return out


def sounds(dat):
    for i, x in enumerate(dat.inst):
        if x[0] == SNDD and x[2] is not None:
            yield Sound(dat, i)


def write_wav(path, s, pcm):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, s.channels, s.rate, s.rate * s.channels * 2, s.channels * 2, 16))
        f.write(b"data" + struct.pack("<I", len(pcm)) + pcm)


def _crc_job(args):
    path, items = args
    with open(path, "rb") as f:
        raw = f.read()
    return [(k, zlib.crc32(s.pcm(raw))) for k, s in items]


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.exit(__doc__.strip())
    folder, rest = argv[0], argv[1:]
    names = [a for a in rest if a.startswith("level")] or [f"level{n}" for n in onidat.LEVELS]
    if "--wav" in rest:
        k = rest.index("--wav")
        name, out = rest[k + 1], rest[k + 2]
        name = name[4:] if name.startswith("SNDD") else name
        for lv in names:
            dat = onidat.Dat(f"{folder}/GameDataFolder/{lv}_Final.dat")
            i = onidat.find(dat, "SNDD" + name)
            if i is None:
                continue
            s = Sound(dat, i)
            pcm = s.pcm(open(f"{folder}/GameDataFolder/{lv}_Final.raw", "rb").read())
            write_wav(out, s, pcm)
            print(f"{lv} {s.name}: {s.channels} ch {s.rate} Hz {s.frames} frames, {s.ticks} ticks -> {out}")
            return
        sys.exit(f"no sound {name}")
    if "--list" in rest:
        for lv in names:
            dat = onidat.Dat(f"{folder}/GameDataFolder/{lv}_Final.dat")
            for s in sounds(dat):
                print(f"{s.index:6d} fmt {s.format} {s.channels}ch {s.rate:5d} Hz {s.frames:8d} frames "
                      f"{s.ticks:5d} ticks {s.size:8d} bytes {s.name}")
        return
    # The summary. Many sounds are in several levels: each distinct block of data is decoded once (in
    # parallel), keyed by its bytes' CRC and its format.
    levels = []
    jobs = {}
    for lv in names:
        dat = onidat.Dat(f"{folder}/GameDataFolder/{lv}_Final.dat")
        path = f"{folder}/GameDataFolder/{lv}_Final.raw"
        with open(path, "rb") as f:
            raw = f.read()
        ss = list(sounds(dat))
        keys = []
        for s in ss:
            key = (zlib.crc32(raw[s.data:s.data + s.size]), s.size, s.flags, s.channels, s.block_align, s.coefs)
            keys.append(key)
            if key not in jobs:
                jobs[key] = (path, s)
        levels.append((lv, ss, keys))
        del raw
    by_path = {}
    for key, (path, s) in jobs.items():
        by_path.setdefault(path, []).append((key, s))
    work = []
    for path, items in by_path.items():
        for k in range(0, len(items), 64):
            work.append((path, items[k:k + 64]))
    crcs = {}
    try:
        from concurrent.futures import ProcessPoolExecutor
        with ProcessPoolExecutor() as ex:
            for res in ex.map(_crc_job, work):
                crcs.update(res)
    except (ImportError, OSError, NotImplementedError):
        for w in work:
            crcs.update(_crc_job(w))
    total = 0
    for lv, ss, keys in levels:
        h, frames = 0, 0
        for s, key in zip(ss, keys):
            line = (f"{s.index} {s.name or '-'} {s.flags} {s.format} {s.channels} {s.rate} {s.frames} "
                    f"{s.ticks} {crcs[key]:08x}\n")
            h = zlib.crc32(line.encode("latin-1"), h)
            frames += s.frames
        print(f"{lv}: sounds={len(ss)} frames={frames} hash={h:08x}")
        total = zlib.crc32(f"{lv} {h:08x}\n".encode(), total)
    print(f"all: hash={total:08x}")


if __name__ == "__main__":
    main(sys.argv[1:])
