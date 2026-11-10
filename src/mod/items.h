#ifndef ZELDA3_MOD_ITEMS_H_
#define ZELDA3_MOD_ITEMS_H_

#include "../types.h"

// New items registered by mods. The ids start above the original item receipt
// table (0..75), so every table indexed by the original id stays untouched.
enum {
  kModItemIdBase = 0x80,
  kModItemMax = 32,
  kModItemHudBase = 25,      // hud_cur_item values for registered items
};

typedef struct ModItemEvent {
  int item;    // custom item id (0x80..0x9f)
  int count;   // current quantity after the receive, or at use time
  int chest;   // chest position for ItemReceive, otherwise 0
  int method;  // item_receipt_method at receive time
} ModItemEvent;

typedef void ModItemUseCallback(void *owner, int ref, const ModItemEvent *ev);
typedef void ModItemReceiveCallback(void *owner, int ref, const ModItemEvent *ev);

struct LuaMod;

// Registers one item. icon_png and receive_png point to the bytes of a 16x16
// 8-bit grayscale PNG whose pixel values are SNES palette indices 0..15.
// receive_png may be NULL, in which case icon_png is reused. text_string may
// be NULL when text_id is used instead. Returns the new item id, or -1 with a
// message in err.
int ModItems_Register(const char *owner_id, void *owner,
                      const char *name,
                      const uint8 *icon_png, size_t icon_size,
                      const uint8 *receive_png, size_t receive_size,
                      const char *text_string, int text_id, int max_count,
                      ModItemUseCallback *on_use, int use_ref,
                      ModItemReceiveCallback *on_receive, int receive_ref,
                      char *err, size_t err_size);

// Removes every item owned by owner. Called when a Lua mod is disabled.
void ModItems_UnregisterOwner(void *owner);

int ModItems_Count(void);
int ModItems_FindByName(const char *name);
bool ModItems_IsCustomId(uint8 id);
bool ModItems_IsCustomHudSlot(int hud_slot);
int ModItems_IdFromHudSlot(int hud_slot);
int ModItems_HudSlotFromId(uint8 id);
const char *ModItems_Name(uint8 id);
int ModItems_CountForId(uint8 id);
int ModItems_MaxForId(uint8 id);
bool ModItems_IsOwned(uint8 id);
bool ModItems_SetCount(uint8 id, int count);

// Item use/receive callbacks. Receiving also increments the quantity (capped
// at max) and then calls on_receive with the new count. SetNextReceiveCount
// makes the next receive grant that many units instead of one (used by shops).
bool ModItems_Use(uint8 id);
void ModItems_OnReceive(uint8 id, int chest, int method);
void ModItems_SetNextReceiveCount(int count);

// Copies the four 4bpp icon tiles of a custom item. The caller can install
// them in sprite VRAM to show the item in a shop without changing the item
// registry.
bool ModItems_GetIconGfx(uint8 id, uint16 out[4 * 16]);

// HUD helpers. The menu name uses the same uppercase HUD glyphs as the
// original item names; longer names are truncated to 16 characters.
const uint16 *ModItems_HudNameTiles(int hud_slot);

// Custom icons borrow the four tiles of the vanilla icon that would occupy
// the same free menu cell. Begin restores every borrowed tile set; the draw
// code calls it before drawing vanilla items and then installs the custom
// icons again. vanilla_words are the four tilemap words of that vanilla icon;
// out receives the four tilemap words for the custom icon.
void ModItems_BeginHudIcons(void);
void ModItems_InstallHudIcon(int hud_slot, const uint16 vanilla_words[4],
                             uint16 out[4]);

// Item receipt helpers.
void ModItems_InstallReceiveGfx(uint8 id);
int ModItems_ReceiptMessage(uint8 id);

// Per-mod save state. The values live in the mod's sidecar file under a
// reserved key, so removing the mod leaves the game save untouched.
void ModItems_LoadSaveState(struct LuaMod *mod);
void ModItems_WriteSaveState(struct LuaMod *mod);
void ModItems_ResetSaveState(struct LuaMod *mod);

// Shared by zelda.item.get/set in Lua. Returns the quantity for a custom item
// name, or -1 when the name is not registered.
int ModItems_GetCountByName(const char *name);
bool ModItems_SetCountByName(const char *name, int count);

#endif  // ZELDA3_MOD_ITEMS_H_
