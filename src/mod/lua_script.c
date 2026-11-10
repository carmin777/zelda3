#include "lua_script.h"
#include "lua_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config.h"
#include "../util.h"
#include "items.h"
#include "mods.h"
#include "shop.h"
#include "sprite_override.h"

#ifdef ZELDA3_LUA

#include <lauxlib.h>
#include <lualib.h>

enum { kLuaInstructionHookCount = 1000 };

static void Lua_EventDispatch(const ModEvent *ev, void *user);
static int Lua_FilterDispatch(ModFilterType type, int value, int arg, void *user);
static bool Lua_DecisionDispatch(ModDecisionType type, bool value, int arg, void *user);
static bool Lua_SpriteTickDispatch(int type, int slot, void *user);

LuaMod *g_lua_mods;
static int64 g_lua_frame;
static int g_lua_frame_sub_id;

static void *Lua_Allocator(void *ud, void *ptr, size_t osize, size_t nsize) {
  LuaMod *mod = ud;
  if (nsize == 0) {
    if (ptr != NULL) {
      if (osize <= mod->mem_used)
        mod->mem_used -= osize;
      else
        mod->mem_used = 0;
      free(ptr);
    }
    return NULL;
  }

  size_t old_size = ptr != NULL ? osize : 0;
  if (nsize > old_size) {
    size_t grow = nsize - old_size;
    if (mod->mem_used > mod->mem_limit || grow > mod->mem_limit - mod->mem_used)
      return NULL;
  }
  void *new_ptr = realloc(ptr, nsize);
  if (new_ptr == NULL)
    return NULL;
  if (nsize >= old_size)
    mod->mem_used += nsize - old_size;
  else
    mod->mem_used -= old_size - nsize;
  return new_ptr;
}

static void Lua_InstructionHook(lua_State *L, lua_Debug *ar) {
  (void)ar;
  LuaMod *mod = *(LuaMod **)lua_getextraspace(L);
  if (mod == NULL || mod->closing)
    return;
  if (mod->instructions_left <= kLuaInstructionHookCount) {
    mod->instructions_left = 0;
    mod->instruction_limit_hit = true;
    luaL_error(L, "instruction limit exceeded");
  } else {
    mod->instructions_left -= kLuaInstructionHookCount;
  }
}

static int Lua_ProtectedCall(LuaMod *mod, int nargs, int nresults, int msgh) {
  mod->instructions_left = g_config.lua_instruction_limit;
  mod->instruction_limit_hit = false;
  lua_sethook(mod->L, Lua_InstructionHook, LUA_MASKCOUNT, kLuaInstructionHookCount);
  int status = lua_pcall(mod->L, nargs, nresults, msgh);
  lua_sethook(mod->L, NULL, 0, 0);
  return status;
}

static const char *Lua_ErrorMessage(lua_State *L, int status, LuaMod *mod,
                                    char *scratch, size_t scratch_size) {
  if (status == LUA_ERRMEM) {
    snprintf(scratch, scratch_size, "memory limit exceeded (%u MB, not enough memory)",
             (unsigned)g_config.lua_memory_mb);
    return scratch;
  }
  const char *message = lua_tostring(L, -1);
  if (message != NULL)
    return message;
  if (mod->instruction_limit_hit)
    return "instruction limit exceeded";
  return "(unknown Lua error)";
}

static char *LuaStrDup(const char *s) {
  size_t length = strlen(s);
  char *r = malloc(length + 1);
  if (!r)
    Die("malloc failed");
  memcpy(r, s, length + 1);
  return r;
}

static int Lua_Traceback(lua_State *L) {
  const char *message = lua_tostring(L, 1);
  if (message == NULL) {
    if (lua_isnoneornil(L, 1))
      message = "(no error object)";
    else
      message = "(error object is not a string)";
  }
  luaL_traceback(L, L, message, 1);
  return 1;
}

static void Lua_ReportError(LuaMod *mod, const char *context, const char *message) {
  fprintf(stderr, "[%s] script error%s%s:\n%s\n", mod->id,
          context != NULL ? " in " : "", context != NULL ? context : "",
          message != NULL ? message : "(unknown Lua error)");
}

