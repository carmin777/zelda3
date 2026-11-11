#include "menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config.h"
#include "../features.h"
#include "../util.h"
#include "draw.h"
#include "mod_features.h"
#include "mods.h"
#include "render_plugin.h"

// The menu is deliberately self-contained: it edits Config and asks the host
// to apply the live parts. Nothing here runs while the menu is closed.

enum {
  kMenuPage_Video,
  kMenuPage_Sound,
  kMenuPage_Gameplay,
  kMenuPage_Mods,
  kMenuPage_Keys,
  kMenuPage_Count,
};

enum {
  kMenuCapture_None,
  kMenuCapture_Keyboard,
  kMenuCapture_Gamepad,
};

enum {
  kMenuLineHeight = 10,
  kMenuModCapacity = 128,
  kMenuTextSize = 256,
};

static const char *const kPageNames[kMenuPage_Count] = {
  "Video", "Sound", "Gameplay", "Mods", "Keys",
};

static const char *const kControlNames[12] = {
  "UP", "DOWN", "LEFT", "RIGHT", "SELECT", "START",
  "A", "B", "X", "Y", "L", "R",
};

static bool g_open;
static int g_page;
static int g_sel[kMenuPage_Count];
static int g_scroll[kMenuPage_Count];
static bool g_dirty;
static bool g_mods_dirty;
static bool g_audio_restart;
static int g_capture;
static int g_capture_row;
static ModListInfo g_mods[kMenuModCapacity];
static int g_mod_count;
static char g_status[96];

static int IntClamp(int value, int min, int max) {
  if (value < min)
    return min;
  if (value > max)
    return max;
  return value;
}

static int PageRowCount(int page) {
  switch (page) {
  case kMenuPage_Video:
    return 6;
  case kMenuPage_Sound:
    return 5;
  case kMenuPage_Gameplay:
    return ModFeatures_Count();
  case kMenuPage_Mods:
    return g_mod_count > 0 ? g_mod_count : 1;
  case kMenuPage_Keys:
    return 12 + 1 + 12;
  default:
    return 0;
  }
}

static int AspectPresetIndex(void) {
  int h = g_config.extend_y ? 240 : 224;
  int extra = g_config.extended_aspect_ratio;
  if (extra == 0)
    return 0;
  if (extra == (h * 16 / 9 - 256) / 2)
    return 1;
  if (extra == (h * 16 / 10 - 256) / 2)
    return 2;
  if (extra == (h * 18 / 9 - 256) / 2)
    return 3;
  return 0;
}

static void SetAspectPreset(int preset) {
  int h = g_config.extend_y ? 240 : 224;
  int extra = 0;
  switch (preset) {
  case 1: extra = (h * 16 / 9 - 256) / 2; break;
  case 2: extra = (h * 16 / 10 - 256) / 2; break;
  case 3: extra = (h * 18 / 9 - 256) / 2; break;
  default: extra = 0; break;
  }
  g_config.extended_aspect_ratio = (uint8)IntClamp(extra, 0, 255);
  uint32 flags = g_config.features0;
  if (g_config.extended_aspect_ratio != 0) {
    if ((g_config.aspect_ratio_flags & kAspectFlag_UnchangedSprites) == 0)
      flags |= kFeatures0_ExtendScreen64;
    if ((g_config.aspect_ratio_flags & kAspectFlag_NoVisualFixes) == 0)
      flags |= kFeatures0_WidescreenVisualFixes;
  } else {
    flags &= ~(kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes);
  }
  g_config.features0 = flags;
}

static const char *const kAspectNames[] = { "4:3", "16:9", "16:10", "18:9" };
static const char *const kFullscreenNames[] = { "OFF", "DESKTOP", "FULL" };
static const char *const kMsuNames[] = { "OFF", "MSU", "DELUXE", "DELUXE+OPUZ" };
static const uint8 kMsuStates[] = { 0, 1, 2, 5 };

