#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>
#ifdef _WIN32
#include "platform/win32/volume_control.h"
#include <direct.h>
#include <Windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "snes/ppu.h"
#include "snes/snes.h"

#include "types.h"
#include "variables.h"

#include "zelda_rtl.h"
#include "zelda_cpu_infra.h"

#include "config.h"
#include "assets.h"
#include "hud.h"
#include "load_gfx.h"
#include "util.h"
#include "audio.h"
#include "mod/coop.h"
#include "mod/events.h"
#include "mod/draw.h"
#include "mod/hooks.h"
#include "mod/local_coop.h"
#include "mod/menu.h"
#include "mod/mods.h"
#include "mod/nwa_server.h"
#include "mod/shared_world.h"
#include "mod/sprite_override.h"
#include "mod/state_table.h"
#include "mod/frame_desc.h"
#include "mod/render_plugin.h"
#include "mod/render_plugin_host.h"

static bool g_run_without_emu = 0;

// Set by --headless. Also read by zelda_cpu_infra.c so that a RAM/VRAM
// mismatch exits with a nonzero status instead of retrying forever.
bool g_headless;

static int g_replay_ref = -1;
static const char *g_replay_file;
static const char *g_load_file;
static long g_stop_replay_at = -1;
static const char *g_save_replay_file;
static const char *g_input_script_file;
static const char *g_input_script2_file;
static const char *g_emu_rom_file;
static const char *g_oracle_rom_file;
static const char *g_seed_rom_file;
static bool g_oracle_ignore_jp;
static const char *g_audio_dump_file;
// Scripted joypad input for --headless: from frame[i] on, hold buttons[i].
static int g_input_script_count;
static long *g_input_script_frame;
static uint16 *g_input_script_buttons;
static int g_input_script2_count;
static long *g_input_script2_frame;
static uint16 *g_input_script2_buttons;
static int g_local_player;
static long g_max_frames = -1;
static int g_hash_every;
static bool g_mod_trace;
static bool g_dump_state;
static const char *g_screenshot_file;
static const char *g_menu_test_file;
static const char *g_menu_screenshot_page;
static long g_nwa_hold_seconds;
static long g_nwa_hold_at;
static long g_force_sprite6b_frame = -1;
// Frame description debug modes (all headless-only except [Graphics]
// FrameDescription, which captures the frames the interactive game draws).
static bool g_frame_check;
static bool g_frame_desc_capture;
static bool g_frame_desc_draw;
static bool g_frame_plugin_draw;
static long g_frame_desc_dump_frame = -1;
static const char *g_render_plugin_file;
// --emu-rom uses the SNES core as the game, not as an oracle.  These point at
// the emulator state while the playable mode is active.
static Snes *g_emu_snes;
static Dsp *g_emu_dsp;
static bool g_emu_mode;

// Forwards
static bool LoadRom(const char *filename);
static bool ParseLongArg(const char *s, long *value);
static uint32 HashState();
static int RunHeadless(const char *rom_file);
static int RunEmuRom(const char *rom_file);
static void LoadInputScript(const char *path);
static void WriteScreenshot(const char *path);
static void WriteEmuScreenshot(const char *path, const uint8 *pixels, int width, int height, size_t pitch);
static void LoadLinkGraphics();
static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big);
static void HandleInput(int keyCode, int modCode, bool pressed);
static void HandleCommand(uint32 j, bool pressed);
static void HandleCommandForPlayer(uint32 j, bool pressed, int player);
static int RemapSdlButton(int button);
static void HandleGamepadInputForPlayer(int button, bool pressed, int player);
static void HandleGamepadAxisInput(int gamepad_id, int axis, int value);
static int ControllerPlayer(SDL_JoystickID id);
static void OpenOneGamepad(int i);
static void UpdateOutputSize(void);
static void HandleVolumeAdjustment(int volume_adjustment);
static void NwaHoldFor(long seconds);
static void LoadAssets();
static void ApplyDimFlashesPalette(bool enabled);
static void SwitchDirectory();

enum {
  kDefaultFullscreen = 0,
  kMaxWindowScale = 10,
  kDefaultFreq = 44100,
  kDefaultChannels = 2,
  kDefaultSamples = 2048,
};

static const char kWindowTitle[] = "The Legend of Zelda: A Link to the Past";
static uint32 g_win_flags = SDL_WINDOW_RESIZABLE;
static SDL_Window *g_window;

static uint8 g_paused, g_turbo, g_replay_turbo = true, g_cursor = true;
static uint8 g_current_window_scale;
static uint8 g_gamepad_buttons;
static uint8 g_gamepad2_buttons;
static int g_input1_state;
static int g_input2_state;
static bool g_display_perf;
static int g_curr_fps;
static int g_ppu_render_flags = 0;
static int g_snes_width, g_snes_height;
static int g_output_width, g_output_height;
#define kMaxOpenControllers 8
static SDL_JoystickID g_controller_ids[kMaxOpenControllers];
static int g_controller_count;
static int g_sdl_audio_mixer_volume = SDL_MIX_MAXVOLUME;
static struct RendererFuncs g_renderer_funcs;
// Last fully rendered game frame. The options menu reuses it while the game is
// paused so redrawing the menu does not touch PPU/DMA state.
static uint8 *g_frame_snapshot;
static size_t g_frame_snapshot_size;
static size_t g_frame_snapshot_pitch;
static int g_frame_snapshot_width;
static int g_frame_snapshot_height;
static uint32 g_gamepad_modifiers[2];
static uint16 g_gamepad_last_cmd[2][kGamepadBtn_Count];

void NORETURN Die(const char *error) {
#if defined(NDEBUG) && defined(_WIN32)
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kWindowTitle, error, NULL);
#endif
  fprintf(stderr, "Error: %s\n", error);
  exit(1);
}

static int g_last_output_width, g_last_output_height;

static void UpdateOutputSize(void) {
  g_output_width = LocalCoop_OutputWidth(g_snes_width);
  g_output_height = LocalCoop_OutputHeight(g_snes_height);
  // Model B split-screen doubles the output only while the automatic split
  // is on. In normal play this returns the pane width unchanged.
  g_output_width = SharedWorld_OutputWidth(g_output_width);
  if (g_window != NULL && g_config.window_width == 0 && g_config.window_height == 0 &&
      (SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) == 0 &&
      (g_last_output_width != g_output_width || g_last_output_height != g_output_height)) {
    g_last_output_width = g_output_width;
    g_last_output_height = g_output_height;
    if (g_current_window_scale != 0)
      SDL_SetWindowSize(g_window, g_current_window_scale * g_output_width,
                        g_current_window_scale * g_output_height);
  }
}

void ChangeWindowScale(int scale_step) {
  if ((SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0)
    return;
  int screen = SDL_GetWindowDisplayIndex(g_window);
  if (screen < 0) screen = 0;
  int max_scale = kMaxWindowScale;
  SDL_Rect bounds;
  int bt = -1, bl, bb, br;
  // note this takes into effect Windows display scaling, i.e., resolution is divided by scale factor
  if (SDL_GetDisplayUsableBounds(screen, &bounds) == 0) {
    // this call may take a while before it is reported by Windows (or not at all in my testing)
    if (SDL_GetWindowBordersSize(g_window, &bt, &bl, &bb, &br) != 0) {
      // guess based on Windows 10/11 defaults
      bl = br = bb = 1;
      bt = 31;
    }
    // Allow a scale level slightly above the max that fits on screen
    int mw = (bounds.w - bl - br + g_output_width / 4) / g_output_width;
    int mh = (bounds.h - bt - bb + g_output_height / 4) / g_output_height;
    max_scale = IntMin(mw, mh);
  }
  int new_scale = IntMax(IntMin(g_current_window_scale + scale_step, max_scale), 1);
  g_current_window_scale = new_scale;
  int w = new_scale * g_output_width;
  int h = new_scale * g_output_height;

  //SDL_RenderSetLogicalSize(g_renderer, w, h);
  SDL_SetWindowSize(g_window, w, h);
  if (bt >= 0) {
    // Center the window on top of the mouse
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = IntMax(IntMin(mx - w / 2, bounds.x + bounds.w - bl - br - w), bounds.x + bl);
    int wy = IntMax(IntMin(my - h / 2, bounds.y + bounds.h - bt - bb - h), bounds.y + bt);
    SDL_SetWindowPosition(g_window, wx, wy);
  } else {
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
}

#define RESIZE_BORDER 20
static SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *pt, void *data) {
  uint32 flags = SDL_GetWindowFlags(win);
  if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0 || (flags & SDL_WINDOW_FULLSCREEN) != 0)
    return SDL_HITTEST_NORMAL;

  if ((SDL_GetModState() & KMOD_CTRL) != 0)
    return SDL_HITTEST_DRAGGABLE;

  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  if (pt->y < RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPRIGHT : SDL_HITTEST_RESIZE_TOP;
  } else if (pt->y >= h - RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMRIGHT : SDL_HITTEST_RESIZE_BOTTOM;
  } else {
    if (pt->x < RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_LEFT;
    } else if (pt->x >= w - RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_RIGHT;
    }
  }
  return SDL_HITTEST_NORMAL;
}

// Normal frames follow the PPU scale (1, or 4 for the 4x4 Mode 7 path). A
// plugin may ask for a larger output (an HD pack at 2x/4x); the buffer and the
// texture are allocated at that size, while the SDL logical size stays at the
// SNES resolution.
static int EffectiveRenderScale(void) {
  int scale = PpuGetCurrentRenderScale(g_zenv.ppu, g_ppu_render_flags);
  if (RenderPlugin_IsEnabled()) {
    int plugin_scale = RenderPlugin_PreferredScale();
    if (plugin_scale > scale)
      scale = plugin_scale;
  }
  return scale;
}

// Renders one output frame through the loaded plugin. The normal renderer
// still runs once into a scratch buffer so the PPU line changes can be
// captured; it restores PPU/DMA before returning. Returns false when there is
// no plugin or the plugin failed, so the caller can use the normal path.
static bool DrawFrameWithPlugin(uint8 *buffer, size_t pitch, int render_scale,
                                uint32 flags) {
  if (!RenderPlugin_IsEnabled())
    return false;

  int pane_count = SharedWorld_IsSplit() ? 2 : 1;
  int pane_width = g_snes_width * render_scale;
  int width = pane_width * pane_count;
  int height = g_snes_height * render_scale;

  if (!RenderPlugin_BeginFrame((uint32_t)frame_ctr_dbg, pane_count, width,
                               height, render_scale, flags))
    return false;
  if (!FrameDesc_DrawCurrentFrame(flags, pane_width, height, true, NULL, 0))
    return false;
  for (int pane = 0; pane < pane_count; pane++) {
    if (!RenderPlugin_FrameDescription(FrameDesc_GetDesc(pane)))
      return false;
  }
  return RenderPlugin_Present(buffer, pitch, width, height, render_scale);
}

static void RenderInstancePane(int index, uint8 *buffer, size_t pitch,
                               int scale) {
  if (LocalCoop_IsActive())
    LocalCoop_Select(index);
  uint32 flags = g_ppu_render_flags;
  if (LocalCoop_IsActive())
    flags &= ~kPpuRenderFlags_4x4Mode7;  // both panes use the 1x renderer
  if (!DrawFrameWithPlugin(buffer, pitch, scale, flags))
    ZeldaDrawPpuFrame(buffer, pitch, flags);
  ModDraw_BeginFrame(buffer, pitch, g_snes_width, g_snes_height, scale);
  MOD_EMIT(kModEv_FrameDraw, index, 2, 0);
  Coop_DrawOverlay(buffer, pitch, g_snes_width, g_snes_height, scale);
  if (index == 0)
    OptionsMenu_Draw();
  ModDraw_EndFrame();
}

// Model B split-screen: each pane is a complete 256x224 view, so run
// FrameDraw once per pane with that pane's buffer and pass the pane index in
// ev.a. Mods keep using pane-local coordinates and can draw different things
// on each half. With the split off this is the original single event.
static void RenderWorldModOverlays(uint8 *buffer, size_t pitch, int scale,
                                   int pane_width) {
  if (SharedWorld_IsSplit()) {
    for (int i = 0; i < 2; i++) {
      uint8 *pane = buffer + (size_t)i * pane_width * 4;
      ModDraw_BeginFrame(pane, pitch, g_snes_width, g_snes_height, scale);
      MOD_EMIT(kModEv_FrameDraw, i, 2, 0);
      Coop_DrawOverlay(pane, pitch, g_snes_width, g_snes_height, scale);
      if (i == 0)
        OptionsMenu_Draw();
      ModDraw_EndFrame();
    }
  } else {
    ModDraw_BeginFrame(buffer, pitch, g_snes_width, g_snes_height, scale);
    MOD_EMIT(kModEv_FrameDraw, 0, 1, 0);
    Coop_DrawOverlay(buffer, pitch, g_snes_width, g_snes_height, scale);
    OptionsMenu_Draw();
    ModDraw_EndFrame();
  }
}

static void CaptureFrameSnapshot(const uint8 *pixels, size_t pitch,
                                 int width, int height) {
  if (pixels == NULL || pitch == 0 || width <= 0 || height <= 0)
    return;
  size_t size = pitch * (size_t)height;
  if (size > g_frame_snapshot_size) {
    uint8 *new_buffer = realloc(g_frame_snapshot, size);
    if (new_buffer == NULL)
      return;
    g_frame_snapshot = new_buffer;
    g_frame_snapshot_size = size;
  }
  memcpy(g_frame_snapshot, pixels, size);
  g_frame_snapshot_pitch = pitch;
  g_frame_snapshot_width = width;
  g_frame_snapshot_height = height;
}