static int Lua_SafeLoad(lua_State *L) {
  size_t length;
  const char *chunk = luaL_checklstring(L, 1, &length);
  const char *name = luaL_optstring(L, 2, chunk);
  const char *mode = luaL_optstring(L, 3, "t");
  if (strchr(mode, 'b') != NULL)
    return luaL_error(L, "bytecode loading is disabled; load() only accepts text (mode 't')");
  if (luaL_loadbufferx(L, chunk, length, name, "t") != LUA_OK) {
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
  }
  return 1;
}

int LuaScript_Print(lua_State *L) {
  LuaMod *mod = lua_touserdata(L, lua_upvalueindex(1));
  int count = lua_gettop(L);
  fprintf(stderr, "[%s] ", mod != NULL && mod->id != NULL ? mod->id : "lua");
  for (int i = 1; i <= count; i++) {
    if (i != 1)
      fputc('\t', stderr);
    size_t length;
    const char *text = luaL_tolstring(L, i, &length);
    fwrite(text, 1, length, stderr);
    lua_pop(L, 1);
  }
  fputc('\n', stderr);
  return 0;
}

static void Lua_OpenSandbox(lua_State *L, LuaMod *mod) {
  static const luaL_Reg libs[] = {
    { LUA_GNAME, luaopen_base },
    { LUA_STRLIBNAME, luaopen_string },
    { LUA_TABLIBNAME, luaopen_table },
    { LUA_MATHLIBNAME, luaopen_math },
    { LUA_UTF8LIBNAME, luaopen_utf8 },
    { NULL, NULL },
  };
  for (const luaL_Reg *lib = libs; lib->name != NULL; lib++) {
    luaL_requiref(L, lib->name, lib->func, 1);
    lua_pop(L, 1);
  }

  // Keep the base library but remove file loading and modules. io, os,
  // package and debug are never opened, so they stay absent.
  lua_pushnil(L);
  lua_setglobal(L, "dofile");
  lua_pushnil(L);
  lua_setglobal(L, "loadfile");
  lua_pushnil(L);
  lua_setglobal(L, "require");
  lua_pushcfunction(L, Lua_SafeLoad);
  lua_setglobal(L, "load");

  lua_pushlightuserdata(L, mod);
  lua_pushcclosure(L, LuaScript_Print, 1);
  lua_setglobal(L, "print");
}

static void Lua_SetIntField(lua_State *L, const char *name, int value) {
  lua_pushinteger(L, value);
  lua_setfield(L, -2, name);
}

static void Lua_PushEventTable(lua_State *L, const ModEvent *ev) {
  int a = ev->filter != NULL ? *ev->filter : ev->a;
  lua_createtable(L, 0, 8);
  Lua_SetIntField(L, "a", a);
  Lua_SetIntField(L, "b", ev->b);
  Lua_SetIntField(L, "c", ev->c);
  switch (ev->type) {
  case kModEv_ModuleChange:
    Lua_SetIntField(L, "module", a);
    Lua_SetIntField(L, "prev", ev->b);
    break;
  case kModEv_RoomEnter:
    Lua_SetIntField(L, "room", a);
    Lua_SetIntField(L, "prev", ev->b);
    break;
  case kModEv_AreaEnter:
    Lua_SetIntField(L, "area", a);
    Lua_SetIntField(L, "prev", ev->b);
    break;
  case kModEv_ItemReceive:
    Lua_SetIntField(L, "item", a);
    Lua_SetIntField(L, "chest", ev->b);
    Lua_SetIntField(L, "method", ev->c);
    break;
  case kModEv_ChestOpen:
    Lua_SetIntField(L, "room", a);
    Lua_SetIntField(L, "chest", ev->b);
    break;
  case kModEv_DialogueOpen:
  case kModEv_DialogueClose:
    Lua_SetIntField(L, "msg", a);
    break;
  case kModEv_LinkDamage:
    Lua_SetIntField(L, "damage", a);
    Lua_SetIntField(L, "health_before", ev->b);
    break;
  case kModEv_SpriteSpawn:
  case kModEv_SpriteKilled:
    Lua_SetIntField(L, "slot", a);
    Lua_SetIntField(L, "type", ev->b);
    break;
  case kModEv_FileLoad:
  case kModEv_FileCreate:
    Lua_SetIntField(L, "slot", a);
    break;
  case kModEv_DungeonEnter:
    Lua_SetIntField(L, "palace", a);
    Lua_SetIntField(L, "entrance", ev->b);
    break;
  case kModEv_DungeonExit:
    Lua_SetIntField(L, "palace", a);
    break;
  case kModEv_Quit:
    Lua_SetIntField(L, "saved", a);
    break;
  case kModEv_OverworldTransition:
    Lua_SetIntField(L, "area", a);
    Lua_SetIntField(L, "direction", ev->b);
    break;
  case kModEv_DropSpawned:
    Lua_SetIntField(L, "item", a);
    Lua_SetIntField(L, "slot", ev->b);
    break;
  default:
    break;
  }
}

