# Voxel renderer plugin (R-4/R-5)

Builds a 3D scene from `FrameDesc`: every visible BG1/BG2 8×8 tile becomes a
textured voxel column whose height comes from the collision attribute, and
sprites with an owner become upright cards positioned by the game's `z`.
BG3 (HUD/text) and owner-less sprites stay 2D on top. Mode 7, force blank and
special screens fall back to the reference renderer.

R-5 adds a free camera, column/sprite shadows, a directional light, an in-game
voxel toggle and per-hack profiles with `extends` inheritance.

The scene is rendered by an offscreen EGL/OpenGL context and read back into the
host's ARGB8888 buffer. On Linux the plugin tries the EGL surfaceless
platform first, so it also works in `--headless` without `DISPLAY`. If EGL or
OpenGL fails, the plugin logs a message and keeps working as a reference
renderer pass-through.

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
| F11 | voxel on/off (falls back to the reference renderer) |
| Alt+Left / Alt+Right | rotate the camera (yaw, ±60°) |
| Alt+Up / Alt+Down | tilt (elevation, 10–89°) |
| Alt+= / Alt+- | zoom in/out (0.5–2.5×) |
| Alt+0 | reset camera, light and shadows to the profile |
| F12 | shadows on/off |
| Shift+F11 | directional light on/off |

The camera still frames only the area described by `FrameDesc`; yaw rotates the
diorama around the pane centre and zoom never reads tiles outside it.

## Camera, light and shadows

Environment variables (or the profile's `camera`/`render`/`lighting`/`shadows`
objects):

| Variable | Default | Meaning |
|---|---:|---|
| `ZELDA3_VOXEL_CAMERA_ANGLE` | 58 | camera elevation in degrees (5–89) |
| `ZELDA3_VOXEL_CAMERA_DISTANCE` | 1.0 | perspective distance; larger is flatter |
| `ZELDA3_VOXEL_CAMERA_HEIGHT` | 1.0 | vertical scale of the voxel columns |
| `ZELDA3_VOXEL_CAMERA_FOLLOW` | 1 | follow Link's ground position (not the jump) |
| `ZELDA3_VOXEL_CAMERA_YAW` | 0 | rotation around the vertical axis, degrees |
| `ZELDA3_VOXEL_CAMERA_ZOOM` | 1.0 | 1.0 = default framing, larger = closer |
| `ZELDA3_VOXEL_HEIGHT_SCALE` | 1.0 | multiplier for all column heights |
| `ZELDA3_VOXEL_SIDE_SHADE` | 0.72 | brightness of vertical faces |
| `ZELDA3_VOXEL_SPRITE_SCALE` | 1.0 | vertical size of sprite cards |
| `ZELDA3_VOXEL_LIGHTING` | 1 | directional light |
| `ZELDA3_VOXEL_SHADOWS` | 1 | column and sprite shadows |
| `ZELDA3_VOXEL_SHADOW_STRENGTH` | 0.32 | shadow alpha (0–1) |
| `ZELDA3_VOXEL_SHADOW_LENGTH` | 1.5 | shadow displacement multiplier |
| `ZELDA3_VOXEL_SCALE` | 1 | output scale (1, 2 or 4) |
| `ZELDA3_VOXEL_PROFILE` | — | profile JSON path |
| `ZELDA3_VOXEL_NO_GL` | 0 | force the reference-renderer fallback |

## Profile

The default profile is `plugins/voxel/default-profile.json`; without it the
plugin uses the built-in table. Heights are in 8-pixel blocks (1.0 = one tile).
`profiles/flat.json` and `profiles/exaggerated.json` are complete examples that
inherit the default through `extends`. A hack can ship a small JSON with only
`exceptions` and point `ZELDA3_VOXEL_PROFILE` at it:

```json
{
  "format": "zelda3-voxel-profile",
  "version": 1,
  "extends": "plugins/voxel/default-profile.json",
  "camera": { "angle": 58, "distance": 1.0, "height": 1.0, "follow_link": true,
              "yaw": 0.0, "zoom": 1.0 },
  "render": { "side_shade": 0.72, "sprite_scale": 1.0, "floor_step": 0.5,
              "skirt": 1.0, "height_scale": 1.0 },
  "lighting": { "enabled": true, "azimuth": 45, "elevation": 55,
                "ambient": 0.55, "diffuse": 0.45 },
  "shadows": { "enabled": true, "strength": 0.32, "length": 1.5,
               "columns": true, "sprites": true },
  "heights": { "0": 0.0, "1": 1.5, "0x20": -1.0 },
  "exceptions": [
    { "indoors": true, "attr": 4, "height": 1.5 },
    { "room": 86, "layer": "bg2", "tile": 512, "height": 2.0 }
  ]
}
```

Exception conditions: `indoors`, `room`, `area`, `floor`, `layer` (`0`/`1` or
`"bg1"`/`"bg2"`), `attr`, `raw` (dungeon byte/Map16 word) and `tile` (tile
number 0–1023). `height` is required. Environment variables override the
profile. `extends` is resolved relative to the profile file first, then as
written (so `plugins/voxel/default-profile.json` also works from a mod folder
when the game is launched from the repository root). Profiles are loaded once
at `init`; there is no hot reload.

## Fallback test

```sh
ZELDA3_VOXEL_NO_GL=1 ./zelda3 --config tests/regress.ini --headless \
  --no-mods --render-plugin plugins/voxel/voxel.so \
  --replay-ref 8 --frames 2000 --screenshot .agents/shots/fallback.ppm \
  zelda3.sfc
```

The screenshot is byte-for-byte identical to the normal renderer.