static void DrawPpuFrameWithPerf() {
  int render_scale = EffectiveRenderScale();
  uint8 *pixel_buffer = 0;
  int pitch = 0;

  g_renderer_funcs.BeginDraw(g_output_width * render_scale,
                             g_output_height * render_scale,
                             &pixel_buffer, &pitch);
  uint32 render_flags = g_ppu_render_flags;
  if (SharedWorld_IsActive())
    render_flags |= kPpuRenderFlags_NoSpriteLimits;
  if (g_display_perf || g_config.display_perf_title) {
    static float history[64], average;
    static int history_pos;
    uint64 before = SDL_GetPerformanceCounter();
    if (!DrawFrameWithPlugin(pixel_buffer, pitch, render_scale, render_flags))
      SharedWorld_DrawFrame(pixel_buffer, pitch, render_flags,
                            g_snes_width * render_scale);
    uint64 after = SDL_GetPerformanceCounter();
    float v = (double)SDL_GetPerformanceFrequency() / (after - before);
    average += v - history[history_pos];
    history[history_pos] = v;
    history_pos = (history_pos + 1) & 63;
    g_curr_fps = average * (1.0f / 64);
  } else {
    if (!DrawFrameWithPlugin(pixel_buffer, pitch, render_scale, render_flags))
      SharedWorld_DrawFrame(pixel_buffer, pitch, render_flags,
                            g_snes_width * render_scale);
  }

  // Mods draw after the PPU and before the renderer presents the frame. This
  // only changes pixel_buffer; the game simulation is untouched. In Model B
  // split-screen the event runs once per pane.
  RenderWorldModOverlays(pixel_buffer, pitch, render_scale,
                         g_snes_width * render_scale);
  CaptureFrameSnapshot(pixel_buffer, pitch,
                       g_output_width * render_scale,
                       g_output_height * render_scale);

  if (g_display_perf)
    RenderNumber(pixel_buffer + pitch * render_scale, pitch, g_curr_fps, render_scale == 4);
  g_renderer_funcs.EndDraw();
}

// Re-present the frozen game frame and draw the menu over it. This avoids
// calling ZeldaDrawPpuFrame while paused: the PPU renderer intentionally writes
// HDMA/PPU state that must not leak into the next simulated frame.
static void DrawFrozenFrameWithMenu(void) {
  int render_scale = EffectiveRenderScale();
  int width = g_output_width * render_scale;
  int height = g_output_height * render_scale;
  uint8 *pixels = NULL;
  int pitch = 0;
  g_renderer_funcs.BeginDraw(width, height, &pixels, &pitch);
  if (pixels == NULL || pitch == 0)
    return;
  memset(pixels, 0, (size_t)pitch * (size_t)height);
  if (g_frame_snapshot != NULL) {
    int copy_width = IntMin(width, g_frame_snapshot_width);
    int copy_height = IntMin(height, g_frame_snapshot_height);
    size_t copy_bytes = (size_t)copy_width * 4;
    if (copy_bytes > g_frame_snapshot_pitch)
      copy_bytes = g_frame_snapshot_pitch;
    if (copy_bytes > (size_t)pitch)
      copy_bytes = (size_t)pitch;
    for (int y = 0; y < copy_height; y++) {
      memcpy(pixels + (size_t)y * pitch,
             g_frame_snapshot + (size_t)y * g_frame_snapshot_pitch, copy_bytes);
    }
  }
  ModDraw_BeginFrame(pixels, pitch, g_snes_width, g_snes_height, render_scale);
  OptionsMenu_Draw();
  ModDraw_EndFrame();
  g_renderer_funcs.EndDraw();
}

static SDL_mutex *g_audio_mutex;
static SDL_AudioDeviceID g_audio_device;
static uint8 *g_audiobuffer, *g_audiobuffer_cur, *g_audiobuffer_end;
static int g_frames_per_block;
static uint8 g_audio_channels;

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
  if (SDL_LockMutex(g_audio_mutex)) Die("Mutex lock failed!");
  while (len != 0) {
    if (g_audiobuffer_end - g_audiobuffer_cur == 0) {
      if (g_emu_dsp != NULL)
        dsp_getSamples(g_emu_dsp, (int16*)g_audiobuffer, g_frames_per_block, g_audio_channels);
      else
        ZeldaRenderAudio((int16*)g_audiobuffer, g_frames_per_block, g_audio_channels);
      g_audiobuffer_cur = g_audiobuffer;
      g_audiobuffer_end = g_audiobuffer + g_frames_per_block * g_audio_channels * sizeof(int16);
    }
    int n = IntMin(len, g_audiobuffer_end - g_audiobuffer_cur);
    if (g_sdl_audio_mixer_volume == SDL_MIX_MAXVOLUME) {
      memcpy(stream, g_audiobuffer_cur, n);
    } else {
      SDL_memset(stream, 0, n);
      SDL_MixAudioFormat(stream, g_audiobuffer_cur, AUDIO_S16, n, g_sdl_audio_mixer_volume);
    }
    g_audiobuffer_cur += n;
    stream += n;
    len -= n;
  }

  if (g_emu_dsp == NULL)
    ZeldaDiscardUnusedAudioFrames();
  SDL_UnlockMutex(g_audio_mutex);
}

// Opens one SDL audio device with the requested channel count. The device is
// returned paused, so the caller can swap the globals without a callback
// racing against them.
static bool OpenAudioDevice(int channels, SDL_AudioDeviceID *out_device,
                            uint8 *out_channels, int *out_frames,
                            uint8 **out_buffer) {
  SDL_AudioSpec want = { 0 }, have;
  want.freq = g_config.audio_freq;
  want.format = AUDIO_S16;
  want.channels = (Uint8)channels;
  want.samples = g_config.audio_samples;
  want.callback = &AudioCallback;
  SDL_AudioDeviceID device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (device == 0)
    return false;
  int frames_per_block = (534 * have.freq) / 32000;
  uint8 *buffer = malloc((size_t)frames_per_block * have.channels * sizeof(int16));
  if (buffer == NULL) {
    SDL_CloseAudioDevice(device);
    return false;
  }
  *out_device = device;
  *out_channels = have.channels;
  *out_frames = frames_per_block;
  *out_buffer = buffer;
  return true;
}

// State for sdl renderer
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static SDL_Rect g_sdl_renderer_rect;
static int g_texture_width, g_texture_height;

static bool SdlRenderer_Init(SDL_Window *window) {

  if (g_config.shader)
    fprintf(stderr, "Warning: Shaders are supported only with the OpenGL backend\n");

  SDL_Renderer *renderer = SDL_CreateRenderer(g_window, -1,
                                              g_config.output_method == kOutputMethod_SDLSoftware ? SDL_RENDERER_SOFTWARE :
                                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return false;
  }
  SDL_RendererInfo renderer_info;
  SDL_GetRendererInfo(renderer, &renderer_info);
  if (kDebugFlag) {
    printf("Supported texture formats:");
    for (int i = 0; i < renderer_info.num_texture_formats; i++)
      printf(" %s", SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
    printf("\n");
  }
  g_renderer = renderer;
  if (!g_config.ignore_aspect_ratio)
    SDL_RenderSetLogicalSize(renderer, g_output_width, g_output_height);
  if (g_config.linear_filtering)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

  int tex_mult = (g_ppu_render_flags & kPpuRenderFlags_4x4Mode7) &&
                 !g_config.coop_local ? 4 : 1;
  g_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                g_output_width * tex_mult, g_output_height * tex_mult);
  if (g_texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    return false;
  }
  g_texture_width = g_output_width * tex_mult;
  g_texture_height = g_output_height * tex_mult;
  return true;
}

static void SdlRenderer_Destroy() {
  SDL_DestroyTexture(g_texture);
  SDL_DestroyRenderer(g_renderer);
}

static void SdlRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  // Automatic split-screen changes the output size at runtime. Recreate the
  // streaming texture and the logical size when that happens.
  if (g_texture_width != width || g_texture_height != height) {
    if (g_texture != NULL)
      SDL_DestroyTexture(g_texture);
    if (!g_config.ignore_aspect_ratio)
      SDL_RenderSetLogicalSize(g_renderer, g_output_width, g_output_height);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, width, height);
    g_texture_width = width;
    g_texture_height = height;
    if (g_texture == NULL) {
      printf("Failed to create texture: %s\n", SDL_GetError());
      return;
    }
  }
  g_sdl_renderer_rect.x = 0;
  g_sdl_renderer_rect.y = 0;
  g_sdl_renderer_rect.w = width;
  g_sdl_renderer_rect.h = height;
  if (SDL_LockTexture(g_texture, &g_sdl_renderer_rect, (void **)pixels, pitch) != 0) {
    printf("Failed to lock texture: %s\n", SDL_GetError());
    return;
  }
}

static void SdlRenderer_EndDraw() {

//  uint64 before = SDL_GetPerformanceCounter();
  SDL_UnlockTexture(g_texture);
//  uint64 after = SDL_GetPerformanceCounter();
//  float v = (double)(after - before) / SDL_GetPerformanceFrequency();
//  printf("%f ms\n", v * 1000);
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, &g_sdl_renderer_rect, NULL);
  SDL_RenderPresent(g_renderer); // vsyncs to 60 FPS?
}

static const struct RendererFuncs kSdlRendererFuncs  = {
  &SdlRenderer_Init,
  &SdlRenderer_Destroy,
  &SdlRenderer_BeginDraw,
  &SdlRenderer_EndDraw,
};

void OpenGLRenderer_Create(struct RendererFuncs *funcs, bool use_opengl_es);

// Shared by the native game and the --emu-rom playable mode so both get the
// same window, renderer, scaling and audio output.
static bool InitializeDisplayAndAudio(SDL_Window **out_window, SDL_AudioDeviceID *out_device) {
  if (g_config.fullscreen == 1)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
  else if (g_config.fullscreen == 2)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN;

  // Window scale (1=100%, 2=200%, 3=300%, etc.)
  g_current_window_scale = (g_config.window_scale == 0) ? 2 : IntMin(g_config.window_scale, kMaxWindowScale);

  // audio_freq: Use common sampling rates (see user config file. values higher than 48000 are not supported.)
  if (g_config.audio_freq < 11025 || g_config.audio_freq > 48000)
    g_config.audio_freq = kDefaultFreq;

  // Currently, the SPC/DSP implementation only supports up to stereo.
  if (g_config.audio_channels < 1 || g_config.audio_channels > 2)
    g_config.audio_channels = kDefaultChannels;

  // audio_samples: power of 2
  if (g_config.audio_samples <= 0 || ((g_config.audio_samples & (g_config.audio_samples - 1)) != 0))
    g_config.audio_samples = kDefaultSamples;

  // set up SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return false;
  }

  bool custom_size  = g_config.window_width != 0 && g_config.window_height != 0;
  int window_width  = custom_size ? g_config.window_width  : g_current_window_scale * g_output_width;
  int window_height = custom_size ? g_config.window_height : g_current_window_scale * g_output_height;

  if (g_config.output_method == kOutputMethod_OpenGL ||
      g_config.output_method == kOutputMethod_OpenGL_ES) {
    g_win_flags |= SDL_WINDOW_OPENGL;
    OpenGLRenderer_Create(&g_renderer_funcs, (g_config.output_method == kOutputMethod_OpenGL_ES));
  } else {
    g_renderer_funcs = kSdlRendererFuncs;
  }

  SDL_Window* window = SDL_CreateWindow(kWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, g_win_flags);
  if(window == NULL) {
    printf("Failed to create window: %s\n", SDL_GetError());
    return false;
  }
  g_window = window;
  SDL_SetWindowHitTest(window, HitTestCallback, NULL);

  if (!g_renderer_funcs.Initialize(window))
    return false;

  SDL_AudioDeviceID device = 0;
  g_audio_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("No mutex");

  if (g_config.enable_audio) {
    if (!OpenAudioDevice(g_config.audio_channels, &device, &g_audio_channels,
                         &g_frames_per_block, &g_audiobuffer)) {
      printf("Failed to open audio device: %s\n", SDL_GetError());
      return false;
    }
    g_audio_device = device;
    g_config.audio_channels = g_audio_channels;
  }

  if (g_config.volume_set)
    MenuHost_SetVolume(g_config.volume);

  *out_window = window;
  *out_device = device;
  return true;
}

static void ShutdownDisplayAndAudio(SDL_Window *window) {
  if (g_audio_device != 0) {
    SDL_PauseAudioDevice(g_audio_device, 1);
    SDL_CloseAudioDevice(g_audio_device);
    g_audio_device = 0;
  }

  SDL_DestroyMutex(g_audio_mutex);
  g_audio_mutex = NULL;
  free(g_audiobuffer);
  g_audiobuffer = NULL;
  g_audiobuffer_cur = NULL;
  g_audiobuffer_end = NULL;

  g_renderer_funcs.Destroy();

  SDL_DestroyWindow(window);
  SDL_Quit();
}

