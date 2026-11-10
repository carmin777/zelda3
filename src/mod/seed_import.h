#ifndef ZELDA3_MOD_SEED_IMPORT_H_
#define ZELDA3_MOD_SEED_IMPORT_H_

#include "../types.h"

// Runtime data from a generated ALttPR seed mod. The mod manifest points at
// the JSON file with "seed_data". The engine applies the locations it knows;
// the rest stays in the file for the coverage tool.
//
// Every hook returns its fallback when no seed mod is active, so the normal
// game and mods without seed data are byte-for-byte identical.

bool ModSeedImport_LoadFile(const char *path, const char *owner_id,
                            char *err, size_t err_size);
void ModSeedImport_Reset(void);
bool ModSeedImport_Active(void);

// True when the generated mod enabled the z3randomizer behavior rules.
bool ModSeedImport_Z3rRulesActive(void);

// Contents of a chest, identified by room and by the 0-based chest order in
// that room (the same order as kDungeonRoomChests / room objects).
uint8 ModSeedImport_ChestItem(uint16 room, int chest_index, uint8 fallback);

// Item the wounded uncle gives (PC 0x2DF45 in the seed ROM).
uint8 ModSeedImport_UncleItem(uint8 fallback);

// Fixed location lookup by the id stored in seed.json, for example
// "fixed:SpriteItemValues:LibraryItem" or "prize:Eastern Palace".
uint8 ModSeedImport_FixedItem(const char *id, uint8 fallback);

// Heart piece/tablet/standing-key location lookup (z3r heartpieces.asm
// LoadIndoorValue/LoadOutdoorValue).
uint8 ModSeedImport_HeartPieceItem(int indoors, uint16 room, uint8 screen,
                                   uint16 link_x, uint8 fallback);

// Boss heart container by boss room (z3r LoadHeartContainerRoomValue).
uint8 ModSeedImport_BossItem(uint16 room, uint8 fallback);

// Dungeon prize receipt by the game dungeon index (0 = Ganon's Tower,
// 1 = Hyrule Castle, ... 12 = Turtle Rock).
uint8 ModSeedImport_PrizeItem(int dungeon_index, uint8 fallback);

// Bonk-key dash item by room (z3r bookofmudora.asm LoadBonkItem).
uint8 ModSeedImport_BonkKeyItem(uint16 room, uint8 fallback);

// New-file SRAM template (z3r OnFileCreation / InitSRAMTable). Returns false
// when the mod has no template; the normal US initialization then runs.
bool ModSeedImport_InitNewSave(uint8 *sram, size_t size);

// Receipt-time context. MapItem() sets it when a z3r item needs the target
// dungeon or a special behavior. AncillaAdd_ItemReceipt() consumes it.
void ModSeedImport_SetReceiptDungeon(int dungeon);
int ModSeedImport_TakeReceiptDungeon(void);
void ModSeedImport_SetSpecialItem(uint8 item);
uint8 ModSeedImport_TakeSpecialItem(void);
void ModSeedImport_ApplySpecialItem(uint8 item);
void ModSeedImport_SetPrizeItem(uint8 item);
uint8 ModSeedImport_TakePrizeItem(void);

// Maps a z3r item id stored in the seed to an item the US engine can receive.
// Ids 0..0x4b pass through. Progressive items are turned into the next US
// item, dungeon items remember their target dungeon, and z3r-only item rules
// are applied at receipt time.
uint8 ModSeedImport_MapItem(uint8 item);

#endif  // ZELDA3_MOD_SEED_IMPORT_H_