static void GetRow(int page, int row, char *label, size_t label_size,
                   char *value, size_t value_size, bool *disabled) {
  *disabled = false;
  label[0] = value[0] = 0;
  switch (page) {
  case kMenuPage_Video:
    switch (row) {
    case 0:
      snprintf(label, label_size, "WINDOW SCALE");
      snprintf(value, value_size, "%dX",
               g_config.window_scale == 0 ? 2
                                          : IntClamp(g_config.window_scale, 1, 10));
      break;
    case 1:
      snprintf(label, label_size, "FULLSCREEN");
      snprintf(value, value_size, "%s", kFullscreenNames[IntClamp(g_config.fullscreen, 0, 2)]);
      break;
    case 2:
      snprintf(label, label_size, "RENDERER");
      snprintf(value, value_size, "%s", g_config.new_renderer ? "NEW" : "OLD");
      break;
    case 3:
      snprintf(label, label_size, "WIDESCREEN");
      snprintf(value, value_size, "%s", kAspectNames[AspectPresetIndex()]);
      break;
    case 4:
      snprintf(label, label_size, "SCREEN LINES");
      snprintf(value, value_size, "%d", g_config.extend_y ? 240 : 224);
      break;
    case 5:
      snprintf(label, label_size, "RENDER PLUGIN");
      if (MenuHost_RenderPluginActive())
        snprintf(value, value_size, "%s", g_config.render_plugin_enabled ? "ON" : "OFF");
      else
        snprintf(value, value_size, "NOT LOADED");
      *disabled = !MenuHost_RenderPluginActive();
      break;
    }
    break;
  case kMenuPage_Sound:
    switch (row) {
    case 0:
      snprintf(label, label_size, "VOLUME");
      snprintf(value, value_size, "%d%%", g_config.volume);
      break;
    case 1:
      snprintf(label, label_size, "MONO");
      snprintf(value, value_size, "%s",
               g_config.audio_channels == 1 ? "ON" : "OFF");
      break;
    case 2: {
      snprintf(label, label_size, "MSU AUDIO");
      int index = 0;
      for (int i = 0; i < (int)countof(kMsuStates); i++) {
        if (g_config.enable_msu == kMsuStates[i])
          index = i;
      }
      snprintf(value, value_size, "%s", kMsuNames[index]);
      break;
    }
    case 3:
      snprintf(label, label_size, "MSU VOLUME");
      snprintf(value, value_size, "%d%%", g_config.msuvolume);
      break;
    case 4:
      snprintf(label, label_size, "RESUME MSU");
      snprintf(value, value_size, "%s", g_config.resume_msu ? "ON" : "OFF");
      break;
    }
    break;
  case kMenuPage_Gameplay: {
    const char *name = ModFeatures_Name(row);
    uint32 mask = ModFeatures_Mask(row);
    snprintf(label, label_size, "%s", name ? name : "");
    snprintf(value, value_size, "%s", (g_config.features0 & mask) ? "ON" : "OFF");
    break;
  }
  case kMenuPage_Mods:
    if (g_mod_count == 0) {
      snprintf(label, label_size, "NO MODS FOUND");
      snprintf(value, value_size, " ");
    } else if (row >= 0 && row < g_mod_count) {
      const ModListInfo *mod = &g_mods[row];
      snprintf(label, label_size, "%s", mod->name[0] ? mod->name : mod->id);
      snprintf(value, value_size, "%s P:%d%s", mod->enabled ? "ON " : "OFF",
               mod->priority, mod->loaded ? "" : " *");
    }
    break;
  case kMenuPage_Keys:
    if (row < 12) {
      char keybuf[64];
      snprintf(label, label_size, "KEY %s", kControlNames[row]);
      Config_FormatKey(Config_GetCommandKey(kKeys_Controls + row), keybuf,
                       sizeof(keybuf));
      snprintf(value, value_size, "%s", keybuf);
    } else if (row == 12) {
      char keybuf[64];
      snprintf(label, label_size, "MENU KEY");
      Config_FormatKey(Config_GetCommandKey(kKeys_OptionsMenu), keybuf,
                       sizeof(keybuf));
      snprintf(value, value_size, "%s", keybuf);
    } else {
      int control = row - 13;
      snprintf(label, label_size, "PAD %s", kControlNames[control]);
      int button = Config_GetCommandGamepad(kKeys_Controls + control);
      snprintf(value, value_size, "%s", Config_GamepadButtonName(button));
    }
    break;
  }
}

static void TouchConfig(void) {
  g_dirty = true;
  g_status[0] = 0;
}

static void ChangeVideo(int row, int delta) {
  switch (row) {
  case 0: {
    int scale = g_config.window_scale == 0 ? 2 : g_config.window_scale;
    g_config.window_scale = (uint8)IntClamp(scale + delta, 1, 10);
    g_config.window_width = 0;
    g_config.window_height = 0;
    break;
  }
  case 1:
    g_config.fullscreen = (uint8)((g_config.fullscreen + delta + 3) % 3);
    break;
  case 2:
    g_config.new_renderer = !g_config.new_renderer;
    break;
  case 3:
    SetAspectPreset((AspectPresetIndex() + delta + 4) % 4);
    break;
  case 4: {
    int preset = AspectPresetIndex();
    g_config.extend_y = !g_config.extend_y;
    SetAspectPreset(preset);
    break;
  }
  case 5:
    if (MenuHost_RenderPluginActive())
      g_config.render_plugin_enabled = !g_config.render_plugin_enabled;
    break;
  }
  MenuHost_ApplyVideo();
  TouchConfig();
}