#undef main
int main(int argc, char** argv) {
  argc--, argv++;
  const char *config_file = NULL;
  const char *mods_dir_override = NULL;
  bool no_mods = false;
  if (argc >= 2 && strcmp(argv[0], "--config") == 0) {
    config_file = argv[1];
    argc -= 2, argv += 2;
  } else {
    SwitchDirectory();
  }

  while (argc > 0 && argv[0][0] == '-') {
    long value;
    if (strcmp(argv[0], "--headless") == 0) {
      g_headless = true;
      argc--, argv++;
    } else if (strcmp(argv[0], "--mod-trace") == 0) {
      g_mod_trace = true;
      argc--, argv++;
    } else if (strcmp(argv[0], "--no-mods") == 0) {
      no_mods = true;
      argc--, argv++;
    } else if (strcmp(argv[0], "--mods-dir") == 0) {
      if (argc < 2 || argv[1][0] == 0)
        Die("--mods-dir expects a directory");
      mods_dir_override = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--nwa-hold") == 0) {
      if (argc < 2 || !ParseLongArg(argv[1], &value) || value < 1)
        Die("--nwa-hold expects a positive number of seconds");
      g_nwa_hold_seconds = value;
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--nwa-hold-at") == 0) {
      if (argc < 2 || !ParseLongArg(argv[1], &value) || value < 1)
        Die("--nwa-hold-at expects a positive frame number");
      g_nwa_hold_at = value;
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--load") == 0) {
      if (argc < 2)
        Die("--load expects a .sav file");
      g_load_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--save-replay") == 0) {
      if (argc < 2)
        Die("--save-replay expects a file");
      g_save_replay_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--stop-replay-at") == 0) {
      if (argc < 2 || !ParseLongArg(argv[1], &value) || value < 1)
        Die("--stop-replay-at expects a positive frame number");
      g_stop_replay_at = value;
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--input-script") == 0) {
      if (argc < 2)
        Die("--input-script expects a file");
      g_input_script_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--input-script2") == 0) {
      if (argc < 2)
        Die("--input-script2 expects a file");
      g_input_script2_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--local-player") == 0) {
      if (argc < 2 || !ParseLongArg(argv[1], &value) || value < 0 || value > 1)
        Die("--local-player expects 0 or 1");
      g_local_player = (int)value;
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--emu-rom") == 0) {
      if (argc < 2 || argv[1][0] == 0)
        Die("--emu-rom expects a ROM file");
      g_emu_rom_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--oracle-rom") == 0) {
      if (argc < 2 || argv[1][0] == 0)
        Die("--oracle-rom expects a ROM file");
      g_oracle_rom_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--seed-rom") == 0) {
      if (argc < 2 || argv[1][0] == 0)
        Die("--seed-rom expects a ROM file");
      g_seed_rom_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--oracle-ignore-jp") == 0) {
      g_oracle_ignore_jp = true;
      argc--, argv++;
    } else if (strcmp(argv[0], "--audio-dump") == 0) {
      if (argc < 2 || argv[1][0] == 0)
        Die("--audio-dump expects a .wav file");
      g_audio_dump_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--replay") == 0) {
      if (argc < 2)
        Die("--replay expects a .sav file");
      g_replay_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--replay-ref") == 0) {
      if (argc < 2 || !ParseLongArg(argv[1], &value) || value < 0 || value > 12)
        Die("--replay-ref expects an index between 0 and 12");
      g_replay_ref = (int)value;
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--frames") == 0) {
      if (argc < 2 || !ParseLongArg(argv[1], &value) || value < 0 || value > 100000000)
        Die("--frames expects a non-negative frame count");
      g_max_frames = value;
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--hash-every") == 0) {
      if (argc < 2 || !ParseLongArg(argv[1], &value) || value < 1 || value > 100000000)
        Die("--hash-every expects a positive frame interval");
      g_hash_every = (int)value;
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--dump-state") == 0) {
      g_dump_state = true;
      argc--, argv++;
    } else if (strcmp(argv[0], "--screenshot") == 0) {
      if (argc < 2 || argv[1][0] == 0)
        Die("--screenshot expects a .ppm file");
      g_screenshot_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--menu-test") == 0) {
      if (argc < 2 || argv[1][0] == 0)
        Die("--menu-test expects a zelda3.user.ini path");
      g_menu_test_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--menu-screenshot") == 0) {
      if (argc < 2 || argv[1][0] == 0)
        Die("--menu-screenshot expects a page name");
      g_menu_screenshot_page = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--force-sprite6b") == 0) {
      if (argc < 2 || !ParseLongArg(argv[1], &value) || value < 1 || value > 100000000)
        Die("--force-sprite6b expects a positive frame number");
      g_force_sprite6b_frame = value;
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--render-plugin") == 0) {
      if (argc < 2 || argv[1][0] == 0)
        Die("--render-plugin expects a library path or builtin:ppu");
      g_render_plugin_file = argv[1];
      argc -= 2, argv += 2;
    } else if (strcmp(argv[0], "--frame-check") == 0) {
      g_frame_check = true;
      argc--, argv++;
    } else if (strcmp(argv[0], "--frame-draw") == 0) {
      g_frame_desc_draw = true;
      argc--, argv++;
    } else if (strcmp(argv[0], "--frame-plugin-draw") == 0) {
      g_frame_plugin_draw = true;
      argc--, argv++;
    } else if (strcmp(argv[0], "--frame-desc") == 0) {
      g_frame_desc_capture = true;
      argc--, argv++;
    } else if (strcmp(argv[0], "--frame-desc-dump") == 0) {
      if (argc < 2 || !ParseLongArg(argv[1], &value) || value < 1 || value > 100000000)
        Die("--frame-desc-dump expects a positive frame number");
      g_frame_desc_dump_frame = value;
      argc -= 2, argv += 2;
    } else {
      Die("Unknown command line option");
    }
  }
  if (!g_headless && (g_replay_ref >= 0 || g_replay_file || g_load_file || g_save_replay_file || g_stop_replay_at > 0 || g_input_script_file || g_input_script2_file || g_local_player || g_max_frames >= 0 || g_hash_every != 0 || g_dump_state || g_screenshot_file || g_menu_test_file || g_menu_screenshot_page || g_nwa_hold_seconds || g_nwa_hold_at || g_force_sprite6b_frame >= 0 || g_frame_check || g_frame_desc_capture || g_frame_desc_draw || g_frame_plugin_draw || g_frame_desc_dump_frame >= 0))
    Die("--replay, --replay-ref, --load, --input-script, --input-script2, --local-player, --frames, --hash-every, --dump-state, --screenshot, --menu-test, --menu-screenshot, --nwa-hold, --force-sprite6b and the frame description options require --headless");
  if (g_emu_rom_file && (g_replay_ref >= 0 || g_replay_file || g_load_file || g_save_replay_file || g_stop_replay_at > 0 || g_dump_state || g_frame_check || g_frame_desc_capture || g_frame_desc_draw || g_frame_plugin_draw || g_frame_desc_dump_frame >= 0))
    Die("--emu-rom cannot be combined with replay/load/save or frame description options");
  if (g_oracle_rom_file && (g_frame_check || g_frame_desc_capture || g_frame_desc_draw || g_frame_plugin_draw || g_frame_desc_dump_frame >= 0))
    Die("--oracle-rom does not support frame description options");
  if (g_oracle_rom_file && g_emu_rom_file)
    Die("--oracle-rom and --emu-rom are different modes");
  if (g_seed_rom_file && (g_oracle_rom_file || g_emu_rom_file))
    Die("--seed-rom cannot be combined with --oracle-rom or --emu-rom");
  if (g_seed_rom_file && !g_headless)
    Die("--seed-rom requires --headless");
  if (g_seed_rom_file && (g_replay_ref >= 0 || g_replay_file || g_load_file ||
                          g_save_replay_file || g_stop_replay_at > 0))
    Die("--seed-rom cannot be combined with replay/load/save options");
  if (g_oracle_rom_file && !g_headless)
    Die("--oracle-rom requires --headless");
  if (g_oracle_rom_file && (g_replay_ref >= 0 || g_replay_file || g_load_file || g_save_replay_file || g_stop_replay_at > 0))
    Die("--oracle-rom cannot be combined with replay/load/save options");
  if (g_oracle_ignore_jp && !g_oracle_rom_file)
    Die("--oracle-ignore-jp requires --oracle-rom");
  if (g_audio_dump_file && (!g_headless || !g_emu_rom_file))
    Die("--audio-dump requires --headless --emu-rom");

  ParseConfigFile(config_file);
  if (g_render_plugin_file != NULL)
    g_config.render_plugin = g_render_plugin_file;
  if (g_config.coop_shared_world && g_config.coop_local)
    Die("[Coop] SharedWorld cannot be combined with Local = 1");
  if (g_local_player && !g_config.coop_local)
    Die("--local-player requires [Coop] Local = 1");
  if (g_input_script2_file && !g_config.coop_local && !g_config.coop_shared_world)
    Die("--input-script2 requires [Coop] Local = 1 or SharedWorld = 1");
  if (g_config.coop_local && (g_emu_rom_file || g_oracle_rom_file))
    Die("[Coop] Local does not support --emu-rom or --oracle-rom");
  EmuSetSeedIgnoreKnownJp(g_oracle_ignore_jp);

#ifdef _WIN32
  // Mark the process DPI-aware before SDL creates any window, so Windows does
  // not bitmap-scale it on monitors with display scaling enabled. This also
  // covers the --emu-rom playable mode, which returns early below.
  SetProcessDPIAware();
#endif

  if (g_emu_rom_file)
    return RunEmuRom(g_emu_rom_file);
  if (g_nwa_hold_seconds && !g_config.nwa_enabled)
    Die("--nwa-hold requires EmuNwa = 1 in the [Network] section");
  if (g_nwa_hold_at && !g_nwa_hold_seconds)
    Die("--nwa-hold-at requires --nwa-hold");
  uint32 ini_features0 = g_config.features0;
  if (no_mods)
    g_config.mods_enabled = false;
  if (mods_dir_override != NULL) {
    // An explicitly requested directory is an explicit request to load mods,
    // unless --no-mods also says otherwise.
    g_config.mods_directory = mods_dir_override;
    if (!no_mods)
      g_config.mods_enabled = true;
  }
  LoadAssets();
  LoadLinkGraphics();
  Mods_LoadAll(g_config.mods_enabled ? g_config.mods_directory : NULL);
  OptionsMenu_Init();
  // DimFlashes patches the palette while assets are loaded, before mods can
  // override the feature. Reapply only when a mod changed its effective value.
  bool ini_dim_flashes = (ini_features0 & kFeatures0_DimFlashes) != 0;
  bool effective_dim_flashes = (g_config.features0 & kFeatures0_DimFlashes) != 0;
  if (effective_dim_flashes != ini_dim_flashes)
    ApplyDimFlashesPalette(effective_dim_flashes);
  // Subscribe the trace after the mods so filter events print their final
  // value (scripts may replace, for example, the item in ItemReceive).
  if (g_mod_trace) {
    ModEvents_EnableTrace();
    ModHooks_EnableTrace();
    ModSpriteOverride_EnableTrace();
  }

  ZeldaInitialize();
  if (g_menu_test_file != NULL) {
    bool ok = OptionsMenu_RunHeadlessTest(g_menu_test_file);
    RenderPlugin_Shutdown();
    return ok ? 0 : 1;
  }
  Coop_Start();
  SharedWorld_Start();
  NwaServer_Start(g_config.nwa_enabled, g_config.nwa_port,
                  g_config.nwa_allow_write,
                  (argc >= 1 && !g_run_without_emu) ? argv[0] : NULL);
  g_zenv.ppu->extraLeftRight = UintMin(g_config.extended_aspect_ratio, kPpuExtraLeftRight);
  g_snes_width = (g_config.extended_aspect_ratio * 2 + 256);
  g_snes_height = (g_config.extend_y ? 240 : 224);
  UpdateOutputSize();


  // Delay actually setting those features in ram until any snapshots finish playing.
  g_wanted_zelda_features = g_config.features0;

  g_ppu_render_flags = g_config.new_renderer * kPpuRenderFlags_NewRenderer |
                       g_config.enhanced_mode7 * kPpuRenderFlags_4x4Mode7 |
                       g_config.extend_y * kPpuRenderFlags_Height240 |
                       g_config.no_sprite_limits * kPpuRenderFlags_NoSpriteLimits;
  bool render_plugin_active = false;
  if (g_config.render_plugin != NULL && g_config.render_plugin[0] != 0) {
    render_plugin_active = RenderPlugin_Load(g_config.render_plugin);
    if (!render_plugin_active)
      fprintf(stderr, "render-plugin: keeping the built-in renderer\n");
    else
      RenderPlugin_SetEnabled(g_config.render_plugin_enabled);
  }
  {
    bool frame_probe = g_config.frame_description || g_frame_check ||
                       g_frame_desc_capture || g_frame_desc_draw ||
                       g_frame_desc_dump_frame >= 0 || render_plugin_active;
    FrameDesc_SetProbeEnabled(frame_probe);
    FrameDesc_SetCaptureEnabled(g_config.frame_description ||
                                g_frame_desc_capture ||
                                render_plugin_active);
  }
  ZeldaEnableMsu(g_config.enable_msu);
  ZeldaSetLanguage(g_config.language);

  if (g_headless) {
    int rc = RunHeadless(g_seed_rom_file ? g_seed_rom_file
                        : g_oracle_rom_file ? g_oracle_rom_file
                                           : (argc >= 1 ? argv[0] : NULL));
    RenderPlugin_Shutdown();
    return rc;
  }

  SDL_Window *window;
  SDL_AudioDeviceID device;
  if (!InitializeDisplayAndAudio(&window, &device))
    return 1;

  if (argc >= 1 && !g_run_without_emu)
    LoadRom(argv[0]);

#if defined(_WIN32)
  _mkdir("saves");
#else
  mkdir("saves", 0755);
#endif

  ZeldaReadSram();

  for (int i = 0; i < SDL_NumJoysticks(); i++)
    OpenOneGamepad(i);

  bool running = true;
  SDL_Event event;
  uint32 lastTick = SDL_GetTicks();
  uint32 curTick = 0;
  uint32 frameCtr = 0;
  bool audiopaused = true;

  if (g_config.autosave)
    HandleCommand(kKeys_Load + 0, true);

  // The clone is made after autosave loading so both instances start from the
  // exact same state.
  LocalCoop_Start();

  while(running) {
    while(SDL_PollEvent(&event)) {
      switch(event.type) {
      case SDL_CONTROLLERDEVICEADDED:
        OpenOneGamepad(event.cdevice.which);
        break;
      case SDL_CONTROLLERAXISMOTION:
        HandleGamepadAxisInput(event.caxis.which, event.caxis.axis, event.caxis.value);
        break;
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        int b = RemapSdlButton(event.cbutton.button);
        if (b >= 0)
          HandleGamepadInputForPlayer(b, event.type == SDL_CONTROLLERBUTTONDOWN,
                                      ControllerPlayer(event.cbutton.which));
        break;
      }
      case SDL_MOUSEWHEEL:
        if (SDL_GetModState() & KMOD_CTRL && event.wheel.y != 0)
          ChangeWindowScale(event.wheel.y > 0 ? 1 : -1);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.state == SDL_PRESSED && event.button.clicks == 2) {
          if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && (g_win_flags & SDL_WINDOW_FULLSCREEN) == 0 && SDL_GetModState() & KMOD_SHIFT) {
            g_win_flags ^= SDL_WINDOW_BORDERLESS;
            SDL_SetWindowBordered(g_window, (g_win_flags & SDL_WINDOW_BORDERLESS) == 0);
          }
        }
        break;
      case SDL_KEYDOWN:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, true);
        break;
      case SDL_KEYUP:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, false);
        break;
      case SDL_QUIT:
        running = false;
        break;
      }
    }

    NwaServer_Poll();

    bool menu_open = OptionsMenu_IsOpen();
    bool paused = g_paused || NwaServer_IsPaused() || menu_open;
    if (paused != audiopaused) {
      audiopaused = paused;
      if (g_audio_device)
        SDL_PauseAudioDevice(g_audio_device, audiopaused);
    }

    if (paused) {
      // Keep drawing the frozen game with the menu overlay on top. The
      // simulation is not stepped, so opening the menu pauses the game.
      if (menu_open && !g_paused && !NwaServer_IsPaused())
        DrawFrozenFrameWithMenu();
      SDL_Delay(16);
      continue;
    }

    // Clear gamepad inputs when joypad directional inputs to avoid wonkiness
    int inputs1 = g_input1_state;
    if (g_input1_state & 0xf0)
      g_gamepad_buttons = 0;
    inputs1 |= g_gamepad_buttons;
    int inputs2 = g_input2_state;
    if (g_input2_state & 0xf0)
      g_gamepad2_buttons = 0;
    inputs2 |= g_gamepad2_buttons;

    bool is_replay = false;
    bool local_drawn = false;
    if (LocalCoop_IsActive()) {
      // Render each pane right after its simulation. This keeps mod draw
      // commands with the instance that emitted them and leaves only one
      // round trip of RAM copies per frame.
      bool draw_local = !g_turbo || (frameCtr & 0xf) == 0;
      uint8 *pixel_buffer = 0;
      int pitch = 0;
      if (draw_local)
        g_renderer_funcs.BeginDraw(g_output_width, g_output_height,
                                   &pixel_buffer, &pitch);
      for (int p = 0; p < 2; p++) {
        LocalCoop_Select(p);
        SDL_LockMutex(g_audio_mutex);
        bool replay = ZeldaRunFrame(p == 0 ? inputs1 : inputs2);
        SDL_UnlockMutex(g_audio_mutex);
        Coop_FrameStep();
        is_replay |= replay;
        if (draw_local) {
          uint8 *pane = pixel_buffer +
              (LocalCoop_IsVertical() ? (size_t)p * g_snes_height * pitch
                                      : (size_t)p * g_snes_width * 4);
          RenderInstancePane(p, pane, pitch, 1);
        }
      }
      LocalCoop_Select(0);
      if (draw_local) {
        CaptureFrameSnapshot(pixel_buffer, pitch, g_output_width, g_output_height);
        g_renderer_funcs.EndDraw();
      }
      local_drawn = true;
    } else {
      SharedWorld_SetInput2((uint16)inputs2);
      SDL_LockMutex(g_audio_mutex);
      is_replay = ZeldaRunFrame(inputs1);
      SDL_UnlockMutex(g_audio_mutex);
      Coop_FrameStep();
    }

    frameCtr++;

    // The Model B camera can split or merge at any frame. Follow the new
    // output size (and resize the window) before drawing.
    if (SharedWorld_IsActive() && SharedWorld_OutputWidth(g_snes_width) != g_output_width)
      UpdateOutputSize();

    if (!local_drawn) {
      if ((g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0)
        continue;
      DrawPpuFrameWithPerf();
    }

    if (g_config.display_perf_title) {
      char title[60];
      snprintf(title, sizeof(title), "%s | FPS: %d", kWindowTitle, g_curr_fps);
      SDL_SetWindowTitle(g_window, title);
    }

    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();

    if (!g_config.disable_frame_delay) {
      static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
      lastTick += delays[frameCtr % 3];

      if (lastTick > curTick) {
        uint32 delta = lastTick - curTick;
        if (delta > 500) {
          lastTick = curTick - 500;
          delta = 500;
        }
//        printf("Sleeping %d\n", delta);
        SDL_Delay(delta);
      } else if (curTick - lastTick > 500) {
        lastTick = curTick;
      }
    }
  }
  if (OptionsMenu_IsOpen())
    OptionsMenu_Close();
  if (g_config.autosave)
    HandleCommand(kKeys_Save + 0, true);

  LocalCoop_Stop();
  SharedWorld_Stop();
  Coop_Stop();
  NwaServer_Stop();
  RenderPlugin_Shutdown();

  ShutdownDisplayAndAudio(window);
  //SaveConfigFile();
  return 0;
}

