#ifndef ZELDA3_MOD_SEED_IMPORT_H_
#define ZELDA3_MOD_SEED_IMPORT_H_

#include "../types.h"

// Data-only fields from the z3randomizer $B0:8000 block. They are copied
// from seed.json "z3r_settings" and are read only while a seed mod is active.
typedef struct ModSeedZ3rSettings {
  int rupoor_deduction;
  int upgrade_5_bombs_refill;
  int upgrade_10_bombs_refill;
  int upgrade_5_arrows_refill;
  int upgrade_10_arrows_refill;
  int progressive_sword_limit;
  int progressive_sword_replacement;
  int progressive_shield_limit;
  int progressive_shield_replacement;
  int progressive_armor_limit;
  int progressive_armor_replacement;
  int bottle_limit;
  int bottle_replacement;
  int progressive_bow_limit;
  int progressive_bow_replacement;
  int ganons_tower_open_target;
  int ganons_tower_open_mode;
  int agahnim_door_style;
  int fix_fake_world;
  int bugfix_set_world_on_agahnim_death;
  int bugfix_pre_aga_dw_dungeon_death;
  int ganon_vulnerable_target;
  int ganon_vulnerable_mode;
  int goal_item_icon;
  int goal_item_requirement;
  int turn_in_goal_items;
  int hud_dungeon_items;
  int item_counter_hud;
  int map_hud_mode;
  int map_mode;
  int compass_mode;
  int fake_boots;
  int byrna_invulnerability;
  int quick_swap;
  int generic_keys;
  int heart_beep;
  int menu_speed;
  int no_bgm;
  int fast_fanfare;
  int inverted;
  uint8 starting_area_exit_offset[7];
  int total_item_count;
  uint8 crystal_pendant_flags[13];
  uint8 starting_area_overworld_door[7];
  uint8 starting_area_exit_table[80];
  uint8 should_start_at_exit[3];
  uint16 extra_hole_map16[16];
  uint16 extra_hole_area[16];
  uint8 extra_hole_entrance[16];
} ModSeedZ3rSettings;

// Starting-point tables copied from the seed ROM ($82:D8D2 in the JP
// z3randomizer build). The engine uses the same layout as the US
// kStartingPoint_* assets, so the import overrides those arrays directly.
typedef struct ModSeedStartingPoints {
  uint16 rooms[7];
  uint8 relative_coords[56];
  uint16 scroll_x[7];
  uint16 scroll_y[7];
  uint16 player_x[7];
  uint16 player_y[7];
  uint16 camera_x[7];
  uint16 camera_y[7];
  uint8 blockset[7];
  int8 floor[7];
  uint8 palace[7];
  uint8 starting_bg[7];
  uint8 quadrant1[7];
  uint8 quadrant2[7];
  uint16 door_settings[7];
} ModSeedStartingPoints;

typedef struct ModSeedOverworldPatch {
  uint16 screen;
  int write_count;
  uint16 *addrs;
  uint16 *values;
  int hole_count;
  uint16 *hole_addrs;
  uint16 *hole_values;
  int no_hole_count;
  uint16 *no_hole_addrs;
  uint16 *no_hole_values;
  bool has_warp;
  uint16 warp_addr;
  uint16 warp_low;
  uint16 warp_high;
} ModSeedOverworldPatch;

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

// Current settings, or NULL when no seed mod is active.
const ModSeedZ3rSettings *ModSeedImport_Z3rSettings(void);

// z3r goal conditions. Both return the vanilla result when Z3rRules is off.
bool ModSeedImport_GanonsTowerOpen(void);
bool ModSeedImport_AgahnimDoorLocked(void);
bool ModSeedImport_GanonVulnerable(void);
int ModSeedImport_CrystalCount(void);
int ModSeedImport_CrystalBossCount(void);
bool ModSeedImport_GenericKeysActive(void);
void ModSeedImport_CountItem(uint8 item, int method);
int ModSeedImport_MapKeyTotal(int dungeon);
int ModSeedImport_MapKeyCollected(int dungeon);

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

// Starting-point tables and inverted overworld tile patches imported from
// the seed ROM. Both are applied only when the seed is inverted.
void ModSeedImport_ApplyStartingPoints(void);
void ModSeedImport_ApplyPalettes(void);
void ModSeedImport_ApplySignText(void);
void ModSeedImport_ApplyOverworldPatches(uint16 screen);
uint8 ModSeedImport_TileAttribute(uint16 index, uint8 fallback);
bool ModSeedImport_MirrorBonk(uint16 x, uint16 y);

// z3r AllowStartFromExit ($82:8496): the selected spawn choice starts on the
// overworld side instead of loading the interior.
bool ModSeedImport_ShouldStartAtExit(int choice);

enum {
  kModNpcOldMan = 0,
  kModNpcSickKid = 1,
  kModNpcSahasrahla = 2,
  kModNpcCatfish = 3,
};
uint8 ModSeedImport_NpcItem(int npc, uint8 fallback);

// z3r CheckHole ($9B:B860): extra map16/area pairs that redirect to an
// entrance before the Chris Houlihan fallback.
bool ModSeedImport_ExtraHole(uint16 pos, uint16 area, uint8 *entrance);

// New-file SRAM template (z3r OnFileCreation / InitSRAMTable). Returns false
// when the mod has no template; the normal US initialization then runs.
bool ModSeedImport_InitNewSave(uint8 *sram, size_t size);

// StartingAreaOverworldDoor[index], or fallback when the seed has no value.
uint8 ModSeedImport_StartingDoor(int index, uint8 fallback);

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