static void ChangeSound(int row, int delta) {
  switch (row) {
  case 0:
    g_config.volume = (uint8)IntClamp(g_config.volume + delta * 5, 0, 100);
    g_config.volume_set = true;
    MenuHost_SetVolume(g_config.volume);
    break;
  case 1: {
    int channels = g_config.audio_channels == 1 ? 2 : 1;
    if (!MenuHost_SetAudioChannels(channels)) {
      // Keep the saved value even if the device could not be reopened.
      g_config.audio_channels = (uint8)channels;
      g_audio_restart = true;
    } else {
      g_audio_restart = false;
    }
    break;
  }
  case 2: {
    int index = 0;
    for (int i = 0; i < (int)countof(kMsuStates); i++) {
      if (g_config.enable_msu == kMsuStates[i])
        index = i;
    }
    index = (index + delta + (int)countof(kMsuStates)) % (int)countof(kMsuStates);
    g_config.enable_msu = kMsuStates[index];
    MenuHost_ApplyAudio();
    break;
  }
  case 3:
    g_config.msuvolume = (uint8)IntClamp(g_config.msuvolume + delta * 5, 0, 100);
    MenuHost_ApplyAudio();
    break;
  case 4:
    g_config.resume_msu = !g_config.resume_msu;
    break;
  }
  TouchConfig();
}

static void ChangeGameplay(int row, int delta) {
  uint32 mask = ModFeatures_Mask(row);
  if (mask == 0)
    return;
  bool on;
  if (delta > 0)
    on = true;
  else if (delta < 0)
    on = false;
  else
    on = (g_config.features0 & mask) == 0;
  g_config.features0 = (g_config.features0 & ~mask) | (on ? mask : 0);
  MenuHost_ApplyFeatures();
  TouchConfig();
}

static void ChangeMods(int row, int delta) {
  if (g_mod_count == 0 || row < 0 || row >= g_mod_count)
    return;
  ModListInfo *mod = &g_mods[row];
  if (delta == 0) {
    mod->enabled = !mod->enabled;
  } else {
    mod->priority = IntClamp(mod->priority + delta, -999, 999);
  }
  g_mods_dirty = true;
  g_status[0] = 0;
  TouchConfig();
}

static void StartCapture(int row) {
  if (row < 12) {
    g_capture = kMenuCapture_Keyboard;
    g_capture_row = kKeys_Controls + row;
  } else if (row == 12) {
    g_capture = kMenuCapture_Keyboard;
    g_capture_row = kKeys_OptionsMenu;
  } else {
    g_capture = kMenuCapture_Gamepad;
    g_capture_row = kKeys_Controls + row - 13;
  }
  g_status[0] = 0;
}

static void ActivateRow(int page, int row) {
  switch (page) {
  case kMenuPage_Video:
    ChangeVideo(row, 1);
    break;
  case kMenuPage_Sound:
    ChangeSound(row, 1);
    break;
  case kMenuPage_Gameplay:
    ChangeGameplay(row, 0);
    break;
  case kMenuPage_Mods:
    ChangeMods(row, 0);
    break;
  case kMenuPage_Keys:
    StartCapture(row);
    break;
  }
}

static void ChangeRow(int page, int row, int delta) {
  switch (page) {
  case kMenuPage_Video:
    ChangeVideo(row, delta);
    break;
  case kMenuPage_Sound:
    ChangeSound(row, delta);
    break;
  case kMenuPage_Gameplay:
    ChangeGameplay(row, delta);
    break;
  case kMenuPage_Mods:
    ChangeMods(row, delta);
    break;
  default:
    break;
  }
}

static void MoveSelection(int delta) {
  int count = PageRowCount(g_page);
  if (count <= 0)
    return;
  g_sel[g_page] = (g_sel[g_page] + delta + count) % count;
}

static void SwitchPage(int delta) {
  g_page = (g_page + delta + kMenuPage_Count) % kMenuPage_Count;
  int count = PageRowCount(g_page);
  if (g_sel[g_page] >= count)
    g_sel[g_page] = count > 0 ? count - 1 : 0;
  g_status[0] = 0;
}

void OptionsMenu_Init(void) {
  g_open = false;
  g_page = kMenuPage_Video;
  memset(g_sel, 0, sizeof(g_sel));
  memset(g_scroll, 0, sizeof(g_scroll));
  g_dirty = false;
  g_mods_dirty = false;
  g_audio_restart = false;
  g_capture = kMenuCapture_None;
  g_status[0] = 0;
  g_mod_count = Mods_ListCount();
  if (g_mod_count > kMenuModCapacity)
    g_mod_count = kMenuModCapacity;
  for (int i = 0; i < g_mod_count; i++) {
    const ModListInfo *mod = Mods_ListGet(i);
    if (mod != NULL)
      g_mods[i] = *mod;
  }
}