static void RenderDigit(uint8 *dst, size_t pitch, int digit, uint32 color, bool big) {
  static const uint8 kFont[] = {
    0x1c, 0x36, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x36, 0x1c,
    0x18, 0x1c, 0x1e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e,
    0x3e, 0x63, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x63, 0x7f,
    0x3e, 0x63, 0x60, 0x60, 0x3c, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x30, 0x38, 0x3c, 0x36, 0x33, 0x7f, 0x30, 0x30, 0x30, 0x78,
    0x7f, 0x03, 0x03, 0x03, 0x3f, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x1c, 0x06, 0x03, 0x03, 0x3f, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x7f, 0x63, 0x60, 0x60, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c,
    0x3e, 0x63, 0x63, 0x63, 0x3e, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x3e, 0x63, 0x63, 0x63, 0x7e, 0x60, 0x60, 0x60, 0x30, 0x1e,
  };
  const uint8 *p = kFont + digit * 10;
  if (!big) {
    for (int y = 0; y < 10; y++, dst += pitch) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1)
          ((uint32 *)dst)[x] = color;
      }
    }
  } else {
    for (int y = 0; y < 10; y++, dst += pitch * 2) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1) {
          ((uint32 *)dst)[x * 2 + 1] = ((uint32 *)dst)[x * 2] = color;
          ((uint32 *)(dst+pitch))[x * 2 + 1] = ((uint32 *)(dst + pitch))[x * 2] = color;
        }
      }
    }
  }
}

static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big) {
  char buf[32], *s;
  int i;
  sprintf(buf, "%d", n);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + ((pitch + i + 4) << big), pitch, *s - '0', 0x404040, big);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + (i << big), pitch, *s - '0', 0xffffff, big);
}

static void HandleCommand_Locked(uint32 j, bool pressed);

// Native-only commands (savestates, replays, cheats, features) do not touch
// the full-ROM emulator.  Keep the few commands that make sense in that mode.
static void HandleEmuCommand(uint32 j, bool pressed) {
  if (!pressed)
    return;
  switch (j) {
  case kKeys_Fullscreen:
    g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
    g_cursor = !g_cursor;
    SDL_ShowCursor(g_cursor);
    break;
  case kKeys_Reset:
    if (g_emu_snes != NULL) {
      SDL_LockMutex(g_audio_mutex);
      snes_softResetPreserveSram(g_emu_snes);
      SDL_UnlockMutex(g_audio_mutex);
    }
    break;
  case kKeys_Pause:
  case kKeys_PauseDimmed:
    g_paused = !g_paused;
    break;
  case kKeys_WindowBigger: ChangeWindowScale(1); break;
  case kKeys_WindowSmaller: ChangeWindowScale(-1); break;
  case kKeys_DisplayPerf: g_display_perf ^= 1; break;
  case kKeys_VolumeUp:
  case kKeys_VolumeDown: HandleVolumeAdjustment(j == kKeys_VolumeUp ? 1 : -1); break;
  default:
    break;
  }
}

static void HandleCommandForPlayer(uint32 j, bool pressed, int player) {
  if (j <= kKeys_Controls_Last) {
    static const uint8 kKbdRemap[] = { 0, 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
    int *state = player == 0 ? &g_input1_state : &g_input2_state;
    if (pressed)
      *state |= 1 << kKbdRemap[j];
    else
      *state &= ~(1 << kKbdRemap[j]);
    return;
  }

  // Player 2 only has game controls; save states, cheats and window commands
  // stay with player 1.
  if (player != 0)
    return;

  if (g_emu_mode) {
    HandleEmuCommand(j, pressed);
    return;
  }

  if (j == kKeys_Turbo) {
    g_turbo = pressed;
    return;
  }

  // Everything that might access audio state
  // (like SaveLoad and Reset) must have the lock.
  SDL_LockMutex(g_audio_mutex);
  HandleCommand_Locked(j, pressed);
  SDL_UnlockMutex(g_audio_mutex);
}

static void HandleCommand(uint32 j, bool pressed) {
  HandleCommandForPlayer(j, pressed, 0);
}

void ZeldaApuLock() {
  SDL_LockMutex(g_audio_mutex);
}

void ZeldaApuUnlock() {
  SDL_UnlockMutex(g_audio_mutex);
}


static void HandleCommand_Locked(uint32 j, bool pressed) {
  if (!pressed)
    return;
  if (j <= kKeys_Load_Last) {
    SaveLoadSlot(kSaveLoad_Load, j - kKeys_Load);
    SharedWorld_ResetFromCurrent();
  } else if (j <= kKeys_Save_Last) {
    SaveLoadSlot(kSaveLoad_Save, j - kKeys_Save);
  } else if (j <= kKeys_Replay_Last) {
    SaveLoadSlot(kSaveLoad_Replay, j - kKeys_Replay);
    SharedWorld_ResetFromCurrent();
  } else if (j <= kKeys_LoadRef_Last) {
    SaveLoadSlot(kSaveLoad_Load, 256 + j - kKeys_LoadRef);
    SharedWorld_ResetFromCurrent();
  } else if (j <= kKeys_ReplayRef_Last) {
    SaveLoadSlot(kSaveLoad_Replay, 256 + j - kKeys_ReplayRef);
    SharedWorld_ResetFromCurrent();
  } else {
    switch (j) {
    case kKeys_CheatLife: PatchCommand('w'); break;
    case kKeys_CheatEquipment: PatchCommand('W'); break;
    case kKeys_CheatKeys: PatchCommand('o'); break;
    case kKeys_CheatWalkThroughWalls: PatchCommand('E'); break;
    case kKeys_ClearKeyLog: PatchCommand('k'); break;
    case kKeys_StopReplay: PatchCommand('l'); break;
    case kKeys_Fullscreen:
      g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
      SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
      g_cursor = !g_cursor;
      SDL_ShowCursor(g_cursor);
      break;
    case kKeys_Reset:
      ZeldaReset(true);
      SharedWorld_ResetFromCurrent();
      break;
    case kKeys_Pause: g_paused = !g_paused; break;
    case kKeys_PauseDimmed:
      g_paused = !g_paused;
      // SDL_RenderPresent may not be called more than once per frame.
      // Seems to work on Windows still. Temporary measure until it's fixed.
#ifdef _WIN32
      if (g_paused) {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 159);
        SDL_RenderFillRect(g_renderer, NULL);
        SDL_RenderPresent(g_renderer);
      }
#endif
      break;
    case kKeys_ReplayTurbo: g_replay_turbo = !g_replay_turbo; break;
    case kKeys_WindowBigger: ChangeWindowScale(1); break;
    case kKeys_WindowSmaller: ChangeWindowScale(-1); break;
    case kKeys_DisplayPerf: g_display_perf ^= 1; break;
    case kKeys_ToggleRenderer: g_ppu_render_flags ^= kPpuRenderFlags_NewRenderer; break;
    case kKeys_ToggleVoxel:
      if (RenderPlugin_IsActive()) {
        RenderPlugin_SetEnabled(!RenderPlugin_IsEnabled());
        printf("[Voxel] %s\n", RenderPlugin_IsEnabled() ? "on" : "off");
        fflush(stdout);
      }
      break;
    case kKeys_VoxelCameraLeft:
      RenderPlugin_Command(kRenderCommand_CameraRotateLeft);
      break;
    case kKeys_VoxelCameraRight:
      RenderPlugin_Command(kRenderCommand_CameraRotateRight);
      break;
    case kKeys_VoxelCameraUp:
      RenderPlugin_Command(kRenderCommand_CameraTiltUp);
      break;
    case kKeys_VoxelCameraDown:
      RenderPlugin_Command(kRenderCommand_CameraTiltDown);
      break;
    case kKeys_VoxelCameraZoomIn:
      RenderPlugin_Command(kRenderCommand_CameraZoomIn);
      break;
    case kKeys_VoxelCameraZoomOut:
      RenderPlugin_Command(kRenderCommand_CameraZoomOut);
      break;
    case kKeys_VoxelCameraReset:
      RenderPlugin_Command(kRenderCommand_CameraReset);
      break;
    case kKeys_VoxelShadows:
      RenderPlugin_Command(kRenderCommand_ToggleShadows);
      break;
    case kKeys_VoxelLighting:
      RenderPlugin_Command(kRenderCommand_ToggleLighting);
      break;
    case kKeys_VoxelLevelLow:
      RenderPlugin_Command(kRenderCommand_CameraLevelLow);
      break;
    case kKeys_VoxelLevelMid:
      RenderPlugin_Command(kRenderCommand_CameraLevelMid);
      break;
    case kKeys_VoxelLevelHigh:
      RenderPlugin_Command(kRenderCommand_CameraLevelHigh);
      break;
    case kKeys_VolumeUp:
    case kKeys_VolumeDown: HandleVolumeAdjustment(j == kKeys_VolumeUp ? 1 : -1); break;
    case kKeys_OptionsMenu: OptionsMenu_Toggle(); break;
    default: assert(0);
    }
  }
}

