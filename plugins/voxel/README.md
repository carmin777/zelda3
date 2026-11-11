# Voxel diorama renderer plugin (R-4/R-5)

Builds a 3D diorama from `FrameDesc`. The scene uses the **whole current
room/area** that the game already has loaded (the dungeon room, up to 2×2
screens, or the 32×32-tile overworld screen), never neighboring rooms or
screens. Each 8×8 tile is classified by collision and by the drawing itself:

- **flat** — ground and water: one textured quad.
- **top** — roofs and ledges: the art lies on the top face; the sides repeat
  the tile drawing in 8-pixel bands, darkened toward the base.
- **upright** — walls, trees, fences, signs and facades: the south face
  reconstructs the 2D drawing (the fold-up rule). A solid upright cell also
  keeps the tile drawing on top; no cell replaces its art with a mean colour.
- **stack** — tree and hedge crowns: every opaque pixel becomes a column and
  the silhouette follows the art. Light-World crowns round their top with a
  pixel-space chamfer distance field across the connected crown cells. Dark
  World trees keep one 8×8-cell dome each. A one/two-cell strip around a
  plateau stays flat instead of raising a comb of thin cards.
- **prop** — an opaque ground tile with a small painted object (the Dark
  World gravestones): the ground stays flat and the silhouette becomes a
  small upright billboard. Only a profile exception requests this split;
  running it on every opaque tile turns cliffs, roofs and stairs into thin
  stretched cards.

Solid tall objects are split into a top band (roof/canopy) and a facade band.
Transparent props are extruded per pixel: a bush stays a bush and the gap
between two fence posts stays open. Vertical faces taller than one and a half
tiles repeat the tile in 8-pixel bands instead of stretching the whole drawing
into the face; the base stays darker than the top. The top of any cell always shows the
2D pixel. Sprites with an owner are camera-facing cards leaning back,
positioned with the game's `z`; BG3/HUD and owner-less sprites stay 2D on top.

Rendering uses a real perspective camera, a depth buffer, a directional light,
a 1024² shadow map, an optional tilt-shift post-process (world only) and
data-only profiles with per-tile/room exceptions. Mode 7, force blank, mosaic
and special screens fall back to the reference renderer.

The offscreen EGL/OpenGL context works in `--headless` without `DISPLAY`; if
EGL/OpenGL fails, the plugin logs a message and stays a reference-renderer
pass-through.

## Build and run

```sh
make -C plugins/voxel

./zelda3 --render-plugin plugins/voxel/voxel.so zelda3.sfc

# headless screenshot
./zelda3 --config tests/regress.ini --headless --no-mods \
  --render-plugin plugins/voxel/voxel.so \
  --replay-ref 6 --frames 300 --screenshot .agents/shots/overworld.ppm \
  zelda3.sfc
```

The `.so`/`.dylib`/`.dll` is a build artifact and is not committed.

## In-game controls

| Key | Action |
|---|---|
| F11 | voxel on/off (off falls back to the reference renderer) |
| Alt+1 / Alt+2 / Alt+3 | camera preset 15° / 35° / 50° (eased) |
| Alt+Left / Alt+Right | rotate the camera (yaw, ±60°) |
| Alt+Up / Alt+Down | tilt continuously (elevation, 10–89°) |
| Alt+= / Alt+- | zoom in/out (0.5–2.5×) |
| Alt+0 | reset camera, light and shadows to the profile |
| F12 | shadows on/off |
| Shift+F11 | directional light on/off |

The profile's default elevation is 54° (a 36° tilt). A preset changes the
target; the plugin eases
toward it across frames. The camera frames the current room and lets the room
geometry fill the tilted trapezoid; it never samples a neighboring screen.

## Environment variables

