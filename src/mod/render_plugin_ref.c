// Reference renderer plugin: the normal PPU path from FrameDesc_Render,
// packaged behind the public plugin interface. It is compiled into the engine
// as builtin:ppu and also into plugins/ppu-ref/, which proves that a plugin
// loaded from a file can produce exactly the same pixels.

#include "render_plugin.h"

#include <string.h>

typedef struct RenderPluginRefState {
  const ZeldaRenderHost *host;
  const FrameDesc *pane[kRenderPluginMaxPanes];
  int pane_count;
} RenderPluginRefState;

static bool RefInit(ZeldaRenderPlugin *plugin, const ZeldaRenderHost *host,
                    uint32_t version) {
  RenderPluginRefState *st = (RenderPluginRefState *)plugin->state;
  if (version != kRenderPluginApiVersion || host == NULL ||
      host->api_version != kRenderPluginApiVersion ||
      host->frame_desc_version != kFrameDescVersion ||
      host->render_frame_desc == NULL)
    return false;
  st->host = host;
  st->pane_count = 0;
  memset(st->pane, 0, sizeof(st->pane));
  return true;
}

static void RefBeginFrame(ZeldaRenderPlugin *plugin,
                          const ZeldaRenderFrameInfo *frame) {
  RenderPluginRefState *st = (RenderPluginRefState *)plugin->state;
  st->pane_count = frame != NULL ? (int)frame->pane_count : 0;
  memset(st->pane, 0, sizeof(st->pane));
}

static void RefFrameDescription(ZeldaRenderPlugin *plugin,
                                const FrameDesc *desc) {
  RenderPluginRefState *st = (RenderPluginRefState *)plugin->state;
  if (desc == NULL || desc->version != kFrameDescVersion ||
      desc->display.pane_index >= kRenderPluginMaxPanes)
    return;
  st->pane[desc->display.pane_index] = desc;
}

static bool RefPresent(ZeldaRenderPlugin *plugin,
                       const ZeldaRenderTarget *target) {
  RenderPluginRefState *st = (RenderPluginRefState *)plugin->state;
  if (target == NULL || target->pixels == NULL)
    return false;

  for (int i = 0; i < st->pane_count; i++) {
    const FrameDesc *desc = st->pane[i];
    if (desc == NULL)
      return false;
    size_t x = (size_t)desc->display.pane_x * (size_t)target->scale;
    size_t y = (size_t)desc->display.pane_y * (size_t)target->scale;
    uint8 *pixels = target->pixels + y * target->pitch + x * 4;
    if (!st->host->render_frame_desc(st->host, desc, pixels, target->pitch))
      return false;
  }
  return true;
}

static void RefShutdown(ZeldaRenderPlugin *plugin) {
  RenderPluginRefState *st = (RenderPluginRefState *)plugin->state;
  st->pane_count = 0;
  memset(st->pane, 0, sizeof(st->pane));
  st->host = NULL;
}

static int32_t RefPreferredScale(ZeldaRenderPlugin *plugin) {
  (void)plugin;
  return 1;
}

static RenderPluginRefState s_ref_state;

ZeldaRenderPlugin g_render_plugin_ref = {
  kRenderPluginApiVersion,
  kFrameDescVersion,
  "ppu-ref",
  "Reference PPU renderer built from FrameDesc",
  &s_ref_state,
  RefInit,
  RefBeginFrame,
  RefFrameDescription,
  RefPresent,
  RefShutdown,
  RefPreferredScale,
  NULL,
};
