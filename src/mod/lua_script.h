#ifndef ZELDA3_MOD_LUA_SCRIPT_H_
#define ZELDA3_MOD_LUA_SCRIPT_H_

#include "../types.h"

// Loads a mod script from path and runs it in its own lua_State. On success
// the script can subscribe to mod events through the global `zelda` table.
// permissions is the mod manifest bitmask (see kModPermission_* in mods.h).
// On failure the function prints a traceback on stderr, removes any handlers
// the script managed to register and returns false. Never dies.
//
// When the game is built with make NO_LUA=1 this only warns that scripting is
// disabled and returns true, so the rest of the mod still loads normally.
bool LuaScript_Load(const char *mod_id, const char *mod_dir, const char *script_name,
                    const char *path, uint32 permissions, int priority,
                    char *err, size_t err_size);

#endif  // ZELDA3_MOD_LUA_SCRIPT_H_
