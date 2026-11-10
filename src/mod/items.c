#include "items.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util.h"
#include "../variables.h"
#include "../zelda_rtl.h"
#include "lua_internal.h"
#include "mods.h"

#include "../../third_party/stb/stb_image.h"

typedef struct ModItem {
  bool used;
  void *owner;
  char *name;
  uint8 id;
  int max_count;
  int count;
  char *text_string;
  int text_id;
  uint16 icon_words[4 * 16];
  uint16 receive_words[4 * 16];
  uint16 name_tiles[16];
  bool hud_tiles_saved;
  uint16 hud_saved_tiles[4][16];
  uint16 hud_saved_numbers[4];
  ModItemUseCallback *on_use;
  int use_ref;
  ModItemReceiveCallback *on_receive;
  int receive_ref;
} ModItem;

static ModItem g_items[kModItemMax];
static int g_item_slots;
static int g_next_receive_count;

static char *ItemStrDup(const char *s) {
  size_t length = strlen(s);
  char *r = malloc(length + 1);
  if (r == NULL)
    return NULL;
  memcpy(r, s, length + 1);
  return r;
}

static void SetError(char *err, size_t err_size, const char *fmt, ...) {
  if (err_size == 0)
    return;
  va_list va;
  va_start(va, fmt);
  vsnprintf(err, err_size, fmt, va);
  va_end(va);
}

static bool ValidItemName(const char *name) {
  size_t len = strlen(name);
  if (len == 0 || len > 32)
    return false;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)name[i];
    if (!isalnum(c) && c != '_' && c != '-')
      return false;
  }
  return true;
}

static int FindFreeSlot(void) {
  for (int i = 0; i < kModItemMax; i++) {
    if (!g_items[i].used)
      return i;
  }
  return -1;
}

static ModItem *ItemForId(uint8 id) {
  if (id < kModItemIdBase || id >= kModItemIdBase + kModItemMax)
    return NULL;
  ModItem *item = &g_items[id - kModItemIdBase];
  return item->used ? item : NULL;
}

int ModItems_Count(void) {
  return g_item_slots;
}

int ModItems_FindByName(const char *name) {
  if (name == NULL)
    return -1;
  for (int i = 0; i < g_item_slots; i++) {
    if (g_items[i].used && strcmp(g_items[i].name, name) == 0)
      return g_items[i].id;
  }
  return -1;
}

bool ModItems_IsCustomId(uint8 id) {
  return ItemForId(id) != NULL;
}

bool ModItems_IsCustomHudSlot(int hud_slot) {
  int index = hud_slot - kModItemHudBase;
  return index >= 0 && index < g_item_slots && g_items[index].used;
}

int ModItems_IdFromHudSlot(int hud_slot) {
  int index = hud_slot - kModItemHudBase;
  if (index < 0 || index >= g_item_slots || !g_items[index].used)
    return 0;
  return g_items[index].id;
}

int ModItems_HudSlotFromId(uint8 id) {
  if (!ModItems_IsCustomId(id))
    return 0;
  return kModItemHudBase + id - kModItemIdBase;
}

const char *ModItems_Name(uint8 id) {
  ModItem *item = ItemForId(id);
  return item != NULL ? item->name : NULL;
}

int ModItems_CountForId(uint8 id) {
  ModItem *item = ItemForId(id);
  return item != NULL ? item->count : 0;
}

int ModItems_MaxForId(uint8 id) {
  ModItem *item = ItemForId(id);
  return item != NULL ? item->max_count : 0;
}

bool ModItems_IsOwned(uint8 id) {
  return ModItems_CountForId(id) > 0;
}

bool ModItems_SetCount(uint8 id, int count) {
  ModItem *item = ItemForId(id);
  if (item == NULL)
    return false;
  if (count < 0)
    count = 0;
  if (count > item->max_count)
    count = item->max_count;
  item->count = count;
  return true;
}

int ModItems_GetCountByName(const char *name) {
  int id = ModItems_FindByName(name);
  return id < 0 ? -1 : ModItems_CountForId((uint8)id);
}

bool ModItems_SetCountByName(const char *name, int count) {
  int id = ModItems_FindByName(name);
  return id >= 0 && ModItems_SetCount((uint8)id, count);
}

