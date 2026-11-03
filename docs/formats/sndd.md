# Sounds (SNDD)

A sound's samples are an `SNDD` instance ([instance files](./dat)): a `WAVEFORMATEX` and the sample
data, which is in the level's `.raw`. Sound groups, impulses and ambients (`OSBD`, the `SBDO` binary
data) refer to them by name and are a later concern.

OpenOni reads sounds with `src/sndd.c`; `tools/onisnd.py` is the reference decoder. Worked out from
`Oni.exe` v1.0 and checked on all 7,386 sounds of the CD (2,380 names; many sounds are in several
levels).

## The instance

The template's data is 0x40 bytes (`SS2rRegisterTemplates` 0x487080 registers `SNDD` with that size).
Offsets are from the instance data, past the 8-byte header.

| Offset | What | |
|---|---|---|
| 0x00 | Flags: how the samples are stored (below) | confirmed |
| 0x04 | `WAVEFORMATEX`: format tag, channels, rate, average bytes per second, block align, bits per sample, extra size | confirmed |
| 0x16 | The ADPCM extension: samples per block, the coefficient count | confirmed (the standard layout) |
| 0x1a | The coefficient pairs, s16 each, room for 7 | confirmed |
| 0x36 | Duration in ticks (1/60 s), u16. 0 means "compute it" | confirmed |
| 0x38 | Bytes of sample data | confirmed |
| 0x3c | `.raw` offset of the data | confirmed |

`SSiSoundData_ProcHandler` 0x4834e0 is the template's callback (registered by `SS2rInitializeFull`
0x483310). On a load it adds the `.raw`'s base (`TMrInstance_GetRawOffset` 0x423e10) to 0x3c. On another
message it adds the instance to the sound system's array at 0x5eb710.

The 0x04..0x35 block is passed to the ACM as the source format, as it is: it is a complete
`ADPCMWAVEFORMAT` (18 bytes of `WAVEFORMATEX`, cbSize 32, then the extension).

### Flags

`SSrSoundData_GetDuration` 0x47f790 and the channel code test them in this order:

| Bits | Storage | Samples (0x47f790) | On the CD |
|---|---|---|---|
| 0x3 | Uncompressed PCM | size / (bits / 8) | none |
| 0x4 | IMA4 packets | the u16 at 0x10 times 64 (0x4841d0) | none |
| 0x8 | MS ADPCM, decoded by the ACM | `SSrMSADPCM_CalculateNumSamples` 0x4841f0 | all |

What tells bits 0 and 1 apart is inferred to be the byte order (the Mac build reads AIFF); the port
decodes either as little-endian PCM, which no CD sound uses. IMA4 is the Mac's format. The Windows exe seems to
have a decoder for it (inferred: 0x484e90 calls 0x483e60 for up to 500 packets at a time, 64 s16
samples per packet per channel out). It isn't ported.

### Duration

`SSrSoundData_GetDuration` 0x47f790 returns the u16 at 0x36 if it isn't 0. Otherwise it computes:

- the sample count of all channels, by the flags (above)
- times 60, over channels × rate
- at least 1

For ADPCM the count comes from the ACM: `acmFormatSuggest` for a PCM format, then `acmStreamOpen`, then
`acmStreamSize` of the data's size, divided by the PCM's bytes per sample. On the CD the stored field is
never 0. It always equals the decoded frames × 60 / rate, counting the short last block (see below).

## The formats on the CD

All 7,386 sounds have flags 8 and the standard seven MS ADPCM coefficient pairs:

| Channels | Rate | Block | Samples per block | Sounds |
|---|---|---|---|---|
| 1 | 22,050 | 512 | 1012 | 6,574 |
| 2 | 22,050 | 1024 | 1012 | 686 |
| 1 | 44,100 | 1024 | 2036 | 126 |

## Decoding

On Windows the exe doesn't decode ADPCM itself. When a channel starts a sound (0x484b70) it opens an ACM
stream from the sound's format to the suggested PCM format (`acmStreamOpen`, non-real-time). The fill
(0x484ef0) converts a buffer's worth of source bytes at a time with `acmStreamConvert`, from the pointer
at 0x3c. The uncompressed fill (0x4851f0) copies the bytes. Both pad the end of a buffer with silence:
0x80 for 8-bit data, 0 otherwise.

So the decoder is the Windows codec. The port follows the published MS ADPCM format:

- A block is `block align` bytes. It starts with a header per channel, each field interleaved by
  channel:
  - the predictor index, u8
  - the step, s16
  - the newer sample, s16
  - the older sample, s16
- The two header samples come out first, older first.
- Then each byte holds two 4-bit signed deltas, the high nibble first. In stereo the high one is left,
  the low one right.
- For each delta:
  - The prediction is newer × coef1 + older × coef2, shifted right 8 (the coefficients are 8.8 fixed
    point).
  - The sample is the prediction + delta × step, clamped to s16.
  - The step becomes step × adapt[nibble] >> 8, at least 16. The adaptation table (230, 230, 230,
    230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230) is the format's.
- A full block gives `samples per block` frames: 2 + (block − 7 × channels) × 2 / channels.
- **The last block is short** on the CD. It gives 2 + (bytes − 7 × channels) × 2 / channels frames
  (none if it doesn't hold the headers).

What is inferred:

- **Rounding.** The shift rounds toward −∞. A division by 256 (as ffmpeg's decoder does) rounds toward
  0, and the samples drift apart by up to a few tens. The port's output equals macOS's own decoder
  (`afconvert`) on every full block, for mono, stereo and 44.1 kHz. The Windows codec itself wasn't run.
- **The short last block.** It is decoded since the stored durations count its samples, for all 7,386
  sounds. `afconvert` drops it.

## Check

`tools/onisnd.py <game>` decodes every sound. It hashes one line per sound and one per level. Each
sound's line has:

- its index and name
- the flags, format tag, channels, rate, frames and duration in ticks
- a CRC of the decoded samples (s16 little-endian, channels interleaved)

`sndd_test` must print the same: `all: hash=97eb43b7`. It also checks that every stored duration agrees
with the decoded frames.

`onisnd.py <game> level0 --wav NAME out/x.wav` writes one sound as a 16-bit WAVE to listen to, and
`--list` lists them.
