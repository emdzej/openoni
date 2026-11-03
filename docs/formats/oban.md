# OBAN: object animations

An object animation moves one rigid thing: a door, the warehouse's forklifts and truck, a cutscene
camera. It holds key frames, each with a rotation, a position and a frame number; between keys both are
lerped. 1,067 of them are on the CD. The port: `src/oban.c`. The reference reader: `tools/onioban.py`.
The check: `tests/oban_test.c` (`all: hash=1f2bffdc`, 334,386 frames).

## Layout

Instance data (offsets as `tm.h` gives them, past the 8-byte instance header):

| Offset | Size | What |
|---|---|---|
| 0x00 | 12 | Zero on the CD |
| 0x0c | 4 | Flags (below) |
| 0x10 | 48 | Start matrix, 3×4 by columns: where the object stands before it plays |
| 0x40 | 48 | Base matrix, 3×4: applied under every frame's transform |
| 0x70 | 2 | Ticks per frame, 80 on the CD (see playing: it changes nothing) |
| 0x72 | 2 | Frames |
| 0x74 | 2 | A door's open frame (50 for the doors, 0 for the others; inferred) |
| 0x76 | 2 | Key count |
| 0x78 | 0x20 each | Keys: quaternion x, y, z, w (floats), position x, y, z (floats), frame (u32) |

The first key is at frame 0 and the last at frames − 1 in every animation on the CD.

Flags (from `OBrAnim_Start` 0x439580, `PHrPhysics_Update_Animation` 0x43a620 and
`OBrAnimation_GetMatrix` 0x439610):

| Bit | Meaning | On the CD |
|---|---|---|
| 0x01 | Loop | 37 |
| 0x02 | Ping-pong: back and forth | none |
| 0x04 | Random start frame | none |
| 0x08 | Starts by itself | none |
| 0x10 | Turned a quarter about x (the doors) | 13 |

## The matrix at a frame

`OBrAnimation_GetMatrix` 0x439610 walks the keys:

- A key at the frame gives its rotation and position.
- Otherwise the first key after the frame and the one before it are mixed. t = (frame − before) /
  (after − before). The position is lerped (0x433ff0, t·after + (1 − t)·before) and the quaternion goes
  through `MUrQuat_Lerp` 0x433360. Before the first key or after the last, the original reads outside the
  keys. No animation on the CD asks for those frames; the port holds the nearest end.

The matrix is T(position) · R(rotation) · Base, by `MUrMatrix_Multiply` 0x4341c0 (a·b on 3×4 matrices
stored by columns). R comes from `MUrQuatToMatrix` 0x4338c0, and the caller's flag 2 leaves it out. With
flag 0x10, 0x434570 then puts a rotation of π/2 about x (a double at 0x529d40) in front.

## Playing

The playing state: the animation, a state word (1 playing, 0x10 paused), the frame, the step (+1 or −1),
a frame to stop at (0: none) and the time of the last step.

- `OBrAnim_Start` 0x439580: playing, at frame 0 or with flag 0x04 a random one, (`UUrRandom` ×
  frames) >> 16. The step becomes 1. A paused animation is unpaused with the time set to now (0x43a600).
- `PHrPhysics_Update_Animation` 0x43a620, each tick:
  - An animation with flag 0x08 that isn't playing starts.
  - While playing, it steps when the last step's time is below the ticks field plus now. That holds on
    every tick, so the animation moves one frame per tick and the field changes nothing. This looks like
    an inverted test in the original; the port keeps it.
  - At the end (the frame count, or the stop frame), ping-pong turns back from frames − 1, loop goes to 0,
    and anything else stops (0x439540: the frame clamped into the animation, the playing bit cleared). A
    ping-pong at a negative frame turns forwards again at 0.
  - It returns true when the animation stopped that tick.
