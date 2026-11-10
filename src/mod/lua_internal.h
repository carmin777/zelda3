#ifndef ZELDA3_MOD_LUA_INTERNAL_H_
#define ZELDA3_MOD_LUA_INTERNAL_H_

#include "events.h"
#include "hooks.h"
#include "lua_script.h"

#ifdef ZELDA3_LUA

#include <lua.h>

struct ModSaveValue;

typedef struct LuaMod {
  char *id;
  char *dir_path;
  lua_State *L;
  bool active;
  bool closing;
  bool state_write;
  bool instruction_limit_hit;
  size_t mem_used;
  size_t mem_limit;
  uint64 instructions_left;
  int priority;
  int sub_ids[kModEv_Count];
  int handler_refs[kModEv_Count];
  int filter_refs[kModFilter_Count];
  int decide_refs[kModDecide_Count];
  int sprite_refs[256];
  struct ModSaveValue *save_values;
  size_t save_bytes;
  int save_count;
  struct LuaMod *next;
} LuaMod;

// Lua mods are linked here in load order. lua_save.c walks the list to load and
// write the per-mod save sidecar files.
extern LuaMod *g_lua_mods;

// Fills the `zelda` table at the top of the stack with the link/item/ram/state
// APIs. state_write enables zelda.state.set.
void LuaApi_Setup(lua_State *L, LuaMod *mod);

// zelda.save.get/set. Keeps the values in C memory; the files are written by
// LuaSave_WriteSlot when the game writes the save and read by
// LuaSave_LoadSlot when a file is loaded.
void LuaSave_Setup(lua_State *L, LuaMod *mod);
void LuaSave_Free(LuaMod *mod);

// Engine-owned values (currently new item quantities) share the mod sidecar
// file. The "$item:" prefix is reserved and rejected by zelda.save.set.
bool LuaSave_SetEngineInt(LuaMod *mod, const char *key, int value);
bool LuaSave_GetEngineInt(LuaMod *mod, const char *key, int *value);
void LuaSave_RemoveEngineKey(LuaMod *mod, const char *key);

// True when name is one of the original zelda.item.get/set names. New items
// may not shadow them.
bool LuaApi_IsVanillaItemName(const char *name);

// Shared by print and zelda.log. The LuaMod pointer is an upvalue.
int LuaScript_Print(lua_State *L);

#endif  // ZELDA3_LUA

#endif  // ZELDA3_MOD_LUA_INTERNAL_H_
