# Credits and licences

**OpenOni** is released under the [GNU General Public License v3.0](https://github.com/emdzej/openoni/blob/main/LICENSE).
It contains no code, art, sound or data from the original game: everything is read from your own copy at run
time.

**Oni** is © 2001 Bungie West and Gathering of Developers (now Take-Two Interactive). OpenOni is not
affiliated with them.

## Published and community sources

- **[OniGalore](https://wiki.oni2.net)**, the Oni community's wiki — background for the instance formats;
  its ONWC offset table is cited in [the weapon format](/formats/onwc). The executable is the authority and
  no code is copied.
- **OniSplit** and the **Daodan DLL**, the community's tools and patches — named as allowed background for
  understanding the formats; no code is copied.
- **Feral's Intel Mac build of Oni 1.2.1** (2015) — about 6,244 of Bungie's own function names. 1,055
  matched names were transferred to the Windows executable after a check. It is a name map, not the
  authority: it is a later version with Feral's changes.
- **MindVision Installer VISE** — the CD's installer archive, reverse engineered so the game can be read
  straight from the CD ([the archive format](/formats/vise)).
- **Takuya Ooura's FFT package** — the definition (`rdft`) the Bink audio's inverse transform follows
  ([Bink](/formats/bink)).

## Reference implementations

- [FFmpeg](https://ffmpeg.org)'s MS ADPCM decoder — compared against; its rounding differs from the
  executable's, so it is not followed ([the sound format](/formats/sndd)).
- macOS `afconvert` — the decoded MS ADPCM matches it on every full block.
- zlib's CRC-32 definition — the checks of `Oni.exe`, `binkw32.dll` and the data.

## Tools

[Ghidra](https://ghidra-sre.org) (headless analysis, Version Tracking), [ghidra-cli](https://github.com/akiselev/ghidra-cli),
[wasi-sdk](https://github.com/WebAssembly/wasi-sdk), [Binaryen](https://github.com/WebAssembly/binaryen),
[VitePress](https://vitepress.dev). Fonts: [Chakra Petch](https://fonts.google.com/specimen/Chakra+Petch) and
[Inter](https://rsms.me/inter/).

## Runtime

[gasm](https://gasm.emdzej.pl) (MIT): `gasm-run`, the C SDK, and on this site `@emdzej/gasm-host`. OpenOni is part of
[ENCORE](https://encore.emdzej.pl), with its sibling reimplementations.

If we have missed someone whose work this project relied on, please
[open an issue](https://github.com/emdzej/openoni/issues) — gaps in these credits are bugs.
