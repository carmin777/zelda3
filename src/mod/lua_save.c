#include "lua_save.h"

#include "lua_internal.h"
#include "items.h"

#ifdef ZELDA3_LUA

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

enum {
  kModSaveMagicSize = 4,
  kModSaveHeaderSize = 8,
  kModSaveMaxKey = 64,
  kModSaveMaxString = 4096,
  kModSaveMaxFile = 64 * 1024,
  kModSaveMaxEntries = 1024,
};

enum {
  kModSaveType_String = 1,
  kModSaveType_Int = 2,
  kModSaveType_Number = 3,
  kModSaveType_Bool = 4,
};

typedef struct ModSaveValue {
  struct ModSaveValue *next;
  char *key;
  uint16_t key_len;
  uint8_t type;
  uint8_t bool_value;
  int64_t int_value;
  double number_value;
  uint8_t *string_value;
  uint32_t string_len;
} ModSaveValue;

static const uint8 kModSaveMagic[kModSaveMagicSize] = { 'Z', 'M', 'S', '1' };

static size_t SaveValueLength(const ModSaveValue *v) {
  switch (v->type) {
  case kModSaveType_String: return v->string_len;
  case kModSaveType_Int: return 8;
  case kModSaveType_Number: return 8;
  case kModSaveType_Bool: return 1;
  default: return 0;
  }
}

static size_t SaveEntrySize(const ModSaveValue *v) {
  return 2 + v->key_len + 1 + 4 + SaveValueLength(v);
}

static void SaveValueFree(ModSaveValue *v) {
  if (v == NULL)
    return;
  free(v->key);
  free(v->string_value);
  free(v);
}

static void SaveValuesClear(LuaMod *mod) {
  ModSaveValue *v = mod->save_values;
  while (v != NULL) {
    ModSaveValue *next = v->next;
    SaveValueFree(v);
    v = next;
  }
  mod->save_values = NULL;
  mod->save_bytes = kModSaveHeaderSize;
  mod->save_count = 0;
}

void LuaSave_Free(LuaMod *mod) {
  SaveValuesClear(mod);
}

static ModSaveValue *SaveValueFind(LuaMod *mod, const char *key, size_t key_len) {
  for (ModSaveValue *v = mod->save_values; v != NULL; v = v->next) {
    if (v->key_len == key_len && memcmp(v->key, key, key_len) == 0)
      return v;
  }
  return NULL;
}

static void SaveValueUnlink(LuaMod *mod, ModSaveValue *value) {
  ModSaveValue **link = &mod->save_values;
  while (*link != NULL && *link != value)
    link = &(*link)->next;
  if (*link == value)
    *link = value->next;
}

static bool SaveValueAdd(LuaMod *mod, ModSaveValue *value) {
  ModSaveValue *old = SaveValueFind(mod, value->key, value->key_len);
  size_t old_size = old != NULL ? SaveEntrySize(old) : 0;
  size_t new_size = SaveEntrySize(value);
  if (mod->save_bytes - old_size + new_size > kModSaveMaxFile)
    return false;
  if (old != NULL) {
    SaveValueUnlink(mod, old);
    SaveValueFree(old);
  } else {
    mod->save_count++;
  }
  value->next = mod->save_values;
  mod->save_values = value;
  mod->save_bytes = mod->save_bytes - old_size + new_size;
  return true;
}

static bool SaveValueRemove(LuaMod *mod, const char *key, size_t key_len) {
  ModSaveValue *v = SaveValueFind(mod, key, key_len);
  if (v == NULL)
    return false;
  mod->save_bytes -= SaveEntrySize(v);
  mod->save_count--;
  SaveValueUnlink(mod, v);
  SaveValueFree(v);
  return true;
}

static uint16 GetU16(const uint8 *p) {
  return (uint16)(p[0] | (p[1] << 8));
}

static uint32 GetU32(const uint8 *p) {
  return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) |
         ((uint32)p[3] << 24);
}

static uint64 GetU64(const uint8 *p) {
  return (uint64)GetU32(p) | ((uint64)GetU32(p + 4) << 32);
}

