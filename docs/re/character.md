# The character records

How the character engine (`ONI_Character.c` in Bungie's tree) keeps a character in memory. Worked out from
`Oni.exe` v1.0, mostly from `ONrGameState_NewCharacter` 0x4dac50, `ONrGameState_NewActiveCharacter`
0x4db830, `ONrGameState_UpdateCharacters` 0x4dccf0, `ONrGameState_DoCharacterFrame` 0x4ddba0,
`ONrCharacter_DoAnimation` 0x4e4400 and `ONrCharacter_NextAnimation` 0x4e2ea0.

Names in *italics* come from the Intel Mac build's name map (`re/mac-matches.tsv`) and haven't been checked
beyond the match. **Confirmed** means the code was read and the meaning follows from it; **inferred** means
the use was seen but the meaning is a reading of it.

## Where they live

Everything is in the game state, the block at `[0x5ece7c]` (`GS` below):

| GS offset | What |
|---|---|
| 0x0084 | The level's environment (0x4fc890 returns its address) |
| 0x00ac | The player's character (`ONrGameState_GetPlayerCharacter` 0x4fc8a0) |
| 0x0144 | Game time, in ticks (`ONrGameState_GetGameTime` 0x4fc880) |
| 0x125c | u16 serial for new characters |
| 0x125e | u16 serial for new active characters |
| 0x1260 | 128 characters, 0x16a0 bytes each (`ONrGameState_GetCharacterList` 0x4fc850) |
| 0xb6260 | u16: one past the highest character slot used |
| 0xb6268 | 64 active characters, 0x2838 bytes each |
| 0x157068 | u16: one past the highest active slot used |
| 0x1670f0 | Active list: pointers to characters; count at 0x1672f0 (0x4fc9b0 / 0x4fc9c0) |
| 0x1672f8 | Living list; count at 0x1674f8 (added by 0x4fcab0, removed by `ONrGameState_LivingCharacterList_Remove` 0x4fcaf0) |
| 0x167500 | Present list; count at 0x167700 (0x4fcb50 / 0x4fcb60, added by 0x4fcb70) |

The debug line of `UpdateCharacters` names the three counts "alive", "active" and "present".

A character is the 0x16a0 record. It is *present* while it's in the level, and *active* while it also owns
one of the 64 active records: the full simulation (animation, physics, collision). The u16 at character+2
is 0 when inactive; otherwise its low byte is the active slot (*ONrGetActiveCharacter* 0x4f1180, also
inlined everywhere). An active record points back through its u16 at +0, which holds the character's u16
at +0. Becoming active: 0x4db830 (*ONrGameState_NewActiveCharacter*); inactive: 0x4f1120
(*ONrCharacter_MakeInactive*).

## The character (0x16a0)

`C` is the record. "OSD" is the `CHAR` object's data in memory, starting at object+0x2c (the object
reader 0x4bf290 builds it from the `OBJC` chunk, version by version; its size, 0x1fc, is returned by
0x4bf0e0). Object offsets below are in-memory ones.

