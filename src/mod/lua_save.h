#ifndef ZELDA3_MOD_LUA_SAVE_H_
#define ZELDA3_MOD_LUA_SAVE_H_

// Mod state saved in a sidecar file next to the game SRAM. There is one file
// per game slot and mod id (saves/sram.mod.<slot>.<id>.sav). These calls are
// no-ops when no Lua mod is active, so the game behavior is unchanged without
// mods.
void LuaSave_LoadSlot(int slot);
void LuaSave_WriteSlot(int slot);
void LuaSave_ResetSlot(int slot);

#endif  // ZELDA3_MOD_LUA_SAVE_H_
