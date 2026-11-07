# The geometry's lights and reflection maps

How the characters, the weapons, the powerups and the level's objects are lit and how their reflection
maps are drawn. The environment isn't lit this way: its light is baked into its quads' shades. The
software geometry engine ("motoko software", `MSrGeomEngine_Initialize` 0x419570) transforms and lights
each geometry's vertices. The OpenGL draw engine then draws its triangles. Status: read from the exe
(confirmed), except where marked. Port: `src/render/geom_draw.c`.

## The lights

The engine keeps its lights in its private context (the pointer at 0x560a28):

| Offset | What |
|---|---|
| +0x60 | The ambient: r, g, b (from 0 to 1) |
| +0x6c | The directional lights' count, at most 4 |
| +0x70 | The lights, 0x18 bytes each: the direction (normalised), then the colour |

They are set through the engine's method table (0x6c85c0, its pointer at 0x560a58):

- +0xc, `0x4135c0`, sets the ambient.
- +0x10, `0x4135e0`, sets the count and the lights. The caller gives each light a direction and a colour.
  The setter takes the direction's length from x, y and x again: the direction's z is never read, and
  its x is stored in place of it. The setter divides the direction by that length and *multiplies* the
  colour by it. The port keeps both quirks.

`ONrGameState_SetupDefaultLighting` 0x4f4a30 sets the game's lights. It runs once in
`ONrGameState_PrepareGeometryEngine` 0x4f4b10, and again after each object with its own shade:

| Light | Direction as given | Colour as given | As stored |
|---|---|---|---|
| 0 | (-0.5, -1, -0.5) | 0.7 | (-0.41, -0.82, -0.41), 0.857 |
| 1 | (0.5, 0.5, 1) | 0.3 | (0.577, 0.577, 0.577), 0.26 |

The number of lights comes from the graphics quality
(`ONrMotoko_GraphicsQuality_NumDirectionalLights` 0x508490: 1, 1, 2, 2, 2 for the qualities 0 to 4), and
so does the ambient: 0.85 with no lights (0x52b37c), 0.65 with one (0x52b390) and 0.55 with two
(0x52b394).

`OBiDraw` 0x438fb0: an object with its own shade (its flag 0x8000, which `obj_shade` sets) is drawn after
`0x4fb290` sets its shade (+0x20, 0x00rrggbb, each channel × 1/255, 0x529d98) as the ambient and no
directional lights. After it, `OBiDraw` sets the default lights again.

## A vertex's colour

`0x417690` lights every vertex of a geometry with `0x417f00`, from the vertex's point and normal in the
world:

- colour = ambient + Σ over the lights of max(0, −direction · normal) × the light's colour, for each
  channel;
- each channel × 255, clamped to 255, rounded (`MUrUnsignedSmallFloat_To_Uns_Round` 0x4340a0).

A geometry with the flag 0x10 (`M3GM`+0) or a texture with the flag 0x800 is not lit: its vertices are
white.

`0x417780` lights only the visible vertices of a geometry the frustum cuts. It also handles the texture
flags 0x8000, 0x10000 and 0x20000, which animate the colours with sine tables. The port lights every
vertex and doesn't animate the colours.

## The reflection maps

For a texture with the flag 0x200, `0x417f00` also computes each vertex's reflection coordinates into the
buffer at +0x258. The view is d = point − the camera's location. It is mirrored about the normal:
r = d − 2 (d · n) n. The coordinates are a sphere map: m = 0.5 / |(rx, ry, rz + 1)|, u = rx m + 0.5,
v = ry m + 0.5. d isn't normalised, so the +1 is in the world's units.

`M3rGeometry_Draw` 0x41a750: when a texture has a reflection map (`TXMP`+0x90) and the graphics quality is
1 or more (0x508540), the geometry is drawn twice. The draw engine's mode +0x620 is set to 7, then to 8,
then restored (0x4057c0, 0x4057e0, 0x4057f0). The second pass runs only if the first succeeds. The two
cases of the OpenGL triangle drawer (0x405b90) are:

- **7**: the reflection map with the reflection coordinates and the vertices' colours, not blended
  (`GL_ONE`, `GL_ZERO`);
- **8**: the texture with its own coordinates and the colours, blended with `GL_ONE`, `GL_SRC_ALPHA`.

What is drawn is texture × light + reflection × light × the texture's alpha. The texture's alpha masks
the reflection.

Inferred: `0x41c5b0` (the draw engine's method +0x4c), which skips the two passes when it is true, is
false on the OpenGL engine.