static bool SecondPlayerControlsActive(void) {
  return LocalCoop_IsActive() || SharedWorld_IsActive();
}

static void HandleInput(int keyCode, int keyMod, bool pressed) {
  if (OptionsMenu_IsOpen()) {
    OptionsMenu_HandleKey(keyCode, keyMod, pressed);
    return;
  }
  if (SecondPlayerControlsActive()) {
    int j2 = FindCmdForSdlKey2(keyCode, keyMod);
    if (j2 >= kKeys_Controls && j2 <= kKeys_Controls_Last) {
      HandleCommandForPlayer(j2, pressed, 1);
      return;
    }
  }
  int j = FindCmdForSdlKey(keyCode, keyMod);
  if (j != 0)
    HandleCommand(j, pressed);
}

static void OpenOneGamepad(int i) {
  if (SDL_IsGameController(i)) {
    SDL_GameController *controller = SDL_GameControllerOpen(i);
    if (!controller) {
      fprintf(stderr, "Could not open gamepad %d: %s\n", i, SDL_GetError());
      return;
    }
    if (g_controller_count < kMaxOpenControllers) {
      SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller);
      g_controller_ids[g_controller_count++] = SDL_JoystickInstanceID(joystick);
    }
  }
}

// Controllers are assigned in the order they are opened: the first one is
// player 1 and any later one drives player 2 in local split-screen.
static int ControllerPlayer(SDL_JoystickID id) {
  if (!SecondPlayerControlsActive())
    return 0;
  for (int i = 0; i < g_controller_count; i++) {
    if (g_controller_ids[i] == id)
      return i == 0 ? 0 : 1;
  }
  return 0;
}

static int RemapSdlButton(int button) {
  switch (button) {
  case SDL_CONTROLLER_BUTTON_A: return kGamepadBtn_A;
  case SDL_CONTROLLER_BUTTON_B: return kGamepadBtn_B;
  case SDL_CONTROLLER_BUTTON_X: return kGamepadBtn_X;
  case SDL_CONTROLLER_BUTTON_Y: return kGamepadBtn_Y;
  case SDL_CONTROLLER_BUTTON_BACK: return kGamepadBtn_Back;
  case SDL_CONTROLLER_BUTTON_GUIDE: return kGamepadBtn_Guide;
  case SDL_CONTROLLER_BUTTON_START: return kGamepadBtn_Start;
  case SDL_CONTROLLER_BUTTON_LEFTSTICK: return kGamepadBtn_L3;
  case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return kGamepadBtn_R3;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kGamepadBtn_L1;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kGamepadBtn_R1;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return kGamepadBtn_DpadUp;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kGamepadBtn_DpadDown;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kGamepadBtn_DpadLeft;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kGamepadBtn_DpadRight;
  default: return -1;
  }
}

static void HandleGamepadInputForPlayer(int button, bool pressed, int player) {
  if (OptionsMenu_IsOpen()) {
    if (player == 0)
      OptionsMenu_HandleGamepadButton(button, pressed);
    return;
  }
  if (!!(g_gamepad_modifiers[player] & (1 << button)) == pressed)
    return;
  g_gamepad_modifiers[player] ^= 1 << button;
  if (pressed) {
    g_gamepad_last_cmd[player][button] = player == 0
        ? FindCmdForGamepadButton(button, g_gamepad_modifiers[player])
        : FindCmdForGamepadButton2(button, g_gamepad_modifiers[player]);
  }
  if (g_gamepad_last_cmd[player][button] != 0)
    HandleCommandForPlayer(g_gamepad_last_cmd[player][button], pressed, player);
}

static void HandleVolumeAdjustment(int volume_adjustment) {
#if SYSTEM_VOLUME_MIXER_AVAILABLE
  int current_volume = GetApplicationVolume();
  int new_volume = IntMin(IntMax(0, current_volume + volume_adjustment * 5), 100);
  SetApplicationVolume(new_volume);
  printf("[System Volume]=%i\n", new_volume);
#else
  g_sdl_audio_mixer_volume = IntMin(IntMax(0, g_sdl_audio_mixer_volume + volume_adjustment * (SDL_MIX_MAXVOLUME >> 4)), SDL_MIX_MAXVOLUME);
  printf("[SDL mixer volume]=%i\n", g_sdl_audio_mixer_volume);
#endif
}

// Approximates atan2(y, x) normalized to the [0,4) range
// with a maximum error of 0.1620 degrees
// normalized_atan(x) ~ (b x + x^2) / (1 + 2 b x + x^2)
static float ApproximateAtan2(float y, float x) {
  uint32 sign_mask = 0x80000000;
  float b = 0.596227f;
  // Extract the sign bits
  uint32 ux_s = sign_mask & *(uint32 *)&x;
  uint32 uy_s = sign_mask & *(uint32 *)&y;
  // Determine the quadrant offset
  float q = (float)((~ux_s & uy_s) >> 29 | ux_s >> 30);
  // Calculate the arctangent in the first quadrant
  float bxy_a = b * x * y;
  if (bxy_a < 0.0f) bxy_a = -bxy_a;  // avoid fabs
  float num = bxy_a + y * y;
  float atan_1q = num / (x * x + bxy_a + num + 0.000001f);
  // Translate it to the proper quadrant
  uint32_t uatan_2q = (ux_s ^ uy_s) | *(uint32 *)&atan_1q;
  return q + *(float *)&uatan_2q;
}

static void HandleGamepadAxisInput(int gamepad_id, int axis, int value) {
  static int last_gamepad_id[2], last_x[2], last_y[2];
  int player = ControllerPlayer((SDL_JoystickID)gamepad_id);
  if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY) {
    // ignore other gamepads unless they have a big input
    if (last_gamepad_id[player] != gamepad_id) {
      if (value > -16000 && value < 16000)
        return;
      last_gamepad_id[player] = gamepad_id;
      last_x[player] = last_y[player] = 0;
    }
    int *cur = axis == SDL_CONTROLLER_AXIS_LEFTX ? &last_x[player]
                                                 : &last_y[player];
    *cur = value;
    int buttons = 0;
    if (last_x[player] * last_x[player] + last_y[player] * last_y[player] >= 10000 * 10000) {
      // in the non deadzone part, divide the circle into eight 45 degree
      // segments rotated by 22.5 degrees that control which direction to move.
      // todo: do this without floats?
      static const uint8 kSegmentToButtons[8] = {
        1 << 4,           // 0 = up
        1 << 4 | 1 << 7,  // 1 = up, right
        1 << 7,           // 2 = right
        1 << 7 | 1 << 5,  // 3 = right, down
        1 << 5,           // 4 = down
        1 << 5 | 1 << 6,  // 5 = down, left
        1 << 6,           // 6 = left
        1 << 6 | 1 << 4,  // 7 = left, up
      };
      uint8 angle = (uint8)(int)(ApproximateAtan2(last_y[player], last_x[player]) * 64.0f + 0.5f);
      buttons = kSegmentToButtons[(uint8)(angle + 16 + 64) >> 5];
    }
    if (player == 0)
      g_gamepad_buttons = buttons;
    else
      g_gamepad2_buttons = buttons;
  } else if ((axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
    if (value < 12000 || value >= 16000)  // hysteresis
      HandleGamepadInputForPlayer(axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? kGamepadBtn_L2 : kGamepadBtn_R2,
                                  value >= 12000, player);
  }
}

static bool LoadRom(const char *filename) {
  size_t length = 0;
  uint8 *file = ReadWholeFile(filename, &length);
  if(!file) Die("Failed to read file");
  bool seed = g_oracle_rom_file || g_seed_rom_file;
  bool result = seed ? EmuInitializeSeed(file, length) : EmuInitialize(file, length);
  free(file);
  if (!result && seed)
    Die("--oracle-rom/--seed-rom: failed to load seed ROM");
  if (g_seed_rom_file)
    ZeldaSetEmuOracleEnabled(false);  // C-only seed mode for route search
  return result;
}

static bool ParseLongArg(const char *s, long *value) {
  char *end;
  if (*s == 0)
    return false;
  long v = strtol(s, &end, 10);
  if (*end != 0)
    return false;
  *value = v;
  return true;
}

static uint32 Fnv1aUpdate(uint32 hash, const void *data, size_t size) {
  const uint8 *p = (const uint8 *)data;
  while (size--)
    hash = (hash ^ *p++) * 16777619u;
  return hash;
}

static uint32 HashState() {
  uint32 hash = 2166136261u;
  hash = Fnv1aUpdate(hash, g_zenv.ram, 0x20000);
  hash = Fnv1aUpdate(hash, g_zenv.sram, 0x2000);
  hash = Fnv1aUpdate(hash, g_zenv.vram, 0x8000 * sizeof(uint16));
  return hash;
}

// Renders one frame only after the headless run has printed its final hash,
// then emits FrameDraw and saves the buffer as a binary PPM (P6). It must stay
// at the end: ZeldaDrawPpuFrame writes PPU/HDMA state and would change a
// running simulation.
static void WriteScreenshot(const char *path) {
  int scale = 1;
  int width, height;
  if (LocalCoop_IsActive()) {
    width = g_output_width;
    height = g_output_height;
  } else if (SharedWorld_IsSplit()) {
    scale = EffectiveRenderScale();
    width = SharedWorld_OutputWidth(g_snes_width) * scale;
    height = g_snes_height * scale;
  } else {
    scale = EffectiveRenderScale();
    width = g_snes_width * scale;
    height = g_snes_height * scale;
  }
  size_t pitch = (size_t)width * 4;
  uint8 *buffer = malloc(pitch * (size_t)height);
  if (buffer == NULL)
    Die("--screenshot: out of memory");

  if (LocalCoop_IsActive()) {
    for (int i = 0; i < 2; i++) {
      uint8 *pane = buffer +
          (LocalCoop_IsVertical() ? (size_t)i * g_snes_height * pitch
                                  : (size_t)i * g_snes_width * 4);
      RenderInstancePane(i, pane, pitch, 1);
    }
    LocalCoop_Select(0);
  } else {
    uint32 flags = g_ppu_render_flags;
    if (SharedWorld_IsActive())
      flags |= kPpuRenderFlags_NoSpriteLimits;
    if (!DrawFrameWithPlugin(buffer, pitch, scale, flags))
      SharedWorld_DrawFrame(buffer, pitch, flags, g_snes_width * scale);
    RenderWorldModOverlays(buffer, pitch, scale, g_snes_width * scale);
  }

  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    free(buffer);
    Die("--screenshot: cannot write file");
  }
  fprintf(f, "P6\n%d %d\n255\n", width, height);
  for (int y = 0; y < height; y++) {
    const uint32 *row = (const uint32 *)(buffer + (size_t)y * pitch);
    for (int x = 0; x < width; x++) {
      uint32 pixel = row[x];
      uint8 rgb[3];
      rgb[0] = (uint8)(pixel >> 16);
      rgb[1] = (uint8)(pixel >> 8);
      rgb[2] = (uint8)pixel;
      if (fwrite(rgb, 1, 3, f) != 3) {
        fclose(f);
        free(buffer);
        Die("--screenshot: write failed");
      }
    }
  }
  if (fclose(f) != 0) {
    free(buffer);
    Die("--screenshot: write failed");
  }
  free(buffer);
}

static uint32 HashEmuState(Snes *snes) {
  uint32 hash = 2166136261u;
  hash = Fnv1aUpdate(hash, snes->ram, 0x20000);
  hash = Fnv1aUpdate(hash, snes->cart->ram, snes->cart->ramSize);
  hash = Fnv1aUpdate(hash, snes->ppu->vram, sizeof(snes->ppu->vram));
  return hash;
}

static void WriteEmuScreenshot(const char *path, const uint8 *pixels,
                               int width, int height, size_t pitch) {
  FILE *f = fopen(path, "wb");
  if (f == NULL)
    Die("--screenshot: cannot write file");
  fprintf(f, "P6\n%d %d\n255\n", width, height);
  for (int y = 0; y < height; y++) {
    const uint32 *row = (const uint32 *)(pixels + (size_t)y * pitch);
    for (int x = 0; x < width; x++) {
      uint32 pixel = row[x];
      uint8 rgb[3] = { (uint8)(pixel >> 16), (uint8)(pixel >> 8), (uint8)pixel };
      if (fwrite(rgb, 1, 3, f) != 3) {
        fclose(f);
        Die("--screenshot: write failed");
      }
    }
  }
  if (fclose(f) != 0)
    Die("--screenshot: write failed");
}