static int Lua_On(lua_State *L) {
  LuaMod *mod = lua_touserdata(L, lua_upvalueindex(1));
  const char *name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  int type = ModEvent_TypeFromName(name);
  if (type < 0)
    return luaL_error(L, "unknown event '%s'", name);

  if (mod->sub_ids[type] == 0) {
    lua_newtable(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    int id = ModEvents_Subscribe((ModEventType)type, Lua_EventDispatch, mod);
    if (id < 0) {
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
      return luaL_error(L, "too many event subscribers");
    }
    mod->sub_ids[type] = id;
    mod->handler_refs[type] = ref;
  }

  lua_rawgeti(L, LUA_REGISTRYINDEX, mod->handler_refs[type]);
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -1) + 1);
  lua_pop(L, 1);
  return 0;
}

static const ModHookDef *Lua_CheckHook(lua_State *L, int arg,
                                       ModHookKind kind) {
  const char *name = luaL_checkstring(L, arg);
  const ModHookDef *def = ModHooks_Find(name);
  if (def == NULL) {
    luaL_error(L, "unknown %s '%s'",
               kind == kModHookKind_Filter ? "filter" : "decision", name);
    return NULL;
  }
  if (def->kind != kind) {
    luaL_error(L, "'%s' is a %s; use zelda.%s('%s', fn)", name,
               def->kind == kModHookKind_Filter ? "filter" : "decision",
               def->kind == kModHookKind_Filter ? "filter" : "decide", name);
    return NULL;
  }
  return def;
}

static void Lua_AddHookHandler(lua_State *L, int *refs, int type) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, refs[type]);
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -1) + 1);
  lua_pop(L, 1);
}

