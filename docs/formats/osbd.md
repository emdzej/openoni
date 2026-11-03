# Sound groups, ambients and impulses (OSBD)

What the game plays is described in three kinds of sound object, all in level 0's `.raw`:

- **Groups** (`.grp`): a list of permutations, each a sound ([SNDD](./sndd)) with a weight and a volume
  and pitch range. Playing a group picks one permutation at random.
- **Ambients** (`.amb`): up to five groups that make a longer sound: an in sound, two base tracks that
  loop, an out sound, and details played at random times around it. Music and dialog are ambients.
- **Impulses** (`.imp`): a group played once at a position, with its distances and an alternate impulse
  for when too many of them play.

Each one is an `OSBD` instance ([instance files](./dat)) that points at a chunk of the `.raw`. The
instance's name gives the object's: `OSBDmus_chase.amb` is the ambient `mus_chase`. The 2,718 instances
are all in level 0: 1,415 groups, 1,005 ambients, 298 impulses.

OpenOni reads them with `src/sound.c`, which also plays them (below). Worked out from `Oni.exe` v1.0:
`OSiBinaryData_ProcHandler` 0x5147b0 and the three class loaders it calls. Field roles are confirmed
where the playing code uses them; the rest is marked.

## The instance

| Offset | What |
|---|---|
| 0x00 | Size of the chunk |
| 0x04 | Its offset in the `.raw` |

`OSBD`'s callback (0x5147b0, registered by `OSiScriptCommands_Initialize` 0x512cb0 through 0x423e70) adds
the `.raw`'s base to 0x04 when the instance loads. It then takes the instance's name without the tag,
cuts it at the extension (0x426690), and calls the loader the chunk's class asks for. The loader
replaces spaces by `_` and keeps at most 31 characters. An object of a name that exists already is
reloaded in place.

## The chunk

All values are little-endian. The class is four characters, stored reversed as in other Bungie binary
data.

| Offset | What |
|---|---|
| 0x00 | Class: `mASO` (`OSAm`, ambient), `rGSO` (`OSGr`, group), `mISO` (`OSIm`, impulse) |
| 0x04 | Size of what follows |
| 0x08 | Version |
| 0x0c | The class's data |

### Group (`OSGr`, 0x513950)

Versions on the CD: 4 and 6. The fields after the version depend on it:

| Present from | Field | |
|---|---|---|
| version 2 | Volume, float (else 1) | confirmed: multiplies the channel's volume |
| version 3 | Pitch, float (else 1) | confirmed: multiplies the permutation's |
| version 6 | Flags, u16; bit 0: don't repeat the last permutation | confirmed |
| version 6 | Last permutation played, u16 | confirmed (the loader keeps the stored value) |
| all | Channels, u32: 1 mono, 2 stereo | confirmed: which channels an ambient takes for it |
| all | Permutation count, u32 | confirmed |
| all | The permutations | |

Before version 6 there is no flags field, and a group of more than three permutations gets the
don't-repeat flag.

A permutation is 0x34 bytes:

| Offset | What |
|---|---|
| 0x00 | Weight, u32 |
| 0x04 | Minimum and maximum volume, floats |
| 0x0c | Minimum and maximum pitch, floats |
| 0x14 | The sound's name, 32 bytes: the `SNDD` instance's name (`ac5lp1.aif`) |

In memory a group is a 0x34-byte header: name, volume, pitch, the two u16s, channels, and an array of
0x38-byte permutations. The permutation in memory has the SNDD pointer at 0x14 and its name at 0x18.

### Ambient (`OSAm`, 0x513580)

Versions on the CD: 4, 5 and 6.