static void PutU16(uint8 *p, uint16 v) {
  p[0] = (uint8)v;
  p[1] = (uint8)(v >> 8);
}

static void PutU32(uint8 *p, uint32 v) {
  p[0] = (uint8)v;
  p[1] = (uint8)(v >> 8);
  p[2] = (uint8)(v >> 16);
  p[3] = (uint8)(v >> 24);
}

static void PutU64(uint8 *p, uint64 v) {
  PutU32(p, (uint32)v);
  PutU32(p + 4, (uint32)(v >> 32));
}

static bool IsPlainIdChar(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// Keys starting with "$item:" belong to the new-item registry. Scripts may
// read the item quantity through zelda.item.get, but not overwrite the
// registry's own save entries.
static bool IsReservedKey(const char *key, size_t key_len) {
  return key_len >= 6 && memcmp(key, "$item:", 6) == 0;
}

// Encode the mod id so it is always a single safe filename component. '%' is
// escaped too, so different ids can never map to the same file name.
static bool EncodeModId(const char *id, char *out, size_t out_size) {
  static const char kHex[] = "0123456789ABCDEF";
  size_t pos = 0;
  for (const unsigned char *p = (const unsigned char *)id; *p != 0; p++) {
    unsigned char c = *p;
    if (IsPlainIdChar(c)) {
      if (pos + 1 >= out_size)
        return false;
      out[pos++] = (char)c;
    } else {
      if (pos + 3 >= out_size)
        return false;
      out[pos++] = '%';
      out[pos++] = kHex[c >> 4];
      out[pos++] = kHex[c & 0xf];
    }
  }
  out[pos] = 0;
  return pos != 0;
}

static bool BuildSavePath(int slot, const char *id, char *out, size_t out_size) {
  if (slot < 0 || slot > 2 || id == NULL)
    return false;
  size_t id_len = strlen(id);
  size_t enc_size = id_len * 3 + 1;
  char *encoded = malloc(enc_size);
  if (encoded == NULL)
    return false;
  if (!EncodeModId(id, encoded, enc_size)) {
    free(encoded);
    return false;
  }
  int n = snprintf(out, out_size, "saves/sram.mod.%d.%s.sav", slot, encoded);
  free(encoded);
  return n > 0 && (size_t)n < out_size;
}

static bool AtomicReplace(const char *temp_path, const char *final_path) {
#ifdef _WIN32
  if (MoveFileExA(temp_path, final_path,
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  DeleteFileA(temp_path);
  return false;
#else
  if (rename(temp_path, final_path) == 0)
    return true;
  remove(temp_path);
  return false;
#endif
}

static void SaveWarn(const LuaMod *mod, const char *what, const char *path) {
  fprintf(stderr, "mods: save state '%s': %s '%s'\n",
          mod->id != NULL ? mod->id : "?", what, path);
}

static bool ParseValues(LuaMod *mod, const uint8 *data, size_t size) {
  if (size < kModSaveHeaderSize ||
      memcmp(data, kModSaveMagic, kModSaveMagicSize) != 0)
    return false;
  uint32 count = GetU32(data + 4);
  if (count > kModSaveMaxEntries)
    return false;

  ModSaveValue *head = NULL;
  size_t pos = kModSaveHeaderSize;
  size_t total = kModSaveHeaderSize;
  for (uint32 i = 0; i < count; i++) {
    if (pos + 2 > size)
      goto invalid;
    uint16 key_len = GetU16(data + pos);
    pos += 2;
    if (key_len == 0 || key_len > kModSaveMaxKey || pos + key_len > size)
      goto invalid;
    const uint8 *key = data + pos;
    pos += key_len;

    if (pos + 5 > size)
      goto invalid;
    uint8 type = data[pos++];
    uint32 value_len = GetU32(data + pos);
    pos += 4;
    size_t expected;
    switch (type) {
    case kModSaveType_String:
      if (value_len > kModSaveMaxString)
        goto invalid;
      expected = value_len;
      break;
    case kModSaveType_Int:
    case kModSaveType_Number:
      expected = 8;
      break;
    case kModSaveType_Bool:
      expected = 1;
      break;
    default:
      goto invalid;
    }
    if (value_len != expected || pos + value_len > size)
      goto invalid;

    size_t entry_size = 2 + key_len + 1 + 4 + value_len;
    if (total + entry_size > kModSaveMaxFile)
      goto invalid;

    ModSaveValue *v = calloc(1, sizeof(*v));
    if (v == NULL)
      goto invalid;
    v->key = malloc((size_t)key_len + 1);
    if (v->key == NULL) {
      free(v);
      goto invalid;
    }
    memcpy(v->key, key, key_len);
    v->key[key_len] = 0;
    v->key_len = key_len;
    v->type = type;
    if (type == kModSaveType_String) {
      v->string_value = malloc(value_len != 0 ? value_len : 1);
      if (v->string_value == NULL) {
        SaveValueFree(v);
        goto invalid;
      }
      memcpy(v->string_value, data + pos, value_len);
      v->string_len = value_len;
    } else if (type == kModSaveType_Int) {
      v->int_value = (int64)GetU64(data + pos);
    } else if (type == kModSaveType_Number) {
      uint64 bits = GetU64(data + pos);
      memcpy(&v->number_value, &bits, sizeof(bits));
    } else {
      v->bool_value = data[pos] != 0;
    }
    pos += value_len;
    total += entry_size;
    v->next = head;
    head = v;
  }
  if (pos != size)
    goto invalid;

  SaveValuesClear(mod);
  mod->save_values = head;
  mod->save_bytes = total;
  mod->save_count = (int)count;
  return true;

invalid:
  while (head != NULL) {
    ModSaveValue *next = head->next;
    SaveValueFree(head);
    head = next;
  }
  return false;
}

static void LoadValueFile(LuaMod *mod, int slot) {
  char path[512];
  SaveValuesClear(mod);
  if (!BuildSavePath(slot, mod->id, path, sizeof(path)))
    return;

  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    SaveWarn(mod, "cannot read", path);
    return;
  }
  long length = ftell(f);
  if (length < 0 || (size_t)length > kModSaveMaxFile) {
    fclose(f);
    SaveWarn(mod, "invalid size in", path);
    return;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    SaveWarn(mod, "cannot read", path);
    return;
  }
  uint8 *data = malloc(length != 0 ? (size_t)length : 1);
  if (data == NULL) {
    fclose(f);
    return;
  }
  bool ok = fread(data, 1, (size_t)length, f) == (size_t)length;
  fclose(f);
  if (!ok) {
    free(data);
    SaveWarn(mod, "cannot read", path);
    return;
  }
  if (!ParseValues(mod, data, (size_t)length))
    SaveWarn(mod, "invalid data in", path);
  free(data);
}

static bool WriteValueFile(LuaMod *mod, int slot) {
  char path[512];
  if (!BuildSavePath(slot, mod->id, path, sizeof(path)))
    return false;

  if (mod->save_values == NULL) {
    remove(path);
    return true;
  }

  uint8 *data = malloc(mod->save_bytes);
  if (data == NULL)
    return false;
  memcpy(data, kModSaveMagic, kModSaveMagicSize);
  PutU32(data + 4, (uint32)mod->save_count);
  size_t pos = kModSaveHeaderSize;
  for (ModSaveValue *v = mod->save_values; v != NULL; v = v->next) {
    PutU16(data + pos, v->key_len);
    pos += 2;
    memcpy(data + pos, v->key, v->key_len);
    pos += v->key_len;
    data[pos++] = v->type;
    size_t value_len = SaveValueLength(v);
    PutU32(data + pos, (uint32)value_len);
    pos += 4;
    if (v->type == kModSaveType_String) {
      memcpy(data + pos, v->string_value, value_len);
    } else if (v->type == kModSaveType_Int) {
      PutU64(data + pos, (uint64)v->int_value);
    } else if (v->type == kModSaveType_Number) {
      uint64 bits;
      memcpy(&bits, &v->number_value, sizeof(bits));
      PutU64(data + pos, bits);
    } else {
      data[pos] = v->bool_value;
    }
    pos += value_len;
  }
  bool ok = pos == mod->save_bytes;

  char temp_path[560];
#ifdef _WIN32
  unsigned long pid = GetCurrentProcessId();
#else
  long pid = (long)getpid();
#endif
  int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp.%ld", path, pid);
  if (ok && n > 0 && (size_t)n < sizeof(temp_path)) {
    FILE *f = fopen(temp_path, "wb");
    if (f != NULL) {
      ok = fwrite(data, 1, pos, f) == pos;
      if (fflush(f) != 0)
        ok = false;
      if (fclose(f) != 0)
        ok = false;
      if (ok && AtomicReplace(temp_path, path))
        ok = true;
      else
        ok = false;
      if (!ok)
        remove(temp_path);
    } else {
      ok = false;
    }
  } else {
    ok = false;
  }
  free(data);
  if (!ok)
    SaveWarn(mod, "cannot write", path);
  return ok;
}

bool LuaSave_SetEngineInt(LuaMod *mod, const char *key, int value) {
  if (mod == NULL || key == NULL)
    return false;
  size_t key_len = strlen(key);
  if (key_len == 0 || key_len > kModSaveMaxKey)
    return false;

  ModSaveValue *v = calloc(1, sizeof(*v));
  if (v == NULL)
    return false;
  v->key = malloc(key_len + 1);
  if (v->key == NULL) {
    free(v);
    return false;
  }
  memcpy(v->key, key, key_len + 1);
  v->key_len = (uint16_t)key_len;
  v->type = kModSaveType_Int;
  v->int_value = value;
  if (!SaveValueAdd(mod, v)) {
    SaveValueFree(v);
    return false;
  }
  return true;
}

bool LuaSave_GetEngineInt(LuaMod *mod, const char *key, int *value) {
  if (mod == NULL || key == NULL)
    return false;
  ModSaveValue *v = SaveValueFind(mod, key, strlen(key));
  if (v == NULL || v->type != kModSaveType_Int)
    return false;
  if (value != NULL)
    *value = (int)v->int_value;
  return true;
}

void LuaSave_RemoveEngineKey(LuaMod *mod, const char *key) {
  if (mod == NULL || key == NULL)
    return;
  SaveValueRemove(mod, key, strlen(key));
}

void LuaSave_LoadSlot(int slot) {
  for (LuaMod *mod = g_lua_mods; mod != NULL; mod = mod->next) {
    if (mod->active) {
      LoadValueFile(mod, slot);
      ModItems_LoadSaveState(mod);
    }
  }
}

void LuaSave_WriteSlot(int slot) {
  for (LuaMod *mod = g_lua_mods; mod != NULL; mod = mod->next) {
    if (mod->active) {
      ModItems_WriteSaveState(mod);
      WriteValueFile(mod, slot);
    }
  }
}

void LuaSave_ResetSlot(int slot) {
  for (LuaMod *mod = g_lua_mods; mod != NULL; mod = mod->next) {
    if (!mod->active)
      continue;
    SaveValuesClear(mod);
    ModItems_ResetSaveState(mod);
    char path[512];
    if (BuildSavePath(slot, mod->id, path, sizeof(path)))
      remove(path);
  }
}

static int Lua_SaveGet(lua_State *L) {
  LuaMod *mod = lua_touserdata(L, lua_upvalueindex(1));
  size_t key_len;
  const char *key = luaL_checklstring(L, 1, &key_len);
  if (key_len == 0 || key_len > kModSaveMaxKey)
    return luaL_error(L, "save key length %I outside 1..%d",
                      (lua_Integer)key_len, kModSaveMaxKey);
  if (IsReservedKey(key, key_len))
    return luaL_error(L, "save key '%s' uses the reserved '$item:' prefix", key);
  ModSaveValue *v = SaveValueFind(mod, key, key_len);
  if (v == NULL) {
    lua_pushnil(L);
    return 1;
  }
  switch (v->type) {
  case kModSaveType_String:
    lua_pushlstring(L, (const char *)v->string_value, v->string_len);
    break;
  case kModSaveType_Int:
    lua_pushinteger(L, (lua_Integer)v->int_value);
    break;
  case kModSaveType_Number:
    lua_pushnumber(L, (lua_Number)v->number_value);
    break;
  case kModSaveType_Bool:
    lua_pushboolean(L, v->bool_value);
    break;
  default:
    lua_pushnil(L);
    break;
  }
  return 1;
}

static int Lua_SaveSet(lua_State *L) {
  LuaMod *mod = lua_touserdata(L, lua_upvalueindex(1));
  size_t key_len;
  const char *key = luaL_checklstring(L, 1, &key_len);
  if (key_len == 0 || key_len > kModSaveMaxKey)
    return luaL_error(L, "save key length %I outside 1..%d",
                      (lua_Integer)key_len, kModSaveMaxKey);
  if (IsReservedKey(key, key_len))
    return luaL_error(L, "save key '%s' uses the reserved '$item:' prefix", key);

  if (lua_isnoneornil(L, 2)) {
    SaveValueRemove(mod, key, key_len);
    return 0;
  }

  int type = lua_type(L, 2);
  if (type != LUA_TSTRING && type != LUA_TNUMBER && type != LUA_TBOOLEAN)
    return luaL_error(L, "save value for '%s' must be a string, number or boolean",
                      key);

  ModSaveValue *v = calloc(1, sizeof(*v));
  if (v == NULL)
    return luaL_error(L, "out of memory saving '%s'", key);
  v->key = malloc(key_len + 1);
  if (v->key == NULL) {
    free(v);
    return luaL_error(L, "out of memory saving '%s'", key);
  }
  memcpy(v->key, key, key_len);
  v->key[key_len] = 0;
  v->key_len = (uint16_t)key_len;

  if (type == LUA_TSTRING) {
    size_t value_len;
    const char *value = luaL_checklstring(L, 2, &value_len);
    if (value_len > kModSaveMaxString) {
      SaveValueFree(v);
      return luaL_error(L, "save string for '%s' has %I bytes; maximum is %d",
                        key, (lua_Integer)value_len, kModSaveMaxString);
    }
    v->type = kModSaveType_String;
    v->string_value = malloc(value_len != 0 ? value_len : 1);
    if (v->string_value == NULL) {
      SaveValueFree(v);
      return luaL_error(L, "out of memory saving '%s'", key);
    }
    memcpy(v->string_value, value, value_len);
    v->string_len = (uint32_t)value_len;
  } else if (type == LUA_TBOOLEAN) {
    v->type = kModSaveType_Bool;
    v->bool_value = lua_toboolean(L, 2) != 0;
  } else if (lua_isinteger(L, 2)) {
    v->type = kModSaveType_Int;
    v->int_value = (int64)lua_tointeger(L, 2);
  } else {
    v->type = kModSaveType_Number;
    v->number_value = (double)lua_tonumber(L, 2);
  }

  if (!SaveValueAdd(mod, v)) {
    SaveValueFree(v);
    return luaL_error(L, "save data for this mod exceeds %d bytes",
                      kModSaveMaxFile);
  }
  return 0;
}

void LuaSave_Setup(lua_State *L, LuaMod *mod) {
  lua_newtable(L);
  lua_pushlightuserdata(L, mod);
  lua_pushcclosure(L, Lua_SaveGet, 1);
  lua_setfield(L, -2, "get");
  lua_pushlightuserdata(L, mod);
  lua_pushcclosure(L, Lua_SaveSet, 1);
  lua_setfield(L, -2, "set");
  lua_setfield(L, -2, "save");
}

#else  // ZELDA3_LUA

void LuaSave_LoadSlot(int slot) {
  (void)slot;
}

void LuaSave_WriteSlot(int slot) {
  (void)slot;
}

void LuaSave_ResetSlot(int slot) {
  (void)slot;
}

#endif  // ZELDA3_LUA
