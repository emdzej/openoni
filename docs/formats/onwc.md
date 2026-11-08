# ONWC: weapon classes

A weapon class: its geometry, the HUD's textures, the recoil, the firing modes and up to 16 shooters,
each naming a particle class (a `BINA` instance `3RAP<name>`) that emits the projectiles. One per
weapon: `w1_tap` (the pistol) to `w14_kpi`. Read by `src/weapon.c`.

Offsets are from the instance data, past the 8-byte header (OniGalore's table counts the header: its
offsets are 8 more). Confirmed against the exe where marked.

| Offset | What |
|---|---|
| 0x00 | Laser sight: origin (3 floats), direction (3 floats; all under 2^-10: none), length (+0x18), colour (+0x1c). Confirmed (below) |
| 0x20 | Crosshair sprite {TXMP, colour, scale}; the locked one at 0x2c; the tunnel's at 0x38, with its count at 0x44 and spacing at 0x48. Confirmed (below) |
| 0x4c | The HUD's textures (TXMP): +0x4c the weapon's picture (54 square at the left ring's centre, 0x501240), +0x50 the ammo's icon (at its size there), +0x54 the ammo's ring (an arc of radii 44 and 30 from pi / 12 round by the ammo's share of 5.76, 0x500ea0). Confirmed |
| 0x58 | Geometry (M3GM): its origin is the grip. Confirmed: drawn in the hand (`ONrCharacter_SetCharacterClass` 0x4d7c30 puts it on the weapon node), its largest coordinate sizes the physics sphere (a quarter of it, `WPiWeapon_CreatePhysics` 0x518e10) |
| 0x5c | Name (32 bytes) |
| 0x7c | Aim speed; 0x80 the recoil a shot adds to the aim (C+0x1d0, confirmed: `WPiPulseShooter` 0x519c50), 0x84 its maximum, 0x88 factor, 0x8c and 0x90 the return speeds |
| 0x94 | The second hand's vector, up vector and place (9 floats, unused) |
| 0xba | u16: the recoil overlay's animation type (confirmed: 149 Autopistol_Recoil for w1_tap) |
| 0xbc | u16: the reload's type; 0xbe the wait after a reload (confirmed: into weapon +0x52) |
| 0xc0 | u16: shots, the ammo when full (confirmed, 0x518180) |
| 0xc2 | u16: the shooters in use (confirmed) |
| 0xc4 | u16: firing modes; 0 can't fire (confirmed, `WPrDepressTrigger` 0x51a1b0) |
| 0xc6 | u16: the pause at each trigger (weapon +0x50); 0xc8 the release delay |
| 0xcc | u32: options. 0x08 rifle (the 0x2000 variant), 0x20 sustained fire, 0x400 alternate fire, 0x2000 the trigger releases itself at the delay's end, 0x4000 a release delay, 0x80000 never fades once dropped (`WPrRelease` 0x51a550), 0x100000 drains ammo; 0x03 and 0x08 pick the share of the owner's last hit a thrown weapon takes (0.07, 0.1, else 0.12). Confirmed in `weapon.c`'s uses |
| 0xd0 | Two firing modes, 0x80 bytes each: the AI's aiming (targeting matrix, prediction speed, ranges, the shooting skill's index at +0x66, the noise radius at +0x58) |
| 0x1d0 | 16 shooters, 0x50 bytes each (below) |
| 0x6d0 | The empty sound's name (32 bytes) |
| 0x6f4 | The glow textures (TXMP; +0x6f8 the second, drawn with ammo) and their scales (+0x6fc, +0x700: of the geometry's bounds), sprites over a dropped weapon (`WPrDisplay` 0x519710) |
| 0x704 | The pickup handle's offset from the weapon's place (3 floats; confirmed: 0x51b180, `WPrGetPickupPosition` 0x518650) |
| 0x710 | Float: the height a dropped weapon is drawn above its physics' place (confirmed, `WPrDisplay` 0x519710) |

## The laser sight and the crosshair

Each frame the characters' display (0x4e1836) works out the player's sight when the camera follows
(mode 1) and `marketing_line_off` (0x5ecb9a) is off (0x4e1900, confirmed): the player (C+0x1e8 0) with a
weapon that isn't through a wall (`ONrCharacter_IsGunThroughWall` 0x4f0150), has ammo (`WPrHasAmmo`
0x51a990) and has a laser direction. `WPrGetMarker` 0x51a8a0 puts the origin and the origin plus the
direction under the weapon's matrix (weapon +0x0c): the start and the way (made a unit). 0x4d4e50 casts
the ray: the active characters but the player (`AMrRayToCharacter` 0x4d5150, `ONrCharacter_Collide_Ray`
0x4f1630 unbounded), the objects (`AMrRayToObject` 0x4d52c0) and the level's quads 1000 along it
(`AKrCollision_Point` 0x441c80, skipping 0x44800; the first hit); the character if nearer than both,
else the object if nearer than the quads, else the quads (a missing one 1e9 away). Nothing met: the end
is the length along. Met within the length: the end is the hit drawn back 2 toward the gun (0x432df0);
beyond it, the length along toward the hit, as if nothing. A living character there (not C+4 bit 1)
makes 0x5ecb68 1.