// Decode a 16x16 8-bit grayscale PNG into four SNES 4bpp tiles. Pixel value 0
// is transparent, 1..15 is the color index inside the tile's palette.
static bool DecodePng(const uint8 *png, size_t png_size, const char *what,
                      uint16 out[4 * 16], char *err, size_t err_size) {
  int width, height, channels;
  stbi_uc *pixels = stbi_load_from_memory(png, (int)png_size, &width, &height,
                                          &channels, 1);
  if (pixels == NULL) {
    SetError(err, err_size, "%s: invalid PNG", what);
    return false;
  }
  if (width != 16 || height != 16) {
    stbi_image_free(pixels);
    SetError(err, err_size, "%s: expected a 16x16 PNG, got %dx%d", what,
             width, height);
    return false;
  }

  for (int y = 0; y < 16; y++) {
    for (int x = 0; x < 16; x++) {
      uint8 v = pixels[y * 16 + x];
      if (v > 15) {
        stbi_image_free(pixels);
        SetError(err, err_size,
                 "%s: pixel (%d,%d) has value %d; use an 8-bit grayscale PNG "
                 "where each pixel is a palette index 0..15",
                 what, x, y, v);
        return false;
      }
    }
  }

  for (int tile = 0; tile < 4; tile++) {
    int tx = tile & 1, ty = tile >> 1;
    uint16 *dst = out + tile * 16;
    memset(dst, 0, 16 * sizeof(uint16));
    for (int row = 0; row < 8; row++) {
      uint16 planes01 = 0, planes23 = 0;
      for (int col = 0; col < 8; col++) {
        uint8 v = pixels[(ty * 8 + row) * 16 + tx * 8 + col];
        planes01 |= (uint16)((v & 1) << col);
        planes01 |= (uint16)(((v >> 1) & 1) << (8 + col));
        planes23 |= (uint16)(((v >> 2) & 1) << col);
        planes23 |= (uint16)(((v >> 3) & 1) << (8 + col));
      }
      dst[row] = planes01;
      dst[8 + row] = planes23;
    }
  }
  stbi_image_free(pixels);
  return true;
}

static uint16 HudGlyph(char c) {
  if (c >= 'a' && c <= 'z')
    c = (char)(c - 'a' + 'A');
  if (c >= 'A' && c <= 'Z')
    return (uint16)(0x2550 + c - 'A');
  if (c >= '0' && c <= '9')
    return (uint16)(0x2490 + c - '0');
  return 0x24F5;
}

static void BuildNameTiles(ModItem *item) {
  const char *text = item->text_string != NULL ? item->text_string : item->name;
  int n = 0;
  for (; text[n] != 0 && n < 16; n++)
    item->name_tiles[n] = HudGlyph(text[n]);
  for (; n < 16; n++)
    item->name_tiles[n] = 0x24F5;
}

int ModItems_Register(const char *owner_id, void *owner,
                      const char *name,
                      const uint8 *icon_png, size_t icon_size,
                      const uint8 *receive_png, size_t receive_size,
                      const char *text_string, int text_id, int max_count,
                      ModItemUseCallback *on_use, int use_ref,
                      ModItemReceiveCallback *on_receive, int receive_ref,
                      char *err, size_t err_size) {
  (void)owner_id;
  if (!ValidItemName(name)) {
    SetError(err, err_size,
             "item name '%s' must use 1..32 letters, digits, '_' or '-'", name);
    return -1;
  }
  if (ModItems_FindByName(name) >= 0) {
    SetError(err, err_size, "item '%s' is already registered", name);
    return -1;
  }
  if (max_count < 1 || max_count > 99) {
    SetError(err, err_size, "item '%s': max must be an integer in 1..99", name);
    return -1;
  }
  if (icon_png == NULL || icon_size == 0) {
    SetError(err, err_size, "item '%s': icon PNG is required", name);
    return -1;
  }

  uint16 icon_words[4 * 16];
  uint16 receive_words[4 * 16];
  char detail[256];
  if (!DecodePng(icon_png, icon_size, "icon", icon_words, detail, sizeof(detail))) {
    SetError(err, err_size, "item '%s': %s", name, detail);
    return -1;
  }
  if (receive_png != NULL && receive_size != 0) {
    if (!DecodePng(receive_png, receive_size, "receive_gfx", receive_words,
                   detail, sizeof(detail))) {
      SetError(err, err_size, "item '%s': %s", name, detail);
      return -1;
    }
  } else {
    memcpy(receive_words, icon_words, sizeof(receive_words));
  }

  int slot = FindFreeSlot();
  if (slot < 0) {
    SetError(err, err_size, "too many registered items (maximum %d)", kModItemMax);
    return -1;
  }

  ModItem *item = &g_items[slot];
  memset(item, 0, sizeof(*item));
  item->used = true;
  item->owner = owner;
  item->name = ItemStrDup(name);
  if (item->name == NULL) {
    memset(item, 0, sizeof(*item));
    SetError(err, err_size, "item '%s': out of memory", name);
    return -1;
  }
  item->id = (uint8)(kModItemIdBase + slot);
  item->max_count = max_count;
  item->text_id = text_id;
  item->on_use = on_use;
  item->use_ref = use_ref;
  item->on_receive = on_receive;
  item->receive_ref = receive_ref;
  memcpy(item->icon_words, icon_words, sizeof(icon_words));
  memcpy(item->receive_words, receive_words, sizeof(receive_words));
  if (text_string != NULL) {
    item->text_string = ItemStrDup(text_string);
    if (item->text_string == NULL) {
      free(item->name);
      memset(item, 0, sizeof(*item));
      SetError(err, err_size, "item '%s': out of memory", name);
      return -1;
    }
  }
  BuildNameTiles(item);
  if (slot + 1 > g_item_slots)
    g_item_slots = slot + 1;
  g_mods_change_content = true;
  return item->id;
}