| Variable | Default | Meaning |
|---|---:|---|
| `ZELDA3_VOXEL_CAMERA_ANGLE` | 54 | camera elevation (5–89; 54 = 36° tilt) |
| `ZELDA3_VOXEL_CAMERA_INDOOR_ANGLE` | 45 | maximum elevation used inside rooms |
| `ZELDA3_VOXEL_CAMERA_DISTANCE` | 1.5 | perspective distance; larger is flatter |
| `ZELDA3_VOXEL_CAMERA_HEIGHT` | 1.0 | vertical scale of the geometry |
| `ZELDA3_VOXEL_CAMERA_FOLLOW` | 1 | follow Link's ground position (not the jump) |
| `ZELDA3_VOXEL_CAMERA_YAW` | 0 | rotation around the vertical axis, degrees |
| `ZELDA3_VOXEL_CAMERA_ZOOM` | 1.0 | 1.0 = default framing, larger = closer |
| `ZELDA3_VOXEL_HEIGHT_SCALE` | 1.0 | multiplier for all column heights |
| `ZELDA3_VOXEL_SIDE_SHADE` | 0.86 | brightness of vertical faces |
| `ZELDA3_VOXEL_EXPOSURE` | 1.04 | world-light gain |
| `ZELDA3_VOXEL_STACK_HEIGHT` | 2.2 | tree canopy dome height, blocks |
| `ZELDA3_VOXEL_MESH_DETAIL` | 1 | cut-out/stack pixel step (1, 2, 4, 8) |
| `ZELDA3_VOXEL_STATS` | 0 | log mesh vertex statistics at shutdown |
| `ZELDA3_VOXEL_NO_CACHE` | 0 | disable the per-room mesh cache |
| `ZELDA3_VOXEL_SPRITE_SCALE` | 1.0 | vertical size of sprite cards |
| `ZELDA3_VOXEL_LIGHTING` | 1 | directional light |
| `ZELDA3_VOXEL_SHADOWS` | 1 | shadow-map pass |
| `ZELDA3_VOXEL_SHADOW_STRENGTH` | 0.15 | shadow darkening (0–1) |
| `ZELDA3_VOXEL_TILT_SHIFT` | 0.0 | world-only miniature blur (0–1) |
| `ZELDA3_VOXEL_SCALE` | 1 | output scale (1, 2 or 4) |
| `ZELDA3_VOXEL_PROFILE` | — | profile JSON path |
| `ZELDA3_VOXEL_NO_GL` | 0 | force the reference-renderer fallback |

`ZELDA3_VOXEL_SHADOW_LENGTH` is still accepted for older profiles; the shadow
map derives the direction from the light, so it no longer changes the result.

## Profile

The default profile is `plugins/voxel/default-profile.json`; without it the
plugin uses the built-in table. Heights are in 8-pixel blocks (1.0 = one tile).
`profiles/flat.json` and `profiles/exaggerated.json` inherit the default
through `extends`. A hack can ship a small JSON with only `exceptions` and
point `ZELDA3_VOXEL_PROFILE` at it:

```json
{
  "format": "zelda3-voxel-profile",
  "version": 1,
  "extends": "plugins/voxel/default-profile.json",
  "camera": { "angle": 60, "indoor_angle": 45, "distance": 1.5,
              "height": 1.0, "follow_link": true, "yaw": 0.0, "zoom": 1.0 },
  "render": { "side_shade": 0.86, "sprite_scale": 1.0, "floor_step": 0.5,
              "skirt": 1.0, "height_scale": 1.0, "tilt_shift": 0.0,
              "exposure": 1.04, "stack_height": 2.2 },
  "lighting": { "enabled": true, "azimuth": 45, "elevation": 55,
                "ambient": 0.55, "diffuse": 0.45 },
  "shadows": { "enabled": true, "strength": 0.42, "length": 1.5,
               "columns": true, "sprites": true },
  "heights": { "0": 0.0, "1": 1.5, "0x20": -1.0 },
  "exceptions": [
    { "indoors": true, "attr": 4, "height": 1.5, "mode": "upright" },
    { "raw_min": 1023, "raw_max": 1027, "height": 2.5, "mode": "top" },
    { "room": 86, "layer": "bg2", "tile": 512, "height": 2.0 }
  ]
}
```