Then, with +0xcc bit 0x8000, `FXrDrawLaser` 0x4f4720 draws the beam: an untextured ribbon from the end
to the start (`M3rContrail_Draw` 0x41a8e0) in the colour, opaque, 0.2 to each side across the camera's
view of it, without writing the depth. With a tunnel sprite (+0x38), up to +0x44 of them go along the
beam, +0x48 apart from a quarter of that, none past the end (`WPrDrawSprite` 0x4e1420, size mode 0).
After the scene and before the lens flares (0x4fbb0a), 0x4f1fa0 draws the crosshair at the end (+0x2c on
a living character, else +0x20), without the fog, through walls with +0xcc 0x40000. `WPrDrawSprite` is
a camera-facing sprite (`M3rSimpleSprite_Draw` 0x41a7e0) of the colour, opaque, without writing the
depth, sized by its scale times 5 (mode 0), the camera's distance to the 0.3 times 0.7 (1, +0xcc
0x20000) or the distance times 0.03 (2, +0xcc 0x10000: the same size on the screen). Three debug
switches (0x5ecb82..0x5ecb84) force the mode. The port: `weapon_sight` (`src/weapon.c`) and
`sight_draw` (`src/render/particle_draw.c`).

## A shooter (0x50 bytes)

| Offset | What |
|---|---|
| 0x00 | Its matrix on the weapon (3x3, then the offset) |
| 0x30 | The particle class's name (16 bytes) |
| 0x40 | Run time: the particle class |
| 0x44 | u16: ammo a shot takes; 0 a shooter that only follows the others (confirmed) |
| 0x46 | u16: the delay between shots (weapon +0x4c); 0x48 the "roughjustice" one |
| 0x4a | u16: the ticks a shot fires at least (weapon +0x54) |
| 0x4c | u16: its trigger, 0 fire, 1 alternate, -1 both (confirmed, `WPiFireShooter` 0x51a220) |
| 0x4e | u16: its delay before it starts (weapon +0x5a per shooter) |

## The weapon (run time, 0x170 bytes, 64 at 0x6221c0)

+0x02 flags (1 in use, 0x10 firing, 0x20 background events on, 0x40 nearby, 0x200 a stop asked, 0x400
delay events on, 0x800 owned, 0x1000 particles made, 0x4000 and 0x8000 dropped), +0x04 the class, +0x08
the owner, +0x0c its matrix (the place at +0x30), +0x3c its physics, +0x40 its facing, +0x44 and +0x46
the fading, +0x48 the time it was made, +0x4c the shot delay, +0x50 the trigger pause, +0x52 the
reload's wait, +0x54 the least firing time, +0x56 the ammo, +0x58 the shooters started, +0x5a their
start delays, +0x7c their particles, +0xfc the noise's wait.

The shooters' particles take the events of `WPiDoEvent` 0x519f70: 1 pulse (a shot), 2 start, 3 stop,
4 and 5 the background start and stop (there is ammo), 14 and 15 the delay start and stop (empty).
A start of a shooter that takes ammo is a shot (`WPiPulseShooter`).
