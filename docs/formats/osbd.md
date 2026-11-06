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

### The placed ambients and the walls

The speech and the particles' sounds start through the OS layer (`OSrAmbient_Start` 0x511730), not
the sound system directly: a table of 1024 records of 0x4c bytes (+0x08 the ambient, +0x0c the sound
system's id or -1, +0x10 placed, +0x14 the place, +0x20 and +0x24 the distances, the ambient's own
when not given, +0x28 the volume, +0x2c the record's index mod 12, +0x30 a time, +0x34 a cached
volume, 1 at first), its ids the slots of an array at 0x61fa34.

The listener (0x61fa18, the same as the sound system's, set by `OSrUpdate` 0x514f20 from
`iComputeDeltaTicks` 0x4fafb0): in the follow camera with a player, her ears (0x4e33e0, with the byte
0x5533e8 1: her location with y her head's, A+0x2480, or the location's 15 above with no active
part) facing her way ((0, 0, 1) turned about y by C+0x170 less C+0x174, 0x4e3640); otherwise the
camera's place (0x636518) and view (0x636524).

`OSiCalc_PositionAndVolume` 0x511a00 (confirmed):

- out of hearing (false) when max^2 < |place - listener|^2 / 9 (0x52b134);
- the ambient's minimum occlusion (+0x40) above 0.99 (0x52a03c): volume 1;
- with the cache (the updates), the cached volume while the game time is under the record's time
  rounded down to 12, plus 12, plus its index mod 12;
- else the walls on `AKrCollision_Point` 0x4412d0 from the place to the listener (all hits; quads of
  flags 0x8044800 skipped), in order from the place: none counted within 0.1 (0.01 squared, 0x529e3c)
  of it before the first; a quad without flag 0x1000000, or any after the first such, counts one. A
  run of 0x1000000 quads from the first (a door's panels) is taken as one object (AGQG +0x34): each
  "faces" the listener unless two-sided (0x200) when its plane's normal has a positive dot with the
  direction to the listener; it counts unless it faces or is within 2 (the first; 0x529818) or 8
  (0x52a740, after one that faced) of the last; one that faces within 3 (9 squared, 0x52a5b0) of the
  last counted takes that count back; once one of them gave its object id (not -1), only quads of
  other ids count;
- n walls, at most 3: max(minimum occlusion, 0.8 - n^2 / 12) (0x5296d8, 0x52b888); none: 1; no
  level: as 3. The time and the volume go to the cache.

At the start: no place, the sound system's start without one (0x4823a0); a place in hearing, started
(0x4821f0) at that volume; out of hearing, an ambient that plays once (+0x24 bit 1) is dropped (-1),
another waits. `OSrUpdate` runs `OSiAmbient_Update` 0x511f00 on every placed record each frame, and
`OSrAmbient_Update` 0x511ec0 with a new place: out of hearing, a playing one is set to volume 0
(`SSrAmbient_Update` 0x4824a0) and stopped (0x481d90) unless it plays once; in hearing, a waiting one
starts, a playing one's volume moves to the new one by at most 0.05 (0x52a02c, 0x52a31c), held to
0..1. A record that plays once goes when its sound has ended (the update fails); a speech line counts
as playing while its record is there. `SSrAmbient_Update` 0x4824a0 sets the place, the velocity, the
direction and the volume (+0x44 of both base channels, held to 0..2).

The level's sound objects (the spheres and volumes, `OSrUpdate`'s playlist) are in
[objc.md](objc.md) "SNDG".

### Flybys

A particle class with a flyby impulse (the name at +0x98 of its definition, 16 bytes; 0x456ff0 looks
it up into the class's +0x8c after the level's sounds) takes one of 24 flyby records (0x61fa38, 0x28
bytes each) when it is made: the game's creation callback (0x508e20, set by `P3rSetCallbacks`
0x455e10) runs after each create event the particle lives through (`P3rCreateEffect`,
`P3rSendEvent`, the emitters, the class change). The record (0x5156f0): bit 0 in use, the place the
frame before (+4) and now (+0x10), the particle's +0x2c then (+0x1c), its class (+0x20), the
particle (+0x24; its +3 the slot); none if all 24 are taken or its real place fails. `OSrUpdate`
steps each record whose particle still has that +0x2c, frees the others; the deletion callback
(0x508e90, after the die event) takes a last step and frees it (0x515a00).

The step (0x5157c0): the place moves on (a failed real place frees the record). With m the frame's
move and a, b the places before and now less the listener's, `d0 = a·m`, `d1 = b·m`; when
`d0 × d1 < 0` the nearest point to the listener lies on the move, at `(d1 p_before − d0 p_now) / (d1
− d0)` (the middle if `|d1 − d0| ≤ 1e-6`). There the class's impulse plays (`SSrImpulse_Play`
0x480440, not when the scripts only), heard through the walls (0x511a00 with the impulse's max
distance and min occlusion), not at all from three times its max distance. In the data only three
classes have one: the Mercury Bow's bolt (`w8_mbo_p01`, `mb_flyby`), `w12_ba2_p01a`
(`benrgy_flyby`) and `w13_muro_p01` (`muro_tr_flyby`). Ported in `src/sound.c` (`flyby_*`).

## Check

`sound_test` loads levels 0 and 1. It builds the databases and resolves every sound name level 1's
scripts use. Then it runs a script with music and two dialogs, an impulse, a fade and a music stop for
900 ticks through the mixer, and prints the databases' numbers, when the dialogs ended and a hash of the
mix.