static int Lua_Filter(lua_State *L) {
  LuaMod *mod = lua_touserdata(L, lua_upvalueindex(1));
  const ModHookDef *def = Lua_CheckHook(L, 1, kModHookKind_Filter);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  int type = def->index;

  if (mod->filter_refs[type] == LUA_NOREF) {
    int sub_id = ModHooks_SubscribeFilter((ModFilterType)type, mod->priority,
                                          Lua_FilterDispatch, mod);
    if (sub_id == 0)
      return luaL_error(L, "too many subscribers for filter '%s'", def->name);
    lua_newtable(L);
    mod->filter_refs[type] = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  Lua_AddHookHandler(L, mod->filter_refs, type);
  return 0;
}

static int Lua_Decide(lua_State *L) {
  LuaMod *mod = lua_touserdata(L, lua_upvalueindex(1));
  const ModHookDef *def = Lua_CheckHook(L, 1, kModHookKind_Decide);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  int type = def->index;

  if (mod->decide_refs[type] == LUA_NOREF) {
    bool ok = ModHooks_SubscribeDecision((ModDecisionType)type, mod->priority,
                                         Lua_DecisionDispatch, mod);
    if (!ok)
      return luaL_error(L, "too many subscribers for decision '%s'", def->name);
    lua_newtable(L);
    mod->decide_refs[type] = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  Lua_AddHookHandler(L, mod->decide_refs, type);
  return 0;
}

static int Lua_Frame(lua_State *L) {
  lua_pushinteger(L, (lua_Integer)g_lua_frame);
  return 1;
}

static void Lua_UnsubscribeAll(LuaMod *mod) {
  for (int type = 0; type < kModEv_Count; type++) {
    if (mod->sub_ids[type] != 0) {
      ModEvents_Unsubscribe(mod->sub_ids[type]);
      mod->sub_ids[type] = 0;
    }
  }
  ModHooks_UnsubscribeUser(mod);
  ModSpriteOverride_UnsubscribeUser(mod);
}

static void Lua_Deactivate(LuaMod *mod) {
  mod->active = false;
  Lua_UnsubscribeAll(mod);
}

static void LuaMod_Unlink(LuaMod *mod) {
  LuaMod **link = &g_lua_mods;
  while (*link != NULL && *link != mod)
    link = &(*link)->next;
  if (*link == mod)
    *link = mod->next;
}

static void LuaMod_Free(LuaMod *mod) {
  if (mod->closing)
    return;
  mod->closing = true;
  Lua_Deactivate(mod);
  LuaMod_Unlink(mod);
  ModItems_UnregisterOwner(mod);
  ModShop_UnregisterOwner(mod);
  LuaSave_Free(mod);
  if (mod->L != NULL) {
    // Closing must not be interrupted by our hook or by the memory budget:
    // lua_close only needs to free, and finalizers may allocate a little.
    lua_sethook(mod->L, NULL, 0, 0);
    mod->mem_limit = (size_t)-1;
    lua_close(mod->L);
    mod->L = NULL;
  }
  free(mod->id);
  free(mod->dir_path);
  free(mod);
}

static void Lua_FrameCounter(const ModEvent *ev, void *user) {
  (void)ev;
  (void)user;
  g_lua_frame++;
}

static void Lua_EnsureFrameCounter(void) {
  if (g_lua_frame_sub_id == 0) {
    int id = ModEvents_Subscribe(kModEv_FrameStart, Lua_FrameCounter, NULL);
    if (id > 0)
      g_lua_frame_sub_id = id;
  }
}

static void Lua_EventDispatch(const ModEvent *ev, void *user) {
  LuaMod *mod = user;
  if (!mod->active || mod->L == NULL)
    return;
  int ref = mod->handler_refs[ev->type];
  if (ref == LUA_NOREF)
    return;

  lua_State *L = mod->L;
  int saved_top = lua_gettop(L);
  lua_pushcfunction(L, Lua_Traceback);
  int msgh = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  int handler_count = (int)lua_rawlen(L, -1);
  const char *event_name = ModEvent_Name(ev->type);
  char context[64];
  snprintf(context, sizeof(context), "event %s", event_name != NULL ? event_name : "?");

  for (int i = 1; i <= handler_count; i++) {
    lua_rawgeti(L, msgh + 1, i);
    Lua_PushEventTable(L, ev);
    int status = Lua_ProtectedCall(mod, 1, 1, msgh);
    if (status != LUA_OK) {
      char scratch[128];
      const char *message = Lua_ErrorMessage(L, status, mod, scratch, sizeof(scratch));
      Lua_ReportError(mod, context, message);
      fprintf(stderr, "mods: script '%s' disabled after handler error\n", mod->id);
      LuaMod_Free(mod);
      return;
    }
    if (ev->filter != NULL && lua_isinteger(L, -1)) {
      lua_Integer value = lua_tointeger(L, -1);
      if (value < 0 || value > 255) {
        char detail[128];
        snprintf(detail, sizeof(detail),
                 "handler for event %s returned item %lld outside 0..255",
                 event_name != NULL ? event_name : "?", (long long)value);
        Lua_ReportError(mod, context, detail);
        fprintf(stderr, "mods: script '%s' disabled after handler error\n", mod->id);
        LuaMod_Free(mod);
        return;
      }
      *ev->filter = (int)value;
    }
    lua_pop(L, 1);
  }
  lua_settop(L, saved_top);
}

static void Lua_DisableAfterHookError(LuaMod *mod, const char *context,
                                      const char *detail) {
  Lua_ReportError(mod, context, detail);
  fprintf(stderr, "mods: script '%s' disabled after handler error\n", mod->id);
  LuaMod_Free(mod);
}

static int Lua_FilterDispatch(ModFilterType type, int value, int arg, void *user) {
  LuaMod *mod = user;
  if (!mod->active || mod->L == NULL)
    return value;
  int ref = mod->filter_refs[type];
  if (ref == LUA_NOREF)
    return value;

  const ModHookDef *def = ModHooks_FilterDef(type);
  lua_State *L = mod->L;
  int saved_top = lua_gettop(L);
  lua_pushcfunction(L, Lua_Traceback);
  int msgh = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  int handler_count = (int)lua_rawlen(L, -1);
  char context[64];
  snprintf(context, sizeof(context), "filter %s",
           def != NULL ? def->name : "?");

  for (int i = 1; i <= handler_count; i++) {
    lua_rawgeti(L, msgh + 1, i);
    lua_pushinteger(L, value);
    lua_pushinteger(L, arg);
    int status = Lua_ProtectedCall(mod, 2, 1, msgh);
    if (status != LUA_OK) {
      char scratch[128];
      const char *message = Lua_ErrorMessage(L, status, mod, scratch, sizeof(scratch));
      Lua_DisableAfterHookError(mod, context, message);
      return value;
    }
    if (!lua_isnil(L, -1)) {
      if (!lua_isinteger(L, -1)) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                 "filter '%s' returned %s; expected an integer or nil",
                 def != NULL ? def->name : "?", luaL_typename(L, -1));
        Lua_DisableAfterHookError(mod, context, detail);
        return value;
      }
      lua_Integer result = lua_tointeger(L, -1);
      if (def != NULL && (result < def->min || result > def->max)) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                 "filter '%s' returned %lld outside %d..%d",
                 def->name, (long long)result, def->min, def->max);
        Lua_DisableAfterHookError(mod, context, detail);
        return value;
      }
      value = (int)result;
    }
    lua_pop(L, 1);
  }
  lua_settop(L, saved_top);
  return value;
}