bool OptionsMenu_IsOpen(void) {
  return g_open;
}

void OptionsMenu_Open(void) {
  g_open = true;
  g_capture = kMenuCapture_None;
  g_status[0] = 0;
  int count = PageRowCount(g_page);
  if (g_sel[g_page] >= count)
    g_sel[g_page] = count > 0 ? count - 1 : 0;
}

static bool SaveUserIni(const char *path);

void OptionsMenu_Close(void) {
  if (g_open && g_dirty)
    SaveUserIni("zelda3.user.ini");
  g_open = false;
  g_capture = kMenuCapture_None;
}

void OptionsMenu_Toggle(void) {
  if (g_open)
    OptionsMenu_Close();
  else
    OptionsMenu_Open();
}

bool OptionsMenu_HandleKey(SDL_Keycode key, SDL_Keymod mod, bool pressed) {
  if (!g_open)
    return false;

  if (g_capture != kMenuCapture_None) {
    if (!pressed)
      return true;
    if (key == SDLK_ESCAPE) {
      g_capture = kMenuCapture_None;
      return true;
    }
    if (g_capture == kMenuCapture_Keyboard) {
      uint16 binding = Config_KeyFromSdl(key, mod);
      if (binding != 0) {
        if (Config_SetCommandKey(g_capture_row, binding)) {
          TouchConfig();
        } else {
          snprintf(g_status, sizeof(g_status), "KEY ALREADY USED");
        }
      }
      g_capture = kMenuCapture_None;
    }
    return true;
  }

  uint16 menu_key = Config_GetCommandKey(kKeys_OptionsMenu);
  if (pressed && menu_key != 0 && Config_KeyFromSdl(key, mod) == menu_key) {
    OptionsMenu_Close();
    return true;
  }

  if (!pressed)
    return true;

  switch (key) {
  case SDLK_UP:
    MoveSelection(-1);
    break;
  case SDLK_DOWN:
    MoveSelection(1);
    break;
  case SDLK_LEFT:
    ChangeRow(g_page, g_sel[g_page], -1);
    break;
  case SDLK_RIGHT:
    ChangeRow(g_page, g_sel[g_page], 1);
    break;
  case SDLK_PAGEUP:
    SwitchPage(-1);
    break;
  case SDLK_PAGEDOWN:
  case SDLK_TAB:
    SwitchPage(1);
    break;
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
  case SDLK_SPACE:
    ActivateRow(g_page, g_sel[g_page]);
    break;
  case SDLK_ESCAPE:
  case SDLK_BACKSPACE:
    if (g_capture != kMenuCapture_None)
      g_capture = kMenuCapture_None;
    else
      OptionsMenu_Close();
    break;
  default:
    break;
  }
  return true;
}

bool OptionsMenu_HandleGamepadButton(int button, bool pressed) {
  if (!g_open)
    return false;

  if (g_capture != kMenuCapture_None) {
    if (pressed && g_capture == kMenuCapture_Gamepad) {
      if (Config_SetCommandGamepad(g_capture_row, button))
        TouchConfig();
      else
        snprintf(g_status, sizeof(g_status), "BUTTON ALREADY USED");
      g_capture = kMenuCapture_None;
    }
    return true;
  }

  if (pressed && button == Config_GetCommandGamepad(kKeys_OptionsMenu)) {
    OptionsMenu_Close();
    return true;
  }

  if (!pressed)
    return true;

  switch (button) {
  case kGamepadBtn_DpadUp:
    MoveSelection(-1);
    break;
  case kGamepadBtn_DpadDown:
    MoveSelection(1);
    break;
  case kGamepadBtn_DpadLeft:
    ChangeRow(g_page, g_sel[g_page], -1);
    break;
  case kGamepadBtn_DpadRight:
    ChangeRow(g_page, g_sel[g_page], 1);
    break;
  case kGamepadBtn_L1:
    SwitchPage(-1);
    break;
  case kGamepadBtn_R1:
    SwitchPage(1);
    break;
  case kGamepadBtn_A:
  case kGamepadBtn_Start:
    ActivateRow(g_page, g_sel[g_page]);
    break;
  case kGamepadBtn_B:
    OptionsMenu_Close();
    break;
  default:
    break;
  }
  return true;
}

static void DrawText(int x, int y, const char *text, uint32 color,
                     int max_chars) {
  if (max_chars < 0)
    return;
  size_t len = strlen(text);
  if (max_chars >= 0 && len > (size_t)max_chars)
    len = (size_t)max_chars;
  ModDraw_AddText(x, y, text, len, color);
}

