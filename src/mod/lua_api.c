#include "lua_internal.h"

#ifdef ZELDA3_LUA

#include <lauxlib.h>
#include <string.h>

#include "../variables.h"
#include "draw.h"
#include "items.h"
#include "shop.h"
#include "state_table.h"

// g_ram holds the full 0x20000-byte work RAM.
enum { kLuaRamSize = 0x20000 };

typedef struct LuaItemDef {
  const char *name;
  unsigned offset;
  unsigned min;
  unsigned max;
} LuaItemDef;

// Every entry is a uint8 in variables.h. The ranges keep a script from
// writing values the game cannot represent (or index tables with).
static const LuaItemDef kLuaItems[] = {
  { "bow", 0xF340, 0, 4 },
  { "boomerang", 0xF341, 0, 3 },
  { "hookshot", 0xF342, 0, 2 },
  { "bombs", 0xF343, 0, 99 },
  { "mushroom", 0xF344, 0, 2 },
  { "powder", 0xF344, 0, 2 },
  { "fire_rod", 0xF345, 0, 2 },
  { "ice_rod", 0xF346, 0, 2 },
  { "bombos", 0xF347, 0, 1 },
  { "ether", 0xF348, 0, 1 },
  { "quake", 0xF349, 0, 1 },
  { "lamp", 0xF34A, 0, 2 },
  { "hammer", 0xF34B, 0, 1 },
  { "flute", 0xF34C, 0, 3 },
  { "net", 0xF34D, 0, 2 },
  { "bug_net", 0xF34D, 0, 2 },
  { "book", 0xF34E, 0, 1 },
  { "book_of_mudora", 0xF34E, 0, 1 },
  { "bottle1", 0xF35C, 0, 8 },
  { "bottle2", 0xF35D, 0, 8 },
  { "bottle3", 0xF35E, 0, 8 },
  { "bottle4", 0xF35F, 0, 8 },
  { "bottle_1", 0xF35C, 0, 8 },
  { "bottle_2", 0xF35D, 0, 8 },
  { "bottle_3", 0xF35E, 0, 8 },
  { "bottle_4", 0xF35F, 0, 8 },
  { "somaria", 0xF350, 0, 2 },
  { "cane_somaria", 0xF350, 0, 2 },
  { "byrna", 0xF351, 0, 2 },
  { "cane_byrna", 0xF351, 0, 2 },
  { "cape", 0xF352, 0, 2 },
  { "mirror", 0xF353, 0, 2 },
  { "gloves", 0xF354, 0, 2 },
  { "boots", 0xF355, 0, 1 },
  { "flippers", 0xF356, 0, 1 },
  { "moon_pearl", 0xF357, 0, 1 },
  { "sword", 0xF359, 0, 4 },
  { "shield", 0xF35A, 0, 3 },
  { "armor", 0xF35B, 0, 2 },
  { "heart_pieces", 0xF36B, 0, 3 },
  { "arrows", 0xF377, 0, 99 },
  { "keys", 0xF36F, 0, 99 },
};

typedef struct LuaLinkField {
  const char *name;
  unsigned offset;
  unsigned width;
  int min;
  int max;
  bool health;
} LuaLinkField;

enum {
  kLuaOffLinkY = 0x20,
  kLuaOffLinkX = 0x22,
  kLuaOffHealthCapacity = 0xF36C,
  kLuaOffHealth = 0xF36D,
  kLuaOffMagic = 0xF36E,
  kLuaOffBombs = 0xF343,
  kLuaOffRupees = 0xF362,
  kLuaOffArrows = 0xF377,
};

static const LuaLinkField kLuaLinkFields[] = {
  { "health", kLuaOffHealth, 1, 0, 160, true },
  { "max_health", kLuaOffHealthCapacity, 1, 0, 160, false },
  { "rupees", kLuaOffRupees, 2, 0, 9999, false },
  { "x", kLuaOffLinkX, 2, 0, 65535, false },
  { "y", kLuaOffLinkY, 2, 0, 65535, false },
  { "bombs", kLuaOffBombs, 1, 0, 99, false },
  { "arrows", kLuaOffArrows, 1, 0, 99, false },
  { "magic", kLuaOffMagic, 1, 0, 128, false },
};