Exception conditions: `indoors`, `room`, `area`, `floor`, `layer` (`0`/`1` or
`"bg1"`/`"bg2"`), `attr`, `raw`, `raw_min`/`raw_max`, `tile` (0–1023) and
`tile_min`/`tile_max`. `height` is required; `mode` is `flat`, `top`,
`upright`, `stack`, `prop` or omitted (auto). `stack` marks a drawn canopy
(trees, bushes) for sprite stacking: every opaque pixel becomes a column and
the silhouette follows the art instead of a box. `prop` marks an opaque
ground tile whose painted object should stand up as a billboard. The
defaults include the Kakariko house roof and facade ranges measured from the
drawing, the Dark World tree canopies, the village gravestone tiles, the
one-layer Lost Woods/Eastern Palace ledges and the Lost Woods hut shading band
(it sits on a recessed attribute and is stood up at the facade).
Environment variables override the
profile. `extends` is resolved relative to the profile file first, then as
written. Profiles are loaded once at `init`; there is no hot reload.

## Room mesh cache

The static room mesh is cached per area/room window. The key covers the
semantic room/area/floor, the whole room attr/raw arrays, the room window,
VRAM and CGRAM (so tile animation and palette changes invalidate it), the
output size and the mesh detail. Sub-tile scroll is applied as a GL vertex
offset, so a key stays valid for up to eight frames of smooth scrolling.

On a hit the room mesh and the atlas slots it references are restored
directly; only sprites, weather and the HUD are rebuilt. On a miss the room
is built normally and copied into the cache. With the 3100-frame Dark World
replay the hit rate is 42.6%; a 600-frame A/B run under the same machine load
was 26.2 s with the cache vs 32.7 s without it (about 20% faster), and the
final screenshot is pixel-identical. `ZELDA3_VOXEL_NO_CACHE=1` disables the
cache for comparison, and `ZELDA3_VOXEL_STATS=1` reports hits/misses.

## Quality presets

`profiles/quality-high.json` (the default look), `quality-medium.json`,
`quality-low.json` and `quality-potato.json` inherit the default profile and
only change `render.mesh_detail` (pixel step for cut-out props and tree
stacks) and the shadow pass. Select one with
`ZELDA3_VOXEL_PROFILE=plugins/voxel/profiles/quality-medium.json`.

`mesh_detail` is 1 (one quad per art pixel), 2, 4 or 8 (one quad per tile).
Measured on the reference machine (Vega 11, headless, 3100 frames of the Dark
World replay with `--frame-plugin-draw`, room cache disabled for a clean
comparison; wall times are indicative because the host was shared):

| preset | mesh detail | shadows | wall time | world verts avg | peak |
|---|---:|---|---:|---:|---:|
| HIGH | 1 | on | 94.8 s | 322 k | 1.26 M |
| MEDIUM | 2 | on | 79.8 s | 132 k | 382 k |
| LOW | 4 | off | 72.7 s | 80 k | 149 k |
| POTATO | 8 | off | 72.4 s | 66 k | 92 k |

The default is HIGH. `ZELDA3_VOXEL_STATS=1` logs the average and peak vertex
counts at shutdown; `ZELDA3_VOXEL_MESH_DETAIL` overrides the profile value.

## Fallback test

```sh
ZELDA3_VOXEL_NO_GL=1 ./zelda3 --config tests/regress.ini --headless \
  --no-mods --render-plugin plugins/voxel/voxel.so \
  --replay-ref 8 --frames 2000 --screenshot .agents/shots/fallback.ppm \
  zelda3.sfc
```

The screenshot is byte-for-byte identical to the normal renderer.