// Same as DrawText, but the last three cells become "..." when the text does
// not fit. List labels use this so a long mod name never reaches the value
// column.
static void DrawTextEllipsized(int x, int y, const char *text, uint32 color,
                               int max_chars) {
  if (max_chars <= 0)
    return;
  size_t len = strlen(text);
  if (len <= (size_t)max_chars) {
    ModDraw_AddText(x, y, text, len, color);
    return;
  }
  if (max_chars <= 3) {
    ModDraw_AddText(x, y, "...", (size_t)max_chars, color);
    return;
  }
  size_t keep = (size_t)max_chars - 3;
  ModDraw_AddText(x, y, text, keep, color);
  ModDraw_AddText(x + (int)keep * 6, y, "...", 3, color);
}

static int TextWidth(const char *text) {
  return (int)strlen(text) * 6;
}

static void DrawHeader(int inner_x, int inner_w, int y) {
  char title[64];
  snprintf(title, sizeof(title), "OPTIONS");
  DrawText(inner_x + 8, y, title, 0xFFFFFFFF, inner_w / 6);
  int total = 0;
  for (int i = 0; i < kMenuPage_Count; i++) {
    total += TextWidth(kPageNames[i]);
    if (i + 1 < kMenuPage_Count)
      total += 8;
  }
  int x = inner_x + (inner_w - total) / 2;
  for (int i = 0; i < kMenuPage_Count; i++) {
    uint32 color = i == g_page ? 0xFFFFE080 : 0xFF9FB4C8;
    DrawText(x, y + 11, kPageNames[i], color, 32);
    x += TextWidth(kPageNames[i]) + 8;
  }
}

void OptionsMenu_Draw(void) {
  if (!g_open)
    return;
  int width = ModDraw_Width();
  int height = ModDraw_Height();
  if (width < 64 || height < 64)
    return;

  int panel_w = width - 16;
  if (panel_w > 360)
    panel_w = 360;
  if (panel_w < 180)
    panel_w = width - 4;
  int panel_h = height - 12;
  int panel_x = (width - panel_w) / 2;
  int panel_y = (height - panel_h) / 2;

  ModDraw_AddRect(0, 0, width, height, 0xA8000000);
  ModDraw_AddRect(panel_x, panel_y, panel_w, panel_h, 0xFF4C6480);
  ModDraw_AddRect(panel_x + 1, panel_y + 1, panel_w - 2, panel_h - 2,
                  0xF0101820);

  int inner_x = panel_x + 5;
  int inner_w = panel_w - 10;
  DrawHeader(inner_x, inner_w, panel_y + 5);
  ModDraw_AddRect(inner_x, panel_y + 27, inner_w, 1, 0xFF4C6480);

  int list_top = panel_y + 32;
  int footer_y = panel_y + panel_h - 19;
  int list_bottom = footer_y - 2;
  int visible = (list_bottom - list_top) / kMenuLineHeight;
  if (visible < 1)
    visible = 1;

  int count = PageRowCount(g_page);
  if (g_sel[g_page] >= count)
    g_sel[g_page] = count > 0 ? count - 1 : 0;
  if (g_sel[g_page] < 0)
    g_sel[g_page] = 0;
  if (g_sel[g_page] < g_scroll[g_page])
    g_scroll[g_page] = g_sel[g_page];
  if (g_sel[g_page] >= g_scroll[g_page] + visible)
    g_scroll[g_page] = g_sel[g_page] - visible + 1;
  if (g_scroll[g_page] < 0)
    g_scroll[g_page] = 0;
  if (g_scroll[g_page] > count - visible)
    g_scroll[g_page] = count > visible ? count - visible : 0;

  for (int i = 0; i < visible && g_scroll[g_page] + i < count; i++) {
    int row = g_scroll[g_page] + i;
    int y = list_top + i * kMenuLineHeight;
    char label[kMenuTextSize];
    char value[96];
    bool disabled;
    GetRow(g_page, row, label, sizeof(label), value, sizeof(value), &disabled);
    if (row == g_sel[g_page])
      ModDraw_AddRect(inner_x, y - 1, inner_w, kMenuLineHeight - 1,
                      0xFF304860);
    uint32 label_color = disabled ? 0xFF707880 : 0xFFE0E0E0;
    uint32 value_color = disabled ? 0xFF707880 : 0xFFFFE080;
    // Mod rows reserve a fixed-width state column so ON/OFF and the priority
    // start at the same pixel even when the names have different lengths.
    int value_x = g_page == kMenuPage_Mods
                      ? inner_x + inner_w - 6 - 12 * 6
                      : inner_x + inner_w - 6 - TextWidth(value);
    if (value_x < inner_x + 10)
      value_x = inner_x + 10;
    int label_max = (value_x - (inner_x + 10)) / 6;
    DrawText(inner_x + 2, y, row == g_sel[g_page] ? ">" : " ",
             label_color, 1);
    DrawTextEllipsized(inner_x + 10, y, label, label_color, label_max);
    DrawText(value_x, y, value, value_color, inner_w / 6);
  }

  if (count > visible) {
    char pos[32];
    snprintf(pos, sizeof(pos), "%d/%d", g_sel[g_page] + 1, count);
    DrawText(inner_x + inner_w - TextWidth(pos), panel_y + 5, pos,
             0xFF9FB4C8, inner_w / 6);
  }

  const char *help;
  if (g_capture != kMenuCapture_None) {
    help = g_capture == kMenuCapture_Gamepad
               ? "PRESS A BUTTON  ESC CANCELS"
               : "PRESS A KEY  ESC CANCELS";
  } else if (g_page == kMenuPage_Mods) {
    help = "ENTER ON/OFF  LEFT/RIGHT PRIORITY";
  } else if (g_page == kMenuPage_Keys) {
    help = "ENTER REBIND  PGUP/PGDN PAGE";
  } else {
    help = "ARROWS MOVE  LEFT/RIGHT CHANGE";
  }
  DrawText(inner_x, footer_y, help, 0xFF9FB4C8, inner_w / 6);
  const char *status = g_status[0] ? g_status
                       : g_mods_dirty && g_audio_restart
                             ? "RESTART REQUIRED FOR MOD/AUDIO CHANGES"
                       : g_mods_dirty ? "RESTART REQUIRED FOR MOD CHANGES"
                       : g_audio_restart ? "RESTART REQUIRED FOR AUDIO CHANGES"
                                         : "PGUP/PGDN PAGE  SAVED ON CLOSE";
  DrawText(inner_x, footer_y + 9, status, 0xFF9FB4C8, inner_w / 6);
}

