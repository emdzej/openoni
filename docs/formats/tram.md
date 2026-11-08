# Animations (TRAM)

An animation is a `TRAM` instance ([instance files](./dat)). It holds:

- a rotation per bone per frame, as keyframe streams
- the root's motion and height
- the move's attacks, throws, sounds and particles

The bulk is in the level's `.raw`. OpenOni reads animations with `src/tram.c`; `tools/onitram.py` is the
reference reader. Worked out from `Oni.exe` v1.0 and checked on all 8,604 animations of the CD: 393,903
frames.

## The instance

Offsets are from the instance data, past the 8-byte header. Fields 0x04 to 0x30, and 0x134, are offsets
into the `.raw`. `TRrAnimation_Prepare` 0x428610 adds the `.raw`'s base to them and sets bit 0 of the
flags.

| Offset | What |
|---|---|
| 0x00 | Run time: the instance's name |
| 0x04 | `.raw`: the root's height per frame (float). 0x428820 lerps it |
| 0x08 | `.raw`: the root's motion per frame (x, z floats). `TRrAnimation_GetPosition` 0x428750 returns (x, 0, z) |
| 0x10 | `.raw`: the animation's own damage, 4 bytes each: the damage (u16), the frame (u16); the count is the byte at 0x17b. `TRrAnimation_GetDamage` 0x429160 adds up a frame's. A thrown victim's animation takes its damage this way, from its thrower (`ONrGameState_UpdateCharacters` 0x4dccf0) |
| 0x1c | `.raw`: the throw, 0x18 bytes, 0 if none: the victim's offset from the thrower (3 floats, turned by the thrower's facing), its facing from the thrower's (+0x0c, radians), the reach (+0x10, `TRrAnimation_GetThrowDistance` 0x4291c0), the victim's animation type (u16 +0x14). Confirmed on the CD: the throws (flag 15) have it; their victims' (`_tgt`, flag 16) don't |
| 0x24 | `.raw`: attacks, 0x18 bytes each, their frame range in the first two u16s; the count is the byte at 0x17f (0x4288c0) |
| 0x28 | `.raw`: 8 bytes a frame: +4 the body's height (u16), +6 its bottom (s16), both in 0.01 (TRrIntersectAnimations 0x42a250) |
| 0x2c | `.raw`: the rotations (below) |
| 0x30 | `.raw`: the per-frame sounds, 0x22 bytes each: an impulse's name (32 bytes) and the frame (u16 +0x20); the count is the u32 at 0x14c (`TRrAnimation_GetNewSoundForFrame` 0x4287c0, [animation sounds](./sabd)) |
| 0x34 | Flags. Bit 0 is the run time's "prepared" |
| 0x38 | Two links to other `TRAM`s |
| 0x4c | u16: the direction an attack pushes the attacker back (0, 1, 5 forward; 2 back; 3 left; 4 right); 0 means the u32 at 0x12c |
| 0x50 | float: the attack's reach |
| 0x54, 0x58 | floats: the attack's lowest and highest point |
| 0x5c | 36 floats: the reach every 10 degrees round the character (TRrCheckAnimationBounds 0x429940) |
| 0x128 | float: the angle of the longest reach, radians |
| 0x12c | u32: the direction, when 0x4c is 0 |
| 0x130 | u32: the count of attack points |
| 0x134 | `.raw`: the attack points, 12 bytes each: the frame (u16), the angle (byte +3, 256ths of the circle), the reach (u16 +4), the bottom and top (s16 +8, +0xa), all lengths in 0.01 |
| 0x150, 0x154 | Run time: the animation's sound (a name) and its frame (u16), set from the [animation sounds](./sabd) (`TRrAnimation_SetSoundName` 0x428800); 0 in the files |
| 0x156 | Frames per second, 60 on the CD |
| 0x158 | Bytes per rotation key: 6 or 16 on the CD |
| 0x15a | Animation type (0x428710) |
| 0x15c | Aiming type |
| 0x15e | The state it starts from (`TRrAnimation_GetFrom` 0x428720) |
| 0x160 | The state it ends in (0x428730) |
| 0x162 | Bones, 19 on the CD |
| 0x164 | Frames |
| 0x166 | Duration (`TRrAnimation_GetDuration` 0x428740), equal to the frames on the CD |
| 0x168 | Variant (`TRrAnimation_GetVarient` 0x428700) |
| 0x178, 0x179 | Bytes: the invulnerable frames, the first and how many (`TRrAnimation_IsInvulnerable` 0x42c2f0): a throw can't start on a character in them |
| 0x17b | Byte: the count of the own damage entries (0x10) |

The other fields are still to be worked out, with the character engine (step 5).

## Rotations

The block starts with a u16 per bone: the offset of that bone's stream from the start of the block. A
stream is keys of the key size. Each key but the last is followed by a byte, the frames to the next
key. On the CD every bone's durations add up to the frames less 1.

The 0x427820 walk finds a frame's rotation:

1. It adds up the durations from the first key, until the sum reaches the frame.
2. It takes the key before the frame, the key after, or a lerp of the two (`MUrQuat_Lerp` 0x433360).

The lerp:

- flips the second quaternion if the dot product is negative
- mixes the two linearly
- renormalizes, giving the identity if the length is 0

**Key sizes** (the switch in 0x427820):

| Size | Key | On the CD |
|---|---|---|
| 16 | A quaternion, 4 floats (x, y, z, w) | 308 animations |
| 6 | Three u16 angles, each times 2π/65535 (the float at 0x5297f0), turned into a quaternion by 0x437050 | 8,296 animations |
| 8 | Four s16, each times 1/32767 | |
| 4 | Four s8, each times 1/127 | |
| 3 | Angles through byte sine tables in the exe | |

**0x437050**, from Euler angles (x, y, z) to a quaternion:

- It takes the half angles of x and z.
- For y it takes 2π less the half angle.
- With c and s for the cosines and sines of those, and a = cₓc_z, b = sₓc_z, c = s_z cₓ:
  - x = b c_y − c s_y
  - y = −sₓs_z c_y − a s_y
  - z = c c_y − b s_y
  - w = −(a c_y + sₓs_z s_y)
- The x87 keeps the sines and cosines of x and z at full precision, and rounds what it stores to
  floats.

## Check

`tools/onitram.py <game>` walks every animation. It hashes one line per animation and one per level.
Each animation's line has:

- its index, name, frames, bones, key size, type and states
- a CRC of every bone's stream as walked, then the motion and heights

`tram_test` must print the same: `all: hash=a992f7fd`. It also decodes every frame of every animation
and checks that each rotation is a unit quaternion.

The decoded rotations aren't hashed, since the x87's precision isn't reproduced bit for bit. Instead, the
Python rounds to floats where the exe stores them. Then `onitram.py game level0 --dump KONOKOidle_spec3
37` and `tram_test` print the same rotations to 5 decimals.