// Per-slot fields from src/variables.h. x and y are handled separately as
// 16-bit values built from x_lo/x_hi and y_lo/y_hi; the entries here are the
// raw byte arrays the game exposes with a sprite_ prefix.
typedef struct LuaSpriteField {
  const char *name;
  uint8 *data;
  int size;
  bool is_signed;
} LuaSpriteField;

static const LuaSpriteField kLuaSpriteFields[] = {
  { "x_lo", sprite_x_lo, 1, false },
  { "x_hi", sprite_x_hi, 1, false },
  { "y_lo", sprite_y_lo, 1, false },
  { "y_hi", sprite_y_hi, 1, false },
  { "x_vel", sprite_x_vel, 1, true },
  { "y_vel", sprite_y_vel, 1, true },
  { "z_vel", sprite_z_vel, 1, true },
  { "x_subpixel", sprite_x_subpixel, 1, false },
  { "y_subpixel", sprite_y_subpixel, 1, false },
  { "state", sprite_state, 1, false },
  { "ai_state", sprite_ai_state, 1, false },
  { "A", sprite_A, 1, false },
  { "B", sprite_B, 1, false },
  { "C", sprite_C, 1, false },
  { "D", sprite_D, 1, false },
  { "E", sprite_E, 1, false },
  { "F", sprite_F, 1, false },
  { "G", sprite_G, 1, false },
  { "graphics", sprite_graphics, 1, false },
  { "head_dir", sprite_head_dir, 1, false },
  { "subtype", sprite_subtype, 1, false },
  { "subtype2", sprite_subtype2, 1, false },
  { "health", sprite_health, 1, false },
  { "delay_main", sprite_delay_main, 1, false },
  { "delay_aux1", sprite_delay_aux1, 1, false },
  { "delay_aux2", sprite_delay_aux2, 1, false },
  { "delay_aux3", sprite_delay_aux3, 1, false },
  { "delay_aux4", sprite_delay_aux4, 1, false },
  { "hit_timer", sprite_hit_timer, 1, false },
  { "stunned", sprite_stunned, 1, false },
  { "flags", sprite_flags, 1, false },
  { "flags2", sprite_flags2, 1, false },
  { "flags3", sprite_flags3, 1, false },
  { "flags4", sprite_flags4, 1, false },
  { "flags5", sprite_flags5, 1, false },
  { "oam_flags", sprite_oam_flags, 1, false },
  { "floor", sprite_floor, 1, false },
  { "wallcoll", sprite_wallcoll, 1, false },
  { "anim_clock", sprite_anim_clock, 1, false },
  { "pause", sprite_pause, 1, false },
  { "bump_damage", sprite_bump_damage, 1, false },
  { "give_damage", sprite_give_damage, 1, false },
  { "defl_bits", sprite_defl_bits, 1, false },
  { "die_action", sprite_die_action, 1, false },
  { "ignore_projectile", sprite_ignore_projectile, 1, false },
  { "obj_prio", sprite_obj_prio, 1, false },
  { "z", sprite_z, 1, false },
  { "z_subpos", sprite_z_subpos, 1, false },
  { "x_recoil", sprite_x_recoil, 1, false },
  { "y_recoil", sprite_y_recoil, 1, false },
  { "room", sprite_room, 1, false },
  { "type", sprite_type, 1, false },
  { "unk1", sprite_unk1, 1, false },
  { "unk2", sprite_unk2, 1, false },
  { "unk3", sprite_unk3, 1, false },
  { "unk4", sprite_unk4, 1, false },
  { "unk5", sprite_unk5, 1, false },
  { "direction", sprite_head_dir, 1, false },
  { "gfx", sprite_graphics, 1, false },
};

static uint32 Lua_ReadRam(unsigned offset, unsigned width) {
  uint32 value = g_ram[offset];
  if (width == 2)
    value |= (uint32)g_ram[offset + 1] << 8;
  return value;
}

static void Lua_WriteRam(unsigned offset, unsigned width, uint32 value) {
  g_ram[offset] = (uint8)value;
  if (width == 2)
    g_ram[offset + 1] = (uint8)(value >> 8);
}

bool LuaApi_IsVanillaItemName(const char *name) {
  for (size_t i = 0; i < arraysize(kLuaItems); i++) {
    if (strcmp(name, kLuaItems[i].name) == 0)
      return true;
  }
  return false;
}

static bool Lua_ItemFindCustom(lua_State *L, int index, int *id) {
  const char *name = luaL_checkstring(L, index);
  int found = ModItems_FindByName(name);
  if (found < 0)
    return false;
  *id = found;
  return true;
}

