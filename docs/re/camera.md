# The camera (CA)

`Oni_Camera.c` (the path string at 0x54d658), 0x4d59a0–0x4d7b70. The port: `src/camera.c`, test
`tests/camera_test.c`.

## Where it runs

- **State:** 0x84 bytes at 0x6364c0. `ONrGameState` +0x80 points at it, set and zeroed by 0x4d5a10, which
  `ONrLevel_Load` 0x5059d0 calls (at 0x50623d). The same function sets the target (0x506567) and calls
  the reset (0x50656c). 0x4f3eb0, a script command that picks the player, does the same.
- **Update:** `CArUpdate` 0x4d5ca0 (name the port's), called from `iComputeDeltaTicks` 0x4fafb0:
  - once a tick, after `ONrGameState_ProcessHeartbeat` 0x4fa6a0, with ticks 1 and no input records
    (0x4fb141);
  - once a frame, with ticks 0 and the frame's input records (0x4fb170).

  Its arguments are ECX = ticks, EDX = record count, and on the stack the records (0x30 bytes each).
- **Hand-off:** when the mode's update returns true, location, view and up go to the active Motoko camera
  (0x63112c) through 0x41b3f0. That function stores them at camera +0x08/+0x14/+0x20, sets the dirty
  bytes +4 and +6, and calls the draw engine's camera method.
- **Jello reset:** before the mode runs, 0x41aab0 clears flag 0x20 on every quad in Motoko's visible
  list (0x6c4568 count, 0x6c456c indices).
- **Camera film:** `ONrGameState` +0xf8 (recording) and +0xf9 (playing) switch to another path, the
  `camera_record` / `camera_play` / `camera_stop` commands, with the track at +0xfc and the frame at
  +0x100. 0x4d6070, 0x4d6010, 0x4d5b50 and 0x4d7a90 belong to it. Not ported.

## State layout

| Offset | Mode | Field |
|---|---|---|
| 0x00 | all | mode: 0 orbit, 1 follow, 2 animate, 3 manual, 4 interpolate |
| 0x04 | orbit | angle (rad) |
| 0x08 | orbit | stop angle (rad, < 0 none) |
| 0x0c | orbit | speed (rad/tick) |
| 0x04 | animate | the OBAN play context (`ObanPlayer`: anim, state u16, frame i16, step, stop, time) |
| 0x04 | manual | fly flag (u32) |
| 0x04 | interpolate | start location, view, up (9 floats) |
| 0x10 | follow | canter (current height offset) |
| 0x14 | follow | distance (current) |
| 0x18 | follow | snap byte: go straight to the goal. Zeroed by the reset; nothing found sets it |
| 0x28 | interpolate | target location, view, up |
| 0x4c | interpolate | tick |
| 0x50 | interpolate | tick count |
| 0x54 | all | the character followed |
| 0x58 | all | location |
| 0x64 | all | view (unit) |
| 0x70 | all | up (unit) |
| 0x80 | all | busy byte (`cm_wait`, the `_block` commands) |

## The functions

The names come from the Intel Mac build, from neighbouring functions. Names marked * are the port's own.

| Address | Name | What it does |
|---|---|---|
| 0x4d5990 | CArGetTarget* | returns +0x54 |
| 0x4d59a0 | CArEnableJello | sets the jello switch 0x54d64c |
| 0x4d59b0 | CArBuildUpVector | up = normalize(view × (view.z, 0, −view.x)). Every mode builds it inline |
| 0x4d5a10 | CArLevelBegin* | the state pointer, zeroed |
| 0x4d5a30 | CArInitialize | the variables below and `cm_jello` (0x4d5b30: on if the argument > 0) |
| 0x4d5ca0 | CArUpdate* | above |
| 0x4d6220 | CArOrbit_Update* | below |
| 0x4d6580 | CArAnimate_Update | steps the OBAN `ticks` times (0x43a620) and takes the matrix at the context's frame. Location is the translation, view M·(0,0,−1), up M·(0,1,0) (0x433ae0, which normalises). Busy while frame < frames − 1 |
| 0x4d6660 | CArOrbit_Enter* | angle 0, then stop and speed; mode 0; busy cleared |
| 0x4d66a0 | CArFollow_Update* | below |
| 0x4d6b10 | CAiTrackJello | below |
| 0x4d7110 | CArFollow_Get_GoalPosition | below |
| 0x4d72b0 | CAiFollow_Divisor* | `max(base − ln(max(|d|²·k, 1))·slope/2, 2)` |
| 0x4d7320 | CAiFollow_Approach* | p += (to − p) / divisor |
| 0x4d73d0 | CArReset* | follow; see below |
| 0x4d7510 | CArAnimate_Enter* | zeroes the context, sets the OBAN, then 0x4394e0 and `OBrAnim_Start` 0x439580 (both pick the start frame); mode 2, busy |
| 0x4d7560 | CArManual_Enter* | fly flag 0, mode 3, not busy |
| 0x4d7590 | CArManual_Update* | below |
| 0x4d77c0 | CArInterpolate_Enter* | target from a matrix. Location is the translation, view M·(0,0,−1), up rebuilt level from the view (the matrix's up is computed, then overwritten). Start = current; tick 0, count; mode 4, busy |
| 0x4d78e0 | CArInterpolate_EnterView* | the same, with the 9 target floats given (caller 0x4b9492) |
| 0x4d7930 | CArDetach* | an interpolation from the current view to itself, count 1 |
| 0x4d7980 | CArInterpolate_Update* | below |
| 0x4d7b60 | CArIsBusy* | +0x80 |

0x4d7b80 and 0x4d7ba0, after the range, belong to the character code.

### Follow (0x4d66a0)

`ticks > 0` and the target has an active part (0x4f1180):

1. **Canter goal.** The character is armed when it holds a weapon (C+0x194) and A+0x1ca0 & 0x7800 is
   non-zero. The goal is `cm_canter_weapon` if armed, otherwise `cm_canter_unarmed`. Pitch p = A+0x21c0:
   - p ≥ 1.2: goal × 0.2;
   - 0.5 < p < 1.2: goal × (1.2 − (p − 0.5)·1.4285713);
   - armed and p < −0.5: goal × (1 + (−p − 0.5)·1.4285713).
2. **Canter step.** The canter moves toward the goal by 0.12 a tick and doesn't pass it.
3. **Distance.** `cm_distance`. When armed and p ≤ −0.5, it is `(max(p + 1, 0) + 0.5)·cm_distance`
   instead.

Then, with `ticks > 0`:

- **Goal and target.** The goal G comes from 0x4d7110. The target T is the feet + (0, `cm_height`, 0).
- **Snap byte set:** location = G, view = T − G.
- **Otherwise:** P = location, F = location + distance·view. Each tick:
  - P += (G − P)/divisor(8, 1, 0.85);
  - F += (T − F)/divisor(8, 3, 0.85).

  Then location = P and view = normalize(F − P). The up is the level one.

The function returns true when `ticks > 0`. It calls the jello in every case, so the jello also runs on
the frame call with no ticks.

**No collision:** the follow camera never tests the walls. It goes through them, and the jello makes
what's in between see-through. The port doesn't add a wall test: that would change the original's
behaviour.

### The goal (0x4d7110)

With an active part, dir = A+0x21b0 (the aim direction). If the pitch is above 0.9, dir becomes
`lerp(dir, (0,1,0), clamp((p − 0.9)·0.6707964, 0, 1))`, normalised. The offset is −distance·dir, and is
zero without an active part.

The goal:

- x = feet x + offset x;
- y = max(`cm_height` + canter + offset y + feet y, feet y + 0.5);
- z = feet z + offset z.

### Reset (0x4d73d0)

1. Zero +0x04..+0x1b.
2. Canter: `cm_canter_weapon` if the character holds a weapon (only C+0x194, unlike the follow update),
   otherwise `cm_canter_unarmed`. Distance: `cm_distance`.
3. Mode 1.
4. Location = goal, view = T − goal, up level. Not busy.

### Orbit (0x4d6220)

1. **Angle.** It advances by clamp(ticks·speed, −2π, 2π). If the stop angle is ≥ 0 and the stop, or the
   stop ± 2π, lies between the old and the new angle (ends included), the angle becomes the stop. Then it
   is wrapped once into [0, 2π).
2. **Direction.** a = angle + A+0x21bc (if active), then + the facing (0x4e33a0: C+0x170 − C+0x174),
   each wrapped. v = (sin a, 0, cos a)·`cm_distance`.
3. **Height.** v.y −= `cm_canter_unarmed` (always the unarmed one).
4. **Placement.** location = feet − v, plus `cm_height` in y; view = normalize(v); up level.
5. The busy byte is never set, so `cm_orbit_block` never waits for an orbit that stops.

### Interpolate (0x4d7980)

1. tick += ticks. At or past the count, tick = count and busy is cleared.
2. t = (sin(tick/count·π − π/2, wrapped) + 1)/2.
3. location = lerp(start, target, t); view = normalize(lerp(start view, target view, t)); up level.

The command clamps the count to at least 1, so `cm_interpolate X 0` is a cut on the next tick.

### Manual (0x4d7590)

The debug fly camera. Developer key 0x18 in 0x4f6200 (0x4f65e2) cycles it:

- not manual → manual (0x4d7560);
- manual → fly flag set;
- fly → reset.

Each input record (offsets in the 0x30-byte record):

- forward f = 5·[+0x10], side s = 5·[+0x18];
- yaw = [+0x08]·−0.10472, pitch = [+0x0c]·−0.10472;
- with the fly flag: yaw −= [+0x24]/60, pitch −= [+0x28]/60.

The side vector is normalize(up × view). The camera then moves:

- location += f·view + s·side, with y += [+0x14];
- the forward part counts in y only with the fly flag.

Negative angles get 2π added. The view turns about up by the yaw, then about the side by the pitch
(`MUrPoint_RotateAboutAxis` 0x432f70: the axis-angle matrix 0x4345f0, a standard right-handed rotation).
The view is normalised and the up rebuilt.

The meanings of the record's fields are inferred.

### The jello (0x4d6b10)

When the jello is on (0x54d64c):

1. **Probes.** side = (up × view) scaled: x·3, (y + 2)·3, z·3. Count 0x54d654 = 1 (at most 5, no
   variable). Probe i aims at head = feet + (0, 12, 0), plus side:
   - i = 1 or 3: side.y is negated first;
   - i = 2: all of side is negated;
   - i ≥ 4: no side.
2. **Sweep.**
   - `cm_jello_radius` > 0: a sphere of that radius from the camera, along the vector shortened to
     |d| − radius (at least 1). `AKrCollision_Sphere`* 0x441cb0, skip flags 0x42000, at most 0x200 − n
     quads.
   - Otherwise: `AKrCollision_Point` 0x441c80.

   The hits are the 0x48-byte records at 0x6a90a8 (count 0x6a906c); their first field is the quad.
3. **Objects.** Each physics context with flag 2 at +0x17 is tested with `PHrCollision_Point` 0x4d5280.
   Those hit get 0x40 at +0x17. Not ported.
4. **Per quad.** Each quad whose highest corner reaches C+0x190 + 1.5 is marked with flag 0x20, and
   `cm_jello_amt` goes into its AGQR entry +2. It is left alone when `cm_plane_test` is on and
   `(feet && cam) == (head || feet || cam)`, where each side is plane·point + d > 0 for:
   - the camera;
   - the feet (C+0x148 x, C+0x190, z);
   - the head (feet y + 15).

   That skips when camera and feet are both in front, or when nothing is.

   **Quirk:** the plane index from AGQC is used as stored, times 16. Bit 31, which marks a negated
   plane, falls out, so the plane is taken unnegated.

The port's sweep is a stand-in, a brute force over the quads' AGQC boxes. It finds the quads the point or
sphere touches, in quad order. The port clears flag 0x20 on the quads it marked itself, not on Motoko's
visible list.

## Variables and commands

| Variable | Type | Initial | Address |
|---|---|---|---|
| `cm_plane_test` | bool | 1 | 0x54d634 |
| `cm_height` | float | 15 | 0x54d638 |
| `cm_distance` | float | 33 | 0x54d63c |
| `cm_canter_weapon` | float | 7 | 0x54d640 |
| `cm_canter_unarmed` | float | 8 | 0x54d644 |
| `cm_jello_radius` | float | 12 | 0x54d648 |
| `cm_jello_amt` | int | 20 | 0x54d650 |

| Command | Function | Arguments | What it does |
|---|---|---|---|
| `cm_jello` | 0x4d5b30 | `mode:int{0 \| 1 }` | |
| `cm_anim` | 0x4bc100 | `cam_spec:string{"move" \| "look" \| "both"} anim_name:string` | The spec is never read. A missing OBAN is a script error |
| `cm_anim_block` | 0x4bc160 | same | |
| `cm_interpolate` | 0x4bbfe0 | `anim_name:string num_frames:int` | Takes the OBAN at frame 0 (flags 0), then count max(n, 1). Calls 0x4f1f40 first |
| `cm_interpolate_block` | 0x4bc060 | same | |
| `cm_orbit` | 0x4bc210 | `speed:float [stopangle:float \| ]` | Degrees: speed per tick, stop default −1 (none) |
| `cm_orbit_block` | 0x4bc250 | same | |
| `cm_detach` | 0x4bbfd0 | | |
| `cm_wait` | 0x4bc1d0 | | Waits while busy |
| `cm_reset` | 0x4bc200 | `[maxspeed:float \| ] [maxFocalAccel:float \| ]` | The arguments are never read. Calls 0x4f1f40, then the reset |

The `_block` forms wait while the camera is busy, then start their move and return. They don't wait for
their own move; a `cm_wait` after them does that.

0x4f1f40 sets A+0x221c = 6 on every active character; its meaning is open (see
[character.md](./character.md)). `cm_barabus` (strings at 0x5488f0) is elsewhere and not ported.
