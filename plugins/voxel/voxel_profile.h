#ifndef ZELDA3_VOXEL_PROFILE_H_
#define ZELDA3_VOXEL_PROFILE_H_

// Optional data-only profile for the voxel renderer (R-4).
//
// The profile is a JSON file. Heights are in 8x8 tile blocks (1.0 = 8 SNES
// pixels). The plugin always starts from a built-in table that mirrors
// default-profile.json; entries in the file override that table, and
// exceptions are evaluated before the table.

#include <stdbool.h>
#include <stdint.h>

#include "../../src/mod/frame_desc.h"

typedef struct VoxelCameraSettings {
  float angle_deg;       // camera elevation, 90 = top-down, 0 = horizon
  float distance_scale;  // 1.0 = default perspective; larger = flatter
  float height_scale;    // vertical scale of voxel columns
  bool follow_link;      // pan the camera slightly toward Link
  float yaw_deg;         // rotation around the vertical axis (free camera)
  float zoom;            // 1.0 = default framing; larger = closer
} VoxelCameraSettings;

typedef struct VoxelRenderSettings {
  float side_shade;      // brightness multiplier for vertical faces
  float sprite_scale;    // vertical size of sprite cards
  float floor_step;      // height difference between dungeon floors, blocks
  float skirt;           // depth of the column skirt below the top, blocks
  float height_scale;    // multiplier for semantic column heights
} VoxelRenderSettings;

typedef struct VoxelLightSettings {
  bool enabled;          // directional light; false = flat shading
  float azimuth_deg;     // horizontal direction of the light
  float elevation_deg;   // 0 = horizon, 90 = straight down
  float ambient;         // shade of faces pointing away from the light
  float diffuse;         // extra shade on faces facing the light
} VoxelLightSettings;

typedef struct VoxelShadowSettings {
  bool enabled;          // master shadow switch
  float strength;        // alpha of the ground shadow (0..1)
  float length;          // shadow length multiplier
  bool columns;          // BG columns cast shadows
  bool sprites;          // sprite cards cast contact shadows
} VoxelShadowSettings;

typedef struct VoxelProfile VoxelProfile;

// Creates a profile with the built-in defaults. Never returns NULL on a
// normal allocation failure path; callers should still check.
VoxelProfile *VoxelProfile_CreateDefault(void);

// Loads |path| on top of the defaults. Returns NULL when the file cannot be
// read or parsed; the caller keeps the default profile and logs a warning.
VoxelProfile *VoxelProfile_Load(const char *path);

void VoxelProfile_Free(VoxelProfile *profile);

// Height in blocks for one BG tile. |layer| is the PPU layer (0 = BG1,
// 1 = BG2), |attr| the collision attribute, |raw| the dungeon byte/Map16 word
// and |tile| the 10-bit tile number. Returns the exception height, the
// per-attribute height, or 0 when no entry matches.
float VoxelProfile_Height(const VoxelProfile *profile, const FrameDesc *desc,
                          int layer, int attr, int raw, int tile);

const VoxelCameraSettings *VoxelProfile_Camera(const VoxelProfile *profile);
const VoxelRenderSettings *VoxelProfile_Render(const VoxelProfile *profile);
const VoxelLightSettings *VoxelProfile_Light(const VoxelProfile *profile);
const VoxelShadowSettings *VoxelProfile_Shadows(const VoxelProfile *profile);

#endif  // ZELDA3_VOXEL_PROFILE_H_