| Offset | What | |
|---|---|---|
| 0x0c | Priority, s32 | confirmed: the channel priority it asks for |
| 0x10 | Flags (below) | confirmed |
| 0x14 | Detail range, float: how far in x and in z a detail plays from the ambient | confirmed |
| 0x18 | Detail interval, minimum and maximum, floats, seconds | confirmed |
| 0x20 | Distances, minimum and maximum, floats: full volume up to the first, silent from the second | confirmed |
| 0x28 | The detail group's name, 32 bytes | confirmed |
| 0x48 | Base track 1's group | confirmed |
| 0x68 | Base track 2's group | confirmed |
| 0x88 | The in sound's group | confirmed |
| 0xa8 | The out sound's group | confirmed |
| 0xc8 | Version 5 on: how many may play at once (else 3) | confirmed |
| 0xcc | Version 6 on: minimum occlusion, float (else 0) | inferred from the Mac build's names; unused here |

Flags:

| Bit | What |
|---|---|
| 0 | Interrupt on stop: a stop cuts the channels at once. Otherwise the base track plays to its end, then the out sound |
| 1 | Play once: the base tracks don't loop |
| 2 | Can pan: its channels pan with the position |

Group names are resolved at level load (0x482670) with a case-sensitive search; a missing one is no
group. In memory the ambient is 0xfc bytes: the fields from 0x20, the five group pointers at 0x44 (detail,
base 1, base 2, in, out), the five names at 0x58, the subtitle cache at 0xf8.

On the CD a dialog line is an ambient with priority 3, flags 3 and base track 1 only (`c00_01_74shinatama`
plays the group of the same name, whose one sound is `c00_01_74shinatama.aif`). Music has an in, a base
and an out track (`mus_chase`: `mus_chase_in`, `mus_chase` with eight segments, `mus_chase_out`).

### Impulse (`OSIm`, 0x513d90)

Versions on the CD: 3, 4, 5 and 6.

| Offset | What | |
|---|---|---|
| 0x0c | The group's name, 32 bytes | confirmed |
| 0x2c | Priority, s32 | confirmed |
| 0x30 | Distances, minimum and maximum, floats | confirmed |
| 0x38 | Maximum and minimum volume angle, minimum angle attenuation, floats | inferred (a cone); unused by the playing code |
| 0x44 | Version 4 on: alternate threshold, u32 | confirmed |
| 0x48 | Version 4 on: the alternate impulse's name, 32 bytes | confirmed |
| 0x68 | Version 5 on: impact velocity, float | inferred |
| 0x6c | Version 6 on: minimum occlusion, float | inferred |

The offsets from 0x68 hold for version 4 and on; a version 3 impulse ends at 0x44.

## How they play

### Channels

The sound system has 16 channels (0x485be0), 7 stereo and 9 mono (0x4830c0: 16 × 0.4, rounded). Each is
a DirectSound buffer of 22,050 Hz, 16 bits, streamed from a thread. A sound plays on a channel of its own
kind. Its volume is the product of four parts: the ambient's or the impulse's, the distance's, the
permutation's random one, and the overall volume. That product is times the group's volume. It goes to
the buffer (0x485480) in hundredths of a decibel:

- Above 0.05: round(3333 × v) − 3333, so 1 is full volume and 0.5 is −16.7 dB.
- At 0.05 or less: −10,000, silence.
- Above 1 the value is out of DirectSound's range (inferred: the call fails and the volume stays).

The pitch becomes the buffer's frequency, 22,050 × pitch Hz (0x485460). So a 44.1 kHz sound plays at
half speed: the ACM's PCM keeps the source rate and the buffer doesn't (inferred from the code, not
heard on Windows). The pan (0x4853b0) is ±(3333 − round(3333 × v)) with v from 0.5 (to the side) to 1
(ahead or behind). Only channels that may pan get it, and only for sounds more than 7 units away.

A channel's flags (byte 0x1b): playing, looping, locked (held by an ambient), updating (its sound just
ended), paused, can pan, owned by an ambient. Every channel has priority 1, and nothing writes it again.

`SSiSoundChannels_GetChannelForPlay` 0x47fab0 finds a channel for a sound of a priority:

1. The first one neither locked nor playing.
2. Else the first unlocked one of a lower priority. With priority 3 (scripts' ambients and dialogs),
   that is any unlocked one.
3. Else the first unlocked one of priority up to the asked one and volume 0.1 or less.
4. Else the quietest one of priority up to the asked one, under full volume, locked or not. It is taken
   from its ambient if it has one.

Priority 0 (ambient details) only gets a free channel.

### Groups

`SSiGroup_SelectPermutation` 0x480040 draws with the second random generator (0x426e30): r = (draw ×
total weight) >> 16, the first permutation whose running sum reaches r. A don't-repeat group draws again
if it got the last one. The flag that ends the retries is never set back, so once a repeat happens all
10 draws are made and the tenth is kept, repeat or not.

`SSrPermutation_Play` 0x47f8d0 draws the volume, then the pitch, with `UUrRandom` 0x426e10, between the
permutation's minimum and maximum. It resolves the sound by name if needed. A looping channel plays the
group again when its sound ends: each loop is a new permutation (the stream's fill, 0x484ef0, calls
`SSrGroup_Play` 0x47ff50 on the channel).

### Ambients

`SSrAmbient_Start` 0x4821f0 takes one of 16 slots. It fails if the ambient already plays as often as it
may. Then it runs the slot's state machine (0x480c20), which also runs every frame and whenever one of
its channels' sound ends:

| State | What happens |
|---|---|
| start | A channel for base 1 (or the in, or the out sound's group) and one for base 2, at the ambient's priority, locked. If none: details only, or done |
| in | The in sound on the first channel |
| in wait | Until it ends |
| base | The base tracks, looping unless play once; the first detail is timed |
| body | Base tracks play; a lost channel is taken again (at volume 0); details at their times |
| end wait | Loops off; until the tracks end |
| out | The out sound on the first channel; the second is let go |
| out wait | Until it ends |
| release | The channels are let go, the slot is free |

`SSrAmbient_Stop` 0x4823c0 turns the loops off and goes to end wait, so music ends its current segment
and then plays its out track. A channel taken again in the body state is set to volume 0. Then the code
asks for a half-second fade to the ambient's volume, but the fade only starts when the target changes,
so the channel stays silent until the volume is set again. The port keeps this.

Volumes fade (0x482000, 0x481860) by a rate per sixtieth of a second toward the target. The overall
volume slides to its target by at most 0.05 a frame (0x483bd0).

### Music, dialog and the scripts

The script commands (registered by 0x512cb0) use these lookups:

- `sound_music_*` and `sound_ambient_*` are the same functions (0x514410, 0x514500, 0x514550). They
  start the ambient at priority 3 in one of 16 script slots, and stop or fade it by name.
- `sound_dialog_play` (0x514140) starts the ambient as the dialog. There is one dialog at a time; while
  one plays, the call does nothing.
- `sound_dialog_play_block` (0x5142e0) waits (the thread stalls) while a dialog plays, then starts its
  own and goes on.
- `sound_dialog_play_interrupt` (0x514300) halts the current dialog first.

A dialog counts as playing until its ambient ends, or sooner when the first sound of base track 1 has
played its length. It counts for 2 s if the ambient couldn't start. The scripts' `sound_dialog_play_block
pause` (56 times in level 1) names no ambient, so it only waits for the current dialog.
`sound_impulse_play` (0x5145f0) plays the impulse at the listener.

Not yet in the port: the level's sound objects (placed ambients, sound spheres and volumes), the
projectiles' flybys, subtitles, and occlusion.

## Check

`sound_test` loads levels 0 and 1. It builds the databases and resolves every sound name level 1's
scripts use. Then it runs a script with music and two dialogs, an impulse, a fade and a music stop for
900 ticks through the mixer, and prints the databases' numbers, when the dialogs ended and a hash of the
mix.
