#include "render_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "../../snes/ppu.h"
#include "frame_desc.h"
#include "render_plugin_host.h"
#include "shared_world.h"

extern ZeldaRenderPlugin g_render_plugin_ref;

typedef struct RenderPluginCheckBuffers {
  uint8 *ref;
  uint8 *got;
  size_t size;
} RenderPluginCheckBuffers;

static bool s_active;
static bool s_enabled;
static bool s_present_failed_logged;
static ZeldaRenderPlugin *s_plugin;
static const char *s_name;
static ZeldaRenderFrameInfo s_frame;
static const FrameDesc *s_descs[kRenderPluginMaxPanes];
static int s_desc_count;
static RenderPluginCheckBuffers s_check;

#ifdef _WIN32
static HMODULE s_library;
#else
static void *s_library;
#endif

static void HostLog(const ZeldaRenderHost *host, int level,
                    const char *message) {
  (void)host;
  if (level >= 2)
    fprintf(stderr, "render-plugin: error: %s\n", message);
  else if (level >= 1)
    fprintf(stderr, "render-plugin: warning: %s\n", message);
  else
    fprintf(stderr, "render-plugin: %s\n", message);
}

static bool HostRenderFrameDesc(const ZeldaRenderHost *host,
                                const FrameDesc *desc, uint8 *pixels,
                                size_t pitch) {
  (void)host;
  return FrameDesc_Render(desc, pixels, pitch);
}

static const ZeldaRenderHost s_host = {
  kRenderPluginApiVersion,
  kFrameDescVersion,
  HostLog,
  HostRenderFrameDesc,
};

static void CloseLibrary(void) {
  if (s_library == NULL)
    return;
#ifdef _WIN32
  FreeLibrary(s_library);
#else
  dlclose(s_library);
#endif
  s_library = NULL;
}

static void UnloadPlugin(void) {
  if (s_active && s_plugin != NULL && s_plugin->shutdown != NULL)
    s_plugin->shutdown(s_plugin);
  s_active = false;
  s_enabled = false;
  s_plugin = NULL;
  s_name = NULL;
  s_desc_count = 0;
  memset(s_descs, 0, sizeof(s_descs));
  CloseLibrary();
}

void RenderPlugin_Shutdown(void) {
  UnloadPlugin();
}

static bool CheckPluginDescriptor(ZeldaRenderPlugin *plugin,
                                  const char *path) {
  if (plugin->api_version != kRenderPluginApiVersion) {
    fprintf(stderr,
            "render-plugin: refusing %s: plugin API version %u, host expects %u\n",
            path, (unsigned)plugin->api_version,
            (unsigned)kRenderPluginApiVersion);
    return false;
  }
  if (!FrameDesc_IsCompatibleVersion(plugin->frame_desc_version)) {
    fprintf(stderr,
            "render-plugin: refusing %s: frame description version %u, host expects %u\n",
            path, (unsigned)plugin->frame_desc_version,
            (unsigned)FrameDesc_CurrentVersion());
    return false;
  }
  if (plugin->init == NULL || plugin->begin_frame == NULL ||
      plugin->frame_description == NULL || plugin->present == NULL) {
    fprintf(stderr,
            "render-plugin: refusing %s: missing init/begin_frame/frame_description/present\n",
            path);
    return false;
  }
  return true;
}