// The seed's battery-backed SRAM lives in seed.srm next to the seed ROM and
// never in the native saves/sram.dat.  Compare against the last written copy
// after every frame and on exit.
typedef struct EmuSram {
  char *path;
  uint8 *snapshot;
  uint32 size;
} EmuSram;

static char *EmuSrmPath(const char *rom_path) {
  const char *base = rom_path;
  for (const char *p = rom_path; *p; p++) {
    if (*p == '/' || *p == '\\')
      base = p + 1;
  }
  const char *dot = strrchr(base, '.');
  size_t stem = (dot != NULL && dot > base) ? (size_t)(dot - rom_path) : strlen(rom_path);
  char *path = (char *)malloc(stem + 5);
  if (path == NULL)
    Die("--emu-rom: out of memory");
  memcpy(path, rom_path, stem);
  memcpy(path + stem, ".srm", 5);
  return path;
}

static void EmuSramInit(EmuSram *sram, const char *rom_path, uint8 *ram, uint32 size) {
  memset(sram, 0, sizeof(*sram));
  sram->path = EmuSrmPath(rom_path);
  sram->size = size;
  sram->snapshot = (uint8 *)malloc(size != 0 ? size : 1);
  if (sram->snapshot == NULL)
    Die("--emu-rom: out of memory");
  FILE *f = fopen(sram->path, "rb");
  if (f != NULL) {
    size_t n = fread(ram, 1, size, f);
    fclose(f);
    if (n != size)
      fprintf(stderr, "emu: short SRAM %s (%u/%u bytes)\n", sram->path,
              (unsigned)n, (unsigned)size);
    else
      fprintf(stderr, "emu: loaded SRAM %s\n", sram->path);
  } else {
    fprintf(stderr, "emu: no SRAM at %s (starting empty)\n", sram->path);
  }
  if (size != 0)
    memcpy(sram->snapshot, ram, size);
}

static bool EmuSramWrite(EmuSram *sram, const uint8 *ram, bool announce) {
  if (sram->size == 0)
    return false;
  FILE *f = fopen(sram->path, "wb");
  if (f == NULL) {
    fprintf(stderr, "emu: cannot write SRAM %s\n", sram->path);
    return false;
  }
  bool ok = fwrite(ram, 1, sram->size, f) == sram->size;
  if (fclose(f) != 0)
    ok = false;
  if (!ok) {
    fprintf(stderr, "emu: failed to write SRAM %s\n", sram->path);
    return false;
  }
  memcpy(sram->snapshot, ram, sram->size);
  if (announce)
    fprintf(stderr, "emu: saved SRAM %s\n", sram->path);
  return true;
}

static bool EmuSramSaveIfChanged(EmuSram *sram, const uint8 *ram, bool announce) {
  if (sram->size == 0 || memcmp(sram->snapshot, ram, sram->size) == 0)
    return false;
  return EmuSramWrite(sram, ram, announce);
}

static void EmuSramFree(EmuSram *sram) {
  free(sram->path);
  free(sram->snapshot);
  memset(sram, 0, sizeof(*sram));
}

// Minimal PCM WAV writer for the --headless --emu-rom audio check.
static void EmuWavPut16(uint8 *p, uint16 v) {
  p[0] = (uint8)v;
  p[1] = (uint8)(v >> 8);
}

static void EmuWavPut32(uint8 *p, uint32 v) {
  p[0] = (uint8)v;
  p[1] = (uint8)(v >> 8);
  p[2] = (uint8)(v >> 16);
  p[3] = (uint8)(v >> 24);
}

static FILE *EmuWavOpen(const char *path) {
  FILE *f = fopen(path, "wb");
  if (f == NULL)
    Die("--audio-dump: cannot write file");
  uint8 header[44] = { 0 };
  if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
    fclose(f);
    Die("--audio-dump: write failed");
  }
  return f;
}

static void EmuWavClose(FILE *f, const char *path, uint32 frames, uint32 freq,
                        uint16 channels) {
  uint32 data_size = frames * channels * 2;
  uint8 header[44];
  memcpy(header, "RIFF", 4);
  EmuWavPut32(header + 4, 36 + data_size);
  memcpy(header + 8, "WAVEfmt ", 8);
  EmuWavPut32(header + 16, 16);
  EmuWavPut16(header + 20, 1);
  EmuWavPut16(header + 22, channels);
  EmuWavPut32(header + 24, freq);
  EmuWavPut32(header + 28, freq * channels * 2);
  EmuWavPut16(header + 32, channels * 2);
  EmuWavPut16(header + 34, 16);
  memcpy(header + 36, "data", 4);
  EmuWavPut32(header + 40, data_size);
  bool ok = fseek(f, 0, SEEK_SET) == 0 && fwrite(header, 1, sizeof(header), f) == sizeof(header);
  if (fclose(f) != 0 || !ok)
    Die("--audio-dump: write failed");
  fprintf(stderr, "emu: wrote %u audio frames to %s\n", (unsigned)frames, path);
}

static int RunEmuRomHeadless(Snes *snes, EmuSram *sram) {
  long frames = g_max_frames < 0 ? 600 : g_max_frames;
  const int width = 256, height = 224;
  size_t pitch = (size_t)width * 4;
  uint8 *pixels = malloc(pitch * 262);
  if (!pixels)
    Die("--emu-rom: out of memory");

  FILE *wav = NULL;
  uint32 wav_frames = 0;
  if (g_audio_dump_file != NULL)
    wav = EmuWavOpen(g_audio_dump_file);

  uint64 start = SDL_GetPerformanceCounter();
  long frame = 0;
  while (frame < frames) {
    NwaServer_Poll();
    if (NwaServer_IsPaused()) {
      SDL_Delay(2);
      continue;
    }
    int script_inputs = 0;
    for (int si = 0; si < g_input_script_count && g_input_script_frame[si] <= frame + 1; si++)
      script_inputs = g_input_script_buttons[si];
    snes->input1->currentState = script_inputs;
    PpuBeginDrawing(snes->ppu, pixels, pitch, kPpuRenderFlags_NewRenderer);
    snes_runFrame(snes);
    frame++;
    if (wav != NULL) {
      int16 samples[534 * 2];
      dsp_getSamples(snes->apu->dsp, samples, 534, 2);
      if (fwrite(samples, sizeof(int16), 534 * 2, wav) != 534 * 2)
        Die("--audio-dump: write failed");
      wav_frames += 534;
    }
    EmuSramSaveIfChanged(sram, snes->cart->ram, false);
    if (g_hash_every > 0 && frame % g_hash_every == 0) {
      printf("emu frame %ld %08x\n", frame, HashEmuState(snes));
      fflush(stdout);
    }
    if (g_nwa_hold_seconds > 0 && frame == g_nwa_hold_at)
      NwaHoldFor(g_nwa_hold_seconds);
  }
  uint64 end = SDL_GetPerformanceCounter();
  double elapsed = frames > 0 ? (double)(end - start) / SDL_GetPerformanceFrequency() : 0.0;
  printf("emu: %ld frames in %.3f s => %.1f fps\n", frames, elapsed,
         elapsed > 0.0 ? frames / elapsed : 0.0);
  printf("emu: final state %08x\n", HashEmuState(snes));

  if (g_nwa_hold_seconds > 0 && g_nwa_hold_at == 0)
    NwaHoldFor(g_nwa_hold_seconds);

  if (wav != NULL)
    EmuWavClose(wav, g_audio_dump_file, wav_frames, 32040, 2);
  if (g_screenshot_file)
    WriteEmuScreenshot(g_screenshot_file, pixels, width, height, pitch);
  if (EmuSramSaveIfChanged(sram, snes->cart->ram, false))
    fprintf(stderr, "emu: saved SRAM %s\n", sram->path);
  fflush(stdout);

  free(pixels);
  return 0;
}

static int RunEmuRomInteractive(Snes *snes, EmuSram *sram) {
  g_emu_mode = true;
  g_emu_snes = snes;
  g_emu_dsp = snes->apu->dsp;
  g_snes_width = 256;
  g_snes_height = 224;
  UpdateOutputSize();
  g_ppu_render_flags = kPpuRenderFlags_NewRenderer;

  SDL_Window *window;
  SDL_AudioDeviceID device;
  if (!InitializeDisplayAndAudio(&window, &device)) {
    g_emu_mode = false;
    g_emu_snes = NULL;
    g_emu_dsp = NULL;
    return 1;
  }

  for (int i = 0; i < SDL_NumJoysticks(); i++)
    OpenOneGamepad(i);

  bool running = true;
  SDL_Event event;
  uint32 lastTick = SDL_GetTicks();
  uint32 curTick = 0;
  uint32 frameCtr = 0;
  bool audiopaused = true;
  uint8 *pixels = NULL;
  int pitch = 0;

  while (running) {
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_CONTROLLERDEVICEADDED:
        OpenOneGamepad(event.cdevice.which);
        break;
      case SDL_CONTROLLERAXISMOTION:
        HandleGamepadAxisInput(event.caxis.which, event.caxis.axis, event.caxis.value);
        break;
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        int b = RemapSdlButton(event.cbutton.button);
        if (b >= 0)
          HandleGamepadInputForPlayer(b, event.type == SDL_CONTROLLERBUTTONDOWN,
                                      ControllerPlayer(event.cbutton.which));
        break;
      }
      case SDL_MOUSEWHEEL:
        if (SDL_GetModState() & KMOD_CTRL && event.wheel.y != 0)
          ChangeWindowScale(event.wheel.y > 0 ? 1 : -1);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.state == SDL_PRESSED && event.button.clicks == 2) {
          if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && (g_win_flags & SDL_WINDOW_FULLSCREEN) == 0 && SDL_GetModState() & KMOD_SHIFT) {
            g_win_flags ^= SDL_WINDOW_BORDERLESS;
            SDL_SetWindowBordered(g_window, (g_win_flags & SDL_WINDOW_BORDERLESS) == 0);
          }
        }
        break;
      case SDL_KEYDOWN:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, true);
        break;
      case SDL_KEYUP:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, false);
        break;
      case SDL_QUIT:
        running = false;
        break;
      }
    }

    NwaServer_Poll();

    bool paused = g_paused || NwaServer_IsPaused();
    if (paused != audiopaused) {
      audiopaused = paused;
      if (g_audio_device)
        SDL_PauseAudioDevice(g_audio_device, audiopaused);
    }
    if (paused) {
      SDL_Delay(16);
      continue;
    }

    // Clear gamepad inputs when joypad directional inputs to avoid wonkiness
    int inputs = g_input1_state;
    if (g_input1_state & 0xf0)
      g_gamepad_buttons = 0;
    inputs |= g_gamepad_buttons;
    snes->input1->currentState = inputs;

    g_renderer_funcs.BeginDraw(256, 224, &pixels, &pitch);
    SDL_LockMutex(g_audio_mutex);
    PpuBeginDrawing(snes->ppu, pixels, pitch, kPpuRenderFlags_NewRenderer);
    snes_runFrame(snes);
    SDL_UnlockMutex(g_audio_mutex);
    frameCtr++;
    EmuSramSaveIfChanged(sram, snes->cart->ram, false);
    g_renderer_funcs.EndDraw();

    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();
    if (!g_config.disable_frame_delay) {
      static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
      lastTick += delays[frameCtr % 3];
      if (lastTick > curTick) {
        uint32 delta = lastTick - curTick;
        if (delta > 500) {
          lastTick = curTick - 500;
          delta = 500;
        }
        SDL_Delay(delta);
      } else if (curTick - lastTick > 500) {
        lastTick = curTick;
      }
    }
  }

  if (EmuSramSaveIfChanged(sram, snes->cart->ram, false))
    fprintf(stderr, "emu: saved SRAM %s\n", sram->path);
  ShutdownDisplayAndAudio(window);
  g_emu_mode = false;
  g_emu_snes = NULL;
  g_emu_dsp = NULL;
  return 0;
}

// Run a complete cartridge ROM (for example an ALttPR seed) in the embedded
// SNES emulator without applying the zelda3 PatchRom patches and without
// running the C reimplementation.  With --headless it is the scripted test
// path; otherwise it is the playable window/audio/input mode.
static int RunEmuRom(const char *rom_file) {
  if (g_nwa_hold_seconds && !g_config.nwa_enabled)
    Die("--nwa-hold requires EmuNwa = 1 in the [Network] section");
  if (g_nwa_hold_at && !g_nwa_hold_seconds)
    Die("--nwa-hold-at requires --nwa-hold");

  size_t length = 0;
  uint8 *file = ReadWholeFile(rom_file, &length);
  if (!file)
    Die("--emu-rom: cannot read ROM");
  uint8 *ram = calloc(1, 0x20000);
  if (!ram)
    Die("--emu-rom: out of memory");
  Snes *snes = snes_init(ram);
  if (!snes_loadRom(snes, file, (int)length))
    Die("--emu-rom: failed to load ROM");
  free(file);
  // The native path narrows the PPU to 256 pixels unless the large-screen
  // option is on.  The emulator draws a plain 256x224 frame.
  snes->ppu->extraLeftRight = 0;
  snes->ppu->emuRomMode = true;

  // Features, mods, MSU and widescreen only exist in the C reimplementation.
  g_config.features0 = 0;
  g_config.mods_enabled = false;
  g_config.extended_aspect_ratio = 0;
  g_config.extend_y = 0;
  g_config.enable_msu = 0;
  fprintf(stderr, "emu: native features, mods, MSU and widescreen disabled\n");

  EmuSram sram;
  EmuSramInit(&sram, rom_file, snes->cart->ram, snes->cart->ramSize);

  NwaServer_Start(g_config.nwa_enabled, g_config.nwa_port,
                  g_config.nwa_allow_write, rom_file);
  NwaServer_SetEmulatorSnes(snes);

  if (g_input_script_file)
    LoadInputScript(g_input_script_file);

  int result;
  if (g_headless)
    result = RunEmuRomHeadless(snes, &sram);
  else
    result = RunEmuRomInteractive(snes, &sram);

  NwaServer_Stop();
  EmuSramFree(&sram);
  snes_free(snes);
  free(ram);
  fflush(stdout);
  return result;
}

