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
  float depth;  // scene Y (plus a small bias); used only by the 3D shaders
} VoxelGlVertex;

typedef struct VoxelGlCamera {
  float center_x, center_y;
  float width, height;
  float tilt;
  float persp;
  float zscale;
  float yaw;
  float zoom;
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
                    const VoxelGlCamera *camera);
bool VoxelGl_Draw2D(VoxelGl *gl, const VoxelGlVertex *vertices, int count);
// Draws the shadow geometry with alpha blending. Must run after the ground and
// columns and before the sprite cards; |strength| is the shadow alpha (0..1).
bool VoxelGl_DrawShadows(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                         const VoxelGlCamera *camera, float strength);

// Reads the framebuffer back into |pixels| (ARGB8888 rows, top-left origin).
bool VoxelGl_EndFrame(VoxelGl *gl, uint8_t *pixels, size_t pitch);

#endif  // ZELDA3_VOXEL_GL_H_
