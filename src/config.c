#include "config.h"
#include "types.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "features.h"
#include "util.h"
#include "mod/mod_features.h"

enum {
  kKeyMod_ScanCode = 0x200,
  kKeyMod_Alt = 0x400,
  kKeyMod_Shift = 0x800,
  kKeyMod_Ctrl = 0x1000,
};

Config g_config;

#define REMAP_SDL_KEYCODE(key) ((key) & SDLK_SCANCODE_MASK ? kKeyMod_ScanCode : 0) | (key) & (kKeyMod_ScanCode - 1)
#define _(x) REMAP_SDL_KEYCODE(x)
#define S(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Shift
#define A(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Alt
#define C(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Ctrl
#define N 0
static const uint16 kDefaultKbdControls[kKeys_Total] = {
  0,
  // Controls
  _(SDLK_UP), _(SDLK_DOWN), _(SDLK_LEFT), _(SDLK_RIGHT), _(SDLK_RSHIFT), _(SDLK_RETURN), _(SDLK_x), _(SDLK_z), _(SDLK_s), _(SDLK_a), _(SDLK_c), _(SDLK_v),
  // LoadState
  _(SDLK_F1), _(SDLK_F2), _(SDLK_F3), _(SDLK_F4), _(SDLK_F5), _(SDLK_F6), _(SDLK_F7), _(SDLK_F8), _(SDLK_F9), _(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // SaveState
  S(SDLK_F1), S(SDLK_F2), S(SDLK_F3), S(SDLK_F4), S(SDLK_F5), S(SDLK_F6), S(SDLK_F7), S(SDLK_F8), S(SDLK_F9), S(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // Replay State
  C(SDLK_F1), C(SDLK_F2), C(SDLK_F3), C(SDLK_F4), C(SDLK_F5), C(SDLK_F6), C(SDLK_F7), C(SDLK_F8), C(SDLK_F9), C(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // Load Ref State
  N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
  // Replay Ref State
  N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
  // CheatLife, CheatKeys, CheatEquipment, CheatWalkThroughWalls
  _(SDLK_w), _(SDLK_o), S(SDLK_w), C(SDLK_e),
  // ClearKeyLog, StopReplay, Fullscreen, Reset, Pause, PauseDimmed, Turbo, ReplayTurbo, WindowBigger, WindowSmaller, DisplayPerf, ToggleRenderer
  _(SDLK_k), _(SDLK_l), A(SDLK_RETURN), C(SDLK_r), S(SDLK_p), _(SDLK_p), _(SDLK_TAB), _(SDLK_t), N, N, _(SDLK_f), _(SDLK_r),
  // ToggleVoxel, VoxelCameraLeft/Right/Up/Down, ZoomIn/Out, Reset, Shadows, Lighting
  _(SDLK_F11), A(SDLK_LEFT), A(SDLK_RIGHT), A(SDLK_UP), A(SDLK_DOWN),
  A(SDLK_EQUALS), A(SDLK_MINUS), A(SDLK_0), _(SDLK_F12), S(SDLK_F11),
};

// Default keys for the second local player. They are only consulted when
// [Coop] Local = 1, and deliberately avoid the default player 1 bindings above.
static const uint16 kDefaultKbdControls2[kKeys_Controls_Last - kKeys_Controls + 1] = {
  _(SDLK_i), _(SDLK_m), _(SDLK_j), _(SDLK_n), _(SDLK_u), _(SDLK_e), _(SDLK_h), _(SDLK_y), _(SDLK_b), _(SDLK_g), _(SDLK_7), _(SDLK_8),
};
#undef _
#undef A
#undef C
#undef S
#undef N

typedef struct KeyNameId {
  const char *name;
  uint16 id, size;
} KeyNameId;

#define M(n) {#n, kKeys_##n, kKeys_##n##_Last - kKeys_##n + 1}
#define S(n) {#n, kKeys_##n, 1}
static const KeyNameId kKeyNameId[] = {
  {"Null", kKeys_Null, 65535},
  M(Controls), M(Load), M(Save), M(Replay), M(LoadRef), M(ReplayRef),
  S(CheatLife), S(CheatKeys), S(CheatEquipment), S(CheatWalkThroughWalls),
  S(ClearKeyLog), S(StopReplay), S(Fullscreen), S(Reset),
  S(Pause), S(PauseDimmed), S(Turbo), S(ReplayTurbo), S(WindowBigger), S(WindowSmaller), S(VolumeUp), S(VolumeDown), S(DisplayPerf), S(ToggleRenderer),
  S(ToggleVoxel), S(VoxelCameraLeft), S(VoxelCameraRight), S(VoxelCameraUp), S(VoxelCameraDown), S(VoxelCameraZoomIn), S(VoxelCameraZoomOut), S(VoxelCameraReset), S(VoxelShadows), S(VoxelLighting),
};
#undef S
#undef M
typedef struct KeyMapHashEnt {
  uint16 key, cmd, next;
} KeyMapHashEnt;

typedef struct KeyMap {
  uint16 hash_first[255];
  KeyMapHashEnt *ents;
  int size;
} KeyMap;

static KeyMap g_keymap, g_keymap2;
static bool has_keynameid[countof(kKeyNameId)];
static bool has_keynameid2;

static bool KeyMapHash_Add(KeyMap *map, uint16 key, uint16 cmd) {
  if ((map->size & 0xff) == 0) {
    if (map->size > 10000)
      Die("Too many keys");
    map->ents = realloc(map->ents, sizeof(KeyMapHashEnt) * (map->size + 256));
    if (!map->ents) Die("realloc failure");
  }
  int i = map->size++;
  KeyMapHashEnt *ent = &map->ents[i];
  ent->key = key;
  ent->cmd = cmd;
  ent->next = 0;
  int j = (uint32)key % 255;

  uint16 *cur = &map->hash_first[j];
  while (*cur) {
    KeyMapHashEnt *ent = &map->ents[*cur - 1];
    if (ent->key == key)
      return false;
    cur = &ent->next;
  }
  *cur = i + 1;
  return true;
}

static int KeyMapHash_Find(const KeyMap *map, uint16 key) {
  int i = map->hash_first[key % 255];
  while (i) {
    const KeyMapHashEnt *ent = &map->ents[i - 1];
    if (ent->key == key)
      return ent->cmd;
    i = ent->next;
  }
  return 0;
}

static int KeyFromSdlKey(SDL_Keycode code, SDL_Keymod mod) {
  if (code & ~(SDLK_SCANCODE_MASK | 0x1ff))
    return -1;
  int key = 0;
  if (code != SDLK_LALT && code != SDLK_RALT)
    key |=  mod & KMOD_ALT ? kKeyMod_Alt : 0;
  if (code != SDLK_LCTRL && code != SDLK_RCTRL)
    key |= mod & KMOD_CTRL ? kKeyMod_Ctrl : 0;
  if (code != SDLK_LSHIFT && code != SDLK_RSHIFT)
    key |= mod & KMOD_SHIFT ? kKeyMod_Shift : 0;
  key |= REMAP_SDL_KEYCODE(code);
  return key;
}

int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod) {
  int key = KeyFromSdlKey(code, mod);
  return key < 0 ? 0 : KeyMapHash_Find(&g_keymap, (uint16)key);
}

int FindCmdForSdlKey2(SDL_Keycode code, SDL_Keymod mod) {
  int key = KeyFromSdlKey(code, mod);
  return key < 0 ? 0 : KeyMapHash_Find(&g_keymap2, (uint16)key);
}

static void ParseKeyArrayTo(KeyMap *map, char *value, int cmd, int size) {
  char *s;
  int i = 0;
  for (; i < size && (s = NextDelim(&value, ',')) != NULL; i++, cmd += (cmd != 0)) {
    if (*s == 0)
      continue;
    int key_with_mod = 0;
    for (;;) {
      if (StringStartsWithNoCase(s, "Shift+")) {
        key_with_mod |= kKeyMod_Shift, s += 6;
      } else if (StringStartsWithNoCase(s, "Ctrl+")) {
        key_with_mod |= kKeyMod_Ctrl, s += 5;
      } else if (StringStartsWithNoCase(s, "Alt+")) {
        key_with_mod |= kKeyMod_Alt, s += 4;
      } else {
        break;
      }
    }
    SDL_Keycode key = SDL_GetKeyFromName(s);
    if (key == SDLK_UNKNOWN) {
      fprintf(stderr, "Unknown key: '%s'\n", s);
      continue;
    }
    if (!KeyMapHash_Add(map, key_with_mod | REMAP_SDL_KEYCODE(key), cmd))
      fprintf(stderr, "Duplicate key: '%s'\n", s);
  }
}

typedef struct GamepadMapEnt {
  uint32 modifiers;
  uint16 cmd, next;
} GamepadMapEnt;

typedef struct GamepadMap {
  uint16 hash_first[kGamepadBtn_Count];
  GamepadMapEnt *ents;
  int size;
} GamepadMap;

static GamepadMap g_joymap, g_joymap2;
static bool has_joypad_controls;
static bool has_joypad_controls2;

static int CountBits32(uint32 n) {
  int count = 0;
  for (; n != 0; count++)
    n &= (n - 1);
  return count;
}

static void GamepadMap_AddTo(GamepadMap *map, int button, uint32 modifiers,
                             uint16 cmd) {
  if ((map->size & 0xff) == 0) {
    if (map->size > 1000)
      Die("Too many joypad keys");
    map->ents = realloc(map->ents, sizeof(GamepadMapEnt) * (map->size + 64));
    if (!map->ents) Die("realloc failure");
  }
  uint16 *p = &map->hash_first[button];
  // Insert it as early as possible but before after any entry with more modifiers.
  int cb = CountBits32(modifiers);
  while (*p && cb < CountBits32(map->ents[*p - 1].modifiers))
    p = &map->ents[*p - 1].next;
  int i = map->size++;
  GamepadMapEnt *ent = &map->ents[i];
  ent->modifiers = modifiers;
  ent->cmd = cmd;
  ent->next = *p;
  *p = i + 1;
}

static int GamepadMap_Find(const GamepadMap *map, int button,
                           uint32 modifiers) {
  const GamepadMapEnt *ent;
  for (int e = map->hash_first[button]; e != 0; e = ent->next) {
    ent = &map->ents[e - 1];
    if ((modifiers & ent->modifiers) == ent->modifiers)
      return ent->cmd;
  }
  return 0;
}

int FindCmdForGamepadButton(int button, uint32 modifiers) {
  return GamepadMap_Find(&g_joymap, button, modifiers);
}

int FindCmdForGamepadButton2(int button, uint32 modifiers) {
  return GamepadMap_Find(&g_joymap2, button, modifiers);
}

static int ParseGamepadButtonName(const char **value) {
  const char *s = *value;
  // Longest substring first
  static const char *const kGamepadKeyNames[] = {
    "Back", "Guide", "Start", "L3", "R3",
    "L1", "R1", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "L2", "R2",
    "Lb", "Rb", "A", "B", "X", "Y"
  };
  static const uint8 kGamepadKeyIds[] = {
    kGamepadBtn_Back, kGamepadBtn_Guide, kGamepadBtn_Start, kGamepadBtn_L3, kGamepadBtn_R3,
    kGamepadBtn_L1, kGamepadBtn_R1, kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight, kGamepadBtn_L2, kGamepadBtn_R2,
    kGamepadBtn_L1, kGamepadBtn_R1, kGamepadBtn_A, kGamepadBtn_B, kGamepadBtn_X, kGamepadBtn_Y,
  };
  for (size_t i = 0; i != countof(kGamepadKeyNames); i++) {
    const char *r = StringStartsWithNoCase(s, kGamepadKeyNames[i]);
    if (r) {
      *value = r;
      return kGamepadKeyIds[i];
    }
  }
  return kGamepadBtn_Invalid;
}

static const uint8 kDefaultGamepadCmds[] = {
  kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight, kGamepadBtn_Back, kGamepadBtn_Start,
  kGamepadBtn_B, kGamepadBtn_A, kGamepadBtn_Y, kGamepadBtn_X, kGamepadBtn_L1, kGamepadBtn_R1,
};

static void ParseGamepadArrayTo(GamepadMap *map, char *value, int cmd,
                                int size) {
  char *s;
  int i = 0;
  for (; i < size && (s = NextDelim(&value, ',')) != NULL; i++, cmd += (cmd != 0)) {
    if (*s == 0)
      continue;
    uint32 modifiers = 0;
    const char *ss = s;
    for (;;) {
      int button = ParseGamepadButtonName(&ss);
      if (button == kGamepadBtn_Invalid) BAD: {
        fprintf(stderr, "Unknown gamepad button: '%s'\n", s);
        break;
      }
      while (*ss == ' ' || *ss == '\t') ss++;
      if (*ss == '+') {
        ss++;
        modifiers |= 1 << button;
      } else if (*ss == 0) {
        GamepadMap_AddTo(map, button, modifiers, cmd);
        break;
      } else
        goto BAD;
    }
  }
}

static void RegisterDefaultKeys() {
  for (int i = 1; i < countof(kKeyNameId); i++) {
    if (!has_keynameid[i]) {
      int size = kKeyNameId[i].size, k = kKeyNameId[i].id;
      for (int j = 0; j < size; j++, k++)
        KeyMapHash_Add(&g_keymap, kDefaultKbdControls[k], k);
    }
  }
  if (!has_keynameid2) {
    for (int i = 0; i < countof(kDefaultKbdControls2); i++)
      KeyMapHash_Add(&g_keymap2, kDefaultKbdControls2[i], kKeys_Controls + i);
  }
  if (!has_joypad_controls) {
    for (int i = 0; i < countof(kDefaultGamepadCmds); i++)
      GamepadMap_AddTo(&g_joymap, kDefaultGamepadCmds[i], 0, kKeys_Controls + i);
  }
  if (!has_joypad_controls2) {
    for (int i = 0; i < countof(kDefaultGamepadCmds); i++)
      GamepadMap_AddTo(&g_joymap2, kDefaultGamepadCmds[i], 0, kKeys_Controls + i);
  }
}

static int GetIniSection(const char *s) {
  if (StringEqualsNoCase(s, "[KeyMap]"))
    return 0;
  if (StringEqualsNoCase(s, "[Graphics]"))
    return 1;
  if (StringEqualsNoCase(s, "[Sound]"))
    return 2;
  if (StringEqualsNoCase(s, "[General]"))
    return 3;
  if (StringEqualsNoCase(s, "[Features]"))
    return 4;
  if (StringEqualsNoCase(s, "[GamepadMap]"))
    return 5;
  if (StringEqualsNoCase(s, "[Mods]"))
    return 6;
  if (StringEqualsNoCase(s, "[Network]"))
    return 7;
  if (StringEqualsNoCase(s, "[Coop]"))
    return 8;
  if (StringEqualsNoCase(s, "[KeyMap2]"))
    return 9;
  if (StringEqualsNoCase(s, "[GamepadMap2]"))
    return 10;
  return -1;
}

bool ParseBool(const char *value, bool *result) {
  bool rv = false;
  switch (*value++ | 32) {
  case '0': if (*value == 0) break; return false;
  case 'f': if (StringEqualsNoCase(value, "alse")) break; return false;
  case 'n': if (StringEqualsNoCase(value, "o")) break; return false;
  case 'o':
    rv = (*value | 32) == 'n';
    if (StringEqualsNoCase(value, rv ? "n" : "ff")) break;
    return false;
  case '1': rv = true; if (*value == 0) break; return false;
  case 'y': rv = true; if (StringEqualsNoCase(value, "es")) break; return false;
  case 't': rv = true; if (StringEqualsNoCase(value, "rue")) break; return false;
  default: return false;
  }
  if (result) {
    *result = rv;
    return true;
  }
  return rv;
}

static bool HandleIniConfig(int section, const char *key, char *value) {
  if (section == 0) {
    for (int i = 0; i < countof(kKeyNameId); i++) {
      if (StringEqualsNoCase(key, kKeyNameId[i].name)) {
        has_keynameid[i] = true;
        ParseKeyArrayTo(&g_keymap, value, kKeyNameId[i].id, kKeyNameId[i].size);
        return true;
      }
    }
  } else if (section == 5) {
    for (int i = 0; i < countof(kKeyNameId); i++) {
      if (StringEqualsNoCase(key, kKeyNameId[i].name)) {
        if (i == 1)
          has_joypad_controls = true;
        ParseGamepadArrayTo(&g_joymap, value, kKeyNameId[i].id, kKeyNameId[i].size);
        return true;
      }
    }
  } else if (section == 9) {
    if (StringEqualsNoCase(key, "Controls")) {
      has_keynameid2 = true;
      ParseKeyArrayTo(&g_keymap2, value, kKeys_Controls,
                      kKeys_Controls_Last - kKeys_Controls + 1);
      return true;
    }
  } else if (section == 10) {
    if (StringEqualsNoCase(key, "Controls")) {
      has_joypad_controls2 = true;
      ParseGamepadArrayTo(&g_joymap2, value, kKeys_Controls,
                          kKeys_Controls_Last - kKeys_Controls + 1);
      return true;
    }
  } else if (section == 1) {
    if (StringEqualsNoCase(key, "WindowSize")) {
      char *s;
      if (StringEqualsNoCase(value, "Auto")){
        g_config.window_width  = 0;
        g_config.window_height = 0;
        return true;
      }
      while ((s = NextDelim(&value, 'x')) != NULL) {
        if(g_config.window_width == 0) {
          g_config.window_width = atoi(s);
        } else {
          g_config.window_height = atoi(s);
          return true;
        }
      }
    } else if (StringEqualsNoCase(key, "EnhancedMode7")) {
      return ParseBool(value, &g_config.enhanced_mode7);
    } else if (StringEqualsNoCase(key, "NewRenderer")) {
      return ParseBool(value, &g_config.new_renderer);
    } else if (StringEqualsNoCase(key, "IgnoreAspectRatio")) {
      return ParseBool(value, &g_config.ignore_aspect_ratio);
    } else if (StringEqualsNoCase(key, "Fullscreen")) {
      g_config.fullscreen = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "WindowScale")) {
      g_config.window_scale = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "OutputMethod")) {
      g_config.output_method = StringEqualsNoCase(value, "SDL-Software") ? kOutputMethod_SDLSoftware :
                               StringEqualsNoCase(value, "OpenGL") ? kOutputMethod_OpenGL : 
                               StringEqualsNoCase(value, "OpenGL ES") ? kOutputMethod_OpenGL_ES :
                                                                        kOutputMethod_SDL;
      return true;
    } else if (StringEqualsNoCase(key, "LinearFiltering")) {
      return ParseBool(value, &g_config.linear_filtering);
    } else if (StringEqualsNoCase(key, "NoSpriteLimits")) {
      return ParseBool(value, &g_config.no_sprite_limits);
    } else if (StringEqualsNoCase(key, "FrameDescription")) {
      return ParseBool(value, &g_config.frame_description);
    } else if (StringEqualsNoCase(key, "RenderPlugin")) {
      g_config.render_plugin = *value ? value : NULL;
      return true;
    } else if (StringEqualsNoCase(key, "LinkGraphics")) {
      g_config.link_graphics = value;
      return true;
    } else if (StringEqualsNoCase(key, "Shader")) {
      g_config.shader = *value ? value : NULL;
      return true;
    } else if (ModFeatures_ParseIni(section, key, value, &g_config.features0)) {
      return true;
    }
  } else if (section == 2) {
    if (StringEqualsNoCase(key, "EnableAudio")) {
      return ParseBool(value, &g_config.enable_audio);
    } else if (StringEqualsNoCase(key, "AudioFreq")) {
      g_config.audio_freq = (uint16)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AudioChannels")) {
      g_config.audio_channels = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AudioSamples")) {
      g_config.audio_samples = (uint16)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "EnableMSU")) {
        if (StringEqualsNoCase(value, "opuz"))
        g_config.enable_msu = kMsuEnabled_Opuz;
      else if (StringEqualsNoCase(value, "deluxe"))
        g_config.enable_msu = kMsuEnabled_MsuDeluxe;
      else if (StringEqualsNoCase(value, "deluxe-opuz"))
        g_config.enable_msu = kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz;
      else 
        return ParseBool(value, (bool*)&g_config.enable_msu);
      return true;
    } else if (StringEqualsNoCase(key, "MSUPath")) {
      g_config.msu_path = value;
      return true;
    } else if (StringEqualsNoCase(key, "MSUVolume")) {
      g_config.msuvolume = atoi(value);
      return true;
    } else if (StringEqualsNoCase(key, "ResumeMSU")) {
      return ParseBool(value, &g_config.resume_msu);
    }
  } else if (section == 3) {
    if (StringEqualsNoCase(key, "Autosave")) {
      g_config.autosave = (bool)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "ExtendedAspectRatio")) {
      const char* s;
      int h = 224;
      bool nospr = false, novis = false;
      // todo: make it not depend on the order
      while ((s = NextDelim(&value, ',')) != NULL) {
        if (strcmp(s, "extend_y") == 0)
          h = 240, g_config.extend_y = true;
        else if (strcmp(s, "16:9") == 0)
          g_config.extended_aspect_ratio = (h * 16 / 9 - 256) / 2;
        else if (strcmp(s, "16:10") == 0)
          g_config.extended_aspect_ratio = (h * 16 / 10 - 256) / 2;
        else if (strcmp(s, "18:9") == 0)
          g_config.extended_aspect_ratio = (h * 18 / 9 - 256) / 2;
        else if (strcmp(s, "4:3") == 0)
          g_config.extended_aspect_ratio = 0;
        else if (strcmp(s, "unchanged_sprites") == 0)
          nospr = true;
        else if (strcmp(s, "no_visual_fixes") == 0)
          novis = true;
        else
          return false;
      }
      if (g_config.extended_aspect_ratio && !nospr)
        g_config.features0 |= kFeatures0_ExtendScreen64;
      if (g_config.extended_aspect_ratio && !novis)
        g_config.features0 |= kFeatures0_WidescreenVisualFixes;
      return true;
    } else if (StringEqualsNoCase(key, "DisplayPerfInTitle")) {
      return ParseBool(value, &g_config.display_perf_title);
    } else if (StringEqualsNoCase(key, "DisableFrameDelay")) {
      return ParseBool(value, &g_config.disable_frame_delay);
    } else if (StringEqualsNoCase(key, "Language")) {
      g_config.language = value;
      return true;
    }
  } else if (section == 4) {
    if (ModFeatures_ParseIni(section, key, value, &g_config.features0))
      return true;
  } else if (section == 6) {
    if (StringEqualsNoCase(key, "Enabled")) {
      return ParseBool(value, &g_config.mods_enabled);
    } else if (StringEqualsNoCase(key, "Directory")) {
      g_config.mods_directory = value;
      return true;
    } else if (StringEqualsNoCase(key, "LuaInstructionLimit")) {
      char *end;
      errno = 0;
      unsigned long long limit = strtoull(value, &end, 10);
      if (errno != 0 || *value == 0 || *end != 0 || limit == 0)
        return false;
      g_config.lua_instruction_limit = (uint64)limit;
      return true;
    } else if (StringEqualsNoCase(key, "LuaMemoryMB")) {
      char *end;
      errno = 0;
      unsigned long long megabytes = strtoull(value, &end, 10);
      if (errno != 0 || *value == 0 || *end != 0 || megabytes == 0 ||
          megabytes > UINT32_MAX)
        return false;
      g_config.lua_memory_mb = (uint32)megabytes;
      return true;
    }
  } else if (section == 7) {
    if (StringEqualsNoCase(key, "EmuNwa")) {
      return ParseBool(value, &g_config.nwa_enabled);
    } else if (StringEqualsNoCase(key, "EmuNwaPort")) {
      char *end;
      errno = 0;
      long port = strtol(value, &end, 10);
      if (errno != 0 || *value == 0 || *end != 0 || port < 1 || port > 65535)
        return false;
      g_config.nwa_port = (uint16)port;
      return true;
    } else if (StringEqualsNoCase(key, "EmuNwaAllowWrite")) {
      return ParseBool(value, &g_config.nwa_allow_write);
    }
  } else if (section == 8) {
    if (StringEqualsNoCase(key, "Enabled")) {
      return ParseBool(value, &g_config.coop_enabled);
    } else if (StringEqualsNoCase(key, "Local")) {
      return ParseBool(value, &g_config.coop_local);
    } else if (StringEqualsNoCase(key, "SharedWorld")) {
      return ParseBool(value, &g_config.coop_shared_world);
    } else if (StringEqualsNoCase(key, "LocalLayout")) {
      g_config.coop_local_vertical =
          StringEqualsNoCase(value, "Vertical") ||
          StringEqualsNoCase(value, "v") ||
          StringEqualsNoCase(value, "stacked");
      if (!g_config.coop_local_vertical &&
          !StringEqualsNoCase(value, "Horizontal") &&
          !StringEqualsNoCase(value, "h") &&
          !StringEqualsNoCase(value, "sidebyside"))
        return false;
      return true;
    } else if (StringEqualsNoCase(key, "Server")) {
      g_config.coop_server = value;
      return true;
    } else if (StringEqualsNoCase(key, "Port")) {
      char *end;
      errno = 0;
      long port = strtol(value, &end, 10);
      if (errno != 0 || *value == 0 || *end != 0 || port < 1 || port > 65535)
        return false;
      g_config.coop_port = (uint16)port;
      return true;
    } else if (StringEqualsNoCase(key, "Group")) {
      g_config.coop_group = value;
      return true;
    } else if (StringEqualsNoCase(key, "PlayerName")) {
      g_config.coop_player_name = value;
      return true;
    } else if (StringEqualsNoCase(key, "PlayerName2")) {
      g_config.coop_player_name2 = value;
      return true;
    } else if (StringEqualsNoCase(key, "PlayerColor")) {
      char *end;
      errno = 0;
      long color = strtol(value, &end, 0);
      if (errno != 0 || *value == 0 || *end != 0 || color < 0 || color > 0xFFFF)
        return false;
      g_config.coop_player_color = (uint16)color;
      return true;
    } else if (StringEqualsNoCase(key, "PlayerColor2")) {
      char *end;
      errno = 0;
      long color = strtol(value, &end, 0);
      if (errno != 0 || *value == 0 || *end != 0 || color < 0 || color > 0xFFFF)
        return false;
      g_config.coop_player_color2 = (uint16)color;
      return true;
    } else if (StringEqualsNoCase(key, "RaceMode")) {
      return ParseBool(value, &g_config.coop_race_mode);
    } else if (StringEqualsNoCase(key, "ShowNames")) {
      return ParseBool(value, &g_config.coop_show_names);
    }
  }
  return false;
}