// ---------------------------------------------------------------------------
// zelda3.user.ini writer

static bool IsManagedSectionName(const char *line, int *section) {
  if (StringEqualsNoCase(line, "[Graphics]")) {
    *section = 1;
    return true;
  }
  if (StringEqualsNoCase(line, "[General]")) {
    *section = 3;
    return true;
  }
  if (StringEqualsNoCase(line, "[Sound]")) {
    *section = 2;
    return true;
  }
  if (StringEqualsNoCase(line, "[Features]")) {
    *section = 4;
    return true;
  }
  if (StringEqualsNoCase(line, "[KeyMap]")) {
    *section = 0;
    return true;
  }
  if (StringEqualsNoCase(line, "[GamepadMap]")) {
    *section = 5;
    return true;
  }
  if (StringEqualsNoCase(line, "[Mods]")) {
    *section = 6;
    return true;
  }
  return false;
}

static bool IsManagedKey(int section, const char *key) {
  if (section == 1) {
    return StringEqualsNoCase(key, "WindowScale") ||
           StringEqualsNoCase(key, "Fullscreen") ||
           StringEqualsNoCase(key, "NewRenderer") ||
           StringEqualsNoCase(key, "RenderPluginEnabled") ||
           ModFeatures_IsManagedKey(section, key);
  }
  if (section == 3)
    return StringEqualsNoCase(key, "ExtendedAspectRatio");
  if (section == 2) {
    if (g_config.volume_set && StringEqualsNoCase(key, "Volume"))
      return true;
    return StringEqualsNoCase(key, "AudioChannels") ||
           StringEqualsNoCase(key, "EnableMSU") ||
           StringEqualsNoCase(key, "MSUVolume") ||
           StringEqualsNoCase(key, "ResumeMSU");
  }
  if (section == 4)
    return ModFeatures_IsManagedKey(section, key);
  if (section == 0 || section == 5)
    return StringEqualsNoCase(key, "Controls") ||
           StringEqualsNoCase(key, "Menu");
  if (section == 6)
    return StringEqualsNoCase(key, "EnableOverrides") ||
           StringEqualsNoCase(key, "DisableOverrides") ||
           StringEqualsNoCase(key, "PriorityOverrides");
  return false;
}

static void Append(char *buf, size_t size, size_t *pos, const char *text) {
  if (*pos >= size)
    return;
  int n = snprintf(buf + *pos, size - *pos, "%s", text);
  if (n < 0)
    return;
  *pos += (size_t)n;
  if (*pos >= size)
    *pos = size - 1;
}

static void FormatAspect(char *buf, size_t size) {
  size_t pos = 0;
  buf[0] = 0;
  if (g_config.extend_y)
    Append(buf, size, &pos, "extend_y, ");
  if (g_config.aspect_ratio_flags & kAspectFlag_UnchangedSprites)
    Append(buf, size, &pos, "unchanged_sprites, ");
  if (g_config.aspect_ratio_flags & kAspectFlag_NoVisualFixes)
    Append(buf, size, &pos, "no_visual_fixes, ");
  Append(buf, size, &pos, kAspectNames[AspectPresetIndex()]);
}