void ModItems_UnregisterOwner(void *owner) {
  ModItems_BeginHudIcons();
  for (int i = 0; i < g_item_slots; i++) {
    ModItem *item = &g_items[i];
    if (item->used && item->owner == owner) {
      if (current_item_y == item->id) {
        current_item_y = 0;
        current_item_active = 0;
      }
      if (hud_cur_item >= kModItemHudBase &&
          ModItems_IdFromHudSlot(hud_cur_item) == item->id)
        hud_cur_item = 0;
      free(item->name);
      free(item->text_string);
      memset(item, 0, sizeof(*item));
    }
  }
  while (g_item_slots > 0 && !g_items[g_item_slots - 1].used)
    g_item_slots--;
}

void ModItems_SetNextReceiveCount(int count) {
  g_next_receive_count = count;
}

void ModItems_OnReceive(uint8 id, int chest, int method) {
  ModItem *item = ItemForId(id);
  if (item == NULL)
    return;
  int amount = g_next_receive_count > 0 ? g_next_receive_count : 1;
  g_next_receive_count = 0;
  if (item->count < item->max_count)
    item->count += amount;
  if (item->count > item->max_count)
    item->count = item->max_count;
  ModItemEvent ev;
  ev.item = id;
  ev.count = item->count;
  ev.chest = chest;
  ev.method = method;
  ModItemReceiveCallback *callback = item->on_receive;
  int ref = item->receive_ref;
  void *owner = item->owner;
  if (callback != NULL)
    callback(owner, ref, &ev);
}

bool ModItems_Use(uint8 id) {
  ModItem *item = ItemForId(id);
  if (item == NULL || item->count <= 0)
    return false;
  ModItemEvent ev;
  ev.item = id;
  ev.count = item->count;
  ev.chest = 0;
  ev.method = 0;
  ModItemUseCallback *callback = item->on_use;
  int ref = item->use_ref;
  void *owner = item->owner;
  if (callback != NULL)
    callback(owner, ref, &ev);
  return true;
}

const uint16 *ModItems_HudNameTiles(int hud_slot) {
  int index = hud_slot - kModItemHudBase;
  if (index < 0 || index >= g_item_slots || !g_items[index].used)
    return NULL;
  // The text field is also the HUD name. Rebuild lazily in case a future API
  // allows renaming an item.
  BuildNameTiles(&g_items[index]);
  return g_items[index].name_tiles;
}

void ModItems_BeginHudIcons(void) {
  for (int i = 0; i < g_item_slots; i++) {
    ModItem *item = &g_items[i];
    if (!item->used || !item->hud_tiles_saved)
      continue;
    for (int t = 0; t < 4; t++) {
      uint32 base = 0x7000 + (uint32)item->hud_saved_numbers[t] * 8;
      if (base + 16 <= 0x8000)
        memcpy(&g_zenv.vram[base], item->hud_saved_tiles[t], 16 * sizeof(uint16));
    }
    item->hud_tiles_saved = false;
  }
}