static const LuaLinkField *Lua_FindLinkField(const char *name) {
  for (size_t i = 0; i < arraysize(kLuaLinkFields); i++) {
    if (strcmp(name, kLuaLinkFields[i].name) == 0)
      return &kLuaLinkFields[i];
  }
  return NULL;
}

static int Lua_ItemGet(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  for (size_t i = 0; i < arraysize(kLuaItems); i++) {
    if (strcmp(name, kLuaItems[i].name) == 0) {
      lua_pushinteger(L, g_ram[kLuaItems[i].offset]);
      return 1;
    }
  }
  int custom_id;
  if (Lua_ItemFindCustom(L, 1, &custom_id)) {
    lua_pushinteger(L, ModItems_CountForId((uint8)custom_id));
    return 1;
  }
  return luaL_error(L, "unknown item '%s'", name);
}

static int Lua_ItemSet(lua_State *L) {
  if (ModDraw_FrameActive())
    return luaL_error(L, "read-only during FrameDraw");
  const char *name = luaL_checkstring(L, 1);
  lua_Integer value = luaL_checkinteger(L, 2);
  for (size_t i = 0; i < arraysize(kLuaItems); i++) {
    if (strcmp(name, kLuaItems[i].name) == 0) {
      const LuaItemDef *item = &kLuaItems[i];
      if (value < (lua_Integer)item->min || value > (lua_Integer)item->max) {
        return luaL_error(L, "item '%s' value %I outside %d..%d", item->name,
                          (lua_Integer)value, (int)item->min, (int)item->max);
      }
      g_ram[item->offset] = (uint8)value;
      return 0;
    }
  }
  int custom_id;
  if (Lua_ItemFindCustom(L, 1, &custom_id)) {
    int max = ModItems_MaxForId((uint8)custom_id);
    if (value < 0 || value > max) {
      return luaL_error(L, "item '%s' value %I outside 0..%d", name,
                        (lua_Integer)value, max);
    }
    ModItems_SetCount((uint8)custom_id, (int)value);
    return 0;
  }
  return luaL_error(L, "unknown item '%s'", name);
}

static int Lua_LinkIndex(lua_State *L) {
  if (lua_type(L, 2) != LUA_TSTRING) {
    lua_pushnil(L);
    return 1;
  }
  const LuaLinkField *field = Lua_FindLinkField(lua_tostring(L, 2));
  if (field == NULL) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushinteger(L, (lua_Integer)Lua_ReadRam(field->offset, field->width));
  return 1;
}

static int Lua_LinkNewIndex(lua_State *L) {
  if (ModDraw_FrameActive())
    return luaL_error(L, "read-only during FrameDraw");
  if (lua_type(L, 2) != LUA_TSTRING)
    return luaL_error(L, "link field name must be a string");
  const char *name = lua_tostring(L, 2);
  const LuaLinkField *field = Lua_FindLinkField(name);
  if (field == NULL)
    return luaL_error(L, "unknown link field '%s'", name);
  lua_Integer value = luaL_checkinteger(L, 3);

  int max = field->max;
  if (field->health) {
    max = g_ram[kLuaOffHealthCapacity];
    if (max > field->max)
      max = field->max;
  }
  if (value < field->min || value > max) {
    return luaL_error(L, "link.%s value %I outside %d..%d", name,
                      (lua_Integer)value, field->min, max);
  }
  Lua_WriteRam(field->offset, field->width, (uint32)value);
  if (strcmp(name, "max_health") == 0 && g_ram[kLuaOffHealth] > value)
    g_ram[kLuaOffHealth] = (uint8)value;
  return 0;
}

static const LuaSpriteField *Lua_FindSpriteField(const char *name) {
  for (size_t i = 0; i < arraysize(kLuaSpriteFields); i++) {
    if (strcmp(name, kLuaSpriteFields[i].name) == 0)
      return &kLuaSpriteFields[i];
  }
  return NULL;
}

static int Lua_SpriteSlot(lua_State *L, int arg) {
  lua_Integer slot = luaL_checkinteger(L, arg);
  if (slot < 0 || slot >= 16)
    luaL_error(L, "sprite slot %I outside 0..15", (lua_Integer)slot);
  return (int)slot;
}

