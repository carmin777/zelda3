#ifndef ZELDA3_MOD_RENDER_PLUGIN_H_
#define ZELDA3_MOD_RENDER_PLUGIN_H_

// Public, versioned renderer plugin API (R-2).
//
// A plugin is a native shared library (dlopen/LoadLibrary). It includes only
// this header and frame_desc.h, and never links against engine internals; all
// engine access goes through ZeldaRenderHost. The host entry point exported by
// the library is:
//
//   ZeldaRenderPlugin *zelda3_render_plugin_get(uint32_t api_version);
//
// Return NULL when api_version is not supported. The returned descriptor must
// stay valid for the lifetime of the library.
//
// Lifecycle for one output frame:
//   begin_frame(frame)
//   frame_description(desc)  // once per pane, pane_index order
//   present(target)          // draw all panes described since begin_frame
//
// Pointer lifetime:
// - The host is valid for the whole plugin lifetime.
// - frame and target point to host storage that is only valid during the
//   call. Copy anything you need to keep.
// - desc and everything it points to are valid from frame_description until
//   the next begin_frame or shutdown. desc->vram/cgram/oam point at the live
//   PPU arrays, which are valid until the next PPU write; a plugin that keeps
//   a frame after present must copy those arrays itself.
//
// Split-screen: SharedWorld draws two complete panes. The host calls
// begin_frame with pane_count = 2, then frame_description once per pane. Each
// desc has display.pane_index/pane_x (logical SNES pixels, before scale); the
// plugin draws pane i at (pane_x * scale, pane_y * scale) inside target.
//
// Mod overlays (zelda.draw) are not part of FrameDesc. They are queued by the
// FrameDraw event in pane-local SNES coordinates and composited by the host
// after present(), so every renderer gets the same UI without reimplementing
// the text/alpha rules. See docs/engine/RENDER.md.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "frame_desc.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  // Bump when a callback signature, a struct layout or the meaning of a field
  // in this API changes. Version 2 added preferred_scale (R-3); version 3 adds
  // the optional command callback used by the interactive camera/shadows
  // controls (R-5) and the camera level presets appended to the command enum.
  kRenderPluginApiVersion = 3,

  // Maximum number of pane descriptions per output frame.
  kRenderPluginMaxPanes = kFrameDesc_MaxPanes,
};

// Optional commands sent by the host for interactive renderer controls. They
// are renderer-specific; a plugin that does not implement command() simply
// ignores them. Values are stable and are sent only from the game loop.
typedef enum ZeldaRenderCommand {
  kRenderCommand_None = 0,
  kRenderCommand_CameraRotateLeft = 1,
  kRenderCommand_CameraRotateRight = 2,
  kRenderCommand_CameraTiltUp = 3,
  kRenderCommand_CameraTiltDown = 4,
  kRenderCommand_CameraZoomIn = 5,
  kRenderCommand_CameraZoomOut = 6,
  kRenderCommand_CameraReset = 7,
  kRenderCommand_ToggleShadows = 8,
  kRenderCommand_ToggleLighting = 9,
  // Preset camera inclinations. The plugin eases toward the new angle, so a
  // preset is a target and never a one-frame snap.
  kRenderCommand_CameraLevelLow = 10,   // 15 degrees
  kRenderCommand_CameraLevelMid = 11,   // 35 degrees
  kRenderCommand_CameraLevelHigh = 12,  // 50 degrees
} ZeldaRenderCommand;

#define ZELDA_RENDER_PLUGIN_BUILTIN "builtin:ppu"
#define ZELDA_RENDER_PLUGIN_ENTRY "zelda3_render_plugin_get"

#if defined(_WIN32)
#define ZELDA_RENDER_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define ZELDA_RENDER_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define ZELDA_RENDER_PLUGIN_EXPORT
#endif

typedef struct ZeldaRenderHost ZeldaRenderHost;
typedef struct ZeldaRenderPlugin ZeldaRenderPlugin;

// One output frame. Widths and heights are already multiplied by scale.
typedef struct ZeldaRenderFrameInfo {
  uint32 frame_index;
  uint32 pane_count;        // 1, or 2 for SharedWorld split-screen
  int32 output_width;       // full output, in pixels
  int32 output_height;
  int32 scale;              // 1 normally, 4 for the 4x4 Mode 7 renderer
  uint32 render_flags;      // kPpuRenderFlags_* snapshot
} ZeldaRenderFrameInfo;

typedef struct ZeldaRenderTarget {
  uint8 *pixels;            // ARGB8888, top-left of the output image
  size_t pitch;             // bytes per row
  int32 width;              // full output, in pixels
  int32 height;
  int32 scale;
} ZeldaRenderTarget;

struct ZeldaRenderHost {
  uint32 api_version;
  uint32 frame_desc_version;

  // Diagnostics. level: 0 info, 1 warning, 2 error. The message is only valid
  // during the call.
  void (*log)(const ZeldaRenderHost *host, int level, const char *message);

  // Reference PPU renderer: draws |desc| at |pixels| with the built-in line
  // renderer, without touching the live PPU. |pixels| points at the pane
  // origin. This is a convenience for plugins that start from the original
  // output; a plugin may ignore it and draw entirely from FrameDesc.
  bool (*render_frame_desc)(const ZeldaRenderHost *host, const FrameDesc *desc,
                            uint8 *pixels, size_t pitch);
};

struct ZeldaRenderPlugin {
  // Filled by the plugin. The host refuses the library when either version
  // does not match the host.
  uint32 api_version;
  uint32 frame_desc_version;
  const char *name;         // short name, for diagnostics
  const char *description;  // optional, may be NULL
  void *state;              // private plugin state; the host never touches it

  // Called once after loading. |version| is kRenderPluginApiVersion. Return
  // false to refuse; the host unloads the library and keeps its own renderer.
  bool (*init)(ZeldaRenderPlugin *plugin, const ZeldaRenderHost *host,
               uint32_t version);

  // Start one output frame, before any frame_description call.
  void (*begin_frame)(ZeldaRenderPlugin *plugin,
                      const ZeldaRenderFrameInfo *frame);

  // One pane description, in pane_index order.
  void (*frame_description)(ZeldaRenderPlugin *plugin, const FrameDesc *desc);

  // Draw the panes described since begin_frame. Return false to report
  // failure; the host falls back to its own renderer for that frame.
  bool (*present)(ZeldaRenderPlugin *plugin,
                  const ZeldaRenderTarget *target);

  // Called once before unloading. May be NULL.
  void (*shutdown)(ZeldaRenderPlugin *plugin);

  // Optional. Returns the output scale the plugin wants for normal frames
  // (1, 2 or 4; the host clamps to 4). The host uses max(PPU scale, this) when
  // allocating the frame buffer; the same scale is passed in frame_info and
  // target. NULL means 1. Returning 1 keeps the existing output size.
  int32_t (*preferred_scale)(ZeldaRenderPlugin *plugin);

  // Optional. Interactive command from the host (ZeldaRenderCommand). The
  // call happens on the game thread between frames; changing renderer state
  // here is safe. Return false when the command is unknown/unsupported.
  bool (*command)(ZeldaRenderPlugin *plugin, int command);
};

typedef ZeldaRenderPlugin *(*ZeldaRenderPluginGetFn)(uint32_t api_version);

#ifdef __cplusplus
}
#endif

#endif  // ZELDA3_MOD_RENDER_PLUGIN_H_