void ModItems_InstallHudIcon(int hud_slot, const uint16 vanilla_words[4],
                             uint16 out[4]) {
  int index = hud_slot - kModItemHudBase;
  if (index < 0 || index >= g_item_slots || !g_items[index].used) {
    out[0] = out[1] = out[2] = out[3] = 0x20F5;
    return;
  }
  ModItem *item = &g_items[index];
  if (vanilla_words == NULL) {
    out[0] = out[1] = out[2] = out[3] = 0x20F5;
    return;
  }

  // If the free-cell assignment changed, put the previous vanilla icon back
  // before borrowing a different tile set.
  bool same = item->hud_tiles_saved;
  for (int t = 0; t < 4 && same; t++) {
    if (item->hud_saved_numbers[t] != (vanilla_words[t] & 0x3ff))
      same = false;
  }
  if (!same) {
    if (item->hud_tiles_saved) {
      for (int t = 0; t < 4; t++) {
        uint32 base = 0x7000 + (uint32)item->hud_saved_numbers[t] * 8;
        if (base + 16 <= 0x8000)
          memcpy(&g_zenv.vram[base], item->hud_saved_tiles[t], 16 * sizeof(uint16));
      }
      item->hud_tiles_saved = false;
    }
    for (int t = 0; t < 4; t++) {
      uint16 tile = vanilla_words[t] & 0x3ff;
      item->hud_saved_numbers[t] = tile;
      uint32 base = 0x7000 + (uint32)tile * 8;
      if (base + 16 <= 0x8000)
        memcpy(item->hud_saved_tiles[t], &g_zenv.vram[base], 16 * sizeof(uint16));
    }
    item->hud_tiles_saved = true;
  }

  for (int t = 0; t < 4; t++) {
    uint16 tile = vanilla_words[t] & 0x3ff;
    out[t] = (uint16)((vanilla_words[t] & 0xfc00) | tile);
    uint32 base = 0x7000 + (uint32)tile * 8;
    if (base + 16 <= 0x8000)
      memcpy(&g_zenv.vram[base], item->icon_words + t * 16, 16 * sizeof(uint16));
  }
}

bool ModItems_GetIconGfx(uint8 id, uint16 out[4 * 16]) {
  ModItem *item = ItemForId(id);
  if (item == NULL)
    return false;
  memcpy(out, item->icon_words, sizeof(item->icon_words));
  return true;
}

void ModItems_InstallReceiveGfx(uint8 id) {
  ModItem *item = ItemForId(id);
  if (item == NULL)
    return;
  memcpy(gfx_4bpp_buffer + 0x2d40, item->receive_words, 0x40);
  memcpy(gfx_4bpp_buffer + 0x2d80, item->receive_words + 32, 0x40);
}

int ModItems_ReceiptMessage(uint8 id) {
  ModItem *item = ItemForId(id);
  return item != NULL ? item->text_id : -1;
}

#ifdef ZELDA3_LUA

static bool BuildStateKey(const ModItem *item, char *key, size_t key_size) {
  int n = snprintf(key, key_size, "$item:%s", item->name);
  return n > 0 && (size_t)n < key_size;
}

void ModItems_LoadSaveState(LuaMod *mod) {
  for (int i = 0; i < g_item_slots; i++) {
    ModItem *item = &g_items[i];
    if (!item->used || item->owner != mod)
      continue;
    char key[64];
    int value = 0;
    if (BuildStateKey(item, key, sizeof(key)) &&
        LuaSave_GetEngineInt(mod, key, &value)) {
      ModItems_SetCount(item->id, value);
    } else {
      item->count = 0;
    }
  }
}

void ModItems_WriteSaveState(LuaMod *mod) {
  for (int i = 0; i < g_item_slots; i++) {
    ModItem *item = &g_items[i];
    if (!item->used || item->owner != mod)
      continue;
    char key[64];
    if (BuildStateKey(item, key, sizeof(key)))
      LuaSave_SetEngineInt(mod, key, item->count);
  }
}

void ModItems_ResetSaveState(LuaMod *mod) {
  for (int i = 0; i < g_item_slots; i++) {
    ModItem *item = &g_items[i];
    if (!item->used || item->owner != mod)
      continue;
    item->count = 0;
    char key[64];
    if (BuildStateKey(item, key, sizeof(key)))
      LuaSave_RemoveEngineKey(mod, key);
  }
}

#else

void ModItems_LoadSaveState(struct LuaMod *mod) {
  (void)mod;
}

void ModItems_WriteSaveState(struct LuaMod *mod) {
  (void)mod;
}

void ModItems_ResetSaveState(struct LuaMod *mod) {
  (void)mod;
}

#endif