| Offset | Type | What | Shown by | |
|---|---|---|---|---|
| 0x000 | u16 | Handle: low byte the slot, high byte the serial (GS+0x125c), bit 15 set | NewCharacter | confirmed |
| 0x002 | u16 | Active handle: low byte the active slot, bits 8–14 the serial (GS+0x125e), bit 15 set; 0 when inactive | 0x4db830, 0x4f1180 | confirmed |
| 0x004 | u32 | Flags, see below | everywhere | confirmed |
| 0x008 | u32 | Weapon flags: bit 0 set/cleared by the reload input | *HandleHeartbeatInput* 0x4f6ad0 | inferred |
| 0x00c | ptr | Class, `ONCC` (with a random variant for OSD flags 0x802) | NewCharacter | confirmed |
| 0x012 | u16 | Team (OSD+0x1c0; for a script-made character, the setup's +0x26). Trigger volumes test `1 << team` | NewCharacter, 0x4cc680 | confirmed |
| 0x014 | char[32] | Name (OSD+0x44, or `ai_%d`) | NewCharacter | confirmed |
| 0x034 | float | Scale: random between ONCC+0xc58 and +0xc5c; applied to the matrices when not 1 (0x4e5a30) | NewCharacter | confirmed |
| 0x038 | u32 | Hit points: ONCC+0xc50 + OSD extra health (at least 1), times the difficulty factor | NewCharacter | confirmed |
| 0x03c | u32 | Maximum hit points, the same at start | NewCharacter, DoCharacterFrame | confirmed |
| 0x040 | u16 | AI: 0 for the player, 0xffff for AI | `AI2rInitializeCharacter` 0x48a6b0 | inferred |
| 0x044 | u32 | Scripts fired: bit *n* set once script *n* has run | `AI2rUpdateAI` 0x489d40, *Die_1_Animating_Hook* 0x4eb550 | confirmed |
| 0x048 | char[8][32] | The eight script names (spawn first). NewCharacter runs the spawn script if +0x48 isn't empty | AI2rInitializeCharacter, NewCharacter | confirmed (which is which past spawn: inferred) |
| 0x148 | float[3] | Position: the feet. Copied from the physics object each tick | NewCharacter, DoCharacterFrame | confirmed |
| 0x154 | float[3] | Previous tick's position | DoCharacterFrame | confirmed |
| 0x160 | float[3] | Location: see [the location](#the-location-0x160) | NewCharacter, DoCharacterFrame | confirmed |
| 0x16c | float | Facing, radians, [0, 2π) | NewCharacter, debug line ("face", ×57.2958) | confirmed |
| 0x170 | float | Desired facing (= facing at start); the aim (0x4d8540) and film record use it | NewCharacter, 0x4d8540 | inferred |
| 0x174 | float | Pending turn: 0 at start; *HandleHeartbeatInput* applies it and zeroes it | 0x4f6ad0 | inferred |
| 0x178 | float[3] | Read as a direction for melee impacts | DoCharacterFrame | inferred |
| 0x184 | ptr | The environment node (`AKEV` BNV) that holds the location (0x440600) | NewCharacter, UpdateCharacters | confirmed |
| 0x188 | ptr | Its pathfinding node (*ONrCharacter_FindOurNode* 0x50c9c0) | NewCharacter, UpdateCharacters | confirmed |
| 0x18c | float | Pelvis height above the location: 7.5 at start; each tick the animation's root height at this frame (0x428820), blended while interpolating | NewCharacter, DoCharacterFrame | confirmed |
| 0x190 | float | Body bottom: max(position y, body box min y) | 0x4e5ff0 | confirmed |
| 0x194 | ptr[3] | Weapon slots; +0x194 the weapon in hand (0 = none) | NewCharacter (0x51ba40), DoCharacterFrame | confirmed |
| 0x1a2 | u16 ×6 | OSD+0x1a8..0x1b2 (u16s), stored at 0x1a2, 0x1a8, 0x1a6, 0x1ac, 0x1a4, 0x1aa in that order | NewCharacter | confirmed copy, meaning open |
| 0x1b2 | u8 | OSD+0x1b6 ≠ 0 | NewCharacter | meaning open |
| 0x1b3 | u8 | OSD+0x1ba ≠ 0 | NewCharacter | meaning open |
| 0x1b4 | u8 | OSD flag 0x200 | NewCharacter | confirmed copy |
| 0x1b6 | u16 | 0 | NewCharacter | |
| 0x1b8 | u16 | OSD+0x1b4, 1 turned into 100 | NewCharacter | meaning open |
| 0x1ba | u16 | OSD+0x1b8; non-zero starts the active record with +0x2815 = 1, +0x2818 = 1.0 | NewCharacter, 0x4db830 | inferred: start invisible/shielded |
| 0x1bc | u32 | 600 if +0x1ba ≠ 0, else 0 | NewCharacter | inferred: its timer |
| 0x1c4 | float[3] | With a weapon in hand: how far the part at TRCM+4 (the weapon's part) moved this tick; 0 otherwise | DoCharacterFrame | confirmed formula |
| 0x1d8 | ptr | Object being picked up (weapon or powerup), cleared when the pickup animation ends | DoCharacterFrame | inferred |
| 0x1dc | u32 | Sound serial, compared with active+0x1c9c (AI noise for footsteps) | DoCharacterFrame | inferred |
| 0x1e0 | u32 | Game time of the last action (debug "idle") | NewCharacter (0), DoCharacterFrame | inferred |
| 0x1e4 | u32 | Idle delay: ONCC+0xc48 plus a random 0–50 % of it | NewCharacter | confirmed formula |
| 0x1e8 | u32 | Controller: 0 the player (or none), 2 AI | AI2rInitializeCharacter | confirmed |
| 0x1f0 | u32 | AI flags (1, or 3; 4 and 0x400 from OSD flags 8 and 0x100; 8/0x10/0x40/0x80/0x20 request scripts) | AI2rInitializeCharacter, AI2rUpdateAI | confirmed |
| 0x1f8.. | | The AI2 state (to about 0x15ec) | AI2 module | not mapped |
| 0x15b0 | | AI movement state (passed to `AI2rMovementState_Initialize` 0x4a8d60) | 0x4db830 | confirmed |
| 0x15ec | code* | AI block function (`AI2rBlockFunction` for AI, 0 for the player) | AI2rInitializeCharacter | confirmed |
| 0x15f0..0x1614 | | Speech state (0x516900 clears it) | NewCharacter | confirmed |
| 0x162c, 0x1630, 0x1634 | u32 | 0 at start; 0x162c/0x1630 a weapon-recoil counter for the player | NewCharacter, 0x4fa520 | inferred |
| 0x1638 | u32 | 0 | NewCharacter | |
| 0x163c | u32 | 1000 | NewCharacter | meaning open |
| 0x1640 | u32 | Ticks since last made active; inactive AI goes inactive after 90 | UpdateCharacters | confirmed |
| 0x1644 | u16 | Deletion countdown: deleted when it reaches 0 | UpdateCharacters | confirmed |
| 0x1648 | float[6] | Body box: min xyz, max xyz | 0x4e5ff0 | confirmed |
| 0x1664 | float | Damage flash target | DoCharacterFrame | inferred |
| 0x1668 | float | Damage flash, moved toward +0x1664 | DoCharacterFrame | inferred |
| 0x1670..0x167c | u32 | 0 at start | NewCharacter | |
| 0x1688..0x169f | | Hurt sound: +0x168a delay, +0x1698 kind (0–3: ONCC+0x110..0x11c, "light"…), +0x168c last kind, +0x1690 time | DoCharacterFrame | confirmed |

**Flags at +4** (confirmed unless marked):

| Bit | Meaning |
|---|---|
| 0x00000001 | Dead (dying, stage 1). Input and DoAnimation stop |
| 0x00000002 | Dying, stage 2: DoCharacterFrame only poses and drifts the body |
| 0x00000004 | Dying, stage 3: waits to be deleted |
| 0x00000008 | Dying, stage 4 (debug "death = 4") |
| 0x00000010 | Leave the animation at the end of its atomic part (DoCharacterFrame calls NextAnimation) |
| 0x00000020, 0x40 | From OSD flags 0x40, 0x80 (*inferred*: unkillable, infinite ammo) |
| 0x00000200 | Cleared when active+0x21f0 counts down |
| 0x00001000 | Cleared when DoAnimation changes animation |
| 0x00008000 | Slot in use |
| 0x00020000 | Kept off the ground (physics flags 0x44; the location keeps the bottom) (*inferred*) |
| 0x00080000 | Death took too long; set by UpdateCharacters |
| 0x00400000 | AI-controlled movement (set when +0x1e8 = 2) |
| 0x04000000, 0x10000000 | From OSD flags 0x1000, 0x400 (*inferred*: no auto drop, boss) |
| 0x20000000 | Already hit by the current melee impact |

## The active character (0x2838)

`A` is the record; `C` its character.

| Offset | Type | What | Shown by | |
|---|---|---|---|---|
| 0x000 | u16 | The character's handle (C+0) | 0x4db830 | confirmed |
| 0x002 | u16 | Own handle (= C+2) | 0x4db830 | confirmed |
| 0x004 | ptr | Physics object (0x43adf0). +0x84 position, +0x68 velocity, +0x11c movement, +0x128 flags, +0x130 scripted animation (`OBAN`), +0x148 callbacks (ground: 0x4eca00), +0x14c owner | *ONrCharacter_EnablePhysics* 0x4dc2b0 | confirmed |
| 0x008 | float[4] | Collision sphere tree, root: centre (position, y + half the height), radius (half of min(72, top − bottom)) | 0x4dc160 | confirmed |
| 0x020 | float[4]+… ×4 | Four stacked spheres, stride 0x18, radius 4 (3 for AI that isn't the player) | 0x4dc160 | confirmed |
| 0x080 | ×2 | Two middle spheres, stride 0x18 | 0x4dc160 | confirmed |
| 0x0b0..0x0b6 | | Cleared each tick | DoCharacterFrame | |
| 0x0b8 | float[3] | Knockback velocity; added at 0x52b2e0×, decays ×0x52b2dc each tick on the ground | DoCharacterFrame | confirmed |
| 0x0c4 | float | Movement this tick, x (→ physics +0x11c, after DoCharacterFrame) | DoCharacterFrame, UpdateCharacters | confirmed |
| 0x0c8 | float | Movement y when ≥ 0 | | confirmed |
| 0x0cc | float | Movement z | | confirmed |
| 0x0d0 | float | Movement y when < 0 (the ground callback checks it) | 0x4eca00 | confirmed |
| 0x0e0 | u16 | Ticks without ground; at 12 sets +0x21dc | 0x4eca00 | confirmed |
| 0x0e4 | float[3] | Last ground point | `FindGroundCollision` 0x4ec040 | confirmed |
| 0x0f0 | float | Height where the ground was lost | 0x4eca00 | confirmed |
| 0x0f4 | u16 | 0 when a fall starts | DoCharacterFrame | |
| 0x0f6 | u16 | Ticks in the air (0 on the ground); over 300 kills (damage 0x5000000) | DoCharacterFrame | confirmed |
| 0x0f8 | float[3] | Air velocity per tick; y loses gravity, clamped | DoCharacterFrame | confirmed |
| 0x104 | float[3] | The motion as the jump was asked (0x4dc7e0, the velocity estimate); at takeoff y + ONCC+8 and at least ONCC+8 / 2 the way the keys go | HandleStartJumping 0x4fa2f0, DoCharacterFrame | confirmed |
| 0x110 | u8 | Jump velocity given | DoCharacterFrame | inferred |
| 0x111 | u8 | Jump start point taken | DoCharacterFrame | inferred |
| 0x112 | u16 | Jump delay: at 0 the jump starts (+0xf6 = 1). Only counted down: nothing in v1.0 sets it | DoCharacterFrame | confirmed |
| 0x114 | float[3] | Jump start point | DoCharacterFrame | inferred |
| 0x120 | ptr | Character jumped at: steers the air velocity toward it | DoCharacterFrame | inferred |
| 0x124 | u32 | Countdown | DoCharacterFrame | |
| 0x128 | u32 | Ground flags | FindGroundCollision | inferred |
| 0x12c | float | Ground height (location y at start) | 0x4db830, FindGroundCollision | confirmed |
| 0x130 | float[4] | Body sphere: pelvis sphere centre in the world, radius grown to hold all 19 parts | 0x4e5ff0 | confirmed |
| 0x140 | float[3] | Trigger segment, bottom | DoCharacterFrame | confirmed |
| 0x14c | float[3] | Trigger segment, top | DoCharacterFrame | confirmed |
| 0x158 | float[3] | Segment sphere centre | 0x4f2810 | confirmed |
| 0x164 | float | Segment sphere radius | 0x4f2810 | confirmed |
| 0x168 | u32[4] | Ids of the trigger volumes the character is in | `ONrGameState_UpdateTrigger` 0x4e0cf0 | confirmed |
| 0x1af8 | ptr | Current animation, `TRAM` | everywhere | confirmed |
| 0x1afc | u16 | State: the current animation's end state; lookups start from it | NextAnimation, DoAnimation | confirmed |
| 0x1afe | u16 | Previous state: the state the current animation started from | NextAnimation | confirmed |
| 0x1b00 | u16 | Current animation type | NextAnimation | confirmed |
| 0x1b02 | u16 | Queued type: played when the current one ends; 0 when none | DoAnimation, NextAnimation | confirmed |
| 0x1b04 | u16 | Type at the start of the tick | DoCharacterFrame | confirmed |
| 0x1b06 | u16 | 0 | 0x4db830 | |
| 0x1b08 | u8 | Interpolating into the current animation | 0x4e41e0 | confirmed |
| 0x1b0c | float | Pelvis height at the interpolation's start | 0x4e41e0 | confirmed |
| 0x1b10 | u16 | State at the interpolation's start | 0x4e41e0 | confirmed |
| 0x1b14 | float[3] | Velocity at the start (*ONrCharacter_GetVelocityEstimate* 0x4eafc0), blended out | 0x4e41e0, DoCharacterFrame | confirmed |
| 0x1b20 | u16 | Interpolation tick | DoCharacterFrame | confirmed |
| 0x1b22 | u16 | Interpolation length | 0x4e41e0 | confirmed |
| 0x1b24 | float[19][4] | Bone rotations at the interpolation's start (copy of +0x1ef0) | 0x4e41e0 | confirmed |
| 0x1c58 | {ptr, u16, u32} ×4 | Overlay animations (stride 0xc): `TRAM`, frame, flags. Slot 1 drives weapon draw/holster/reload types 0xa0–0xab | DoCharacterFrame, debug line | inferred |
| 0x1c88 | u16 | Current frame | DoCharacterFrame | confirmed |
| 0x1c8a | u16 | Hard pause: no new animation until 0; raised to the animation's TRAM+0x14a | NextAnimation, DoAnimation | confirmed use |
| 0x1c8c | u16 | Soft pause: TRAM+0x148 | NextAnimation, DoAnimation | confirmed use |
| 0x1c8e | u16 | Interpolation length asked for the next change (at least 8) | DoAnimation | confirmed |
| 0x1c90 | u32 | Hold: while non-zero NextAnimation repeats the animation; at 0 DoAnimation(stand) | DoCharacterFrame, NextAnimation | confirmed |
| 0x1c94, 0x1c96 | u16 | Countdowns that block DoAnimation like the hard pause | DoAnimation | confirmed |
| 0x1c98, 0x1c9a | u16 | Countdowns | DoCharacterFrame | |
| 0x1c9c | u32 | Last sound serial heard (see C+0x1dc) | DoCharacterFrame | inferred |
| 0x1ca0 | u16 | Variant bits for lookups. 0x7800: the weapon kind (cleared by `& 0x87ff`); 0x0100, 0x0200 cleared/set by input timing | everywhere | confirmed role |
| 0x1ca2 | u16 | Fight timer, counts down (debug "fight") | DoCharacterFrame | confirmed |
| 0x1ca4 | u8 | Count of animation particles; entries from +0x1ca8, 0x10 bytes; +0x1ca6 u16 mask of the ones on | DoCharacterFrame | inferred |
| 0x1dac..0x1eaf | | Class particles (ONCC+0x430), up to 16 entries of 0x10 from +0x1db0, count +0x1dae | DoCharacterFrame | inferred |
| 0x1eb0, 0x1eb4 | | Input combo timing | 0x4f6ad0 | inferred |
| 0x1ed8 | u16 | 0xffff at start | 0x4db830 | |
| 0x1eda | u16 | Throw: the thrower's character slot (0xffff none) | 0x4db830, DoCharacterFrame | inferred |
| 0x1edc | ptr | Throw partner (character) | UpdateCharacters | inferred |
| 0x1ee4..0x1eec | | Root rotation | 0x4e5360 | inferred |
| 0x1ef0 | float[19][4] | Bone rotations (quaternions) | *ONrCharacter_BuildQuaternions* 0x4e5360 | confirmed |
| 0x2020 | | Bone set for the matrices (0x4274c0) | 0x4e5a30 | inferred |
| 0x2150 | u32 | Game time of the last input | 0x4f6ad0 | confirmed |
| 0x2158 | 0x30 bytes | Input this tick: two floats (turn x, y), then held (u64 +0x2168), not held (+0x2170), went down (+0x2178), went up (+0x2180) | 0x4f6ad0, DoFilmUpdate | inferred layout |
| 0x2188 | u64 | Previous held | DoFilmUpdate | inferred |
| 0x2190 | u32 | Time of a key press (−1 none), sets/clears variant 0x0100 | DoCharacterFrame | inferred |
| 0x2194 | u8 | Frozen: DoCharacterFrame returns at once, movement zero | DoCharacterFrame | confirmed |
| 0x2195 | u8 | Aim tracking on (1 at start) | 0x4db830 | inferred |
| 0x2198 | float[3] | Aim target point | 0x4dbe20 | confirmed |
| 0x21a4, 0x21b0 | float[3] | Aim directions | 0x4d8540, 0x4dbe20 | confirmed |
| 0x21bc | float | Aim left/right, radians | 0x4d8540, DoFilmUpdate | confirmed |
| 0x21c0 | float | Aim up/down | 0x4d8540 | confirmed |
| 0x21c4, 0x21c8 | float | World aim angles | 0x4d8540 | confirmed |
| 0x21cc, 0x21d0 | float | Auto-aim correction, zeroed each tick | DoCharacterFrame | confirmed |
| 0x21dc | u8 | Start falling (set by the ground callback) | 0x4eca00, DoCharacterFrame | confirmed |
| 0x21df..0x21e4 | u8 | Fire requests; +0x21e3, +0x21e4 stop firing | DoCharacterFrame | inferred |
| 0x21f0 | u16 | Countdown that clears C flag 0x200 | DoCharacterFrame | confirmed |
| 0x21f4 | u32 | Film: 1 playing, 2 recording, 4 an offset to apply (+0x2208 per tick, +0x2204 ticks) | DoCharacterFrame, DoFilmUpdate | confirmed |
| 0x2214 | ptr | Film | DoFilmUpdate | confirmed |
| 0x221c, 0x2220 | u32 | 6 at start; deletion waits for +0x221c 0 or 6 | 0x4db830, UpdateCharacters | meaning open |
| 0x2228 | ptr | Hand block (0x424920): +4 the weapon's model, +0x24 0x12 or 0xe by ONCC+0xc8c | 0x4db830 | inferred |
| 0x2278 | float[19][12] | World matrices of the 19 parts (stride 0x30, translation at +0x24): +0x229c pelvis, +0x232c left foot, +0x23bc right foot, +0x247c head | 0x4e5a30, *ONrCharacter_GetPelvisPosition* 0x4e34f0 | confirmed |
| 0x2800 | u8 | Shield on | UpdateCharacters | inferred |
| 0x2815 | u8 | Invisible | 0x4db830, UpdateCharacters | inferred |
| 0x2818 | float | Its amount | 0x4db830 | inferred |
| 0x281c, 0x2820 | | Debug footstep display | DoCharacterFrame | confirmed |
| 0x2824..0x282c | float[3] | Corpse drift velocity (dying, stage 2) | DoCharacterFrame | confirmed |
| 0x2830 | u16 | Dying: ticks standing still (> 120 ends the stage) | UpdateCharacters | confirmed |
| 0x2832 | u16 | Dying: ticks in all (> 900: "dieing for to long") | UpdateCharacters | confirmed |

## One tick

`ONrGameState_ProcessHeartbeat` 0x4fa6a0 runs, in order: the timer, key remap *ONiRemapKeys* 0x4fa520,
*HandleUtilityInput* 0x4f6200, `DoFilmUpdate` 0x4ed420, `AI2rUpdateAI` 0x489d40 (**AI**),
`SLrScript_Update` 0x478070, doors 0x4385e0, **`ONrGameState_UpdateCharacters` 0x4dccf0**, weapons
(0x507880, *WPiUpdate* 0x5192b0, 0x5190f0), **physics *OBiPhysics_Update* 0x43af50** (moves the physics
objects by the movement the characters left in +0xc4, with the ground callbacks), particles 0x45c830,
0x438f10, the cinematic 0x4f3420, `ONrInGameUI_Update` 0x504780.

`ONrGameState_UpdateCharacters` 0x4dccf0 (3,759 bytes):

1. `OBJrObjectType_EnumerateObjects` 0x4d0080 over the trigger volumes: `ONrGameState_UpdateTrigger`
   0x4e0cf0 → `OBJrTriggerVolume_IntersectsCharacter` 0x4cc680. Uses last tick's segment.
2. Debug keys (`ONrDebugKey_WentDown` 0x5050d0).
3. Return unless characters run (byte 0x54e165).
4. **Present list.** Make active if all must be (byte 0x5ecbac → 0x4db830). Count down +0x1644
   (`ONrGameState_DeleteCharacter` 0x4dc480). **Death**: stage 2 waits for the body to settle (A+0x2830,
   +0x2832), then drops the weapon (0x4e4ab0), sets stage 3, removes physics (*WPrPowerup_RemovePhysics*
   0x43aef0) and, if inactive, *Die_4_Gone_Hook* 0x4ebb60; stage 3 deletes once allowed (corpse
   0x4ef340). The living get `ONrCharacter_Shield_Update` 0x4ef710 and `ONrCharacter_Invis_Update`
   0x4ef530.
5. **Active list, input.** *ONrCharacter_HandleHeartbeatInput* 0x4f6ad0 for each one not dying
   (flag 2). Inside: *HandleTurnInput* 0x4f7b90, `ONrCharacter_HandlePickupInput` 0x4eda40, then the
   movement handlers (*HandleStartRunning* 0x4f8ef0, *HandleStartSidestep* 0x4f83f0,
   *HandleStartTauntOrAction* 0x4f80d0, *HandleFallThrough* 0x4f9a70, *HandleStop* 0x4f9fa0, …), which
   call `ONrCharacter_DoAnimation` 0x4e4400 and `ONrCharacter_UpdateAnimationForVarient` 0x4eb1a0. AI
   characters get their input from AI2 through the same block (+0x2158) (*inferred*). Before the frame,
   *ONiRunGame* 0x4d4640 masks each input event's held bits (+0 low, +4 high; events 0x30 apart) with
   the allowed keys at G+0x1679b0/G+0x1679b4: all at *ONrGameState_LevelBegin* 0x4f5350, 0x10003/0 after
   a bare `lock_keys` (*AIiScript_LockKeys* 0x4bd4e0), and a group's bits ORed in by `lock_keys <group>`
   (*confirmed*): movement 0x6600000, jump 0x8000000, crouch 0x10000000 + high 0x100, attack 0x60000000 +
   high 0xc0, pause 4, action high 2, inventory high 0x30, walk high 1, hypo high 4, reload high 8, all.
6. Run the scripts queued meanwhile (the queue at 0x632540, `ONrScript_ExecuteSimple` 0x5100b0).
7. **Present list, the frame.** Debug text for the watched character, then
   **`ONrGameState_DoCharacterFrame` 0x4ddba0**. Queued scripts again.
8. **Active list, contacts.** Throws: a partner at frame 0 → 0x4e9330. **Damage** an animation does to
   its own character at this frame (TRAM+0x10 list, 0x429160) → `ONrCharacter_TakeDamage` 0x4e65f0.
   Queued scripts again.
9. If byte 0x54e164: 0x4e7130 (not read).
10. **Present list.** C+0x184 (0x440600, the BNV of the location), C+0x188 (0x50c9c0), and the
    movement A+0xc4 added to the physics object (0x43beb0).
11. Unless all must be active: C+0x1640 += 1; AI characters unseen for 90 ticks, on the ground, not
    throwing, go inactive (0x4f1120, "%s going inactive").

`ONrGameState_DoCharacterFrame` 0x4ddba0 (11,828 bytes), for character `C`, active `A`:

1. Return if A+0x2194. `ONrSpeech_Update` 0x516a60.
2. **Pickup**: at the action frame (min(TRAM+0x174, frames − 1)) of types 0x9d/0xe3 (powerup:
   0x51b3e0, *WPrPowerup_Give* 0x51ca30, *WPrPowerup_Delete* 0x51c4f0) or 0x9e/0x9f/0xe4/0xe5 (weapon:
   0x51b180, `ONrCharacter_PickupWeapon` 0x4ed770). `WPrInventory_Update` 0x51bad0.
3. **Inactive**: a box around the old location (0x433e80/0x433f00, radius 4, or 3 for AI that isn't the
   player), then the location becomes the position; skip to 20.
4. Weapon bookkeeping; reload (`WPrTryReload` 0x51c560); variant bit 0x0100 from input timing.
5. **Player aim**: aim relax, `ONrCharacter_AutoAim` 0x4efad0 against every active character.
6. Countdowns: A+0x1c90 (→ DoAnimation(stand)), 0x1c8a, 0x1c8c, 0x1c94..0x1c9a, 0x281c, 0x124. A+0x1b04
   = type. C+0x18c = root height (0x428820).
7. Scripted animation (physics +0x130) ends at its last frame.
8. **Dying (flag 2)**: matrices (steps 15's four calls), corpse drift, return.
9. **Jump/fall** (**falling**): A+0x112 countdown starts the jump; A+0x21dc starts a fall (velocity
   0x4eafc0 or the jump's); on the ground the motion is the animation's root motion
   (`TRrAnimation_GetPosition` 0x428750, blended while interpolating), stepped down by up to 4 to stay on
   the ground (`AKrCollision_Point` 0x441c80), else y − 0.5; in the air, A+0xf8 with gravity, steering
   toward A+0x120.
10. Rotate the motion by the facing (0x433010); add knockback (A+0xb8).
11. Scripted animation: physics position from `OBrAnim_Matrix` 0x4395f0, less C+0x18c. Film offset.
12. **C+0x154 = C+0x148; C+0x148 = physics position.** Movement → A+0xc4..0xd0 (all 0 if frozen).
13. **Throw end** and drop/swap weapons (0x51b590, *WPrSlot_Swap* 0x51bd00); AI 0x48b770.
14. Animation particles and class particles (*P3rSendEvent* 0x45b2c0); damage flash C+0x1668.
15. **Overlays** (A+0x1c58): weapon draw/holster/reload at their action frame; sounds
    (*OSiVariant_Impulse_Get* 0x516670, 0x512500).
16. **Footsteps / impacts**: `ONrCharacter_PlayAnimationImpact` 0x4f0e80 at the feet (A+0x232c,
    A+0x23bc); AI hears them (`AI2rKnowledge_Sound` 0x49a8a0, 60 or 150).
17. **Animation**: A+0x1c88 += 1; interpolation tick; A+0x1ca2 −= 1. At the end
    (`TRrAnimation_GetDuration` 0x428740) → **`ONrCharacter_NextAnimation` 0x4e2ea0**; also when flag
    0x10 and not atomic (*TRrAnimation_IsAtomic* 0x428c90: TRAM+0x16c ≤ frame ≤ +0x16e).
18. **Aim**: the player (or film) 0x4d8540; AI toward A+0x2198 (the code of 0x4dbe20, inlined).
19. **Pose and bounds**: *ONrCharacter_BuildQuaternions* 0x4e5360, matrices 0x4e5a30, body bounds
    0x4e5ff0, sphere tree 0x4dc160. Trigger segment, 0x4f2810. Shield and invisibility again. Weapon
    particles (0x428f90, 0x4fce30). Firing (*ONiCharacter_Fire* caller 0x4e9040) and stopping
    (0x51a3b0).
20. **Location** (C+0x160), C+0x1c4, hurt sound (0x516a00, *OSrImpulse_Play* 0x512480).

`ONrCharacter_NextAnimation` 0x4e2ea0 (617 bytes), (C in ECX, A in EDX):

1. Clears C flag 0x10.
2. The type to play: 0xdf if the current type is 0xde, else the queued A+0x1b02.
3. If A+0x1c90 is non-zero, the current animation again. Otherwise `TRrCollection_Lookup` 0x427f00 on
   ONCC+0xc80 with (type, state A+0x1afc, variant A+0x1ca0); if none, "invalid next animation" and
   (6 stand, 7 standing).
4. *RemapAnimationHook* caller 0x4ea550 may substitute it.
5. A+0x1c8a and +0x1c8c become at least the old animation's pauses toward the new one (0x428a70 TRAM+0x14a,
   0x428a40 TRAM+0x148; 0 when the new one is one of the old one's two links at TRAM+0x38).
6. `TRrAnimation_GetEndInterpolation` 0x428cb0; *ONiCharacter_OldAnimationHook* 0x4ead30.
7. With an interpolation: 0x4e41e0 (start an interpolated change). Without: A+0x1af8 = new, frame 0,
   A+0x1b02 = 0, A+0x1afe = old state, A+0x1b00 = its type (0x428710, TRAM+0x15a), A+0x1afc = its end
   state (0x428730, TRAM+0x160).
8. *ONrCharacter_NewAnimationHook* caller 0x4e97a0.

`ONrCharacter_DoAnimation` 0x4e4400 (1,419 bytes), (C, A, flags, type); returns the animation or 0:

1. Nothing if C is dead (flag 1).
2. Lookup (type, A+0x1afc, A+0x1ca0). With flag 8, none found and frame < 8: retry from A+0x1afe and drop
   the flags.
3. Clears C flag 0x1000 if the animation differs. None → 0.
4. **Pauses**: a hard pause (A+0x1c94, 0x1c96, 0x1c8a, or the current's toward this one) lets only
   non-attack types 6, 0x11, 0x17, 0x18, 0x28–0x2b through; a soft pause the same, but not 0x17.
5. **Flag 1, "when it can"**: if the new one differs from the current (type, variant or end state) and
   the current one may be cut (not the victim check 0x4e36f0, not two TRAM flags, not one of the listed
   attack types, atomic only if a shortcut allows), it plays now; otherwise flag 2 (queue).
6. Variant bit 0x0200 cleared (unless standing-to-stand, or the fight timer runs).
7. Same type (or the start types 0xd0–0xd3, 0xd9, 0xda while the type they start is asked) with a
   variant the character has: **queue** (A+0x1b02 = type). Flag 2: queue.
8. Otherwise switch: interpolation max(8, A+0x1c8e), a shortcut's own length (0x428b70), at most
   TRAM+0x172 (0x428e00). Remap hook, old-animation hook, then the switch as in NextAnimation 7 (or
   0x4e41e0 with the interpolation). New-animation hook; A+0x1c8e = 0.

`ONrCharacter_UpdateAnimationForVarient` 0x4eb1a0 (118 bytes): unless the type is 0x6a–0x71, looks up
the current type from A+0x1afe with the variant now in A+0x1ca0; if that animation's variant (0x428700,
TRAM+0x168) is higher than the current one's, switches to it with an 8-tick interpolation (0x4e41e0).

## Making a character

`ONrGameState_NewCharacter` 0x4dac50: ECX the `CHAR` object (or 0), EDX an AI setup (or 0), then a
starting point (or 0) and a u16* for the slot.

1. **Slot**: the first of the 128 without flag 0x8000 (none: error 1); raises GS+0xb6260; clears the
   0x16a0 bytes; handle from GS+0x125c; flags 0x8000; sets the defaults (C+0x18c 7.5, C+0x163c 1000, the
   rest 0); speech reset 0x516900.
2. **From an object** (tag `CHAR`):
   - **Class**: `ONCC` by the name at OSD+4. With OSD flags 0x802 (random costume / upgrade) it
     collects every class with the same variant (`ONCV` name at ONCC+0x42c +4, or the upgrade at +0x24
     with flag 0x800 above difficulty 1) and takes one at random (`UUrLocalRandom` 0x426e30). None:
     console error, the named class stays.
   - **Name** OSD+0x44 → C+0x14. Weapon slots cleared (0x51ba40).
   - **Weapon**: `ONWC` by OSD+0x64. Found: a new weapon (0x518690), into the hand (*ONrCharacter_UseWeapon*
     caller 0x4e4c00), then 0x519b30 with the u16 at OSD+0x1c4 (*inferred*: ammo). Not found and not "none": "can't find weapon class".
   - **Position** = object+0xc (OSD-independent); previous position the same; location = position,
     y + 0.5. **Facing** = object+0x228, the object's y rotation × π/180 (set by 0x4bf9f0).
   - **Health** = ONCC+0xc50 + OSD+0x1a4, at least 1, into current and maximum.
   - **Team** OSD+0x1c0 → C+0x12; the other OSD fields as in the table.
3. **From a setup** (no object; script `chr_create`, film, debug): the class is the setup's (+0x28, or by
   number), the name copied or `ai_%d`, team the setup's +0x26, health the class's, position and facing
   from the starting point or a flag (`ONrLevel_Flag_ID_To_Flag` 0x506c80); weapon from the setup's
   +0x150.
4. **AI**: `AI2rInitializeCharacter` 0x48a6b0: the eight script names, OSD flags into C flags, the
   controller (+0x1e8: 0 with OSD flag 1, the player; else 2, AI, with `AI2rBlockFunction`), the job
   and alert fields, `AI2rExecutor_Initialize` 0x498490.
5. **Difficulty**: hit points × the factor at `[0x5ecbfc]`+0xaec + difficulty×4 (difficulty
   0x50f240, 0–2) for the player; for AI, only if `AI2rTeam_FriendOrFoe` 0x48b2c0 against the player
   gives 2 (*inferred*: hostile), +0xae0. Rounded (0x4340a0). Factor 1: unchanged.
6. **Class preparation**, once per class: particle classes (`P3rGetParticleClass` 0x45a030) for ONCC
   +0xc2c and the ONCP list, the ONIA impulses (0x487140), the hurt sounds (ONCC+0x110..0x11c,
   0x512440).
7. **Scale** C+0x34 random in the class range; idle delay C+0x1e4.
8. Previous position = position; facing − C+0x174 wrapped to [0, 2π) rotates a vector (0x433010, target
   not traced); BNV C+0x184 and graph node
   C+0x188; added to the present and living lists.
9. **Player** (C+0x1e8 = 0): made active at once (0x4db830) and `ONrGameState_SetPlayerNum` 0x4fc8f0.
   **AI**: 0x4a7ff0, `AI2rMovementStub_Initialize` 0x4ac0f0, *AI2rAlarm_Stop* 0x48af80(0). It becomes
   active later: in UpdateCharacters only in all-active mode; otherwise elsewhere (not traced).
10. **Spawn script** (C+0x48 not empty): scheduled (`ONrScript_Schedule` 0x510160) before the game
    starts, run (0x5100b0) after.

**The difficulty's health** (0x4db352, after `AI2rInitializeCharacter`): the hit points and their most
(C+0x38, C+0x3c) times a factor of the game settings (ONGS) by the difficulty (persist +0x48, 0 easy to
2 hard), rounded by 0x4340a0: +0xaec for the player (C+0x1e8 0); +0xae0 for a character whose team
takes the player's for an enemy (`AI2rTeam_FriendOrFoe`, this team first, 2), none while there is no
player. The data's factors: the player 1.3 on easy, the enemies 1.25 on hard, 1 otherwise. Confirmed.
Ported (`difficulty_health`, chars.c); not yet: a hard game's other inventory for CHARs with flag
0x800 (0x4dac50).

**The shots' spread** (`P3iEmitParticle` 0x45d610, direction 5): the cone's angle is its base, widened
when the emitting particle has an owner by the owner's inaccuracy times the second angle.
`WPrParticleInaccuracy` 0x51ced0 gives it: for a living AI-controlled character in combat its shooting
skill's (+0x10, a factor about 1: 0.5 to 3 in the classes), for an enemy of the player times the
difficulty's factor (ONGS +0xad4: 1.2, 1, 0.6); for a turret (0x3000000) its class's +0x5f8 times the
same. The player's own shots take the base alone. Ported (`particle_inaccuracy`, weapon.c), but for
the patrol's targeting and the turrets, whose shots the port leaves without an owner.

**Becoming active**, 0x4db830: the first free of the 64 records, cleared; handles linked; A+0xe4 = the
position; animation defaults (A+0x1ed8/0x1eda 0xffff, overlays cleared, A+0x2190 −1, A+0x2195 1,
A+0x221c/0x2220 6, the hand block); **the first animation** (0x4d7f90): AI movement may choose; otherwise
weapon variant bits, then type 6 (stand) in state 7 (standing) for the current variant; aim (0x4d8540 for
the player, 0x4dbe20 for AI); pose (0x4e5360, 0x4e5a30(−1), 0x4e5ff0, 0x4dc160); physics (0x4dc2b0)
with the ground found by `FindGroundCollision` 0x4ec040, or a ray (0x441c80) down to the floor, which
moves the position; AI movement state; the trigger segment from the position to 12 units above it, its
sphere (0x4f2810).

## The location (0x160)

The character's reference point, used for the BNV lookup (C+0x184), jump steering, impacts and the inactive
pelvis (*ONrCharacter_GetPelvisPosition* 0x4e34f0: A+0x229c when active, else C+0x160 with y + C+0x18c):

- At creation, and when inactive: the position, y + 0.5.
- Active, each tick (end of DoCharacterFrame): the pelvis matrix's translation (A+0x229c, A+0x22a4) for
  x and z; for y the body bottom C+0x190, or max(C+0x190, the ground contact A+0xe8) while on the ground
  (A+0xe0 = 0, A+0xf6 = 0, no flag 0x20000); then y + 0.5.

So "the position with y + 0.5" only holds until the character's first active frame.

## Trigger volumes: A+0x140, +0x14c, +0x158, +0x164

`OBJrTriggerVolume_IntersectsCharacter` 0x4cc680 (ECX volume, EDX team mask, stack the character):

1. No active record, or the team bit (`1 << C+0x12`) not in the mask: no.
2. Spheres: squared distance between A+0x158 and the volume's centre (+0x2d4) ≤ (A+0x164 + volume
   radius +0x2e0)². Not: no.
3. The segment from **A+0x140** to **A+0x14c** crosses the volume (0x43f750, with the direction
   A+0x14c − A+0x140); else A+0x140 inside all six planes (0x43f910).

**What they are**: a vertical segment through the character's feet, and its bounding sphere.

- **A+0x140** = (C+0x148, **C+0x190**, C+0x150): position x, z; y the body bottom.
- **A+0x14c** = (C+0x148, **C+0x1658**, C+0x150): position x, z; y the body top.
- **A+0x158** = the midpoint; **A+0x164** = half the length (0x4f2810).

Written at step 19 of DoCharacterFrame, after the pose, from that tick's position. Dying characters
(flag 2) return before that step and keep their last segment; inactive characters keep theirs too
(but have no active record, so step 1 rejects them). At activation (0x4db830) the segment is
the position to the position + 12 in y.

**The body box** (0x4e5ff0, C in ECX, A in EDX), from the class's highest-detail body (ONCC+0xc34 →
`TRBS`+0x10 → `TRCM`+0x48, 19 parts; each part's `M3GM` → `PNTA` bounding sphere at +0x24):

1. A+0x130 = part 0's (pelvis) sphere, centre through the pelvis matrix A+0x2278; C+0x1648 = the box
   of that sphere (centre ± radius).
2. For parts 1–18: centre through the part's matrix (A+0x2278 + 0x30×part); A+0x13c grows to the
   distance to it plus its radius. **Parts 1–10 only** (legs, torso, neck, head; not the arms 11–18)
   grow the box.
3. **C+0x190** = max(C+0x14c, box min y). **C+0x1658** = max(box max y, box min y + 16).
4. Then the box also takes a sphere of radius 2 at the position (after step 3, so it doesn't change
   them).

The parts' order (0 pelvis, 1–3 left leg, 4–6 right leg, 7–10 spine to head, 11–14 left arm, 15–18 right
arm) is the usual one; the code shows feet at 3 and 6 and the head at 10. Inferred, not checked part by
part.

## Open

- The meaning of the OSD fields copied to C+0x1a2..0x1bc, and the names of the eight scripts past the
  spawn one. [objc.md](../formats/objc.md) says OSD flag 0x04 marks the player; the code treats bit 0
  (0x01) as the player and 0x04 as "not initially present" (0x48a360). Worth a check against the data.
- The input block layout (A+0x2158): the four u64 masks are a reading of how they're used.
- 0x4e7130 (characters against each other?), 0x4e9330 (throws), and the AI state from C+0x1f8.

## Jumping and falling

- **Jump** (`HandleStartJumping` 0x4fa2f0): needs jump gone down this tick, and isn't taken while the
  type is 5, 0x12, 0x13, 0x14 or 0x29. The table, held keys to type: forward 0x12, step left 0x14,
  step right 0x29, back 0x13, none 5. 0x29 is a landing type (jump right would be 0x15), so from the
  ground the right jump finds no animation and the jump in place is taken: kept as the original has it.
  From a stand only type 5 exists; 0x12 needs a run.
- **Takeoff**: an animation whose from state is 0x25..0x2e sets A+0x21dc (`ONrCharacter_NewAnimationHook`
  0x4e97a0). Step 9 then sets the air velocity: after a jump, the kept motion with y + ONCC+8 (1.1 for
  Konoko); held forward makes z at least ONCC+8 / 2, back at most minus that, then left and right on x.
  Without a jump, the velocity estimate with y = -0.5.
- **In the air**: each tick the motion is the air velocity, y loses ONCC+4 (0.06) down to ONCC+0xc (-4).
  Jump held adds ONCC+0x10 (0.03) while A+0xf6 < ONCC+0x16 (u16, 20).
- **Landing**: `FindGroundCollision` 0x4ec040 clears the air velocity and time and sets A+0x21dd (and
  A+0x21de when the fall hurt). `HandleHeartbeatInput` tries, NOW: forward 0x28, step left 0x2a,
  step right 0x29, back 0x2b, none 0x18; the hard one 0x89, 0x8b, 0x8a, 0x8c, 0x88, from the state, then
  from 0x26, then 0x25, then the soft table.
- **Long fall**: in the air for 91 ticks (0x5b), type 0xdc at once.
- **Off the ground**: the ground callback 0x4eca00 counts ticks without ground (A+0xe0); at 12 the fall
  starts. On the ground the motion's y is the gap below (`AKrCollision_Point` down 4) when 0.5 to 4,
  else -0.5, so the physics keeps the character on the floor.

## Hits

The contacts between characters, step 9 of `ONrGameState_UpdateCharacters` (0x4e7130, when byte
0x54e164 is set):

- **Pairs** (0x4e7130): every two characters on the active list, neither with flags 0x40004 nor dying
  (flag 2), closer than `[0x52a99c]` (squared distance), go to `iCollideTwoLikelyCharacters` 0x4e73e0.
  Afterwards each character with a knockdown asked (A+0x1b06) is knocked down (`ONrCharacter_Knockdown`
  0x4e4e10) unless dead; one with flag 0x80 (blocked a hit) is held in a block: fight mode (A+0x1ca1 |= 2,
  A+0x1ca2 = ONCC+0xc44), hold cleared, type 0x17 at once. Flag 0x80 is cleared.
- **Part contact** (0x4e73e0): the collision body is the class's body set +0x10 (the drawn one). Unless
  either is the other's throw partner (A+0x1ed8), every part of one against every part of the other:
  `M3rIntersect_GeomGeom` 0x41ef50, the two geometries' bounding spheres (PNTA +0x24 centre, +0x30 radius)
  under the parts' matrices (A+0x2278, 0x30 bytes per part), touching when the centres are closer than
  the sum of the radii. Two masks per character: which of its parts touch the other.
- **Attacks** (0x4e8e70 for each side): the current animation's attacks at this frame (0x4290c0: the
  TRAM+0x0c list, 0x20 bytes each, count the byte at +0x17a; active when +0xe <= frame <= +0x10). An
  attack whose part mask (+0x00) meets the touching parts lands; the result is the landed attacks' bits
  and the highest +0x1a among them (the priority). Then `HandleGenericMask` 0x4e8c50 for both: the two
  push apart (the unit vector between them, through 0x433da0, out of the movement A+0xc4, and A+0xb4
  counts the pushes).
- **Priority**: the side whose priority is higher or equal hits first (`HandleAttackMask` 0x4e7740 for
  each landed attack); the other side hits too only if its priority is equal or higher.
- **`HandleAttackMask`** 0x4e7740 (4,637 bytes): each attack hits a character once per animation (8
  slots of attack ids at the attacker's A+...); blocking through `ONrCharacter_CouldBlock` 0x4e62b0;
  the defender's facing toward the attacker; the hit part's material (`ONrCharacter_GetMaterialType`
  0x4e6200); then `ONrCharacter_TakeDamage` 0x4e65f0 (2,573 bytes).
- **The attack entry** (0x20 bytes): +0x00 the attacker's parts (mask), +0x04 knockback, +0x08 flags
  (1 unblockable; 8 halves a blocked hit's knockback), +0x0c damage, +0x0e/+0x10 the frames, +0x12 the
  reaction type, +0x14 stun, +0x16 block stun, +0x18 stagger, +0x1a priority.
- **A hit that isn't blocked**: staggered (A+0x1c98 or A+0x1c9a) and heavy (stun > 20), the reaction
  becomes a knockdown (0x4d→0x50, 0x4e→0x51, 0x52→0x53, 0x55→0x58, 0x56→0x59, 0x57→0x5a, 0x5b→0x5c,
  else 0x4f); heavy and not staggered, 0xc6. Lying (state 0x12 or 0x32) it is 0x54. From behind (the
  attacker more than pi/2 from the facing): 0x4c→0x55, 0x4d/0x4e/0x7e/0x7f→0x56, 0x4f→0x58, 0x50→0x59,
  0x51→0x5a, 0x52→0x5b, 0x53→0x5c, 0xc6→0xd5, 0xd6→0xd7. The reaction goes to A+0x1b06; unless 0x54 or
  0xd4 the defender turns to face the attacker (and away for the ones from behind). Then the damage,
  and the stun into A+0x1c9a (heavy) or A+0x1c94.
- **`TakeDamage`**: the four stun countdowns cleared; nothing for the dead (flag 1); invincible (flag
  0x2000000) takes none; unkillable (0x20) keeps 1. A killing blow plays the reaction if it is one that
  leaves the character down, then `ONrCharacter_Die_1_Animating_Hook` 0x4eb550 sets flag 1 and
  `ONiCharacter_RunDeathAnimation` 0x4eb970 knocks down by 0x4f unless already lying in a reaction. The
  knockback adds along the direction to A+0xb8 unless unstoppable (0x100); step 10 of DoCharacterFrame
  moves 0.07 of it a tick on the ground, keeping 0.93, and drops it under 0.01 squared or in the air.
- **Stage 2**: when a dead character's next animation isn't a reaction, `NewAnimationHook` sets flag 2
  (dying) and keeps its drift (A+0x2824 from the knockback or the air velocity); DoCharacterFrame then
  only poses it, and it gets no input.

- **Blocking** (`ONrCharacter_CouldBlock` 0x4e62b0): a block state (0x4e38b0: the animation from
  standing, crouching or 0x2f in a standing, turning, crouching or block type, or a backward move; the
  from state is tested twice, the to state read and unused), no hit stun (A+0x1c94), a block (0x17) from
  the state in the fight variant, the attacker within 20 degrees (0x54e168) of the aim (C+0x170 minus
  C+0x174). The block animation's flags 3 and 2 must allow the attack's high (4) and low (2) flags;
  flag 1 is unblockable. The AI asks its block function (C+0x15ec). A blocked hit turns the defender to
  the attacker; with attack flag 8 it takes half the damage (at least 1) and half the knockback, else
  none and a knockback of 1. Then a stagger (+0x18: reaction 0xc6, A+0x1c9a) or a block held (flag
  0x80, A+0x1c96 = +0x16, the block type 0x17 after the contacts).

Ported in `src/chr_hit.c` (`hit_test`: Konoko's punches and kicks on a training dummy, 4 and 5 hit
points each, once per blow, then a killing blow; a dummy's punch blocked facing it, 10 taken facing
away). Not yet: the AI's block decision, the material and impact effects,
the push apart, in-air hits, the knockdown's remaps.

## Throws

A throw is a move that turns into another as it starts: `RemapAnimationHook` 0x4ea550, called for every
animation a character is about to play.

- **Which moves**: for a character the AI doesn't drive (C+0x04 flag 0x400000 clear, or the active
  record's +0x21f4 bit 1), Punch_Forward (0x1f), Kick_Forward (0x1a), Run_Punch (0x43) and Run_Kick
  (0x42) try the throws (0x6a, 0x6c), (0x6b, 0x6d), (0x6e, 0x6f) and (0x70, 0x71), the front throw then
  the behind one. The AI's throws are its executor's (`AI2rExecutor_TryingToThrow` 0x498d50).
- **The target** (`ONiCharacter_FindNearestOpponent` 0x4ea2b0, from `AttemptThrow` 0x4eaa10 with 24
  units and pi/4): living characters but the thrower, not friends (`AI2rTeam_FriendOrFoe` 0x48b2c0 not 0),
  not unstoppable (flag 0x100) nor 0x40000, whose class allows it (ONCC +0xc8f is 0), and whose animation
  hasn't TRAM flag 1. The angle is the direction to it (atan2 of the x and z differences) less the aim
  (C+0x170 minus C+0x174), into (-pi, pi]; the least angle wins. A first turn of the angle into
  (-pi, pi] is undone by the test after it, so it stays in [0, 2 pi) until the offset is taken off.
  Two cheat bytes skip the player (0x5ece91, also the player's invincibility, and 0x5ece90).
- **The choice** (`AttemptThrow` 0x4eaa10): the target not in its animation's invulnerable frames
  (`TRrAnimation_IsInvulnerable` 0x42c2f0, TRAM +0x178 and +0x179). The front type if the target's
  facing is more than pi/2 from the thrower's (they face each other), else the behind type. Every
  animation of that type from the thrower's state, in its collection and then its parents'
  (0x427dd0, 0x427ef0), with no variant bits the thrower lacks (fight mode aside), goes to
  `AttemptSpecificThrow` 0x4ea950. It needs the target within the throw's reach (TRAM +0x1c, +0x10) and
  the victim's animation: the throw's victim type from the target's state, in the **thrower's**
  collection, the target's variant with fight mode (it should have 0x200, else a console message). The
  best has the highest victim variant << 16 | throw variant.
- **Success**: the target's A+0x1eda names the thrower; the thrower turns by the angle to it, keeps the
  target and the victim animation (A+0x1edc, A+0x1ee0) and enters fight mode. The throw replaces the
  move.
- **The start** (`ONrCharacter_NewAnimationHook` 0x4e97a0, an animation with TRAM flag 15): every new
  animation first resets A+0x1ed8 (0xffff) and, unless it is a throw, forgets the target. For a throw
  with a living target in use: the target enters fight mode if the throw has the fight variant, the AI's
  notice (0x48b530, an AI2 target), C+0x1bc cleared on both, the throw's noise
  (`AI2rKnowledge_Sound` 0x49a8a0, 150); the victim animation, remapped, through the old animation hook,
  set from the target's state with 6 frames of interpolation, then the target's own new animation hook.
  The target is moved (through the physics, 0x4e9330) to the thrower's place plus the throw's offset
  turned by the thrower's facing (`MUrPoint_RotateYAxis` 0x433010); the thrower's A+0x1ed8 names it;
  its facing is the thrower's plus the throw's angle (only wrapped above 2 pi); it gets flag 0x1000
  (being thrown, dropped by `ONrCharacter_DoAnimation` as soon as it asks for another animation).
- **While it lasts**: the thrower and the target don't touch (`iCollideTwoLikelyCharacters`, A+0x1ed8).
  `ONrGameState_UpdateCharacters` 0x4dccf0, after the frames: a thrower on frame 0 of its throw moves the
  target to its hands again; then every living character takes its animation's own damage at the frame
  (TRAM +0x10, `TRrAnimation_GetDamage` 0x429160), from its thrower if the animation is a throw victim's
  (flag 16; a damage boost for an overpowered thrower, class +0xc8d).

Ported in `src/throw.c` (`chars_test`: Konoko throws the throw dummy, Throw_Forward_Punch on
KONCOMthrow_fw_p_tgt, 20 damage at frame 42). Not yet: the AI's throws, the AI's notice, the noise, the
overpowered boost, the physics move (the port places the target).

## The animation's particles

An animation's particles are TRAM +0x24 (0x18 bytes each, the count the byte at +0x17f; earlier read as
attacks): the first and last frame (u16s), the bone (u16 +4), a name (+8, 16 bytes). The class's list is
ONCC +0x430, an ONCP: the count at +0x14, entries of 0x58 from +0x18: the name (16), the particle class's
name (64, +0x10), a u16 at +0x50 (0x8000: an impact effect, "snap"), the class at +0x54 (run time).
*Confirmed* on level 12: the ninja's list has `teleport` (teleport_e01) and `fireball` (w10_sni_e01).

- `ONrCharacter_NewAnimationHook` 0x4e97a0, last: A+0x1ca4 the count, A+0x1ca6 = 0 (the mask of those in
  their frames), and for each, by its name in the list, A+0x1ca8 + 0x10 i: the entry, the particle's
  handle, the particle, the bone. The particles are made at once (0x4e96a0: `P3rCreateEffect` with the
  orientation identity 0x529d10, the position the bone geometry's box middle in the bone's space, hanging
  from the bone's matrix A+0x2278 + 0x30 bone, the owner `WPrOwner_MakeMeleeDamageFromCharacter` 0x51ce00,
  0x2000000 | the index; then particle flag 0x2000) between `P3rSetGlobalTint` 0x456f90 with the health
  colour (0x4ef450) and its end 0x456fa0: a class taking a tint (flags2 bit 0, its tint a variable's) gets
  it. A name not in the list: "no particle found for anim".
- `DoCharacterFrame` after the action frame (0x4df576): the mask of the particles whose first <= frame <=
  last (0x4288c0); when it changes, each live one gets the health colour (0x456fb0) and event 2 (start)
  coming in, 3 (stop) going out, at the game time / 60. An impact one coming in on a throw (the throw's
  target, or this one when thrown) with the victim dead: its particle killed and, if the victim's C+4
  0x20000000 isn't set, `ONrImpactEffect` 2 at the bone (with the material) and the bit set (the impact
  effect isn't ported).
- `ONiCharacter_OldAnimationHook` 0x4ead30, last: each live one stopped (event 3) if in its frames, then
  killed (0x456c70); the count and the mask 0.

## The inventory over time

C+0x1ae is the hypo's healing still to come, C+0x1b0 the wait to the next point, C+0x1bc the ticks
invisible since the last hit; A+0x2815 the invisibility's effect on, A+0x2818 how much of the body shows.
The game settings (ONGS, 0x5ecbfc) give +0 the over-health cap (2, times the maximum), +4 a hypo's dose
(0.25 of the maximum), +8 the over part (1). *Confirmed* by `tests/inventory_test.c`.

- `WPrInventory_Update` 0x51bad0, each tick after the pickup: a wait counts down; else with healing to
  come, below the cap a point of it (0x4e64a0, over the maximum allowed) and, still under the maximum,
  the class's delay (ONCC +0x5c), or at the cap the health set to it and the healing dropped; without,
  health over the maximum loses a point (the regeneration cheat 0x620134 instead gives the player one a
  second). Either of the last two waits 10. Then the shield shown (C+0x1b6) moves toward what is left by 2,
  and C+0x1bc counts while invisible (0 otherwise).
- A hypo (0x51c770 kind 2, from `ONrCharacter_HandlePickupInput` 0x4eda40 on the hypo key, Tab, when no
  weapon is dropped; event sound 4): only while health and healing to come don't pass the maximum. Under
  it, the dose; past the maximum, the over part scaled by the dose's share that goes over. At it, the over
  part. The healing becomes what that leaves above the health, the wait 0.
- `ONrCharacter_Invis_Update` 0x4ef530, late in `DoCharacterFrame` (after the matrices; for the dying in
  `ONrGameState_UpdateCharacters`): invisibility starting turns the effect on at 1; on, the body fades
  toward 0 while more than 120 ticks are left (0.0085 a tick, 0x52b33c), back toward 1 after (0.02,
  0x52b338); at 0 with none left it ends (only when the invisibility is taken away at once). The count
  goes down unless 0xffff (for ever). In a teleport's first half (0xde) from its action frame the body
  fades to none by its end (never above A+0x2818 as it was: 0 for one never invisible), with C+4 0x40000.
- A hit (`HandleAttackMask` 0x4e7740) zeroes the attacker's C+0x1bc and shows both to at least 0.6; a
  throw (`NewAnimationHook`) both, and both ticks.
- The AI's sight (`AI2iKnowledge_CheckVisible` 0x49adf0) passes over a character invisible for 90 ticks
  or more; a target lost from sight while invisible is pursued at once (0x490510).
- `ONiCharacter_Display_Body` 0x4e1d70: the body at A+0x2818 (blended below 1, none at 0) while the effect
  is on or C+4 0x40000; on, the INVIS texture (0x550e78) over the body scaled 1.4 (`TRrBody_DrawMagic`
  0x427770, the body's lower detail) at (1 - A+0x2818) x 56.1 (0x52b300) of 255.

## Holstering

A character has three weapon slots, C+0x194: slot 0 is the hand, slots 1 and 2 (C+0x198, C+0x19c)
are stowed. *WPrSlot_Swap* 0x51bd00 trades two slots. The weapon leaving the hand gets flags 0xc000
and loses 0x0800 (owned), so its firing and background events stop; the one coming in is owned.

- **The searches**: 0x51bc70 finds the last stowed weapon. It tries slot 1 first, then 2 down to 0,
  skipping the excluded slot. With none, it gives no weapon but the last empty slot other than 1.
  0x51bc20 finds an empty slot: slot 1 first if asked, else the last other than 1.
- **The key** (*ONrCharacter_Holster* 0x4edd20, the swap key; also a pickup with nothing to take): a
  weapon in hand that can be holstered (ONWC flag 0x02 clear) plays the holster overlay. That is
  type 0xa0 in state 7 (the animation's varient table) on overlay slot 1, the flag 1 when forced. An
  empty hand with a stowed weapon plays the draw instead (*ONrCharacter_Draw* 0x4edeb0): 0xa1, or
  0xa2 for a rifle (ONWC +0xcc bit 3). Both are refused while attacking or in one of the blocked
  animation types: 0x4c–0x5c, 0x60–0x65, 0x72–0x77, 0x7e–0x84, 0x8d, 0xc6, 0xd4–0xd7.
- **At the overlay's action frame** (DoCharacterFrame 0x4dfb43, TRAM +0x174), for the holster:
  - forced: the weapon goes to an empty slot (slot 1 first), or is dropped if there is none;
  - unforced: it trades places with the last stowed weapon, or goes into the empty slot the search
    gave.

  With a weapon in hand after that, its varient is set and its draw plays from its own action frame.
  For a draw, an empty hand takes the last stowed weapon.
- **The scripts**: `chr_forceholster` (0x4b86f0) and the cutscenes. `begin_cutscene` holsters the
  player's weapon unless its flag 2 ("weapon") is set (*ONrCharacter_ForceHolster* 0x4eff60), and
  `end_cutscene` draws one into an empty hand (*ONrCharacter_ForceDraw* 0x4effa0). Unless flag 0x20
  ("keepparticles"), `ONrGameState_BeginCutscene` 0x4fd070 also asks the particles' cleanup (0x458310,
  as `p3_removedangerous` does): the next particle update (0x45c830) goes over the classes in use, and
  for those with flags2 0x10 kills their particles, with flags2 0x08 sets their life to 0.01; the others
  of those classes, unless fading or chopped already, are marked expiring. Confirmed.
- **Saves** keep all three slots: each is put back (0x4d7bf0), slot 2 then 1 then 0, the stowed ones
  stowed.

Ported in `src/weapon.c`, `src/chars.c` and `src/save.c` (level 1: the cutscene after the training
holsters the new pistol and draws it at its end; Q then holsters it to slot 2 and draws it again).

## The AI's executor

How an AI character's wishes become its input (notes for step 1 of the AI's port, docs/re/port-plan.md):

- `AI2rUpdateAI` 0x489d40 runs the state, then `AI2rMovement_Update` 0x4a8550: two countdowns
  (C+0x1594, C+0x1598), then for a character with an active record 0x4a97b0 (the movement's own update)
  and **`AI2rExecutor_Update` 0x498550**.
- `AI2rExecutor_Move` 0x498b00 (direction, mode, facing, a flag) sets the active record's A+0x1a98
  (movement mode), A+0x1a9c (the move's kind, 0..4), A+0x1aa4 (the facing), A+0x1ab0.
- `AI2rExecutor_Update`: unless an override (A+0x1ab9), the mode (A+0x1a98, else the movement's
  default 0x4a8d00) goes to A+0x1aa0, and the kind (A+0x1a9c, 0..4) picks key bits (the switch's
  values are lost by the decompiler: read them from the disassembly). Unless the character holds its
  facing (0x4e3d00): turn-left (0x800000) or turn-right (0x1000000) bits turn it; otherwise the wanted
  facing (A+0x1ac8, else A+0x1aa4, plus the offset C+0x174) less the facing, clamped to the class's
  turn rate (ONCC+0x124 times 0x52a878, halved for modes 3..5 unless A+0x1ab0, or A+0x1aa8 once),
  sets the standing-turn flags (A+0x21e5 left under 0x52a040, A+0x21e6 right over 0x529dfc) and the
  desired facing (0x4e3110). With an override: crouch (A+0x1ac4 & 1: mode 4 or 5), else mode 6 or 7,
  or 3 when crouching. Then the override's attack (A+0x1acc) is checked against the animation, and
  `DoFilmUpdate` builds the input block (A+0x2158) from all of it, then `AI2rExecutor_UpdateAim`.
- **The key bits** (read from the disassembly; the jump table at 0x498acc): kind 0 forward
  (0x200000), 1 back (0x400000), 2 step left (0x2000000), 3 step right (0x4000000), 4 none. Mode 3
  adds crouch (0x10000000), modes 4 and 5 walk (bit 32). The default mode (0x4a8d00) by the alert level
  C+0xf94 (lull, low, medium, high, combat): 4, 4, 6, 7, 7 when hurrying (`AI2rIsHurrying` 0x48b020),
  else 4, 4, 5, 5, 5; 7 for a character that isn't the AI's.
- **The turn**: the rate is ONCC+0x124 times 0.0628 (0x52a878, a hundredth of a turn). The facing and
  the desired facing both become the facing plus the clamped turn (0x4e3110): the AI turns itself.
- **The input block** (the stack block at 0x498a94 handed to `DoFilmUpdate` 0x4ed420): no mouse; held
  the keys; not held their complement; gone down: jump, crouch and the action (bit 33) whenever held;
  gone up: last tick's keys (A+0x2188) that aren't held now.

Ported in `src/ai.c` (the overrides, the attack override and the aim not yet), driven by a stand-in
mover (straight to a point) until the movement layer is; `ai2_movetoflag` uses it. `ai_test`: a thug
turns round, walks 60 to a point and faces the way asked.

## Character setups (AISA)

`chr_create <id>` (*AI2iScript_ChrCreate* 0x4b7710) makes a character from the level's setup array,
AISA, linked at ONLV +0x5c (*confirmed*). It is a u16 count at +0x16, then 0x160-byte records from
+0x18: +0x00 the name (empty: `ai_<slot>`), +0x20 the script id (0x506d50 looks it up), +0x22 the flag id,
+0x24 the flags, +0x26 the team, +0x28 the ONCC, +0x4c the scripts and +0x150 the ONWC. The
character stands at the flag (*ONrLevel_Flag_ID_To_Flag*), if 15 above it is inside a BNV
(*AKrNodeFromPoint* 0x440600). *ONrGameState_NewCharacter* 0x4dac50 takes the setup when there is no
CHAR object. *AI2rInitializeCharacter* 0x48a6b0 then gives it the default combat and melee and alert
lull, with job alert low and no alarm groups; script id 0 or flag 1 means no AI. In every level's
AISA the names, scripts and weapons are empty. The cutscenes use these characters as the films' actors.
