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

## The environment's colours

The environment isn't lit at run time: each quad's corners carry a shade (`AGQG` +0x20..+0x2c, made
when the level was built), and that is the whole of its light. `MSrGeomContext_Method_Env_DrawGQList`
0x40ff30 (the draw engine's method +0x30, from `M3rEnv_DrawGQList` 0x41a930 for the solid list and then
the transparent one) sets, before its quads:

| Draw state | Value | What |
|---|---|---|
| int 0 | 3 | textured |
| int 1 | 0 | |
| int 7 | 0xffffffff | the constant colour: white |
| int 9 | 0xff | the alpha (the transparent list takes it per quad from the texture map's +2 word) |
| int 0x11 | 1 | the colours from the quads' shades |

Each quad then sets its texture (int ptr 4; the "selected" texture at +0x210 in two debug cases) and
whether it needs clipping (int 0xe). The OpenGL back end's commit (0x405410) turns int 0x11 = 1 into its
mode 6 (+0x620), and the triangle drawer (0x405b90) draws mode 6 with each corner's shade as
`glColor4ub(r, g, b, alpha)`, the texture coordinates and the point.

What decides the brightness, checked against the exe:

- **The texture environment**: the only `glTexEnv` call (0x40da4e) sets `GL_DECAL` on unit 1, for the
  combiners' second texture. Unit 0 keeps GL's default, `GL_MODULATE`: texel × shade, unscaled.
- **The texels**: the textures are converted to 8-bit RGB or RGBA before the upload
  (`IMrImage_ConvertPixelType` 0x44ac20, through the table at 0x535a90 of (from, to, function)
  triples). RGB555 and ARGB1555 widen each 5-bit channel by a shift of 3 (0x44af30, 0x44afa0, 0x44b0b0:
  31 becomes 248, the low bits zero); ARGB4444 repeats each nibble (0x44b020: 15 becomes 255).
- **No reflection maps**: mode 6 never takes the two-texture path. Modes 4 and 5 (a texture's
  reflection map in one pass, through `GL_DECAL` or the NV register combiners, 0x40d640 with the
  context's +0x584) need int 0x11 = 0, so only the geometry has them.
- **The gamma**: at the default 0.5 the display ramp is the identity.
- **The fog**: see below. It greys the far parts (past about 80 to 200 units), not the whole picture.

The port (`src/render/env_draw.c`, `src/im.c`, the shader's texel × colour) does the same at each step.
An earlier note that the port's environment was darker than the original's came from an impression,
not a measured capture, and nothing in the draw path accounts for it. A capture of the original at the
same place, same gamma and same settings is the check that remains (port plan, step 4).

## The fog

The OpenGL back end's fog is `GL_LINEAR` (0x40c748), set from the GL context at each frame's start
(0x4058a3: `GL_FOG_START` from +0x5d8, `GL_FOG_END` from +0x5dc, `GL_FOG_COLOR` from +0x5f4 when they
change). Its coordinate is the depth on the screen: the back end draws points already projected
(`glVertex3f(x, y, -z)`, z over w from `MSrTransform_PointListToFrustumScreen` 0x418100), so GL's eye
distance is that z. The projection (0x41aeb0) is GL's usual one, near 2 and far the level's far plane
(`gs_farclipplane_set`): z = (f + n − 2 f n / d) / (f − n) at a distance d along the view. With n 2 and f
5000, 0.975 is about 80 units and 0.99 about 185.

- **The values**: `gl_reset_fog_parameters` 0x40ca50 (`M3rDraw_Reset_Fog`, from `ONrLevel_Unload`) sets
  the colour to 0.25 grey, the start to 0.975 and the end to 1, and stops the changes. The level's scripts
  then set their own (`gl_fog_red`, `_green`, `_blue`, `_start`, `_end`, +0x5f4.. and +0x5d8, +0x5dc) and
  change them over frames (`gl_fog_start_changeto` 0x40cb20, `gl_fog_end_changeto` 0x40cb90, stepped by
  `gl_fog_update` 0x40cc00).
- **What takes it**: draw state int 0xf (on by default, 0x5083b3) turns `GL_FOG` on (the commit,
  0x405440). It is off for the sky (0x4fb69f, around `ONrSky_Draw`), the 2D draws (0x4f2041, 0x4f5284,
  0x4fbb3a) and the particle classes with flags2 0x100000 (0x46059d). A texture with flag 0x400 drawn
  with the default blend is additive and turns `GL_FOG` off while it is bound (0x40d640).
- **The drivers without it**: with "3Dfx" or "formac" in the renderer's name the fog is never turned on
  (+0x58e, the console's `gl_fog_disabled`, 0x40c92b).
- **The particles' fade**: `gl_calculate_fog_factor` 0x40dcf0 projects a point and gives 0 without fog
  (start = end, start not below 1, end not above 0) or at or before the start, (end − z) / (end − start)
  between, and 1 at or past the end. The sprite, vector, geometry and contrail classes (0x4617cc,
  0x461056, 0x460b21, 0x462316) multiply their alpha by it when it is above 0 (at most 255). So a
  particle fades as it goes into the fog, and one before the fog's start keeps its alpha.

The port (`src/render/gl_dev.c`): the shader fogs on the fragment's depth (`gl_FragCoord.z` × 2 − 1, the
same z, since the port's projection is the original's) in every 3D mode but the additive and the sky;
`gl_dev_fog_factor` is 0x40dcf0. Not ported: the geometry class's fade (the port's geometry takes no alpha), the flags2 0x100000 classes (the port doesn't draw the sky's
particles apart) and `gl_fog_disabled`. Inferred: the contrail fades each end at its own point.
