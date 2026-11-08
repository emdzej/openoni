# Particle classes (BINA `3RAP`)

A particle class is a `BINA` instance whose payload starts with the tag `PAR3` (stored as `3RAP`) and
its size; the instance's name is `BINA3RAP<class name>`. `P3rLoadParticleDefinition` 0x459f20 copies the
block past the tag and size (the *definition*) and `0x4574e0` sets it up: the variables get their
offsets in the particle, the actions' variable slots and the emitters' classes are resolved. The port
keeps the definition as loaded (`src/particle.c`, the `D_*`, `A_*` and `E_*` offsets) and the class's
run-time record (0x98 bytes at 0x65c520 in the original) beside it.

## The definition

Offsets from the definition's start (past the tag and size). Confirmed by the setup and the readers.

| Offset | Size | |
|---|---|---|
| 0x02 | u16 | version (the loader upgrades the older ones) |
| 0x04 | u32 | flags 1: 0x01 decorative, 0x08 a y scale of its own, 0x100 geometry, 0x200 collides with the walls, 0x400 with the characters, 0x800 the scale follows the velocity; bits 12..24 the blocks the particle has (below) |
| 0x08 | u32 | flags 2: 0x08/0x10 expire/die in cutscenes, 0x20/0x40 low/medium detail, 0x800000 a decal, 0x1000000 hidden, 0x2000000 invisible, 0x4000000 edge fade, 0x8000000 a vector, 0x10000000 locked to its link, 0x20000000 a contrail emitter, 0x40000000 a lens flare, 0x80000000 one-sided |
| 0x0c | u32 | the actions masked off at the start |
| 0x10 | u16 | variables (0x34 bytes each) |
| 0x12 | u16 | actions (0x1a8 bytes each) |
| 0x14 | u16 | emitters (0x1bc bytes each, 8 at most) |
| 0x18 | 16 x u32 | the events' action lists (each: the first action's index and the one past the last, u16s), in the order update, pulse, start, stop, bgfx_start, bgfx_stop, hit_wall, hit_char, lifetime, explode, brokenlink, create, die, newattractor, delay_start, delay_stop |
| 0x58 | value | the lifetime |
| 0x74 | value | the collision radius (a point unless its type is 3 and it is above 0) |
| 0x90, 0x94 | | the AI's dodge and alert radii |
| 0x98 | 16 bytes | the flyby sound |
| 0xa8.. | values | the appearance: scale, y scale, rotation, alpha, the texture's name (+0x118, 32 bytes), x offset, x shorten, tint, edge fade min and max, contrail, lens-flare distance (+0x1e0) and fades (+0x1fc, +0x1fe), the decals' maximum (+0x200), fade (+0x202) and wrap angle (+0x204) |
| 0x220.. | | the attractor: its kind, selector, target, name (+0x22c), distance, angle |
| 0x304 | | the variables, then the actions, then the emitters |

A *value* is 0x1c bytes: its type (u32) then its data. Types: 0 a variable (its name, 16 bytes; its
type +0x14 and its offset in the particle +0x18 after the setup), 3 a float, 4 a uniform range, 5 a
bell curve (mean, deviation), 6 a string (16 bytes), 7 and 10 an integer or enum, 8 a colour range, 9
a colour bell curve, 11 a cycle (period, maximum). `P3iCalculateValue` 0x45ad80 draws one.

A variable (0x34 bytes): its name (16), its type (+0x10: 2 float, 8 colour, 0x1000 ping-pong state, 4,
0x2000, 0x4000 strings), its offset (u16 +0x14) and its first value (+0x18).

An action (0x1a8 bytes): its type (+0x00, the table below), a payload (+0x04: a class or a sound,
resolved), its variable slots (+0x08, 24 bytes each: a variable's name and, after the setup, its type and
offset) and its parameters (+0xc8, values of 0x1c bytes).

An emitter (0x1bc bytes): the class's name (64), the class (+0x40, resolved), flags (+0x44: 1 on at the
start, 0x10 adds the parent's velocity, 0x40 orients along the velocity, 0x80 inherits the tint, 0x100,
0x200, 0x400 the attractors' share-out), a threshold and probability (+0x48, +0x4a), the count (+0x4c),
the link (+0x50: 0 none, 1 the emitter, 2.. another emitter's last, 10 the emitter's link), then the
kinds of the rate (+0x54), position (+0x58), direction (+0x5c) and speed (+0x60), the orientation's
direction and up (+0x64, +0x68) and the parameters (+0x6c, values: the rate's from slot 0, the
position's from 2, the direction's from 5, the speed's from 8).

Position kinds: 0 point, 1 line, 2 circle, 3 sphere, 4 offset, 5 cylinder, 6 the owner's body surface,
7 the owner's bones (see the run time: docs/re/particles.md, "The bodies"). Direction kinds: 0 straight
(up), 1 random, 2 cone, 3 ring, 4 offset, 5 inaccurate (the owner's inaccuracy), 6 towards the
attractor.

## The particle's blocks

Flags 1 bits 12..24 ask for these, laid out after the emitters' states (12 bytes each) in this order
(the sizes at 0x536028): velocity 12, orientation 36, offset 12, the attachment's matrix 4, the previous
position 16, the decal 20, the texture's index 4 and time 4, the owner 4, the contrail 32, the lens flare
4, the attractor 8, the collision cache 20. The variables follow.
