#ifndef ZELDA3_MOD_RENDER_PLUGIN_HOST_H_
#define ZELDA3_MOD_RENDER_PLUGIN_HOST_H_

// Engine-side renderer plugin manager. Plugin authors only need
// render_plugin.h; this header is for the game loop.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "frame_desc.h"

// Loads "[Graphics] RenderPlugin" or --render-plugin. "builtin:ppu" selects
// the in-process reference renderer. Returns false and leaves the normal path
// untouched when the file/version/init check fails.
bool RenderPlugin_Load(const char *path);
void RenderPlugin_Shutdown(void);
bool RenderPlugin_IsActive(void);
// An active plugin can be disabled at runtime (voxel on/off) without unloading
// it; drawing then falls back to the reference renderer.
bool RenderPlugin_IsEnabled(void);
void RenderPlugin_SetEnabled(bool enabled);
// Sends an optional ZeldaRenderCommand to the plugin. Returns false when the
// plugin is not active or does not implement command().
bool RenderPlugin_Command(int command);
const char *RenderPlugin_Name(void);
uint32_t RenderPlugin_FrameDescVersion(void);

// Optional output scale requested by the active plugin (1 when there is no
// plugin or it does not implement preferred_scale). The game loop uses
// max(PpuGetCurrentRenderScale(), this) for normal frames.
int RenderPlugin_PreferredScale(void);

// One output frame. pane_count is 1, or 2 for SharedWorld split-screen.
bool RenderPlugin_BeginFrame(uint32_t frame_index, int pane_count,
                             int output_width, int output_height, int scale,
                             uint32_t render_flags);
bool RenderPlugin_FrameDescription(const FrameDesc *desc);
bool RenderPlugin_Present(uint8 *pixels, size_t pitch, int width, int height,
                          int scale);

// --frame-check with a loaded plugin: draw the normal frame, then ask the
// plugin to draw the captured descriptions and compare every pixel.
bool RenderPlugin_CheckFrame(long frame, uint32_t render_flags, int pane_width,
                             int pane_height, FrameDescCheckStats *stats);

#endif  // ZELDA3_MOD_RENDER_PLUGIN_HOST_H_