static int Lua_SpriteGet(lua_State *L) {
  int slot = Lua_SpriteSlot(L, 1);
  const char *name = luaL_checkstring(L, 2);
  if (strcmp(name, "x") == 0) {
    lua_pushinteger(L, sprite_x_lo[slot] | sprite_x_hi[slot] << 8);
    return 1;
  }
  if (strcmp(name, "y") == 0) {
    lua_pushinteger(L, sprite_y_lo[slot] | sprite_y_hi[slot] << 8);
    return 1;
  }
  const LuaSpriteField *field = Lua_FindSpriteField(name);
  if (field == NULL)
    return luaL_error(L, "unknown sprite field '%s'", name);
  int value = field->data[slot * field->size];
  if (field->size == 2)
    value |= field->data[slot * field->size + 1] << 8;
  if (field->is_signed)
    value = (int8)value;
  lua_pushinteger(L, value);
  return 1;
}

static int Lua_SpriteSet(lua_State *L) {
  if (ModDraw_FrameActive())
    return luaL_error(L, "read-only during FrameDraw");
  if (!lua_toboolean(L, lua_upvalueindex(1)))
    return luaL_error(L, "state_write permission is required to write sprite fields");
  int slot = Lua_SpriteSlot(L, 1);
  const char *name = luaL_checkstring(L, 2);
  lua_Integer value = luaL_checkinteger(L, 3);

  if (strcmp(name, "x") == 0 || strcmp(name, "y") == 0) {
    if (value < 0 || value > 65535)
      return luaL_error(L, "sprite %s value %I outside 0..65535", name,
                        (lua_Integer)value);
    uint8 *lo = name[0] == 'x' ? sprite_x_lo : sprite_y_lo;
    uint8 *hi = name[0] == 'x' ? sprite_x_hi : sprite_y_hi;
    lo[slot] = (uint8)value;
    hi[slot] = (uint8)(value >> 8);
    return 0;
  }

  const LuaSpriteField *field = Lua_FindSpriteField(name);
  if (field == NULL)
    return luaL_error(L, "unknown sprite field '%s'", name);
  int min = field->is_signed ? -128 : 0;
  int max = field->size == 2 ? 65535 : (field->is_signed ? 127 : 255);
  if (value < min || value > max) {
    return luaL_error(L, "sprite field '%s' value %I outside %d..%d", name,
                      (lua_Integer)value, min, max);
  }
  field->data[slot * field->size] = (uint8)value;
  if (field->size == 2)
    field->data[slot * field->size + 1] = (uint8)(value >> 8);
  return 0;
}

static int Lua_RamRead8(lua_State *L) {
  lua_Integer address = luaL_checkinteger(L, 1);
  if (address < 0 || address >= kLuaRamSize)
    return luaL_error(L, "ram address %I outside 0..0x1FFFF", (lua_Integer)address);
  lua_pushinteger(L, g_ram[address]);
  return 1;
}

static int Lua_RamRead16(lua_State *L) {
  lua_Integer address = luaL_checkinteger(L, 1);
  if (address < 0 || address + 1 >= kLuaRamSize)
    return luaL_error(L, "ram address %I outside 0..0x1FFFE for read16", (lua_Integer)address);
  lua_pushinteger(L, (lua_Integer)g_ram[address] | ((lua_Integer)g_ram[address + 1] << 8));
  return 1;
}

static const StateVar *Lua_StateFind(lua_State *L, int arg) {
  const char *name = luaL_checkstring(L, arg);
  const StateVar *var = StateTable_Find(name);
  if (var == NULL) {
    luaL_error(L, "unknown state variable '%s'", name);
    return NULL;
  }
  return var;
}

static unsigned Lua_StateOffset(lua_State *L, const StateVar *var, int arg) {
  lua_Integer index = 0;
  if (!lua_isnoneornil(L, arg)) {
    if (!var->is_array) {
      luaL_error(L, "state variable '%s' is not an array", var->name);
      return var->offset;
    }
    index = luaL_checkinteger(L, arg);
    if (index < 0 || index >= (lua_Integer)var->count) {
      luaL_error(L, "state variable '%s' index %I outside 0..%d", var->name,
                 (lua_Integer)index, (int)var->count - 1);
      return var->offset;
    }
  }
  return var->offset + (unsigned)index * var->elem_size;
}

static int Lua_StateGet(lua_State *L) {
  const StateVar *var = Lua_StateFind(L, 1);
  unsigned offset = Lua_StateOffset(L, var, 2);
  lua_pushinteger(L, (lua_Integer)Lua_ReadRam(offset, var->elem_size));
  return 1;
}