static bool ParseOneConfigFile(const char *filename, int depth) {
  char *filedata = (char*)ReadWholeFile(filename, NULL), *p;
  if (!filedata)
    return false;
  
  int section = -2;
  g_config.memory_buffer = filedata;

  for (int lineno = 1; (p = NextLineStripComments(&filedata)) != NULL; lineno++) {
    if (*p == 0)
      continue; // empty line
    if (*p == '[') {
      section = GetIniSection(p);
      if (section < 0)
        fprintf(stderr, "%s:%d: Invalid .ini section %s\n", filename, lineno, p);
    } else if (*p == '!' && SkipPrefix(p + 1, "include ")) {
      char *tt = p + 8;
      char *new_filename = ReplaceFilenameWithNewPath(filename, NextPossiblyQuotedString(&tt));
      if (depth > 10 || !ParseOneConfigFile(new_filename, depth + 1))
        fprintf(stderr, "Warning: Unable to read %s\n", new_filename);
      free(new_filename);
    } else if (section == -2) {
      fprintf(stderr, "%s:%d: Expecting [section]\n", filename, lineno);
    } else {
      char *v = SplitKeyValue(p);
      if (v == NULL) {
        fprintf(stderr, "%s:%d: Expecting 'key=value'\n", filename, lineno);
        continue;
      }
      if (section >= 0 && !HandleIniConfig(section, p, v))
        fprintf(stderr, "%s:%d: Can't parse '%s'\n", filename, lineno, p);
    }
  }
  return true;
}

