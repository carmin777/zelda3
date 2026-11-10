#ifndef ZELDA3_MOD_SHOP_H_
#define ZELDA3_MOD_SHOP_H_

#include <stddef.h>

#include "../types.h"

enum {
  kModShopMaxSlots = 3,
  kModShopMaxRooms = 16,
};

typedef struct ModShopEntry {
  uint8 item;   // original item receipt id or 0x80..0x9f for a mod item
  uint8 count;  // amount granted for custom items (1..99)
  uint16 price; // rupees (0..999)
} ModShopEntry;

// Lua registration. A later mod that sets the same room wins; the previous
// entry is replaced. The room is the low byte of dungeon_room_index, the same
// value SpritePrep_Shopkeeper uses in kShopKeeperWhere.
bool ModShop_Set(void *owner, int room, const ModShopEntry *entries, int count,
                 char *err, size_t err_size);
void ModShop_UnregisterOwner(void *owner);

// Returns the configured items for a room, or NULL when the room uses the
// vanilla shop table.
const ModShopEntry *ModShop_FindForRoom(int room, int *count);

// Per-sprite runtime data for the generic shop-item subclass. It lives next to
// the registry instead of borrowing sprite_* fields, which are too small for
// an item id, a price and a quantity.
void ModShop_SetRuntime(int sprite_slot, const ModShopEntry *entry);
bool ModShop_GetRuntime(int sprite_slot, ModShopEntry *entry);

#endif  // ZELDA3_MOD_SHOP_H_