static int Lua_StateSet(lua_State *L) {
  if (ModDraw_FrameActive())
    return luaL_error(L, "read-only during FrameDraw");
  const StateVar *var = Lua_StateFind(L, 1);
  if (!lua_toboolean(L, lua_upvalueindex(1)))
    return luaL_error(L, "state_write permission is required to write state");

  int value_arg;
  unsigned offset;
  if (lua_isnoneornil(L, 3)) {
    // set(name, value): arrays write index 0.
    value_arg = 2;
    offset = var->offset;
  } else {
    // set(name, index, value).
    value_arg = 3;
    offset = Lua_StateOffset(L, var, 2);
  }

  lua_Integer value = luaL_checkinteger(L, value_arg);
  lua_Integer max = var->elem_size == 2 ? 65535 : 255;
  if (value < 0 || value > max) {
    return luaL_error(L, "state variable '%s' value %I outside 0..%d",
                      var->name, (lua_Integer)value, (int)max);
  }
  Lua_WriteRam(offset, var->elem_size, (uint32)value);
  return 0;
}

static int Lua_StateInfo(lua_State *L) {
  const StateVar *var = Lua_StateFind(L, 1);
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, (lua_Integer)var->offset);
  lua_setfield(L, -2, "offset");
  lua_pushinteger(L, var->elem_size);
  lua_setfield(L, -2, "size");
  lua_pushinteger(L, var->count);
  lua_setfield(L, -2, "count");
  lua_pushboolean(L, var->is_array);
  lua_setfield(L, -2, "array");
  return 1;
}

static int Lua_StateList(lua_State *L) {
  const char *prefix = luaL_optstring(L, 1, NULL);
  size_t prefix_length = prefix != NULL ? strlen(prefix) : 0;
  lua_createtable(L, 0, 0);
  lua_Integer count = 0;
  for (unsigned i = 0; i < kStateVarCount; i++) {
    if (prefix != NULL &&
        strncmp(kStateVars[i].name, prefix, prefix_length) != 0)
      continue;
    lua_pushstring(L, kStateVars[i].name);
    lua_rawseti(L, -2, ++count);
  }
  return 1;
}

static void Lua_RequireFrameDraw(lua_State *L) {
  if (!ModDraw_FrameActive())
    luaL_error(L, "zelda.draw is only valid inside a FrameDraw handler");
}

static lua_Integer Lua_DrawCoord(lua_State *L, int arg, const char *name) {
  lua_Integer value = luaL_checkinteger(L, arg);
  if (value < -1000000 || value > 1000000)
    luaL_error(L, "%s %I outside -1000000..1000000", name, (lua_Integer)value);
  return value;
}

static lua_Integer Lua_DrawSize(lua_State *L, int arg, const char *name) {
  lua_Integer value = luaL_checkinteger(L, arg);
  if (value < 0 || value > 1000000)
    luaL_error(L, "%s %I outside 0..1000000", name, (lua_Integer)value);
  return value;
}

static uint32 Lua_DrawColor(lua_State *L, int arg) {
  lua_Integer value = luaL_checkinteger(L, arg);
  if (value < 0 || value > 0xFFFFFFFFLL) {
    luaL_error(L, "color %I outside 0..0xFFFFFFFF", (lua_Integer)value);
    return 0;
  }
  // 0xRRGGBB has no alpha and is opaque; 0xAARRGGBB uses the high byte.
  if (value <= 0xFFFFFF)
    return (uint32)value | 0xFF000000u;
  return (uint32)value;
}

static int Lua_DrawRect(lua_State *L) {
  Lua_RequireFrameDraw(L);
  int x = (int)Lua_DrawCoord(L, 1, "x");
  int y = (int)Lua_DrawCoord(L, 2, "y");
  int w = (int)Lua_DrawSize(L, 3, "width");
  int h = (int)Lua_DrawSize(L, 4, "height");
  uint32 color = Lua_DrawColor(L, 5);
  const char *err = ModDraw_AddRect(x, y, w, h, color);
  if (err != NULL)
    return luaL_error(L, "%s", err);
  return 0;
}

