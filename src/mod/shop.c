#include "shop.h"

#include <stdio.h>
#include <string.h>

typedef struct ModShop {
  bool used;
  void *owner;
  uint8 room;
  uint8 count;
  ModShopEntry entries[kModShopMaxSlots];
} ModShop;

typedef struct ModShopRuntime {
  bool used;
  ModShopEntry entry;
} ModShopRuntime;

static ModShop g_shops[kModShopMaxRooms];
static ModShopRuntime g_runtime[16];

static void SetError(char *err, size_t err_size, const char *message) {
  if (err_size != 0)
    snprintf(err, err_size, "%s", message);
}

bool ModShop_Set(void *owner, int room, const ModShopEntry *entries, int count,
                 char *err, size_t err_size) {
  if (room < 0 || room > 0xff) {
    SetError(err, err_size, "shop room must be between 0x00 and 0xFF");
    return false;
  }
  if (count < 1 || count > kModShopMaxSlots) {
    SetError(err, err_size, "a shop needs between 1 and 3 items");
    return false;
  }

  ModShop *slot = NULL;
  for (int i = 0; i < kModShopMaxRooms; i++) {
    if (g_shops[i].used && g_shops[i].room == (uint8)room) {
      slot = &g_shops[i];
      break;
    }
  }
  if (slot == NULL) {
    for (int i = 0; i < kModShopMaxRooms; i++) {
      if (!g_shops[i].used) {
        slot = &g_shops[i];
        break;
      }
    }
  }
  if (slot == NULL) {
    SetError(err, err_size, "too many shops defined (maximum 16 rooms)");
    return false;
  }

  memset(slot, 0, sizeof(*slot));
  slot->used = true;
  slot->owner = owner;
  slot->room = (uint8)room;
  slot->count = (uint8)count;
  memcpy(slot->entries, entries, (size_t)count * sizeof(*entries));
  return true;
}

void ModShop_UnregisterOwner(void *owner) {
  for (int i = 0; i < kModShopMaxRooms; i++) {
    if (g_shops[i].used && g_shops[i].owner == owner)
      memset(&g_shops[i], 0, sizeof(g_shops[i]));
  }
}

const ModShopEntry *ModShop_FindForRoom(int room, int *count) {
  uint8 room_byte = (uint8)room;
  for (int i = 0; i < kModShopMaxRooms; i++) {
    if (g_shops[i].used && g_shops[i].room == room_byte) {
      if (count != NULL)
        *count = g_shops[i].count;
      return g_shops[i].entries;
    }
  }
  return NULL;
}

void ModShop_SetRuntime(int sprite_slot, const ModShopEntry *entry) {
  if (sprite_slot < 0 || sprite_slot >= (int)(sizeof(g_runtime) / sizeof(g_runtime[0])))
    return;
  g_runtime[sprite_slot].used = true;
  g_runtime[sprite_slot].entry = *entry;
}

bool ModShop_GetRuntime(int sprite_slot, ModShopEntry *entry) {
  if (sprite_slot < 0 || sprite_slot >= (int)(sizeof(g_runtime) / sizeof(g_runtime[0])))
    return false;
  if (!g_runtime[sprite_slot].used)
    return false;
  *entry = g_runtime[sprite_slot].entry;
  return true;
}