// Input script format, one entry per line ('#' starts a comment):
//   <frame> <buttons>
// <frame> counts headless frames from 1; <buttons> is '-' or a list of
// B Y Select Start Up Down Left Right A X L R separated by spaces or '+'.
// Buttons are held from that frame until the next entry.
static void LoadInputScriptInto(const char *path, int *script_count,
                                long **script_frame, uint16 **script_buttons) {
  static const char *const kNames[12] = { "B", "Y", "Select", "Start", "Up", "Down", "Left", "Right", "A", "X", "L", "R" };
  FILE *f = fopen(path, "r");
  if (!f)
    Die("--input-script: cannot open file");
  char line[512];
  int cap = 0, lineno = 0;
  long last = 0;
  while (fgets(line, sizeof(line), f)) {
    lineno++;
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;
    char *tok = strtok(line, " \t\r\n+");
    if (!tok)
      continue;
    long frame;
    if (!ParseLongArg(tok, &frame) || frame < 1 || frame < last) {
      fprintf(stderr, "input-script line %d: bad or decreasing frame\n", lineno);
      Die("--input-script: invalid file");
    }
    uint16 buttons = 0;
    while ((tok = strtok(NULL, " \t\r\n+")) != NULL) {
      if (strcmp(tok, "-") == 0)
        continue;
      int b = 0;
      while (b < 12 && StringEqualsNoCase(tok, kNames[b]) == 0) b++;
      if (b == 12) {
        fprintf(stderr, "input-script line %d: unknown button '%s'\n", lineno, tok);
        Die("--input-script: invalid file");
      }
      buttons |= 1 << b;
    }
    if (*script_count == cap) {
      cap = cap ? cap * 2 : 64;
      *script_frame = realloc(*script_frame, cap * sizeof(long));
      *script_buttons = realloc(*script_buttons, cap * sizeof(uint16));
      if (!*script_frame || !*script_buttons)
        Die("malloc failed");
    }
    (*script_frame)[*script_count] = frame;
    (*script_buttons)[(*script_count)++] = buttons;
    last = frame;
  }
  fclose(f);
}

static void LoadInputScript(const char *path) {
  LoadInputScriptInto(path, &g_input_script_count, &g_input_script_frame,
                      &g_input_script_buttons);
}

static void LoadInputScript2(const char *path) {
  LoadInputScriptInto(path, &g_input_script2_count, &g_input_script2_frame,
                      &g_input_script2_buttons);
}

static void NwaHoldFor(long seconds) {
  if (seconds <= 0)
    return;
  uint64_t end = (uint64_t)SDL_GetTicks() + (uint64_t)seconds * 1000;
  while ((uint64_t)SDL_GetTicks() < end) {
    NwaServer_Poll();
    SDL_Delay(2);
  }
  NwaServer_Poll();
}

// Diagnostic for --force-sprite6b. Prints only state changes of type 0x6B
// sprites so the forced test can report what the soldier actually did.
static void TraceForcedSprite6B(long frame) {
  static int last_state[16], last_ai[16], last_c[16], last_health[16];
  static bool initialized;
  if (!initialized) {
    memset(last_state, -1, sizeof(last_state));
    initialized = true;
  }
  for (int j = 15; j >= 0; j--) {
    int state = sprite_type[j] == 0x6B ? sprite_state[j] : -1;
    int ai = sprite_ai_state[j];
    int c = sprite_C[j];
    int health = sprite_health[j];
    if (state != last_state[j] || (state >= 0 && (ai != last_ai[j] || c != last_c[j] || health != last_health[j]))) {
      if (state < 0)
        printf("trace6b frame %ld slot %d gone\n", frame, j);
      else
        printf("trace6b frame %ld slot %d state %d ai %d C %d hp %d x %d y %d\n",
               frame, j, state, ai, c, health,
               sprite_x_lo[j] | sprite_x_hi[j] << 8, sprite_y_lo[j] | sprite_y_hi[j] << 8);
      last_state[j] = state;
      last_ai[j] = ai;
      last_c[j] = c;
      last_health[j] = health;
    }
  }
}

static FrameDescCheckStats g_frame_check_stats;
static bool g_frame_check_failed;
static uint8 *s_plugin_draw_buffer;
static size_t s_plugin_draw_size;

// Runs the optional per-frame description work after the simulation of one
// frame. It never leaves PPU/DMA changes behind, so the replay hashes and the
// side-by-side comparison are unaffected.
static void FrameDescriptionStep(long frame, bool allow_dump) {
  if (!g_frame_check && !g_frame_desc_capture && !g_frame_desc_draw &&
      !g_frame_plugin_draw && g_frame_desc_dump_frame < 0)
    return;
  // The interactive voxel toggle disables the plugin; skip the passive
  // description capture too so the reference path really costs nothing extra.
  if (RenderPlugin_IsActive() && !RenderPlugin_IsEnabled() && !g_frame_check)
    return;

  uint32 flags = (uint32)g_ppu_render_flags;
  if (LocalCoop_IsActive())
    flags &= ~kPpuRenderFlags_4x4Mode7;
  int scale = PpuGetCurrentRenderScale(g_zenv.ppu, flags);
  int width = g_snes_width * scale;
  int height = g_snes_height * scale;

  if (g_frame_check) {
    uint64 before = g_frame_check_stats.diff_pixels;
    bool ok = RenderPlugin_IsActive()
        ? RenderPlugin_CheckFrame(frame, flags, width, height,
                                  &g_frame_check_stats)
        : FrameDesc_CheckFrame(frame, flags, width, height,
                               &g_frame_check_stats);
    if (!ok) {
      g_frame_check_failed = true;
      if (before == 0 && g_frame_check_stats.diff_pixels != 0) {
        fprintf(stderr,
                "frame-check: frame %ld differs at (%d, %d): "
                "reference %08x description %08x\n",
                frame, g_frame_check_stats.first_x,
                g_frame_check_stats.first_y,
                (unsigned)g_frame_check_stats.first_ref_pixel,
                (unsigned)g_frame_check_stats.first_desc_pixel);
      }
    }
  } else if (g_frame_plugin_draw) {
    // Headless plugin benchmark: capture the description and let the plugin
    // draw it at its preferred scale. No reference render or comparison, so
    // the time difference against --frame-desc is the plugin cost.
    int plugin_scale = EffectiveRenderScale();
    int pane_count = SharedWorld_IsSplit() ? 2 : 1;
    int pane_width = g_snes_width * plugin_scale;
    int plugin_width = pane_width * pane_count;
    int plugin_height = g_snes_height * plugin_scale;
    size_t plugin_pitch = (size_t)plugin_width * 4;
    size_t plugin_size = plugin_pitch * (size_t)plugin_height;
    if (s_plugin_draw_size < plugin_size) {
      uint8 *buffer = (uint8 *)realloc(s_plugin_draw_buffer, plugin_size);
      if (buffer != NULL) {
        s_plugin_draw_buffer = buffer;
        s_plugin_draw_size = plugin_size;
      }
    }
    if (s_plugin_draw_buffer == NULL ||
        !FrameDesc_DrawCurrentFrame(flags, pane_width, plugin_height, true,
                                    NULL, 0) ||
        !RenderPlugin_BeginFrame((uint32_t)frame, pane_count, plugin_width,
                                 plugin_height, plugin_scale, flags)) {
      g_frame_check_failed = true;
    } else {
      for (int pane = 0; pane < pane_count; pane++) {
        if (!RenderPlugin_FrameDescription(FrameDesc_GetDesc(pane))) {
          g_frame_check_failed = true;
          break;
        }
      }
      if (!g_frame_check_failed &&
          !RenderPlugin_Present(s_plugin_draw_buffer, plugin_pitch,
                                plugin_width, plugin_height, plugin_scale))
        g_frame_check_failed = true;
    }
  } else if (g_frame_desc_capture || g_frame_desc_draw) {
    if (!FrameDesc_DrawCurrentFrame(flags, width, height,
                                    g_frame_desc_capture, NULL, 0)) {
      fprintf(stderr, "frame-desc: failed to draw frame %ld\n", frame);
      g_frame_check_failed = true;
    }
  }

  if (allow_dump && g_frame_desc_dump_frame == frame) {
    if (SharedWorld_IsSplit()) {
      fprintf(stderr, "frame-desc: frame %ld skipped (split-screen)\n",
              frame);
    } else if (!FrameDesc_DumpCurrentFrame(frame, flags, width, height)) {
      g_frame_check_failed = true;
    }
  }
}