void ParseConfigFile(const char *filename) {
  g_config.msuvolume = 100;  // default msu volume, 100%
  g_config.mods_enabled = true;
  g_config.mods_directory = "mods";
  g_config.lua_instruction_limit = 10000000;
  g_config.lua_memory_mb = 32;
  g_config.nwa_enabled = false;
  g_config.nwa_port = 48879;
  g_config.nwa_allow_write = false;
  g_config.coop_enabled = false;
  g_config.coop_local = false;
  g_config.coop_local_vertical = false;
  g_config.coop_shared_world = false;
  g_config.coop_server = "alttp.online";
  g_config.coop_port = 4590;
  g_config.coop_group = "zelda3";
  g_config.coop_player_name = "Link";
  g_config.coop_player_name2 = "Link2";
  g_config.coop_player_color = 0x085C;
  g_config.coop_player_color2 = 0x0B7C;
  g_config.coop_show_names = true;
  g_config.coop_race_mode = false;

  if (filename != NULL || !ParseOneConfigFile("zelda3.user.ini", 0)) {
    if (filename == NULL)
      filename = "zelda3.ini";
    if (!ParseOneConfigFile(filename, 0))
      fprintf(stderr, "Warning: Unable to read config file %s\n", filename);
  }
  RegisterDefaultKeys();
}