static void AppendSeparated(char *buf, size_t size, size_t *pos,
                            const char *text) {
  if (*pos != 0)
    Append(buf, size, pos, ", ");
  Append(buf, size, pos, text);
}

static bool SafeOverrideId(const char *id) {
  if (id[0] == 0)
    return false;
  for (const char *p = id; *p; p++) {
    if (*p == ',' || *p == '=' || *p == '%' || *p == ' ' || *p == '\t')
      return false;
  }
  return true;
}

static void BuildModOverrides(char *enable, size_t enable_size,
                              char *disable, size_t disable_size,
                              char *priority, size_t priority_size) {
  size_t ep = 0, dp = 0, pp = 0;
  enable[0] = disable[0] = priority[0] = 0;
  for (int i = 0; i < g_mod_count; i++) {
    const ModListInfo *mod = &g_mods[i];
    if (!SafeOverrideId(mod->id)) {
      fprintf(stderr, "menu: mod id '%s' cannot be stored in zelda3.user.ini\n",
              mod->id);
      continue;
    }
    if (mod->enabled != mod->manifest_enabled)
      AppendSeparated(mod->enabled ? enable : disable,
                      mod->enabled ? enable_size : disable_size,
                      mod->enabled ? &ep : &dp, mod->id);
    if (mod->priority != mod->manifest_priority) {
      char item[96];
      snprintf(item, sizeof(item), "%s=%d", mod->id, mod->priority);
      AppendSeparated(priority, priority_size, &pp, item);
    }
  }
}

