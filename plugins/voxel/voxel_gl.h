#ifndef ZELDA3_VOXEL_GL_H_
#define ZELDA3_VOXEL_GL_H_

// Offscreen OpenGL renderer for the voxel plugin. The implementation uses EGL
// (surfaceless when available) and an FBO, so it works in --headless mode.
// On platforms without EGL the module reports failure and the plugin falls
// back to the reference renderer.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct VoxelGl VoxelGl;

typedef struct VoxelGlVertex {
  float x, y, z;
  float u, v;
  float shade;
  float depth;  // reserved depth bias; the 3D shader uses world x/y/z
} VoxelGlVertex;

typedef struct VoxelGlCamera {
  float center_x, center_y;
  float width, height;
  float yaw;
  float cos_elev;  // cos of the camera elevation above the horizon
  float sin_elev;
  float distance;  // camera distance in world pixels (larger = flatter)
  float zoom;
  float zscale;    // world height multiplier
} VoxelGlCamera;

typedef void (*VoxelGlLogFn)(void *user, int level, const char *message);

VoxelGl *VoxelGl_Create(VoxelGlLogFn log, void *log_user);
void VoxelGl_Destroy(VoxelGl *gl);
bool VoxelGl_IsReady(const VoxelGl *gl);

// Binds the offscreen framebuffer, sets the full viewport and clears it.
bool VoxelGl_BeginFrame(VoxelGl *gl, int width, int height, uint32_t clear_rgb);

// Restricts drawing to one pane. x/y/width/height are physical pixels;
// logical_width/logical_height are the pane's SNES-pixel size.
void VoxelGl_SetViewport(VoxelGl *gl, int x, int y, int width, int height,
                         int logical_width, int logical_height);

// Uploads the whole tile atlas (RGBA8, tightly packed).
bool VoxelGl_UploadAtlas(VoxelGl *gl, const uint8_t *rgba, int width, int height);

// Draws triangles with the 3D (perspective) or 2D (screen-space) shader.
bool VoxelGl_Draw3D(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                    const VoxelGlCamera *camera, float offset_x,
                    float offset_y);
// Copies the world framebuffer to the output with an optional tilt-shift
// blur. The HUD/2D pass runs afterwards, so it stays sharp.
bool VoxelGl_CompositeWorld(VoxelGl *gl, float strength);
// Draws a screen-space panel into the world target, before the 3D pass.
bool VoxelGl_DrawBackdrop(VoxelGl *gl, const VoxelGlVertex *vertices,
                          int count);
// Adds/subtracts a translucent screen-space panel over the finished world.
bool VoxelGl_DrawOverlay(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                         bool subtract, bool half);
bool VoxelGl_Draw2D(VoxelGl *gl, const VoxelGlVertex *vertices, int count);
// Renders one shadow-map pass from the sun. Both meshes are casters. The
// main 3D draw samples the result; |strength| 0 disables the pass.
bool VoxelGl_RenderShadowMap(VoxelGl *gl, const VoxelGlVertex *terrain,
                             int terrain_count, const VoxelGlVertex *sprites,
                             int sprite_count, const VoxelGlCamera *camera,
                             float light_x, float light_y, float light_z,
                             float strength, float terrain_offset_x,
                             float terrain_offset_y);
// Legacy ground-shadow quads (kept as a fallback for other consumers).
bool VoxelGl_DrawShadows(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                         const VoxelGlCamera *camera, float strength);

// Reads the framebuffer back into |pixels| (ARGB8888 rows, top-left origin).
bool VoxelGl_EndFrame(VoxelGl *gl, uint8_t *pixels, size_t pitch);

#endif  // ZELDA3_VOXEL_GL_H_
