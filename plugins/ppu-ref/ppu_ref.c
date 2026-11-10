// File-loaded copy of the reference PPU renderer. The implementation lives in
// the engine tree so builtin:ppu and this shared library cannot drift apart;
// compilation of this file exports the plugin entry point.

#include "../../src/mod/render_plugin.h"

#include "../../src/mod/render_plugin_ref.c"

ZELDA_RENDER_PLUGIN_EXPORT ZeldaRenderPlugin *zelda3_render_plugin_get(
    uint32_t api_version) {
  if (api_version != kRenderPluginApiVersion)
    return NULL;
  return &g_render_plugin_ref;
}