bool RenderPlugin_Load(const char *path) {
  UnloadPlugin();
  if (path == NULL || path[0] == 0)
    return false;

  ZeldaRenderPlugin *plugin = NULL;
  const char *name = path;
  s_present_failed_logged = false;

  if (strcmp(path, ZELDA_RENDER_PLUGIN_BUILTIN) == 0) {
    plugin = &g_render_plugin_ref;
    name = ZELDA_RENDER_PLUGIN_BUILTIN;
  } else {
#ifdef _WIN32
    s_library = LoadLibraryA(path);
    if (s_library == NULL) {
      fprintf(stderr, "render-plugin: cannot load %s (error %lu)\n", path,
              (unsigned long)GetLastError());
      return false;
    }
    ZeldaRenderPluginGetFn get_plugin =
        (ZeldaRenderPluginGetFn)(void *)GetProcAddress(
            s_library, ZELDA_RENDER_PLUGIN_ENTRY);
#else
    s_library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (s_library == NULL) {
      fprintf(stderr, "render-plugin: cannot load %s: %s\n", path, dlerror());
      return false;
    }
    ZeldaRenderPluginGetFn get_plugin = NULL;
    void *symbol = dlsym(s_library, ZELDA_RENDER_PLUGIN_ENTRY);
    memcpy(&get_plugin, &symbol, sizeof(get_plugin));
#endif
    if (get_plugin == NULL) {
      fprintf(stderr,
              "render-plugin: refusing %s: missing entry point %s\n",
              path, ZELDA_RENDER_PLUGIN_ENTRY);
      CloseLibrary();
      return false;
    }
    plugin = get_plugin(kRenderPluginApiVersion);
    if (plugin == NULL) {
      fprintf(stderr,
              "render-plugin: refusing %s: plugin API version %u is not supported\n",
              path, (unsigned)kRenderPluginApiVersion);
      CloseLibrary();
      return false;
    }
  }

  if (!CheckPluginDescriptor(plugin, path)) {
    CloseLibrary();
    return false;
  }

  s_plugin = plugin;
  s_name = name;
  if (!plugin->init(plugin, &s_host, kRenderPluginApiVersion)) {
    fprintf(stderr, "render-plugin: refusing %s: init() failed\n", path);
    s_plugin = NULL;
    s_name = NULL;
    CloseLibrary();
    return false;
  }
  s_active = true;
  s_enabled = true;

  fprintf(stderr,
          "render-plugin: loaded %s (%s, api %u, frame desc %u)\n",
          name, plugin->name != NULL ? plugin->name : "unnamed",
          (unsigned)plugin->api_version,
          (unsigned)plugin->frame_desc_version);
  return true;
}

bool RenderPlugin_IsActive(void) {
  return s_active;
}

bool RenderPlugin_IsEnabled(void) {
  return s_active && s_enabled;
}

void RenderPlugin_SetEnabled(bool enabled) {
  if (!s_active)
    return;
  s_enabled = enabled;
}

bool RenderPlugin_Command(int command) {
  if (!s_active || s_plugin == NULL || s_plugin->command == NULL)
    return false;
  return s_plugin->command(s_plugin, command);
}

const char *RenderPlugin_Name(void) {
  return s_active ? s_name : NULL;
}

int RenderPlugin_PreferredScale(void) {
  if (!s_active || s_plugin == NULL || s_plugin->preferred_scale == NULL)
    return 1;
  int scale = (int)s_plugin->preferred_scale(s_plugin);
  if (scale < 1)
    return 1;
  if (scale > 4)
    return 4;
  return scale;
}

uint32_t RenderPlugin_FrameDescVersion(void) {
  return kFrameDescVersion;
}

bool RenderPlugin_BeginFrame(uint32_t frame_index, int pane_count,
                             int output_width, int output_height, int scale,
                             uint32_t render_flags) {
  if (!s_active || s_plugin == NULL)
    return false;
  if (pane_count < 1)
    pane_count = 1;
  if (pane_count > kRenderPluginMaxPanes)
    pane_count = kRenderPluginMaxPanes;

  s_frame.frame_index = frame_index;
  s_frame.pane_count = (uint32_t)pane_count;
  s_frame.output_width = output_width;
  s_frame.output_height = output_height;
  s_frame.scale = scale;
  s_frame.render_flags = render_flags;
  s_desc_count = 0;
  memset(s_descs, 0, sizeof(s_descs));
  s_plugin->begin_frame(s_plugin, &s_frame);
  return true;
}

bool RenderPlugin_FrameDescription(const FrameDesc *desc) {
  if (!s_active || s_plugin == NULL || desc == NULL)
    return false;
  if (desc->version != kFrameDescVersion) {
    if (!s_present_failed_logged) {
      fprintf(stderr,
              "render-plugin: description version %u, host expects %u\n",
              (unsigned)desc->version, (unsigned)kFrameDescVersion);
      s_present_failed_logged = true;
    }
    return false;
  }
  if (s_desc_count >= kRenderPluginMaxPanes)
    return false;
  s_descs[s_desc_count++] = desc;
  s_plugin->frame_description(s_plugin, desc);
  return true;
}