static bool Lua_DecisionDispatch(ModDecisionType type, bool value, int arg, void *user) {
  LuaMod *mod = user;
  if (!mod->active || mod->L == NULL)
    return value;
  int ref = mod->decide_refs[type];
  if (ref == LUA_NOREF)
    return value;

  const ModHookDef *def = ModHooks_DecisionDef(type);
  lua_State *L = mod->L;
  int saved_top = lua_gettop(L);
  lua_pushcfunction(L, Lua_Traceback);
  int msgh = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  int handler_count = (int)lua_rawlen(L, -1);
  char context[64];
  snprintf(context, sizeof(context), "decision %s",
           def != NULL ? def->name : "?");

  for (int i = 1; i <= handler_count; i++) {
    lua_rawgeti(L, msgh + 1, i);
    lua_pushboolean(L, value);
    lua_pushinteger(L, arg);
    int status = Lua_ProtectedCall(mod, 2, 1, msgh);
    if (status != LUA_OK) {
      char scratch[128];
      const char *message = Lua_ErrorMessage(L, status, mod, scratch, sizeof(scratch));
      Lua_DisableAfterHookError(mod, context, message);
      return value;
    }
    if (!lua_isnil(L, -1)) {
      if (!lua_isboolean(L, -1)) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                 "decision '%s' returned %s; expected a boolean or nil",
                 def != NULL ? def->name : "?", luaL_typename(L, -1));
        Lua_DisableAfterHookError(mod, context, detail);
        return value;
      }
      value = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
  }
  lua_settop(L, saved_top);
  return value;
}

static int Lua_SpriteOriginal(lua_State *L) {
  void *call = lua_touserdata(L, lua_upvalueindex(1));
  int slot = (int)luaL_checkinteger(L, 1);
  const char *error = ModSpriteOverride_CallOriginal(call, slot);
  if (error != NULL)
    return luaL_error(L, "%s", error);
  return 0;
}

static bool Lua_SpriteTickDispatch(int type, int slot, void *user) {
  LuaMod *mod = user;
  if (!mod->active || mod->L == NULL)
    return false;
  int ref = mod->sprite_refs[type];
  if (ref == LUA_NOREF)
    return false;

  lua_State *L = mod->L;
  int saved_top = lua_gettop(L);
  lua_pushcfunction(L, Lua_Traceback);
  int msgh = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  const char *name = ModSpriteOverride_TypeName(type);
  char context[64];
  snprintf(context, sizeof(context), "override Sprite:%s",
           name != NULL ? name : "?");

  lua_pushinteger(L, slot);
  lua_pushlightuserdata(L, ModSpriteOverride_CurrentCall());
  lua_pushcclosure(L, Lua_SpriteOriginal, 1);
  int status = Lua_ProtectedCall(mod, 2, 0, msgh);
  if (status != LUA_OK) {
    char scratch[128];
    const char *message = Lua_ErrorMessage(L, status, mod, scratch, sizeof(scratch));
    Lua_DisableAfterHookError(mod, context, message);
    return false;
  }
  lua_settop(L, saved_top);
  return true;
}

