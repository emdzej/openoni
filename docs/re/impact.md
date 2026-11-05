# Impact effects

What a hit makes: a bullet on a wall, a kick on a body. An impact type (Impt) meeting a material
(Mtrl) picks the sounds and particles of the game's table of effects. Ported in `src/impact.c`, with the
particle side in `src/particle.c` (`p3_create_effect`, the impact action). Confirmed against the
Windows exe unless marked.

## The trees

The Impt (94) and Mtrl (30) instances each form one tree under a single root, by their parent links.
`MArImpacts_PreProcess` 0x4874a0 and `MArMaterials_PreProcess` 0x487aa0 number them: an index is the
node's place in a depth-first walk from the root, a node before its children, siblings in `strcmp`
order of their names. A material's Mtrl+0 is then its index.

- Impact types: the "Blunt" type and everything below it are marked blunt (0x4871b0).
- Materials: "Glass" and below are glass, except "Unbreak_Glass" and below.
- Accessors: type by name 0x487140, material by name 0x4871d0, a type's parent 0x487190, a
  material's parent 0x487240, a material's depth 0x487220.

## The table (ONIE)

The BINA instance whose data starts with the bytes "EINO" (read at 0x4fe9b0). After an 8-byte offset,
a header of seven u32: the version (2), then the counts of impact names (94), material names (30),
particles (84), sounds (288), entries (361) and material indices (223). The parts follow in that order,
and their sizes add up to the instance's size exactly:

| Part | Size each | Contents |
|---|---|---|
| impact names | 0x84 | the type's name (remapped to the tree's index at load) |
| material names | 0x84 | the material's name, likewise |
| per-type lookup | 8 | type, count, first material index |
| particles | 0x54 | the class's name (64 chars), the effect spec (P3rCreateEffect's) at +0x40 |
| sounds | 0x30 (0x28 before version 2) | the impulse's name; at +0x24 the impulse, +0x28 flags (bit 0: an AI sound too), +0x2a the AI sound's type, +0x2c its radius |
| entries | 0x14 | type u16, material u16, component u16 (0..2), modifier u16 (0 any, 1 heavy, 2 medium, 3 light), particle count u16, sound u32 at +0xc, first particle at +0x10 |
| material indices | 8 | material, count, first entry |

The material indices of a type are sorted by material at load. At each level's start the particles'
classes and the sounds' impulses are looked up again, once the level's own classes are loaded. Three
classes named by the table exist in no file (`h2h_thuflash_e01`, `h2h_tclflash_e01`,
`h2h_griflash_e01`): their particles are skipped, as the original's are.

## The lookup (0x4fe7f0)

For each of the three components, find an entry for the type on the material with the modifier
(an entry's modifier 0 matches any). Climb the type's parents from the type itself. At each type, climb
the material's parents, walking the type's sorted material list down from its end.

- Not blunt: the first entry found for a component is kept.
- Blunt: the entry on the deepest material is kept.

## ONrImpactEffect (0x4fe5b0)

For each component with an entry:

- the impulse sound plays at the location's position (0x512480). Its volume is 0.7·|v|²/iv² + 0.3
  when the speed is under the impulse's +0x84 (iv), full otherwise;
- with the sound's flag bit 0, an AI sound (`AI2rKnowledge_Sound` 0x49a8a0) the guards can hear (**not ported**: no
  hearing yet);
- each particle's spec is created with P3rCreateEffect at the location.

## P3rCreateEffect (0x456030)

The spec (0x14): class, orientation (0..3), location (0..5), then a union. For locations other than
decals it holds an offset; for decals, the static flag and the random-rotation bytes. The location
struct the callers pass is 0x6c bytes (mirrored by `P3EffectLoc`).

- Location 0, 4, 5: the position as given; 1: the position plus offset × normal; 2, 3: untouched.
- Orientation, a jump table:
  - 0: the parent's matrix if there is one, else along the direction, else the identity;
  - 1: reversed (the matrix's columns 1 and 2 negated);
  - 2: reflected (d − 2·(d·n)/(n·n)·n);
  - 3: along the normal, with the parent matrix's up (4), else 8.
- Decals need location 4 and a hit of kind 1 (a wall); anything else kills the particle. The decal
  takes:
  - the scale from the class's D_SCALE (0xa8) and the y scale from D_YSCALE (0xc4);
  - the wrap angle from D_DECAL_WRAP (0x204, 60° by default, turned to radians);
  - the axes: ax = normalize(normal × up), falling back to (−n.z, 0, n.x), then (0, 0, 1), else the
    particle dies; ay = ax × n; then a random rotation;
  - alpha × 255, rounded; the tint D_TINT;
  - the flags: 1 static, 4 for the class's flags2 0x400000, 0x10 for flags2 0x200000.

  A return of 0 from P3rCreateDecal is success: the particle then gets flag 0x800 (dynamic) or 0x1000
  (static), and its decal block's second word the particle's handle. Otherwise the particle dies. The
  decals themselves: [decal.md](decal.md).

## The particle actions

The action table at 0x5374a0 holds entries {type, parameter count, name[16], function}.

- `P3iAction_CollisionEffect` 0x465840: creates an effect spec at the particle's place or its last
  collision.
- `P3iAction_ImpactEffect` 0x465ac0 (not analysed by Ghidra; read from the bytes). Its type is set in
  P3rUpdateActionData 0x457240 (variable type 60: `MArImpactType_GetByName(name) & 0xffff`).
  - The modifier is parameter 1 (under 4).
  - On a collision, the place is the hit point + 0.2 × normal.
  - The material: for a wall, its texture's material; for a character, `ONrCharacter_GetMaterialType`
    0x4e6200 with the body part nearest the point (0x4ef9e0, by the parts' sphere centres, 7 by
    default).

## ONrCharacter_GetMaterialType (0x4e6200)

Super_Shield for a character with flags bit 0x40000000, or with its class's super level (ONCC+0xc8e)
over 0.5. Shield while the force shield is up (unless ignored). Otherwise the body's material for the
part (the CBPM linked at ONCC+0xc3c, one Mtrl link a part), and "Character" if none.

## Not yet

- The melee's impacts (ONrCharacter_PlayAnimationImpact) and the AI's hearing of the impact sounds.