static int Lua_DrawPixel(lua_State *L) {
  Lua_RequireFrameDraw(L);
  int x = (int)Lua_DrawCoord(L, 1, "x");
  int y = (int)Lua_DrawCoord(L, 2, "y");
  uint32 color = Lua_DrawColor(L, 3);
  const char *err = ModDraw_AddRect(x, y, 1, 1, color);
  if (err != NULL)
    return luaL_error(L, "%s", err);
  return 0;
}

static int Lua_DrawText(lua_State *L) {
  Lua_RequireFrameDraw(L);
  int x = (int)Lua_DrawCoord(L, 1, "x");
  int y = (int)Lua_DrawCoord(L, 2, "y");
  size_t length;
  const char *text = luaL_checklstring(L, 3, &length);
  uint32 color = Lua_DrawColor(L, 4);
  const char *err = ModDraw_AddText(x, y, text, length, color);
  if (err != NULL)
    return luaL_error(L, "%s", err);
  lua_Integer width = length != 0 ? (lua_Integer)length * 6 - 1 : 0;
  lua_pushinteger(L, width);
  return 1;
}

static int Lua_DrawWidth(lua_State *L) {
  Lua_RequireFrameDraw(L);
  lua_pushinteger(L, ModDraw_Width());
  return 1;
}

static int Lua_DrawHeight(lua_State *L) {
  Lua_RequireFrameDraw(L);
  lua_pushinteger(L, ModDraw_Height());
  return 1;
}

typedef struct LuaShopItemName {
  const char *name;
  uint8 item;
} LuaShopItemName;

// Original item receipt ids for the items the vanilla shops already sell.
// A numeric id is also accepted, including the 0x80..0x9f range returned by
// zelda.item.register.
static const LuaShopItemName kLuaShopItems[] = {
  { "red_potion", 0x2e },
  { "potion", 0x2e },
  { "fighter_shield", 4 },
  { "shield", 4 },
  { "fire_shield", 5 },
  { "heart", 0x42 },
  { "arrows", 0x44 },
  { "bombs", 0x31 },
  { "bee", 0x0e },
};

static int Lua_ShopResolveItem(lua_State *L, int index) {
  if (lua_isinteger(L, index)) {
    lua_Integer value = lua_tointeger(L, index);
    if (value < 0 || value > 255)
      return luaL_error(L, "shop item id must be between 0 and 255");
    return (int)value;
  }
  const char *name = luaL_checkstring(L, index);
  int custom_id = ModItems_FindByName(name);
  if (custom_id >= 0)
    return custom_id;
  for (size_t i = 0; i < arraysize(kLuaShopItems); i++) {
    if (strcmp(name, kLuaShopItems[i].name) == 0)
      return kLuaShopItems[i].item;
  }
  return luaL_error(L, "unknown shop item '%s'", name);
}

static int Lua_ShopReadEntry(lua_State *L, int index, ModShopEntry *entry) {
  if (!lua_istable(L, index))
    return luaL_error(L, "shop item %d must be a table", index);

  lua_getfield(L, index, "item");
  int item = Lua_ShopResolveItem(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, index, "price");
  if (!lua_isinteger(L, -1))
    return luaL_error(L, "shop item 'price' must be an integer");
  lua_Integer price = lua_tointeger(L, -1);
  lua_pop(L, 1);
  if (price < 0 || price > 999)
    return luaL_error(L, "shop price must be between 0 and 999");

  int count = 1;
  lua_getfield(L, index, "count");
  if (!lua_isnil(L, -1)) {
    if (!lua_isinteger(L, -1))
      return luaL_error(L, "shop item 'count' must be an integer");
    lua_Integer value = lua_tointeger(L, -1);
    if (value < 1 || value > 99)
      return luaL_error(L, "shop count must be between 1 and 99");
    count = (int)value;
  }
  lua_pop(L, 1);

  entry->item = (uint8)item;
  entry->price = (uint16)price;
  entry->count = (uint8)count;
  return 0;
}