static int Lua_Override(lua_State *L) {
  LuaMod *mod = lua_touserdata(L, lua_upvalueindex(1));
  if (!mod->state_write)
    return luaL_error(L, "zelda.override requires the state_write permission");
  const char *key = luaL_checkstring(L, 1);
  if (strncmp(key, "Sprite:", 7) != 0)
    return luaL_error(L, "unknown override '%s'; expected 'Sprite:<type>'", key);
  int type;
  if (!ModSpriteOverride_ParseType(key + 7, &type))
    return luaL_error(L, "unknown sprite type '%s' in override '%s'", key + 7, key);
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_getfield(L, 2, "tick");
  if (!lua_isfunction(L, -1))
    return luaL_error(L, "override '%s' needs a 'tick' function", key);
  if (mod->sprite_refs[type] != LUA_NOREF)
    return luaL_error(L, "sprite type '%s' already has an override in this mod", key);
  if (!ModSpriteOverride_Subscribe(type, mod->priority, Lua_SpriteTickDispatch, mod))
    return luaL_error(L, "too many subscribers for override '%s'", key);
  mod->sprite_refs[type] = luaL_ref(L, LUA_REGISTRYINDEX);
  return 0;
}

static void Lua_PushItemEvent(lua_State *L, const ModItemEvent *ev) {
  lua_createtable(L, 0, 4);
  Lua_SetIntField(L, "item", ev->item);
  Lua_SetIntField(L, "count", ev->count);
  Lua_SetIntField(L, "chest", ev->chest);
  Lua_SetIntField(L, "method", ev->method);
}

static void Lua_ItemUseDispatch(void *owner, int ref, const ModItemEvent *ev) {
  LuaMod *mod = owner;
  if (!mod->active || mod->L == NULL || ref == LUA_NOREF)
    return;
  lua_State *L = mod->L;
  int saved_top = lua_gettop(L);
  lua_pushcfunction(L, Lua_Traceback);
  int msgh = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  Lua_PushItemEvent(L, ev);
  int status = Lua_ProtectedCall(mod, 1, 0, msgh);
  if (status != LUA_OK) {
    char scratch[128];
    const char *message = Lua_ErrorMessage(L, status, mod, scratch, sizeof(scratch));
    Lua_DisableAfterHookError(mod, "item use", message);
    return;
  }
  lua_settop(L, saved_top);
}

static void Lua_ItemReceiveDispatch(void *owner, int ref, const ModItemEvent *ev) {
  LuaMod *mod = owner;
  if (!mod->active || mod->L == NULL || ref == LUA_NOREF)
    return;
  lua_State *L = mod->L;
  int saved_top = lua_gettop(L);
  lua_pushcfunction(L, Lua_Traceback);
  int msgh = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  Lua_PushItemEvent(L, ev);
  int status = Lua_ProtectedCall(mod, 1, 0, msgh);
  if (status != LUA_OK) {
    char scratch[128];
    const char *message = Lua_ErrorMessage(L, status, mod, scratch, sizeof(scratch));
    Lua_DisableAfterHookError(mod, "item receive", message);
    return;
  }
  lua_settop(L, saved_top);
}