static int RunHeadless(const char *rom_file) {
  bool rom_loaded = false;
  g_audio_mutex = SDL_CreateMutex();
  if (!g_audio_mutex)
    Die("No mutex");
  if (rom_file != NULL)
    rom_loaded = LoadRom(rom_file);
  if ((g_replay_ref >= 0) + (g_replay_file != NULL) + (g_load_file != NULL) > 1)
    Die("use only one of --replay, --replay-ref and --load");
  if (g_input_script_file)
    LoadInputScript(g_input_script_file);
  if (g_input_script2_file)
    LoadInputScript2(g_input_script2_file);
  if (LocalCoop_IsEnabled())
    LocalCoop_Start();
  if (g_load_file) {
    if (!rom_loaded)
      Die("--load requires a ROM");
    // The emulator runs its reset entry point on the first frame. In the
    // interactive game that has already happened before a replay is loaded,
    // so do the same here and keep the loaded snapshot untouched.
    if (LocalCoop_IsActive()) {
      for (int i = 0; i < 2; i++) {
        LocalCoop_Select(i);
        ZeldaRunFrame(0);
      }
      for (int i = 0; i < 2; i++) {
        LocalCoop_Select(i);
        if (!ZeldaLoadFile(g_load_file))
          Die("--load: cannot open file");
      }
    } else {
      ZeldaRunFrame(0);
      if (!ZeldaLoadFile(g_load_file))
        Die("--load: cannot open file");
    }
    SharedWorld_ResetFromCurrent();
    LocalCoop_Select(g_local_player);
  }
  if (g_replay_ref >= 0 || g_replay_file) {
    if (!rom_loaded)
      Die("--replay and --replay-ref require a ROM");
    if (LocalCoop_IsActive()) {
      for (int i = 0; i < 2; i++) {
        LocalCoop_Select(i);
        ZeldaRunFrame(0);
      }
      LocalCoop_Select(g_local_player);
      if (g_replay_file) {
        if (!ZeldaReplayFile(g_replay_file))
          Die("--replay: cannot open file");
      } else {
        SaveLoadSlot(kSaveLoad_Replay, 256 + g_replay_ref);
      }
      // Put the other player at the same base snapshot without replaying the
      // same inputs.
      LocalCoop_Select(1 - g_local_player);
      bool ok = g_replay_file ? ZeldaLoadReplayBaseFile(g_replay_file)
                              : ZeldaLoadReplayBaseRef(g_replay_ref);
      if (!ok)
        Die("--replay: cannot load the base snapshot");
    } else {
      ZeldaRunFrame(0);
      if (g_replay_file) {
        if (!ZeldaReplayFile(g_replay_file))
          Die("--replay: cannot open file");
      } else {
        SaveLoadSlot(kSaveLoad_Replay, 256 + g_replay_ref);
      }
    }
    SharedWorld_ResetFromCurrent();
    LocalCoop_Select(g_local_player);
  }

  if (LocalCoop_IsActive()) {
    for (int i = 0; i < 2; i++) {
      LocalCoop_Select(i);
      ZeldaApplyModFeatureOverrides();
    }
  } else {
    ZeldaApplyModFeatureOverrides();
  }
  LocalCoop_Select(g_local_player);
  bool compare_on = rom_loaded && enhanced_features0 == 0 && g_zenv.dialogue_flags == 0 &&
                    !g_mods_change_content && !g_nwa_changed_content &&
                    !g_coop_changed_content && !SharedWorld_IsActive();
  printf("compare: %s\n", compare_on ? "on" : "off");
  fflush(stdout);

  long frames = 0;
  while (g_max_frames < 0 || frames < g_max_frames) {
    uint32 coop_frame_start = SDL_GetTicks();
    NwaServer_Poll();
    if (NwaServer_IsPaused()) {
      SDL_Delay(2);
      continue;
    }
    int script_inputs1 = 0;
    for (int si = 0; si < g_input_script_count && g_input_script_frame[si] <= frames + 1; si++)
      script_inputs1 = g_input_script_buttons[si];
    int script_inputs2 = 0;
    for (int si = 0; si < g_input_script2_count && g_input_script2_frame[si] <= frames + 1; si++)
      script_inputs2 = g_input_script2_buttons[si];
    bool is_replay = false;
    if (LocalCoop_IsActive()) {
      for (int p = 0; p < 2; p++) {
        LocalCoop_Select(p);
        bool replay = ZeldaRunFrame(p == 0 ? script_inputs1 : script_inputs2);
        FrameDescriptionStep(frames + 1, p == g_local_player);
        Coop_FrameStep();
        if (p == g_local_player)
          is_replay = replay;
      }
      LocalCoop_Select(g_local_player);
    } else {
      SharedWorld_SetInput2((uint16)script_inputs2);
      is_replay = ZeldaRunFrame(script_inputs1);
      Coop_FrameStep();
      FrameDescriptionStep(frames + 1, true);
    }
    frames++;
    // Keep the headless output size in sync with the automatic split so the
    // final --screenshot has both panes.
    if (SharedWorld_IsActive() && SharedWorld_OutputWidth(g_snes_width) != g_output_width)
      UpdateOutputSize();
    // The network co-op is a 60 Hz protocol. Pacing headless runs keeps two
    // instances in step and avoids flooding the relay with thousands of
    // packets per second. The local in-memory transport needs no pacing.
    if (Coop_IsActive() && !Coop_IsLocal()) {
      uint32 elapsed = SDL_GetTicks() - coop_frame_start;
      if (elapsed < 16)
        SDL_Delay(16 - elapsed);
    }
    // The final replay frame still returns true; the next call returns false.
    // regress.sh passes the exact replay length, so this normally is not hit.
    // --stop-replay-at N: leave replay mode after frame N (like the StopReplay
    // key) and keep running from there with the input script.
    if (g_stop_replay_at > 0 && frames == g_stop_replay_at && is_replay)
      PatchCommand('l');
    else if ((g_replay_ref >= 0 || g_replay_file) && !is_replay && g_stop_replay_at < 0)
      break;
    if (frames == g_force_sprite6b_frame)
      PatchCommand('C');
    if (g_force_sprite6b_frame >= 0)
      TraceForcedSprite6B(frames);
    if (g_nwa_hold_seconds > 0 && frames == g_nwa_hold_at)
      NwaHoldFor(g_nwa_hold_seconds);
    if (g_hash_every > 0 && frames % g_hash_every == 0) {
      printf("frame %ld %08x\n", frames, HashState());
      fflush(stdout);
    }
  }
  if (g_nwa_hold_seconds > 0 && g_nwa_hold_at == 0)
    NwaHoldFor(g_nwa_hold_seconds);
  printf("final %ld %08x\n", frames, HashState());
  if (g_save_replay_file) {
    if (!ZeldaSaveReplayFile(g_save_replay_file))
      Die("--save-replay: cannot write file");
    printf("saved replay %s\n", g_save_replay_file);
  }
  if (g_dump_state) {
    int players = (LocalCoop_IsActive() || SharedWorld_IsActive()) ? 2 : 1;
    for (int p = 0; p < players; p++) {
      if (LocalCoop_IsActive())
        LocalCoop_Select(p);
      else if (SharedWorld_IsActive())
        SharedWorld_DumpSelect(p);
      for (unsigned i = 0; i < kStateVarCount; i++) {
        const StateVar *var = &kStateVars[i];
        if (var->is_array)
          continue;
        uint32 value = g_ram[var->offset];
        if (var->elem_size == 2)
          value |= (uint32)g_ram[var->offset + 1] << 8;
        if (players == 2)
          printf("state p%d %s %u\n", p + 1, var->name, (unsigned)value);
        else
          printf("state %s %u\n", var->name, (unsigned)value);
      }
    }
    if (LocalCoop_IsActive())
      LocalCoop_Select(g_local_player);
    else if (SharedWorld_IsActive())
      SharedWorld_DumpDone();
  }
  if (g_menu_screenshot_page != NULL) {
    if (!OptionsMenu_OpenPageByName(g_menu_screenshot_page))
      Die("--menu-screenshot: unknown page (use Video, Sound, Gameplay, Mods or Keys)");
  }
  if (g_screenshot_file)
    WriteScreenshot(g_screenshot_file);
  if (g_frame_check) {
    printf("frame-check: %llu frames, %llu pixels, %llu differing pixels",
           (unsigned long long)g_frame_check_stats.frames,
           (unsigned long long)g_frame_check_stats.pixels,
           (unsigned long long)g_frame_check_stats.diff_pixels);
    if (g_frame_check_stats.split_frames != 0)
      printf(", %llu split-screen frames (2 panes)",
             (unsigned long long)g_frame_check_stats.split_frames);
    printf("\n");
    if (g_frame_check_stats.mode7_frames != 0)
      printf("frame-check: %llu mode7 frames (%llu with 4x4 perspective)\n",
             (unsigned long long)g_frame_check_stats.mode7_frames,
             (unsigned long long)g_frame_check_stats.mode7_4x4_frames);
    if (g_frame_check_stats.diff_pixels != 0) {
      fprintf(stderr,
              "frame-check: first mismatch at frame %ld pixel (%d, %d)\n",
              g_frame_check_stats.first_frame, g_frame_check_stats.first_x,
              g_frame_check_stats.first_y);
    }
  }
  if (g_frame_desc_dump_frame > 0 && frames < g_frame_desc_dump_frame)
    fprintf(stderr, "frame-desc: dump frame %ld was not reached (%ld frames)\n",
            g_frame_desc_dump_frame, frames);
  fflush(stdout);

  LocalCoop_Stop();
  SharedWorld_Stop();
  Coop_Stop();
  NwaServer_Stop();
  SDL_DestroyMutex(g_audio_mutex);
  g_audio_mutex = NULL;
  return g_frame_check_failed ? 1 : 0;
}

static bool ParseLinkGraphics(uint8 *file, size_t length) {
  ZsprData zspr;
  if (!Mods_ParseZspr(file, length, &zspr))
    return false;
  if (kPalette_ArmorAndGloves_SIZE != 150 || kLinkGraphics_SIZE != 0x7000)
    Die("ParseLinkGraphics: Invalid asset sizes");
  memcpy(kLinkGraphics, zspr.pixels, 0x7000);
  if (zspr.palette_length >= 120)
    memcpy(kPalette_ArmorAndGloves, zspr.palette, 120);
  if (zspr.palette_length >= 124)
    memcpy(kGlovesColor, zspr.palette + 120, 4);
  return true;
}

static void LoadLinkGraphics() {
  if (g_config.link_graphics) {
    fprintf(stderr, "Loading Link Graphics: %s\n", g_config.link_graphics);
    size_t length = 0;
    uint8 *file = ReadWholeFile(g_config.link_graphics, &length);
    if (file == NULL || !ParseLinkGraphics(file, length))
      Die("Unable to load file");
    free(file);
  }
}


const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];

static uint16 g_dim_flashes_original[3];
static bool g_dim_flashes_original_saved;

static void ApplyDimFlashesPalette(bool enabled) {
  if (!g_dim_flashes_original_saved) {
    memcpy(g_dim_flashes_original, &kPalette_DungBgMain[0x484],
           sizeof(g_dim_flashes_original));
    g_dim_flashes_original_saved = true;
  }
  if (enabled) {
    kPalette_DungBgMain[0x484] = 0x70;
    kPalette_DungBgMain[0x485] = 0x95;
    kPalette_DungBgMain[0x486] = 0x57;
  } else {
    kPalette_DungBgMain[0x484] = g_dim_flashes_original[0];
    kPalette_DungBgMain[0x485] = g_dim_flashes_original[1];
    kPalette_DungBgMain[0x486] = g_dim_flashes_original[2];
  }
}

// Live application of options-menu changes. They only run when the user
// changes a value; the menu being closed leaves all of this untouched.
void MenuHost_SetVolume(int volume) {
  volume = IntMin(IntMax(volume, 0), 100);
#if SYSTEM_VOLUME_MIXER_AVAILABLE
  SetApplicationVolume(volume);
#else
  g_sdl_audio_mixer_volume = volume * SDL_MIX_MAXVOLUME / 100;
#endif
}

void MenuHost_ApplyVideo(void) {
  g_zenv.ppu->extraLeftRight =
      UintMin(g_config.extended_aspect_ratio, kPpuExtraLeftRight);
  g_snes_width = g_config.extended_aspect_ratio * 2 + 256;
  g_snes_height = g_config.extend_y ? 240 : 224;
  g_ppu_render_flags =
      g_config.new_renderer * kPpuRenderFlags_NewRenderer |
      g_config.enhanced_mode7 * kPpuRenderFlags_4x4Mode7 |
      g_config.extend_y * kPpuRenderFlags_Height240 |
      g_config.no_sprite_limits * kPpuRenderFlags_NoSpriteLimits;
  RenderPlugin_SetEnabled(g_config.render_plugin_enabled);
  g_current_window_scale = g_config.window_scale == 0
                               ? 2
                               : IntMin(g_config.window_scale, kMaxWindowScale);
  UpdateOutputSize();
  if (g_window == NULL)
    return;

  g_win_flags &= ~(SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP);
  if (g_config.fullscreen == 1) {
    g_win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
  } else if (g_config.fullscreen == 2) {
    g_win_flags |= SDL_WINDOW_FULLSCREEN;
    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN);
  } else {
    SDL_SetWindowFullscreen(g_window, 0);
    if (g_config.window_width == 0 && g_config.window_height == 0)
      SDL_SetWindowSize(g_window, g_current_window_scale * g_output_width,
                        g_current_window_scale * g_output_height);
  }
}

void MenuHost_ApplyAudio(void) {
  ZeldaEnableMsu(g_config.enable_msu);
}

bool MenuHost_SetAudioChannels(int channels) {
  if (channels < 1 || channels > 2)
    return false;
  // No device (headless) or audio disabled: only the saved config changes.
  if (!g_config.enable_audio || g_audio_device == 0) {
    g_config.audio_channels = (uint8)channels;
    return true;
  }

  SDL_AudioDeviceID old_device = g_audio_device;
  SDL_PauseAudioDevice(old_device, 1);

  SDL_AudioDeviceID new_device;
  uint8 new_channels;
  int new_frames;
  uint8 *new_buffer;
  if (!OpenAudioDevice(channels, &new_device, &new_channels, &new_frames,
                       &new_buffer)) {
    fprintf(stderr, "menu: cannot reopen audio device: %s\n", SDL_GetError());
    if (!OptionsMenu_IsOpen())
      SDL_PauseAudioDevice(old_device, 0);
    return false;
  }

  SDL_LockMutex(g_audio_mutex);
  uint8 *old_buffer = g_audiobuffer;
  g_audio_device = new_device;
  g_audio_channels = new_channels;
  g_frames_per_block = new_frames;
  g_audiobuffer = new_buffer;
  g_audiobuffer_cur = new_buffer;
  g_audiobuffer_end = new_buffer;
  g_config.audio_channels = new_channels;
  SDL_UnlockMutex(g_audio_mutex);

  SDL_CloseAudioDevice(old_device);
  free(old_buffer);
  // The new device starts paused. The main loop resumes it when the menu
  // closes (or leaves it paused if the game was already paused).
  if (!OptionsMenu_IsOpen())
    SDL_PauseAudioDevice(new_device, 0);
  return true;
}

void MenuHost_ApplyFeatures(void) {
  uint32 old = g_wanted_zelda_features;
  g_wanted_zelda_features = g_config.features0;
  if ((old ^ g_wanted_zelda_features) &
      (kFeatures0_NewStyleInventory | kFeatures0_HorizontalMagicBar |
       kFeatures0_ReduxHud))
    Hud_Rebuild();
  ApplyDimFlashesPalette((g_config.features0 & kFeatures0_DimFlashes) != 0);
}

bool MenuHost_RenderPluginActive(void) {
  return RenderPlugin_IsActive();
}

const char *MenuHost_RenderPluginName(void) {
  const char *name = RenderPlugin_Name();
  return name != NULL ? name : "";
}

static void LoadAssets() {
  size_t length = 0;
  uint8 *data = ReadWholeFile("zelda3_assets.dat", &length);
  if (!data) {
    size_t bps_length, bps_src_length;
    uint8 *bps, *bps_src;
    bps = ReadWholeFile("zelda3_assets.bps", &bps_length);
    if (!bps)
      Die("Failed to read zelda3_assets.dat. Please see the README for information about how you get this file.");
    bps_src = ReadWholeFile("zelda3.sfc", &bps_src_length);
    if (!bps_src)
      Die("Missing file: zelda3.sfc");
    data = ApplyBps(bps_src, bps_src_length, bps, bps_length, &length);
    if (!data)
      Die("Unable to apply zelda3_assets.bps. Please make sure you got the right version of 'zelda3.sfc'");
  }

  static const char kAssetsSig[] = { kAssets_Sig };

  if (length < 16 + 32 + 32 + 8 + kNumberOfAssets * 4 ||
      memcmp(data, kAssetsSig, 48) != 0 ||
      *(uint32*)(data + 80) != kNumberOfAssets)
    Die("Invalid assets file");

  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);

  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    if ((uint64)offset + size > length)
      Die("Assets file corruption");
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }

  if (g_config.features0 & kFeatures0_DimFlashes) // patch dungeon floor palettes
    ApplyDimFlashesPalette(true);
}

// Go some steps up and find zelda3.ini
static void SwitchDirectory() {
  char buf[4096];
  if (!getcwd(buf, sizeof(buf) - 32))
    return;
  size_t pos = strlen(buf);

  for (int step = 0; pos != 0 && step < 3; step++) {
    memcpy(buf + pos, "/zelda3.ini", 12);
    FILE *f = fopen(buf, "rb");
    if (f) {
      fclose(f);
      buf[pos] = 0;
      if (step != 0) {
        printf("Found zelda3.ini in %s\n", buf);
        int err = chdir(buf);
        (void)err;
      }
      return;
    }
    pos--;
    while (pos != 0 && buf[pos] != '/' && buf[pos] != '\\')
      pos--;
  }
}

MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}