bool RenderPlugin_Present(uint8 *pixels, size_t pitch, int width, int height,
                          int scale) {
  if (!s_active || s_plugin == NULL || pixels == NULL)
    return false;

  ZeldaRenderTarget target;
  target.pixels = pixels;
  target.pitch = pitch;
  target.width = width;
  target.height = height;
  target.scale = scale;
  if (!s_plugin->present(s_plugin, &target)) {
    if (!s_present_failed_logged) {
      fprintf(stderr, "render-plugin: %s: present() failed\n",
              s_name != NULL ? s_name : "plugin");
      s_present_failed_logged = true;
    }
    return false;
  }
  return true;
}

static bool EnsureCheckBuffers(size_t size) {
  if (s_check.size >= size)
    return true;
  uint8 *ref = (uint8 *)realloc(s_check.ref, size);
  if (ref == NULL)
    return false;
  s_check.ref = ref;
  uint8 *got = (uint8 *)realloc(s_check.got, size);
  if (got == NULL)
    return false;
  s_check.got = got;
  s_check.size = size;
  return true;
}

// --frame-check with a plugin: the normal renderer produces the reference and
// captures the descriptions, then the plugin draws the same descriptions and
// every output pixel is compared.
bool RenderPlugin_CheckFrame(long frame, uint32_t render_flags, int pane_width,
                             int pane_height, FrameDescCheckStats *stats) {
  if (!s_active)
    return false;
  int pane_count = SharedWorld_IsSplit() ? 2 : 1;
  int out_width = pane_width * pane_count;
  size_t pitch = (size_t)out_width * 4;
  size_t size = pitch * (size_t)pane_height;
  if (!EnsureCheckBuffers(size))
    return false;

  memset(s_check.ref, 0, size);
  memset(s_check.got, 0, size);
  if (!FrameDesc_DrawCurrentFrame(render_flags, pane_width, pane_height, true,
                                  s_check.ref, pitch)) {
    fprintf(stderr, "frame-check: failed to capture frame %ld\n", frame);
    return false;
  }

  FrameDesc *first = FrameDesc_GetDesc(0);
  if (first == NULL) {
    fprintf(stderr, "frame-check: no description for frame %ld\n", frame);
    return false;
  }
  int scale = (int)first->display.scale;
  if (!RenderPlugin_BeginFrame(first->frame_index, pane_count, out_width,
                               pane_height, scale, render_flags))
    return false;
  for (int pane = 0; pane < pane_count; pane++) {
    FrameDesc *desc = FrameDesc_GetDesc(pane);
    if (desc == NULL || !RenderPlugin_FrameDescription(desc)) {
      fprintf(stderr, "frame-check: failed to describe frame %ld pane %d\n",
              frame, pane);
      return false;
    }
  }
  if (!RenderPlugin_Present(s_check.got, pitch, out_width, pane_height, scale)) {
    fprintf(stderr, "frame-check: failed to present frame %ld\n", frame);
    return false;
  }

  const uint32 *ref = (const uint32 *)s_check.ref;
  const uint32 *got = (const uint32 *)s_check.got;
  uint32 count = (uint32)out_width * (uint32)pane_height;
  uint64 diff = 0;
  for (uint32 i = 0; i < count; i++) {
    if (ref[i] != got[i]) {
      if (diff == 0 && stats->diff_pixels == 0) {
        stats->first_frame = frame;
        stats->first_x = (int)(i % (uint32)out_width);
        stats->first_y = (int)(i / (uint32)out_width);
        stats->first_ref_pixel = ref[i];
        stats->first_desc_pixel = got[i];
      }
      diff++;
    }
  }
  stats->frames++;
  stats->pixels += count;
  stats->diff_pixels += diff;
  if (pane_count == 2)
    stats->split_frames++;
  if (first->ppu.mode == 7) {
    stats->mode7_frames++;
    if ((render_flags & kPpuRenderFlags_4x4Mode7) != 0)
      stats->mode7_4x4_frames++;
    if (stats->mode7_frames == 1)
      printf("frame-check: first mode7 frame %ld (4x4=%d)\n", frame,
             (render_flags & kPpuRenderFlags_4x4Mode7) != 0);
  }
  return diff == 0;
}