static int Lua_ItemRegister(lua_State *L) {
  LuaMod *mod = lua_touserdata(L, lua_upvalueindex(1));
  luaL_checktype(L, 1, LUA_TTABLE);

  lua_getfield(L, 1, "name");
  if (!lua_isstring(L, -1))
    return luaL_error(L, "zelda.item.register: 'name' must be a string");
  const char *name = lua_tostring(L, -1);
  if (LuaApi_IsVanillaItemName(name))
    return luaL_error(L, "zelda.item.register: '%s' is an original item name", name);
  lua_pop(L, 1);

  lua_getfield(L, 1, "icon");
  if (!lua_isstring(L, -1))
    return luaL_error(L, "zelda.item.register: 'icon' must be a PNG path");
  const char *icon_rel = lua_tostring(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "receive_gfx");
  const char *receive_rel = NULL;
  if (lua_isstring(L, -1))
    receive_rel = lua_tostring(L, -1);
  else if (!lua_isnil(L, -1))
    return luaL_error(L, "zelda.item.register: 'receive_gfx' must be a PNG path");
  lua_pop(L, 1);

  lua_getfield(L, 1, "text");
  const char *text_string = NULL;
  int text_id = -1;
  if (lua_isinteger(L, -1)) {
    lua_Integer v = lua_tointeger(L, -1);
    if (v < 0 || v > 0xffff)
      return luaL_error(L, "zelda.item.register: 'text' id %I outside 0..65535",
                        (lua_Integer)v);
    text_id = (int)v;
  } else if (lua_type(L, -1) == LUA_TSTRING) {
    text_string = lua_tostring(L, -1);
  } else if (!lua_isnil(L, -1)) {
    return luaL_error(L, "zelda.item.register: 'text' must be a string or a message id");
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "message");
  if (!lua_isnil(L, -1)) {
    lua_Integer v = luaL_checkinteger(L, -1);
    if (v < 0 || v > 0xffff)
      return luaL_error(L, "zelda.item.register: 'message' id %I outside 0..65535",
                        (lua_Integer)v);
    text_id = (int)v;
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "max");
  int max_count = 1;
  if (!lua_isnil(L, -1)) {
    lua_Integer v = luaL_checkinteger(L, -1);
    if (v < 1 || v > 99)
      return luaL_error(L, "zelda.item.register: 'max' must be in 1..99");
    max_count = (int)v;
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_use");
  int use_ref = LUA_NOREF;
  if (!lua_isnil(L, -1)) {
    if (!lua_isfunction(L, -1))
      return luaL_error(L, "zelda.item.register: 'on_use' must be a function");
    lua_pushvalue(L, -1);
    use_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_receive");
  int receive_ref = LUA_NOREF;
  if (!lua_isnil(L, -1)) {
    if (!lua_isfunction(L, -1))
      return luaL_error(L, "zelda.item.register: 'on_receive' must be a function");
    lua_pushvalue(L, -1);
    receive_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  lua_pop(L, 1);

  char detail[256];
  char *icon_path = NULL;
  if (!ModResolvePath(mod->dir_path, icon_rel, &icon_path, detail, sizeof(detail)))
    return luaL_error(L, "zelda.item.register: icon: %s", detail);
  size_t icon_size = 0;
  uint8 *icon_data = ReadWholeFile(icon_path, &icon_size);
  free(icon_path);
  if (icon_data == NULL)
    return luaL_error(L, "zelda.item.register: unable to read icon '%s'", icon_rel);

  uint8 *receive_data = NULL;
  size_t receive_size = 0;
  if (receive_rel != NULL) {
    char *receive_path = NULL;
    if (!ModResolvePath(mod->dir_path, receive_rel, &receive_path, detail, sizeof(detail))) {
      free(icon_data);
      return luaL_error(L, "zelda.item.register: receive_gfx: %s", detail);
    }
    receive_data = ReadWholeFile(receive_path, &receive_size);
    free(receive_path);
    if (receive_data == NULL) {
      free(icon_data);
      return luaL_error(L, "zelda.item.register: unable to read receive_gfx '%s'", receive_rel);
    }
  }

  char err[256];
  int id = ModItems_Register(mod->id, mod, name, icon_data, icon_size,
                             receive_data, receive_size, text_string, text_id,
                             max_count, Lua_ItemUseDispatch, use_ref,
                             Lua_ItemReceiveDispatch, receive_ref,
                             err, sizeof(err));
  free(icon_data);
  free(receive_data);
  if (id < 0) {
    if (use_ref != LUA_NOREF)
      luaL_unref(L, LUA_REGISTRYINDEX, use_ref);
    if (receive_ref != LUA_NOREF)
      luaL_unref(L, LUA_REGISTRYINDEX, receive_ref);
    return luaL_error(L, "zelda.item.register: %s", err);
  }
  lua_pushinteger(L, id);
  return 1;
}

static void Lua_SetupZelda(lua_State *L, LuaMod *mod) {
  lua_newtable(L);

  lua_pushlightuserdata(L, mod);
  lua_pushcclosure(L, Lua_On, 1);
  lua_setfield(L, -2, "on");

  lua_pushlightuserdata(L, mod);
  lua_pushcclosure(L, Lua_Filter, 1);
  lua_setfield(L, -2, "filter");

  lua_pushlightuserdata(L, mod);
  lua_pushcclosure(L, Lua_Decide, 1);
  lua_setfield(L, -2, "decide");

  lua_pushlightuserdata(L, mod);
  lua_pushcclosure(L, Lua_Override, 1);
  lua_setfield(L, -2, "override");

  lua_pushcfunction(L, Lua_Frame);
  lua_setfield(L, -2, "frame");

  lua_pushlightuserdata(L, mod);
  lua_pushcclosure(L, LuaScript_Print, 1);
  lua_setfield(L, -2, "log");

  LuaApi_Setup(L, mod);

  // zelda.item.register. The item table was just created by LuaApi_Setup.
  lua_getfield(L, -1, "item");
  lua_pushlightuserdata(L, mod);
  lua_pushcclosure(L, Lua_ItemRegister, 1);
  lua_setfield(L, -2, "register");
  lua_pop(L, 1);

  lua_setglobal(L, "zelda");
}

bool LuaScript_Load(const char *mod_id, const char *mod_dir, const char *script_name,
                    const char *path, uint32 permissions, int priority,
                    char *err, size_t err_size) {
  LuaMod *mod = calloc(1, sizeof(*mod));
  if (mod == NULL)
    Die("malloc failed");
  mod->id = LuaStrDup(mod_id);
  mod->dir_path = LuaStrDup(mod_dir != NULL ? mod_dir : ".");
  mod->priority = priority;
  mod->state_write = (permissions & kModPermission_StateWrite) != 0;
  mod->save_bytes = 8;  // "ZMS1" + entry count; no entries yet
  for (int type = 0; type < kModEv_Count; type++)
    mod->handler_refs[type] = LUA_NOREF;
  for (int type = 0; type < kModFilter_Count; type++)
    mod->filter_refs[type] = LUA_NOREF;
  for (int type = 0; type < kModDecide_Count; type++)
    mod->decide_refs[type] = LUA_NOREF;
  for (int type = 0; type < 256; type++)
    mod->sprite_refs[type] = LUA_NOREF;

  uint64 mem_limit = (uint64)g_config.lua_memory_mb * 1024 * 1024;
  mod->mem_limit = mem_limit > (uint64)SIZE_MAX ? SIZE_MAX : (size_t)mem_limit;
  mod->L = lua_newstate(Lua_Allocator, mod);
  if (mod->L == NULL) {
    if (err_size != 0)
      snprintf(err, err_size, "script '%s': unable to create Lua state (memory limit %u MB)",
               script_name, (unsigned)g_config.lua_memory_mb);
    free(mod->id);
    free(mod->dir_path);
    free(mod);
    return false;
  }
  *(LuaMod **)lua_getextraspace(mod->L) = mod;

  Lua_OpenSandbox(mod->L, mod);
  Lua_SetupZelda(mod->L, mod);
  // Subscribe before running the script so even its first FrameStart handler
  // sees the counter advance.
  Lua_EnsureFrameCounter();

  lua_State *L = mod->L;
  lua_pushcfunction(L, Lua_Traceback);
  int msgh = lua_gettop(L);
  int status = luaL_loadfilex(L, path, "t");
  if (status == LUA_OK)
    status = Lua_ProtectedCall(mod, 0, 0, msgh);
  if (status != LUA_OK) {
    char context[256];
    char scratch[128];
    const char *message = Lua_ErrorMessage(L, status, mod, scratch, sizeof(scratch));
    snprintf(context, sizeof(context), "loading '%s'", script_name);
    Lua_ReportError(mod, context, message);
    if (err_size != 0)
      snprintf(err, err_size, "script '%s' failed to load (see stderr)", script_name);
    LuaMod_Free(mod);
    return false;
  }
  lua_settop(L, 0);

  mod->active = true;
  mod->next = g_lua_mods;
  g_lua_mods = mod;
  // A script can change game state, so the side-by-side comparison with the
  // original ROM no longer applies.
  g_mods_change_content = true;
  return true;
}

#else  // ZELDA3_LUA

bool LuaScript_Load(const char *mod_id, const char *mod_dir, const char *script_name,
                    const char *path, uint32 permissions, int priority,
                    char *err, size_t err_size) {
  (void)mod_dir;
  (void)path;
  (void)permissions;
  (void)priority;
  (void)err;
  (void)err_size;
  fprintf(stderr, "mods: '%s': script '%s' ignored: Lua scripting disabled (built with NO_LUA=1)\n",
          mod_id, script_name);
  return true;
}

#endif  // ZELDA3_LUA