static int Lua_ShopSet(lua_State *L) {
  LuaMod *mod = lua_touserdata(L, lua_upvalueindex(1));
  lua_Integer room = luaL_checkinteger(L, 1);
  if (room < 0 || room > 0xff)
    return luaL_error(L, "shop room must be between 0x00 and 0xFF");
  luaL_checktype(L, 2, LUA_TTABLE);

  int table = lua_absindex(L, 2);
  int count;
  int entries_index;
  bool single;
  bool items_table = false;

  lua_getfield(L, table, "items");
  if (!lua_isnil(L, -1)) {
    if (!lua_istable(L, -1))
      return luaL_error(L, "shop 'items' must be a table");
    entries_index = lua_gettop(L);
    count = (int)lua_rawlen(L, entries_index);
    single = false;
    items_table = true;
  } else {
    lua_pop(L, 1);
    lua_getfield(L, table, "item");
    single = !lua_isnil(L, -1);
    lua_pop(L, 1);
    entries_index = table;
    count = single ? 1 : (int)lua_rawlen(L, table);
  }

  if (count < 1 || count > kModShopMaxSlots)
    return luaL_error(L, "a shop needs between 1 and 3 items");

  ModShopEntry entries[kModShopMaxSlots];
  for (int i = 0; i < count; i++) {
    int entry;
    if (single) {
      entry = table;
    } else {
      lua_rawgeti(L, entries_index, i + 1);
      entry = lua_gettop(L);
    }
    Lua_ShopReadEntry(L, entry, &entries[i]);
    if (!single)
      lua_pop(L, 1);
  }
  if (items_table)
    lua_pop(L, 1);

  char err[256];
  if (!ModShop_Set(mod, (int)room, entries, count, err, sizeof(err)))
    return luaL_error(L, "%s", err);
  return 0;
}

void LuaApi_Setup(lua_State *L, LuaMod *mod) {
  bool state_write = mod->state_write;
  // zelda.link: fields with validated writes.
  lua_newtable(L);
  lua_newtable(L);
  lua_pushcfunction(L, Lua_LinkIndex);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, Lua_LinkNewIndex);
  lua_setfield(L, -2, "__newindex");
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "link");

  // zelda.item.get/set.
  lua_newtable(L);
  lua_pushcfunction(L, Lua_ItemGet);
  lua_setfield(L, -2, "get");
  lua_pushcfunction(L, Lua_ItemSet);
  lua_setfield(L, -2, "set");
  lua_setfield(L, -2, "item");

  // zelda.shop.set(room, items). Replaces what the shopkeeper in that room
  // sells; the entries keep their owner so disabling the mod restores the
  // vanilla shop.
  lua_newtable(L);
  lua_pushlightuserdata(L, mod);
  lua_pushcclosure(L, Lua_ShopSet, 1);
  lua_setfield(L, -2, "set");
  lua_setfield(L, -2, "shop");

  // zelda.ram.read8/read16.
  lua_newtable(L);
  lua_pushcfunction(L, Lua_RamRead8);
  lua_setfield(L, -2, "read8");
  lua_pushcfunction(L, Lua_RamRead16);
  lua_setfield(L, -2, "read16");
  lua_setfield(L, -2, "ram");

  // zelda.state.get/set/info/list. set is gated by the manifest permission.
  lua_newtable(L);
  lua_pushcfunction(L, Lua_StateGet);
  lua_setfield(L, -2, "get");
  lua_pushboolean(L, state_write);
  lua_pushcclosure(L, Lua_StateSet, 1);
  lua_setfield(L, -2, "set");
  lua_pushcfunction(L, Lua_StateInfo);
  lua_setfield(L, -2, "info");
  lua_pushcfunction(L, Lua_StateList);
  lua_setfield(L, -2, "list");
  lua_setfield(L, -2, "state");

  // zelda.sprite.get/set: named access to one sprite slot's fields.
  lua_newtable(L);
  lua_pushcfunction(L, Lua_SpriteGet);
  lua_setfield(L, -2, "get");
  lua_pushboolean(L, state_write);
  lua_pushcclosure(L, Lua_SpriteSet, 1);
  lua_setfield(L, -2, "set");
  lua_setfield(L, -2, "sprite");

  // zelda.draw. Only valid while a FrameDraw handler is running.
  lua_newtable(L);
  lua_pushcfunction(L, Lua_DrawRect);
  lua_setfield(L, -2, "rect");
  lua_pushcfunction(L, Lua_DrawPixel);
  lua_setfield(L, -2, "pixel");
  lua_pushcfunction(L, Lua_DrawText);
  lua_setfield(L, -2, "text");
  lua_pushcfunction(L, Lua_DrawWidth);
  lua_setfield(L, -2, "width");
  lua_pushcfunction(L, Lua_DrawHeight);
  lua_setfield(L, -2, "height");
  lua_setfield(L, -2, "draw");

  // zelda.save.get/set. Values are persisted only when the game saves.
  LuaSave_Setup(L, mod);
}

#endif  // ZELDA3_LUA
