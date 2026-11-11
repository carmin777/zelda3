#ifndef ZELDA3_MOD_MENU_H_
#define ZELDA3_MOD_MENU_H_

#include <stdbool.h>
#include <SDL_keycode.h>

// In-game options menu. The menu is an overlay drawn through the same pixel
// buffer used by zelda.draw, after the PPU/plugin presented the frame. It
// never needs Lua and never changes the simulation while it is closed.

void OptionsMenu_Init(void);
void OptionsMenu_Toggle(void);
void OptionsMenu_Open(void);
void OptionsMenu_Close(void);
bool OptionsMenu_IsOpen(void);

// Input. Return true when the event was consumed by the menu. Key events are
// raw SDL keys so that the menu still works while the game keys are being
// rebound. Gamepad buttons are kGamepadBtn_* values.
bool OptionsMenu_HandleKey(SDL_Keycode key, SDL_Keymod mod, bool pressed);
bool OptionsMenu_HandleGamepadButton(int button, bool pressed);

// Called from inside a ModDraw frame (after mod overlays) while the game is
// frozen. A no-op when the menu is closed.
void OptionsMenu_Draw(void);

// Headless helpers. The screenshot helper opens the menu on a page after the
// final hash was printed; the test helper runs the real open/change/save path.
bool OptionsMenu_OpenPageByName(const char *name);
bool OptionsMenu_RunHeadlessTest(const char *save_path);

// Host hooks implemented in src/main.c. They apply the live parts of a menu
// change (window/PPU flags, audio device, features).
void MenuHost_ApplyVideo(void);
void MenuHost_ApplyAudio(void);
void MenuHost_ApplyFeatures(void);
void MenuHost_SetVolume(int volume);
// Reopens the SDL audio device with 1 (mono) or 2 (stereo) channels. Returns
// false when the device could not be reopened; the previous device then stays
// untouched for a restart-based fallback.
bool MenuHost_SetAudioChannels(int channels);
bool MenuHost_RenderPluginActive(void);
const char *MenuHost_RenderPluginName(void);

#endif  // ZELDA3_MOD_MENU_H_