static bool SaveUserIni(const char *path) {
  size_t old_size = 0;
  char *old = (char *)ReadWholeFile(path, &old_size);
  bool has_include = false;
  if (old != NULL) {
    char *scan = old;
    while (*scan) {
      char *eol = strchr(scan, '\n');
      size_t n = eol ? (size_t)(eol - scan) : strlen(scan);
      char line[512];
      size_t c = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
      memcpy(line, scan, c);
      line[c] = 0;
      char *p = line;
      while (*p == ' ' || *p == '\t')
        p++;
      if (StringStartsWithNoCase(p, "!include "))
        has_include = true;
      scan = eol ? eol + 1 : scan + n;
      if (eol == NULL)
        break;
    }
  }

  char tmp_path[1024];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  remove(tmp_path);
  FILE *f = fopen(tmp_path, "wb");
  if (f == NULL) {
    fprintf(stderr, "menu: cannot write %s\n", tmp_path);
    free(old);
    return false;
  }

  fprintf(f, "# Updated by the in-game options menu.\n");
  if (!has_include)
    fprintf(f, "!include zelda3.ini\n");

  if (old != NULL) {
    int section = -1;
    char *scan = old;
    while (*scan) {
      char *eol = strchr(scan, '\n');
      size_t n = eol ? (size_t)(eol - scan) : strlen(scan);
      char line[1024];
      size_t c = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
      memcpy(line, scan, c);
      line[c] = 0;
      char *trim = line;
      while (*trim == ' ' || *trim == '\t')
        trim++;
      size_t trim_len = strlen(trim);
      while (trim_len > 0 &&
             (trim[trim_len - 1] == '\r' || trim[trim_len - 1] == ' ' ||
              trim[trim_len - 1] == '\t'))
        trim[--trim_len] = 0;
      int new_section = -1;
      bool skip = false;
      if (*trim == '[') {
        section = IsManagedSectionName(trim, &new_section) ? new_section : -1;
      } else if (section >= 0 && *trim != 0 && *trim != '#' && *trim != '!' &&
                 *trim != '[') {
        char keybuf[256];
        snprintf(keybuf, sizeof(keybuf), "%s", trim);
        char *eq = strchr(keybuf, '=');
        if (eq != NULL) {
          char *end = eq;
          while (end > keybuf && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
          *end = 0;
          if (IsManagedKey(section, keybuf))
            skip = true;
        }
      }
      if (!skip) {
        fwrite(scan, 1, n, f);
        fputc('\n', f);
      }
      scan = eol ? eol + 1 : scan + n;
      if (eol == NULL)
        break;
    }
    free(old);
  }

  char aspect[128];
  FormatAspect(aspect, sizeof(aspect));
  fprintf(f, "\n[General]\n");
  fprintf(f, "ExtendedAspectRatio = %s\n", aspect);

  fprintf(f, "\n[Graphics]\n");
  fprintf(f, "WindowScale = %d\n", g_config.window_scale);
  fprintf(f, "Fullscreen = %d\n", g_config.fullscreen);
  fprintf(f, "NewRenderer = %d\n", g_config.new_renderer ? 1 : 0);
  fprintf(f, "RenderPluginEnabled = %d\n", g_config.render_plugin_enabled ? 1 : 0);
  fprintf(f, "DimFlashes = %d\n",
          (g_config.features0 & kFeatures0_DimFlashes) ? 1 : 0);

  fprintf(f, "\n[Sound]\n");
  if (g_config.volume_set)
    fprintf(f, "Volume = %d\n", g_config.volume);
  fprintf(f, "AudioChannels = %d\n", g_config.audio_channels == 1 ? 1 : 2);
  fprintf(f, "EnableMSU = %d\n", g_config.enable_msu);
  fprintf(f, "MSUVolume = %d\n", g_config.msuvolume);
  fprintf(f, "ResumeMSU = %d\n", g_config.resume_msu ? 1 : 0);

  fprintf(f, "\n[Features]\n");
  for (int i = 0; i < ModFeatures_Count(); i++) {
    if (ModFeatures_IniSection(i) != 4)
      continue;
    uint32 mask = ModFeatures_Mask(i);
    fprintf(f, "%s = %d\n", ModFeatures_Name(i),
            (g_config.features0 & mask) ? 1 : 0);
  }

  char controls[512];
  char gamepad[512];
  char menu_key[64];
  Config_FormatControls(controls, sizeof(controls));
  Config_FormatGamepadControls(gamepad, sizeof(gamepad));
  Config_FormatKey(Config_GetCommandKey(kKeys_OptionsMenu), menu_key,
                   sizeof(menu_key));
  fprintf(f, "\n[KeyMap]\n");
  fprintf(f, "Controls = %s\n", controls);
  fprintf(f, "Menu = %s\n", menu_key);
  fprintf(f, "\n[GamepadMap]\n");
  fprintf(f, "Controls = %s\n", gamepad);
  fprintf(f, "Menu = %s\n",
          Config_GamepadButtonName(Config_GetCommandGamepad(kKeys_OptionsMenu)));

  char enable[2048], disable[2048], priority[4096];
  BuildModOverrides(enable, sizeof(enable), disable, sizeof(disable),
                    priority, sizeof(priority));
  fprintf(f, "\n[Mods]\n");
  fprintf(f, "EnableOverrides = %s\n", enable);
  fprintf(f, "DisableOverrides = %s\n", disable);
  fprintf(f, "PriorityOverrides = %s\n", priority);

  bool ok = fflush(f) == 0;
  ok = fclose(f) == 0 && ok;
  if (!ok) {
    fprintf(stderr, "menu: failed while writing %s\n", tmp_path);
    remove(tmp_path);
    return false;
  }
  remove(path);
  if (rename(tmp_path, path) != 0) {
    fprintf(stderr, "menu: cannot replace %s\n", path);
    remove(tmp_path);
    return false;
  }
  g_dirty = false;
  return true;
}

bool OptionsMenu_OpenPageByName(const char *name) {
  for (int i = 0; i < kMenuPage_Count; i++) {
    if (StringEqualsNoCase(name, kPageNames[i])) {
      g_page = i;
      g_sel[i] = 0;
      g_scroll[i] = 0;
      g_open = true;
      g_capture = kMenuCapture_None;
      g_status[0] = 0;
      return true;
    }
  }
  return false;
}

bool OptionsMenu_RunHeadlessTest(const char *save_path) {
  uint32 before = g_config.features0;
  // Known baseline for the Mono toggle: OFF -> ON.
  g_config.audio_channels = 2;
  OptionsMenu_Toggle();
  // Use the real key path: Video -> Gameplay, then Enter on the first row.
  OptionsMenu_HandleKey(SDLK_PAGEDOWN, KMOD_NONE, true);
  OptionsMenu_HandleKey(SDLK_PAGEDOWN, KMOD_NONE, true);
  g_sel[kMenuPage_Gameplay] = 0;
  g_scroll[kMenuPage_Gameplay] = 0;
  OptionsMenu_HandleKey(SDLK_RETURN, KMOD_NONE, true);
  uint32 after = g_config.features0;
  bool changed = ((before ^ after) & kFeatures0_SwitchLR) != 0;
  bool on = (after & kFeatures0_SwitchLR) != 0;

  // Gameplay -> Sound, second row (Mono), Enter.
  OptionsMenu_HandleKey(SDLK_PAGEUP, KMOD_NONE, true);
  g_sel[kMenuPage_Sound] = 1;
  g_scroll[kMenuPage_Sound] = 0;
  OptionsMenu_HandleKey(SDLK_RETURN, KMOD_NONE, true);
  bool mono = g_config.audio_channels == 1;

  bool saved = SaveUserIni(save_path);
  printf("menu-test: ItemSwitchLR %d -> %d mono=%d saved=%s\n",
         (before & kFeatures0_SwitchLR) ? 1 : 0, on ? 1 : 0, mono ? 1 : 0,
         saved ? save_path : "(failed)");
  g_open = false;
  g_capture = kMenuCapture_None;
  return changed && on && mono && saved;
}
