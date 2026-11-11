#include "seed_import.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../third_party/cjson/cJSON.h"
#include "../assets.h"
#include "../features.h"
#include "../util.h"
#include "../variables.h"
#include "events.h"
#include "shop.h"

typedef struct SeedChest {
  uint16 room;
  uint8 index;
  uint8 item;
} SeedChest;

typedef struct SeedFixed {
  char *id;
  uint8 item;
} SeedFixed;

typedef struct SeedShopGroup {
  bool used;
  bool any_door;
  int id;
  int room;
  int door;
  int count;
  ModShopEntry entries[3];
} SeedShopGroup;

static SeedChest *g_chests;
static int g_chest_count;
static SeedFixed *g_fixed;
static int g_fixed_count;
static int g_uncle_item = -1;
static bool g_active;

static uint8 g_save_template[0x500];
static bool g_has_save_template;
static ModSeedZ3rSettings g_z3r;
static ModSeedStartingPoints g_start_points;
static bool g_has_start_points;
static uint8 g_ow_bg_main_palette[420];
static uint8 g_ow_bg_aux12_palette[840];
static bool g_has_ow_bg_main_palette;
static bool g_has_ow_bg_aux12_palette;
static uint16 g_hc_sign_left;
static uint16 g_hc_sign_right;
static bool g_has_hc_sign_text;
static uint8 g_npc_items[4];
static bool g_has_npc_items[4];
static ModSeedOverworldPatch *g_overworld_patches;
static int g_overworld_patch_count;
static uint8 g_tr_tile_attributes[512];
static bool g_has_tr_tile_attributes;
static uint16 g_mirror_bonk[23][4];
static int g_mirror_bonk_count;
// Number of small keys placed in chests per dungeon, counted from the seed
// chest table. z3r MapHUDMode uses MapTotalsWRAM for the same value.
static uint8 g_map_key_totals[13];

static int g_receipt_dungeon = -1;
static uint8 g_special_item;
static uint8 g_prize_item;

static int g_shop_owner;

static const char *const kDungeonNames[13] = {
  "Ganon's Tower", "Hyrule Castle", "Eastern Palace", "Desert Palace",
  "Agahnim's Tower", "Swamp Palace", "Palace of Darkness", "Misery Mire",
  "Skull Woods", "Ice Palace", "Tower of Hera", "Thieves' Town", "Turtle Rock",
};

// z3r item id -> game dungeon index used by cur_palace_index_x2 >> 1.
// 0x70/0x71 are Light/Dark World maps and have no dungeon target.
static const int8 kMapDungeon[16] = {
  -1, -1, 0, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 13,
};
static const int8 kCompassDungeon[16] = {
  -1, -1, 0, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 13,
};
static const int8 kBigKeyDungeon[14] = {
  0, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 13,
};
static const int8 kSmallKeyDungeon[16] = {
  13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, -1, -1,
};

// z3r pendantcrystalhud.asm boss_room_ids reordered to the
// CrystalPendantFlags_2 order used by the seed and kDungeonCrystalPendantBit:
// GT, HC, Eastern, Desert, Agahnim, Swamp, PoD, Mire, Skull, Ice, Hera,
// Thieves, Turtle Rock.
static const uint8 kBossRooms[13] = {
  0x0D, 0x80, 0xC8, 0x33, 0x20, 0x06, 0x5A,
  0x90, 0x29, 0xDE, 0x07, 0xAC, 0xA4,
};

static void SetError(char *err, size_t err_size, const char *fmt, ...) {
  if (err_size == 0)
    return;
  va_list va;
  va_start(va, fmt);
  vsnprintf(err, err_size, fmt, va);
  va_end(va);
}

static char *StrDup(const char *s) {
  size_t length = strlen(s);
  char *r = malloc(length + 1);
  if (r != NULL)
    memcpy(r, s, length + 1);
  return r;
}

static void InitZ3rSettings(ModSeedZ3rSettings *s) {
  memset(s, 0, sizeof(*s));
  s->rupoor_deduction = 10;
  s->upgrade_5_bombs_refill = 50;
  s->upgrade_10_bombs_refill = 50;
  s->upgrade_5_arrows_refill = 70;
  s->upgrade_10_arrows_refill = 70;
  s->progressive_sword_limit = 4;
  s->progressive_sword_replacement = 0x47;
  s->progressive_shield_limit = 3;
  s->progressive_shield_replacement = 0x47;
  s->progressive_armor_limit = 2;
  s->progressive_armor_replacement = 0x47;
  s->bottle_limit = 4;
  s->bottle_replacement = 0x47;
  s->progressive_bow_limit = 2;
  s->progressive_bow_replacement = 0x47;
  s->ganons_tower_open_target = 7;
  s->ganon_vulnerable_target = 7;
  s->turn_in_goal_items = 1;
  s->hud_dungeon_items = 0x0F;
  s->byrna_invulnerability = 1;
  s->heart_beep = 0x20;
  s->menu_speed = 0x08;
  s->total_item_count = 0xD8;
  for (int i = 0; i < 16; i++) {
    s->extra_hole_map16[i] = 0xFFFF;
    s->extra_hole_area[i] = 0xFFFF;
  }
}

void ModSeedImport_Reset(void) {
  free(g_chests);
  g_chests = NULL;
  g_chest_count = 0;
  for (int i = 0; i < g_fixed_count; i++)
    free(g_fixed[i].id);
  free(g_fixed);
  g_fixed = NULL;
  g_fixed_count = 0;
  g_uncle_item = -1;
  g_has_save_template = false;
  memset(g_save_template, 0, sizeof(g_save_template));
  g_has_start_points = false;
  memset(&g_start_points, 0, sizeof(g_start_points));
  g_has_ow_bg_main_palette = false;
  g_has_ow_bg_aux12_palette = false;
  memset(g_ow_bg_main_palette, 0, sizeof(g_ow_bg_main_palette));
  memset(g_ow_bg_aux12_palette, 0, sizeof(g_ow_bg_aux12_palette));
  g_has_hc_sign_text = false;
  g_hc_sign_left = g_hc_sign_right = 0;
  memset(g_npc_items, 0, sizeof(g_npc_items));
  memset(g_has_npc_items, 0, sizeof(g_has_npc_items));
  g_has_tr_tile_attributes = false;
  memset(g_tr_tile_attributes, 0, sizeof(g_tr_tile_attributes));
  g_mirror_bonk_count = 0;
  memset(g_mirror_bonk, 0, sizeof(g_mirror_bonk));
  for (int i = 0; i < g_overworld_patch_count; i++) {
    free(g_overworld_patches[i].addrs);
    free(g_overworld_patches[i].values);
    free(g_overworld_patches[i].hole_addrs);
    free(g_overworld_patches[i].hole_values);
    free(g_overworld_patches[i].no_hole_addrs);
    free(g_overworld_patches[i].no_hole_values);
  }
  free(g_overworld_patches);
  g_overworld_patches = NULL;
  g_overworld_patch_count = 0;
  InitZ3rSettings(&g_z3r);
  g_receipt_dungeon = -1;
  g_special_item = 0;
  g_prize_item = 0;
  memset(g_map_key_totals, 0, sizeof(g_map_key_totals));
  g_active = false;
  ModShop_UnregisterOwner(&g_shop_owner);
}

bool ModSeedImport_Active(void) {
  return g_active;
}

bool ModSeedImport_Z3rRulesActive(void) {
  return (enhanced_features0 & kFeatures0_Z3rRules) != 0;
}

const ModSeedZ3rSettings *ModSeedImport_Z3rSettings(void) {
  return g_active ? &g_z3r : NULL;
}

static int Popcount8(uint8 value) {
  int count = 0;
  while (value) {
    count += value & 1;
    value >>= 1;
  }
  return count;
}

int ModSeedImport_CrystalCount(void) {
  return Popcount8((uint8)(link_has_crystals & 0x7f));
}

bool ModSeedImport_GenericKeysActive(void) {
  const ModSeedZ3rSettings *s = ModSeedImport_Z3rSettings();
  return ModSeedImport_Z3rRulesActive() && s != NULL && s->generic_keys;
}

int ModSeedImport_CrystalBossCount(void) {
  const ModSeedZ3rSettings *s = ModSeedImport_Z3rSettings();
  if (s == NULL)
    return 0;
  int count = 0;
  for (int i = 0; i < 13; i++) {
    if ((s->crystal_pendant_flags[i] & 0x40) &&
        (save_dung_info[kBossRooms[i]] & 0x800))
      count++;
  }
  return count;
}

int ModSeedImport_MapKeyTotal(int dungeon) {
  if (dungeon < 0 || dungeon >= 13)
    return 0;
  return g_map_key_totals[dungeon];
}

// z3r inventory.asm LoadKeys/SaveKeys: GenericKeys keeps one shared counter
// instead of the per-dungeon table.
int ModSeedImport_MapKeyCollected(int dungeon) {
  if (dungeon < 0 || dungeon >= 13)
    return 0;
  if (ModSeedImport_GenericKeysActive())
    return link_num_keys;
  int palace = BYTE(cur_palace_index_x2);
  if (palace != 0xFF && (palace >> 1) == dungeon)
    return link_num_keys;
  int slot = dungeon == 1 ? 0 : dungeon;  // sewers share Hyrule Castle's slot
  return link_keys_earned_per_dungeon[slot];
}

// z3r goalitem.asm CheckTowerOpen/CheckEnoughCrystalsForGanon. Mode 0 keeps
// the vanilla 7-crystal check; mode 1 compares CrystalCounter with the target.
bool ModSeedImport_GanonsTowerOpen(void) {
  const ModSeedZ3rSettings *s = ModSeedImport_Z3rSettings();
  if (!ModSeedImport_Z3rRulesActive() || s == NULL ||
      s->ganons_tower_open_mode == 0)
    return (link_has_crystals & 0x7f) == 0x7f;
  return ModSeedImport_CrystalCount() >= s->ganons_tower_open_target;
}

// z3r entrances.asm LockAgahnimDoors. The position test is the same as
// LockAgahnimDoorsCore: Link must be standing at the Ganon's Tower door.
static bool AtAgahnimDoor(void) {
  return link_x_coord >= 1992 && link_x_coord < 2088 && link_y_coord < 1720;
}

bool ModSeedImport_AgahnimDoorLocked(void) {
  const ModSeedZ3rSettings *s = ModSeedImport_Z3rSettings();
  if (!ModSeedImport_Z3rRulesActive() || s == NULL)
    return false;
  if (s->agahnim_door_style == 0)
    return false;
  if (s->agahnim_door_style == 1) {
    if ((sram_progress_indicator & 0xf) >= 2)
      return false;
    return AtAgahnimDoor();
  }
  if (s->agahnim_door_style != 2 || !AtAgahnimDoor())
    return false;
  if (ModSeedImport_GanonsTowerOpen())
    return false;
  if (s->inverted != 0) {
    if (save_ow_event_info[0x43] & 0x20)
      return false;
    if (link_state_bits & 0x80)
      return false;
  }
  return true;
}

// z3r goalitem.asm CheckGanonVulnerability. Modes 0..5 and 9 are covered;
// Modes 7/8 use the crystal-boss counter, 10/11 the total item counter and
// mode 6 keeps the gate closed as z3r's light-speed branch does.
bool ModSeedImport_GanonVulnerable(void) {
  const ModSeedZ3rSettings *s = ModSeedImport_Z3rSettings();
  if (!ModSeedImport_Z3rRulesActive() || s == NULL)
    return true;  // vanilla path, not reached by the seed-only hooks
  switch (s->ganon_vulnerable_mode) {
  case 0: return true;
  case 1: return false;
  case 2:
    return sram_progress_indicator >= 3;
  case 3:
    return (save_ow_event_info[0x5b] & 0x20) != 0 &&
           ModSeedImport_CrystalCount() >= s->ganon_vulnerable_target;
  case 4:
    return ModSeedImport_CrystalCount() >= s->ganon_vulnerable_target;
  case 5: {
    int goal = WORD(g_ram[0xF418]);
    return goal >= s->goal_item_requirement;
  }
  case 6:
    return false;  // z3r light speed: the gate stays closed.
  case 7:
    return ModSeedImport_CrystalCount() >= s->ganon_vulnerable_target &&
           ModSeedImport_CrystalBossCount() >= s->ganon_vulnerable_target;
  case 8:
    return ModSeedImport_CrystalBossCount() >= s->ganon_vulnerable_target;
  case 9:
    return (link_which_pendants & 7) == 7 &&
           (link_has_crystals & 0x7f) == 0x7f &&
           (save_ow_event_info[0x5b] & 0x20) != 0;
  case 10:
    return WORD(g_ram[0xF423]) >= s->total_item_count;
  case 11:
    return WORD(g_ram[0xF423]) >= s->total_item_count &&
           sram_progress_indicator >= 3;
  default:
    return false;
  }
}

// z3r inventory.asm AddInventory: only receipt IDs with InventoryTable bit 0
// (total item counter) are counted, and shop purchases are skipped.
static bool ItemCountsForTotal(uint8 item) {
  if (item == 0x26 || (item >= 0x37 && item <= 0x39))
    return false;
  return item <= 0x25 || item >= 0x27;
}

void ModSeedImport_CountItem(uint8 item, int method) {
  if (!g_active || !ModSeedImport_Z3rRulesActive() || method == 2)
    return;
  if (!ItemCountsForTotal(item))
    return;
  uint16 count = WORD(g_ram[0xF423]);
  if (count != 0xFFFF)
    WORD(g_ram[0xF423]) = count + 1;
}

static bool GetInt(cJSON *object, const char *name, int min, int max, int *out) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsNumber(item))
    return false;
  int value = item->valueint;
  if (item->valuedouble != (double)value || value < min || value > max)
    return false;
  *out = value;
  return true;
}

static const char *GetString(cJSON *object, const char *name) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == NULL)
    return NULL;
  return item->valuestring;
}

static void GetZ3rInt(cJSON *object, const char *name, int min, int max, int *out) {
  int value;
  if (GetInt(object, name, min, max, &value))
    *out = value;
}

// Fields of the z3r $B0:8000 block. Data-only values and the behavior flags
// consumed by Z3rRules are copied here; InvertedMode stays recorded in
// seed.json until the inverted-world round.
static void ParseZ3rSettings(cJSON *root, ModSeedZ3rSettings *s) {
  cJSON *object = cJSON_GetObjectItemCaseSensitive(root, "z3r_settings");
  if (!cJSON_IsObject(object))
    return;
  GetZ3rInt(object, "rupoor_deduction", 0, 0xFFFF, &s->rupoor_deduction);
  GetZ3rInt(object, "upgrade_5_bombs_refill", 0, 0xFF, &s->upgrade_5_bombs_refill);
  GetZ3rInt(object, "upgrade_10_bombs_refill", 0, 0xFF, &s->upgrade_10_bombs_refill);
  GetZ3rInt(object, "upgrade_5_arrows_refill", 0, 0xFF, &s->upgrade_5_arrows_refill);
  GetZ3rInt(object, "upgrade_10_arrows_refill", 0, 0xFF, &s->upgrade_10_arrows_refill);
  GetZ3rInt(object, "progressive_sword_limit", 1, 4, &s->progressive_sword_limit);
  GetZ3rInt(object, "progressive_sword_replacement", 0, 0xFF, &s->progressive_sword_replacement);
  GetZ3rInt(object, "progressive_shield_limit", 1, 3, &s->progressive_shield_limit);
  GetZ3rInt(object, "progressive_shield_replacement", 0, 0xFF, &s->progressive_shield_replacement);
  GetZ3rInt(object, "progressive_armor_limit", 1, 2, &s->progressive_armor_limit);
  GetZ3rInt(object, "progressive_armor_replacement", 0, 0xFF, &s->progressive_armor_replacement);
  GetZ3rInt(object, "bottle_limit", 1, 4, &s->bottle_limit);
  GetZ3rInt(object, "bottle_replacement", 0, 0xFF, &s->bottle_replacement);
  GetZ3rInt(object, "progressive_bow_limit", 1, 2, &s->progressive_bow_limit);
  GetZ3rInt(object, "progressive_bow_replacement", 0, 0xFF, &s->progressive_bow_replacement);
  GetZ3rInt(object, "ganons_tower_open_target", 0, 0xFFFF, &s->ganons_tower_open_target);
  GetZ3rInt(object, "ganons_tower_open_mode", 0, 1, &s->ganons_tower_open_mode);
  GetZ3rInt(object, "agahnim_door_style", 0, 2, &s->agahnim_door_style);
  GetZ3rInt(object, "fix_fake_world", 0, 1, &s->fix_fake_world);
  GetZ3rInt(object, "bugfix_set_world_on_agahnim_death", 0, 1,
            &s->bugfix_set_world_on_agahnim_death);
  GetZ3rInt(object, "bugfix_pre_aga_dw_dungeon_death", 0, 1,
            &s->bugfix_pre_aga_dw_dungeon_death);
  GetZ3rInt(object, "ganon_vulnerable_target", 0, 0xFFFF, &s->ganon_vulnerable_target);
  GetZ3rInt(object, "ganon_vulnerable_mode", 0, 0x0B, &s->ganon_vulnerable_mode);
  GetZ3rInt(object, "goal_item_icon", 0, 0xFFFF, &s->goal_item_icon);
  GetZ3rInt(object, "goal_item_requirement", 0, 0xFFFF, &s->goal_item_requirement);
  GetZ3rInt(object, "turn_in_goal_items", 0, 1, &s->turn_in_goal_items);
  GetZ3rInt(object, "hud_dungeon_items", 0, 0x1F, &s->hud_dungeon_items);
  GetZ3rInt(object, "item_counter_hud", 0, 1, &s->item_counter_hud);
  GetZ3rInt(object, "map_hud_mode", 0, 0xFF, &s->map_hud_mode);
  GetZ3rInt(object, "map_mode", 0, 0xFF, &s->map_mode);
  GetZ3rInt(object, "compass_mode", 0, 0xFF, &s->compass_mode);
  GetZ3rInt(object, "fake_boots", 0, 1, &s->fake_boots);
  GetZ3rInt(object, "byrna_invulnerability", 0, 1, &s->byrna_invulnerability);
  GetZ3rInt(object, "quick_swap", 0, 1, &s->quick_swap);
  GetZ3rInt(object, "generic_keys", 0, 1, &s->generic_keys);
  GetZ3rInt(object, "heart_beep", 0, 0xFF, &s->heart_beep);
  GetZ3rInt(object, "menu_speed", 0, 0xFF, &s->menu_speed);
  GetZ3rInt(object, "no_bgm", 0, 1, &s->no_bgm);
  GetZ3rInt(object, "fast_fanfare", 0, 1, &s->fast_fanfare);
  GetZ3rInt(object, "inverted", 0, 1, &s->inverted);
  GetZ3rInt(object, "total_item_count", 0, 0xFFFF, &s->total_item_count);

  cJSON *crystals = cJSON_GetObjectItemCaseSensitive(object, "crystal_pendant_flags");
  if (cJSON_IsArray(crystals)) {
    for (int i = 0; i < 13; i++) {
      cJSON *item = cJSON_GetArrayItem(crystals, i);
      if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 0xFF)
        s->crystal_pendant_flags[i] = (uint8)item->valueint;
    }
  }

  cJSON *doors = cJSON_GetObjectItemCaseSensitive(object, "starting_area_overworld_door");
  if (cJSON_IsArray(doors)) {
    for (int i = 0; i < 7; i++) {
      cJSON *item = cJSON_GetArrayItem(doors, i);
      if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 0xFF)
        s->starting_area_overworld_door[i] = (uint8)item->valueint;
    }
  }
  cJSON *exit_offsets = cJSON_GetObjectItemCaseSensitive(object, "starting_area_exit_offset");
  if (cJSON_IsArray(exit_offsets)) {
    for (int i = 0; i < 7; i++) {
      cJSON *item = cJSON_GetArrayItem(exit_offsets, i);
      if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 0xFF)
        s->starting_area_exit_offset[i] = (uint8)item->valueint;
    }
  }
  cJSON *exit_table = cJSON_GetObjectItemCaseSensitive(object, "starting_area_exit_table");
  if (cJSON_IsArray(exit_table)) {
    for (int i = 0; i < 80; i++) {
      cJSON *item = cJSON_GetArrayItem(exit_table, i);
      if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 0xFF)
        s->starting_area_exit_table[i] = (uint8)item->valueint;
    }
  }
  cJSON *should_exit = cJSON_GetObjectItemCaseSensitive(object, "should_start_at_exit");
  if (cJSON_IsArray(should_exit)) {
    for (int i = 0; i < 3; i++) {
      cJSON *item = cJSON_GetArrayItem(should_exit, i);
      if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 0xFF)
        s->should_start_at_exit[i] = (uint8)item->valueint;
    }
  }
  cJSON *extra_map16 = cJSON_GetObjectItemCaseSensitive(object, "extra_hole_map16");
  cJSON *extra_area = cJSON_GetObjectItemCaseSensitive(object, "extra_hole_area");
  cJSON *extra_entrance = cJSON_GetObjectItemCaseSensitive(object, "extra_hole_entrance");
  for (int i = 0; i < 16; i++) {
    cJSON *map16 = cJSON_GetArrayItem(extra_map16, i);
    cJSON *area = cJSON_GetArrayItem(extra_area, i);
    cJSON *entrance = cJSON_GetArrayItem(extra_entrance, i);
    if (cJSON_IsNumber(map16) && map16->valueint >= 0 && map16->valueint <= 0xFFFF)
      s->extra_hole_map16[i] = (uint16)map16->valueint;
    if (cJSON_IsNumber(area) && area->valueint >= 0 && area->valueint <= 0xFFFF)
      s->extra_hole_area[i] = (uint16)area->valueint;
    if (cJSON_IsNumber(entrance) && entrance->valueint >= 0 && entrance->valueint <= 0xFF)
      s->extra_hole_entrance[i] = (uint8)entrance->valueint;
  }
}

static bool GetU8Array(cJSON *object, const char *name, uint8 *out, int count) {
  cJSON *array = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsArray(array))
    return false;
  for (int i = 0; i < count; i++) {
    cJSON *item = cJSON_GetArrayItem(array, i);
    if (!cJSON_IsNumber(item) || item->valueint < -128 || item->valueint > 255)
      return false;
    out[i] = (uint8)item->valueint;
  }
  return true;
}

static bool GetU16Array(cJSON *object, const char *name, uint16 *out, int count) {
  cJSON *array = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsArray(array))
    return false;
  for (int i = 0; i < count; i++) {
    cJSON *item = cJSON_GetArrayItem(array, i);
    if (!cJSON_IsNumber(item) || item->valueint < 0 || item->valueint > 0xFFFF)
      return false;
    out[i] = (uint16)item->valueint;
  }
  return true;
}

static bool ParseStartingPoints(cJSON *root, ModSeedStartingPoints *out) {
  cJSON *object = cJSON_GetObjectItemCaseSensitive(root, "starting_points");
  if (!cJSON_IsObject(object))
    return false;
  bool ok = GetU16Array(object, "rooms", out->rooms, 7) &&
            GetU8Array(object, "relative_coords", out->relative_coords, 56) &&
            GetU16Array(object, "scroll_x", out->scroll_x, 7) &&
            GetU16Array(object, "scroll_y", out->scroll_y, 7) &&
            GetU16Array(object, "player_x", out->player_x, 7) &&
            GetU16Array(object, "player_y", out->player_y, 7) &&
            GetU16Array(object, "camera_x", out->camera_x, 7) &&
            GetU16Array(object, "camera_y", out->camera_y, 7) &&
            GetU8Array(object, "blockset", out->blockset, 7) &&
            GetU8Array(object, "floor", (uint8*)out->floor, 7) &&
            GetU8Array(object, "palace", out->palace, 7) &&
            GetU8Array(object, "starting_bg", out->starting_bg, 7) &&
            GetU8Array(object, "quadrant1", out->quadrant1, 7) &&
            GetU8Array(object, "quadrant2", out->quadrant2, 7) &&
            GetU16Array(object, "door_settings", out->door_settings, 7);
  return ok;
}

static bool ParseWriteList(cJSON *object, const char *name, int *out_count,
                           uint16 **out_addrs, uint16 **out_values) {
  cJSON *array = cJSON_GetObjectItemCaseSensitive(object, name);
  if (array == NULL)
    return true;
  if (!cJSON_IsArray(array))
    return false;
  int count = cJSON_GetArraySize(array);
  if (count > 0x10000)
    return false;
  uint16 *addrs = count ? malloc((size_t)count * sizeof(*addrs)) : NULL;
  uint16 *values = count ? malloc((size_t)count * sizeof(*values)) : NULL;
  if (count && (addrs == NULL || values == NULL)) {
    free(addrs);
    free(values);
    return false;
  }
  for (int i = 0; i < count; i++) {
    cJSON *pair = cJSON_GetArrayItem(array, i);
    if (!cJSON_IsArray(pair) || cJSON_GetArraySize(pair) != 2)
      goto fail;
    cJSON *addr = cJSON_GetArrayItem(pair, 0);
    cJSON *value = cJSON_GetArrayItem(pair, 1);
    if (!cJSON_IsNumber(addr) || !cJSON_IsNumber(value) ||
        addr->valueint < 0 || addr->valueint > 0xFFFF ||
        value->valueint < 0 || value->valueint > 0xFFFF)
      goto fail;
    addrs[i] = (uint16)addr->valueint;
    values[i] = (uint16)value->valueint;
  }
  *out_count = count;
  *out_addrs = addrs;
  *out_values = values;
  return true;
fail:
  free(addrs);
  free(values);
  return false;
}

static bool ParseOverworldPatches(cJSON *root) {
  cJSON *object = cJSON_GetObjectItemCaseSensitive(root, "overworld_patches");
  if (!cJSON_IsObject(object))
    return true;
  cJSON *screens = cJSON_GetObjectItemCaseSensitive(object, "screens");
  if (!cJSON_IsArray(screens))
    return false;
  int count = cJSON_GetArraySize(screens);
  if (count > 128)
    return false;
  if (count == 0)
    return true;
  ModSeedOverworldPatch *patches = calloc((size_t)count, sizeof(*patches));
  if (patches == NULL)
    return false;
  int used = 0;
  for (int i = 0; i < count; i++) {
    cJSON *entry = cJSON_GetArrayItem(screens, i);
    if (!cJSON_IsObject(entry))
      goto fail;
    cJSON *screen = cJSON_GetObjectItemCaseSensitive(entry, "screen");
    if (!cJSON_IsNumber(screen) || screen->valueint < 0 || screen->valueint > 0x7F)
      goto fail;
    ModSeedOverworldPatch *patch = &patches[used];
    patch->screen = (uint16)screen->valueint;
    if (!ParseWriteList(entry, "writes", &patch->write_count,
                        &patch->addrs, &patch->values))
      goto fail;
    if (!ParseWriteList(entry, "hole", &patch->hole_count,
                        &patch->hole_addrs, &patch->hole_values))
      goto fail;
    if (!ParseWriteList(entry, "no_hole", &patch->no_hole_count,
                        &patch->no_hole_addrs, &patch->no_hole_values))
      goto fail;
    cJSON *warp = cJSON_GetObjectItemCaseSensitive(entry, "warp");
    if (cJSON_IsObject(warp)) {
      cJSON *addr = cJSON_GetObjectItemCaseSensitive(warp, "addr");
      cJSON *low = cJSON_GetObjectItemCaseSensitive(warp, "low");
      cJSON *high = cJSON_GetObjectItemCaseSensitive(warp, "high");
      if (!cJSON_IsNumber(addr) || !cJSON_IsNumber(low) || !cJSON_IsNumber(high))
        goto fail;
      patch->has_warp = true;
      patch->warp_addr = (uint16)addr->valueint;
      patch->warp_low = (uint16)low->valueint;
      patch->warp_high = (uint16)high->valueint;
    }
    used++;
  }
  g_overworld_patches = patches;
  g_overworld_patch_count = used;
  return true;
fail:
  for (int i = 0; i < used; i++) {
    free(patches[i].addrs);
    free(patches[i].values);
    free(patches[i].hole_addrs);
    free(patches[i].hole_values);
    free(patches[i].no_hole_addrs);
    free(patches[i].no_hole_values);
  }
  free(patches);
  return false;
}

static bool ParseMirrorBonk(cJSON *root, uint16 out[23][4], int *out_count) {
  cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "mirror_bonk");
  if (array == NULL)
    return true;
  if (!cJSON_IsArray(array))
    return false;
  int count = cJSON_GetArraySize(array);
  if (count > 23)
    return false;
  for (int i = 0; i < count; i++) {
    cJSON *rect = cJSON_GetArrayItem(array, i);
    if (!cJSON_IsArray(rect) || cJSON_GetArraySize(rect) != 4)
      return false;
    for (int j = 0; j < 4; j++) {
      cJSON *value = cJSON_GetArrayItem(rect, j);
      if (!cJSON_IsNumber(value) || value->valueint < 0 ||
          value->valueint > 0xFFFF)
        return false;
      out[i][j] = (uint16)value->valueint;
    }
  }
  *out_count = count;
  return true;
}

static bool AppendChest(SeedChest **chests, int *count, int *capacity,
                        uint16 room, int index, int item) {
  if (*count == *capacity) {
    int new_capacity = *capacity ? *capacity * 2 : 64;
    SeedChest *new_chests = realloc(*chests, sizeof(**chests) * new_capacity);
    if (new_chests == NULL)
      return false;
    *chests = new_chests;
    *capacity = new_capacity;
  }
  SeedChest *chest = &(*chests)[(*count)++];
  chest->room = room;
  chest->index = (uint8)index;
  chest->item = (uint8)item;
  return true;
}

static bool AppendFixed(SeedFixed **fixed, int *count, int *capacity,
                        const char *id, int item) {
  if (*count == *capacity) {
    int new_capacity = *capacity ? *capacity * 2 : 64;
    SeedFixed *new_fixed = realloc(*fixed, sizeof(**fixed) * new_capacity);
    if (new_fixed == NULL)
      return false;
    *fixed = new_fixed;
    *capacity = new_capacity;
  }
  char *copy = StrDup(id);
  if (copy == NULL)
    return false;
  SeedFixed *entry = &(*fixed)[(*count)++];
  entry->id = copy;
  entry->item = (uint8)item;
  return true;
}

static int AppendShop(SeedShopGroup *groups, int *count, int id, int room,
                      int door, int any_door, int slot, int item, int price) {
  int group = -1;
  for (int i = 0; i < *count; i++) {
    if (groups[i].id == id) {
      group = i;
      break;
    }
  }
  if (group < 0) {
    if (*count >= 32)
      return -1;
    group = (*count)++;
    memset(&groups[group], 0, sizeof(groups[group]));
    groups[group].id = id;
    groups[group].room = room;
    groups[group].door = door;
    groups[group].any_door = any_door != 0;
  }
  if (slot < 0 || slot >= 3)
    return -1;
  groups[group].entries[slot].item = (uint8)item;
  groups[group].entries[slot].price = (uint16)price;
  groups[group].entries[slot].count = 1;
  if (slot >= groups[group].count)
    groups[group].count = slot + 1;
  return 0;
}

static bool HexNibble(char c, int *out) {
  if (c >= '0' && c <= '9') {
    *out = c - '0';
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *out = c - 'a' + 10;
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *out = c - 'A' + 10;
    return true;
  }
  return false;
}

static bool DecodeHex(const char *text, uint8 *out, int size) {
  int pos = 0;
  while (*text) {
    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t')
      text++;
    if (!*text)
      break;
    int hi, lo;
    if (!HexNibble(text[0], &hi) || !HexNibble(text[1], &lo) || pos >= size)
      return false;
    out[pos++] = (uint8)(hi * 16 + lo);
    text += 2;
  }
  return pos == size;
}

static bool LoadJson(cJSON *root, const char *owner_id,
                     char *err, size_t err_size) {
  cJSON *format = cJSON_GetObjectItemCaseSensitive(root, "format");
  if (!cJSON_IsNumber(format) || format->valueint != 1) {
    SetError(err, err_size, "seed_data: unsupported format (expected 1)");
    return false;
  }

  cJSON *locations = cJSON_GetObjectItemCaseSensitive(root, "locations");
  if (!cJSON_IsArray(locations)) {
    SetError(err, err_size, "seed_data: 'locations' must be an array");
    return false;
  }

  SeedChest *chests = NULL;
  int chest_count = 0, chest_capacity = 0;
  SeedFixed *fixed = NULL;
  int fixed_count = 0, fixed_capacity = 0;
  SeedShopGroup shops[32];
  int shop_count = 0;
  int uncle_item = -1;
  uint8 save_template[0x500];
  bool has_save_template = false;
  ModSeedZ3rSettings z3r;
  ModSeedStartingPoints start_points;
  bool has_start_points;
  uint8 ow_bg_main_palette[420];
  uint8 ow_bg_aux12_palette[840];
  uint8 tr_tile_attributes[512];
  uint16 mirror_bonk[23][4];
  int mirror_bonk_count = 0;
  uint16 hc_sign_left = 0, hc_sign_right = 0;
  bool has_hc_sign_text = false;
  uint8 npc_items[4] = { 0 };
  bool has_npc_items[4] = { false };
  bool has_ow_bg_main_palette = false;
  bool has_ow_bg_aux12_palette = false;
  bool has_tr_tile_attributes = false;
  bool has_mirror_bonk;
  InitZ3rSettings(&z3r);
  ParseZ3rSettings(root, &z3r);
  memset(&start_points, 0, sizeof(start_points));
  has_start_points = ParseStartingPoints(root, &start_points);
  cJSON *palettes = cJSON_GetObjectItemCaseSensitive(root, "overworld_palettes");
  if (cJSON_IsObject(palettes)) {
    const char *bg_main = GetString(palettes, "bg_main");
    const char *bg_aux12 = GetString(palettes, "bg_aux12");
    if (bg_main != NULL &&
        !DecodeHex(bg_main, ow_bg_main_palette, sizeof(ow_bg_main_palette))) {
      SetError(err, err_size, "seed_data: overworld_palettes.bg_main must be 0x1A4 hex bytes");
      goto fail;
    }
    if (bg_aux12 != NULL &&
        !DecodeHex(bg_aux12, ow_bg_aux12_palette, sizeof(ow_bg_aux12_palette))) {
      SetError(err, err_size, "seed_data: overworld_palettes.bg_aux12 must be 0x348 hex bytes");
      goto fail;
    }
    has_ow_bg_main_palette = bg_main != NULL;
    has_ow_bg_aux12_palette = bg_aux12 != NULL;
  }
  cJSON *hc_sign = cJSON_GetObjectItemCaseSensitive(root, "hc_sign_text");
  if (cJSON_IsObject(hc_sign)) {
    int left, right;
    if (GetInt(hc_sign, "hc_left", 0, 0xFFFF, &left) &&
        GetInt(hc_sign, "hc_right", 0, 0xFFFF, &right)) {
      hc_sign_left = (uint16)left;
      hc_sign_right = (uint16)right;
      has_hc_sign_text = true;
    }
  }
  cJSON *npc = cJSON_GetObjectItemCaseSensitive(root, "npc_items");
  if (cJSON_IsObject(npc)) {
    static const char *const kKeys[4] = {
      "old_man", "sick_kid", "sahasrahla", "catfish"
    };
    for (int i = 0; i < 4; i++) {
      int value;
      if (GetInt(npc, kKeys[i], 0, 0xFF, &value)) {
        npc_items[i] = (uint8)value;
        has_npc_items[i] = true;
      }
    }
  }
  const char *tr_hex = GetString(root, "tr_tile_attributes");
  if (tr_hex != NULL) {
    if (!DecodeHex(tr_hex, tr_tile_attributes, sizeof(tr_tile_attributes))) {
      SetError(err, err_size, "seed_data: tr_tile_attributes must be 0x200 hex bytes");
      goto fail;
    }
    has_tr_tile_attributes = true;
  }
  memset(mirror_bonk, 0, sizeof(mirror_bonk));
  has_mirror_bonk = ParseMirrorBonk(root, mirror_bonk, &mirror_bonk_count);
  if (!has_mirror_bonk) {
    SetError(err, err_size, "seed_data: invalid mirror_bonk");
    goto fail;
  }

  const char *template_hex = GetString(root, "save_template");
  if (template_hex != NULL) {
    if (!DecodeHex(template_hex, save_template, sizeof(save_template))) {
      SetError(err, err_size, "seed_data: save_template must be 0x500 hex bytes");
      goto fail;
    }
    has_save_template = true;
  }

  cJSON *location = NULL;
  int location_index = 0;
  cJSON_ArrayForEach(location, locations) {
    if (!cJSON_IsObject(location)) {
      SetError(err, err_size, "seed_data: locations[%d] is not an object", location_index);
      goto fail;
    }
    const char *type = GetString(location, "type");
    if (type == NULL) {
      SetError(err, err_size, "seed_data: locations[%d] has no string 'type'", location_index);
      goto fail;
    }

    if (strcmp(type, "chest") == 0) {
      int room, index, item;
      if (!GetInt(location, "room", 0, 0xFFFF, &room) ||
          !GetInt(location, "index", 0, 0xFF, &index) ||
          !GetInt(location, "item", 0, 0xFF, &item)) {
        SetError(err, err_size,
                 "seed_data: locations[%d] chest needs room, index and item",
                 location_index);
        goto fail;
      }
      if (!AppendChest(&chests, &chest_count, &chest_capacity, (uint16)room, index, item)) {
        SetError(err, err_size, "seed_data: out of memory");
        goto fail;
      }
    } else if (strcmp(type, "uncle") == 0) {
      int item;
      if (!GetInt(location, "item", 0, 0xFF, &item)) {
        SetError(err, err_size, "seed_data: uncle needs item 0..255");
        goto fail;
      }
      uncle_item = item;
    } else if (strcmp(type, "fixed") == 0 || strcmp(type, "prize") == 0) {
      const char *id = GetString(location, "id");
      int item;
      if (id == NULL || !GetInt(location, "item", 0, 0xFF, &item)) {
        SetError(err, err_size, "seed_data: locations[%d] fixed/prize needs id and item",
                 location_index);
        goto fail;
      }
      if (!AppendFixed(&fixed, &fixed_count, &fixed_capacity, id, item)) {
        SetError(err, err_size, "seed_data: out of memory");
        goto fail;
      }
    } else if (strcmp(type, "shop") == 0) {
      int shop, slot, room, item, price, door = 0, any_door = 1;
      if (!GetInt(location, "shop", 0, 0xFF, &shop) ||
          !GetInt(location, "slot", 0, 2, &slot) ||
          !GetInt(location, "room", 0, 0xFFFF, &room) ||
          !GetInt(location, "item", 0, 0xFF, &item) ||
          !GetInt(location, "price", 0, 999, &price)) {
        SetError(err, err_size,
                 "seed_data: locations[%d] shop needs shop, slot, room, item and price",
                 location_index);
        goto fail;
      }
      GetInt(location, "door", 0, 0xFF, &door);
      GetInt(location, "any_door", 0, 1, &any_door);
      if (AppendShop(shops, &shop_count, shop, room, door, any_door,
                     slot, item, price) < 0) {
        SetError(err, err_size, "seed_data: invalid shop entry at locations[%d]",
                 location_index);
        goto fail;
      }
    }
    location_index++;
  }

  if (chest_count == 0 && fixed_count == 0 && uncle_item < 0 &&
      shop_count == 0 && !cJSON_IsObject(
          cJSON_GetObjectItemCaseSensitive(root, "z3r_settings"))) {
    SetError(err, err_size, "seed_data: no locations");
    goto fail;
  }

  uint8 map_key_totals[13] = { 0 };
  for (int i = 0; i < chest_count; i++) {
    uint8 item = chests[i].item;
    if (item >= 0xA0 && item <= 0xAF) {
      int dungeon = kSmallKeyDungeon[item - 0xA0];
      if (dungeon >= 0 && dungeon < 13)
        map_key_totals[dungeon]++;
    }
  }

  ModSeedImport_Reset();
  g_chests = chests;
  g_chest_count = chest_count;
  g_fixed = fixed;
  g_fixed_count = fixed_count;
  g_uncle_item = uncle_item;
  g_z3r = z3r;
  memcpy(g_map_key_totals, map_key_totals, sizeof(g_map_key_totals));
  if (has_save_template) {
    memcpy(g_save_template, save_template, sizeof(g_save_template));
    g_has_save_template = true;
  }
  if (has_start_points) {
    g_start_points = start_points;
    g_has_start_points = true;
  }
  if (has_ow_bg_main_palette) {
    memcpy(g_ow_bg_main_palette, ow_bg_main_palette,
           sizeof(g_ow_bg_main_palette));
    g_has_ow_bg_main_palette = true;
  }
  if (has_ow_bg_aux12_palette) {
    memcpy(g_ow_bg_aux12_palette, ow_bg_aux12_palette,
           sizeof(g_ow_bg_aux12_palette));
    g_has_ow_bg_aux12_palette = true;
  }
  if (has_tr_tile_attributes) {
    memcpy(g_tr_tile_attributes, tr_tile_attributes,
           sizeof(g_tr_tile_attributes));
    g_has_tr_tile_attributes = true;
  }
  if (has_hc_sign_text) {
    g_hc_sign_left = hc_sign_left;
    g_hc_sign_right = hc_sign_right;
    g_has_hc_sign_text = true;
  }
  for (int i = 0; i < 4; i++) {
    if (has_npc_items[i]) {
      g_npc_items[i] = npc_items[i];
      g_has_npc_items[i] = true;
    }
  }
  if (mirror_bonk_count > 0) {
    memcpy(g_mirror_bonk, mirror_bonk, sizeof(g_mirror_bonk));
    g_mirror_bonk_count = mirror_bonk_count;
  }
  if (!ParseOverworldPatches(root)) {
    SetError(err, err_size, "seed_data: invalid overworld_patches");
    ModSeedImport_Reset();
    return false;
  }
  g_active = true;

  // Audit event for the test bench. It is emitted only while a trace
  // subscriber exists and makes the forged z3r_settings observable.
  MOD_EMIT(kModEv_SeedSetting, 0, g_z3r.fake_boots, 0);
  MOD_EMIT(kModEv_SeedSetting, 1, g_z3r.quick_swap, 0);
  MOD_EMIT(kModEv_SeedSetting, 2, g_z3r.generic_keys, 0);
  MOD_EMIT(kModEv_SeedSetting, 3, g_z3r.item_counter_hud, 0);
  MOD_EMIT(kModEv_SeedSetting, 4, g_z3r.map_hud_mode, 0);
  MOD_EMIT(kModEv_SeedSetting, 5, g_z3r.no_bgm, 0);
  MOD_EMIT(kModEv_SeedSetting, 6, g_z3r.fast_fanfare, 0);

  for (int i = 0; i < shop_count; i++) {
    const SeedShopGroup *group = &shops[i];
    if (!ModShop_SetForDoor(&g_shop_owner, group->room & 0xFF, group->door,
                            group->any_door, group->entries, group->count,
                            err, err_size)) {
      ModSeedImport_Reset();
      return false;
    }
  }

  fprintf(stderr, "mods: seed '%s': %d chest(s), %d fixed/prize, %d shop(s), "
          "uncle %s, save template %s\n",
          owner_id, chest_count, fixed_count, shop_count,
          uncle_item >= 0 ? "loaded" : "not present",
          has_save_template ? "loaded" : "not present");
  return true;

fail:
  free(chests);
  for (int i = 0; i < fixed_count; i++)
    free(fixed[i].id);
  free(fixed);
  return false;
}

bool ModSeedImport_LoadFile(const char *path, const char *owner_id,
                            char *err, size_t err_size) {
  size_t length = 0;
  uint8 *data = ReadWholeFile(path, &length);
  if (data == NULL) {
    SetError(err, err_size, "seed_data: unable to read '%s'", path);
    return false;
  }
  cJSON *root = cJSON_ParseWithOpts((const char *)data, NULL, 1);
  free(data);
  if (root == NULL) {
    SetError(err, err_size, "seed_data: invalid JSON");
    return false;
  }
  bool ok = cJSON_IsObject(root) && LoadJson(root, owner_id, err, err_size);
  if (cJSON_IsObject(root) == 0)
    SetError(err, err_size, "seed_data: root must be an object");
  cJSON_Delete(root);
  return ok;
}

uint8 ModSeedImport_ChestItem(uint16 room, int chest_index, uint8 fallback) {
  if (!g_active)
    return fallback;
  for (int i = 0; i < g_chest_count; i++) {
    if (g_chests[i].room == room && g_chests[i].index == (uint8)chest_index) {
      uint8 item = g_chests[i].item;
      return item <= 0x4B ? item : ModSeedImport_MapItem(item);
    }
  }
  return fallback;
}

uint8 ModSeedImport_UncleItem(uint8 fallback) {
  if (!g_active || g_uncle_item < 0)
    return fallback;
  return (uint8)g_uncle_item;
}

uint8 ModSeedImport_FixedItem(const char *id, uint8 fallback) {
  if (!g_active || id == NULL)
    return fallback;
  for (int i = 0; i < g_fixed_count; i++) {
    if (strcmp(g_fixed[i].id, id) == 0)
      return g_fixed[i].item;
  }
  return fallback;
}

uint8 ModSeedImport_HeartPieceItem(int indoors, uint16 room, uint8 screen,
                                   uint16 link_x, uint8 fallback) {
  if (!g_active)
    return fallback;
  if (indoors) {
    switch (room) {
    case 225: return ModSeedImport_FixedItem(
        "fixed:HeartPieceIndoorValues:HeartPiece_Forest_Thieves", fallback);
    case 226: return ModSeedImport_FixedItem(
        "fixed:HeartPieceIndoorValues:HeartPiece_Lumberjack_Tree", fallback);
    case 234: return ModSeedImport_FixedItem(
        "fixed:HeartPieceIndoorValues:HeartPiece_Spectacle_Cave", fallback);
    case 283:
      return ModSeedImport_FixedItem((link_x >> 8) & 1 ?
          "fixed:HeartPieceIndoorValues:HeartPiece_Graveyard_Warp" :
          "fixed:HeartPieceIndoorValues:HeartPiece_Circle_Bushes", fallback);
    case 294: return ModSeedImport_FixedItem(
        "fixed:HeartPieceIndoorValues:HeartPiece_Mire_Warp", fallback);
    case 295: return ModSeedImport_FixedItem(
        "fixed:HeartPieceIndoorValues:HeartPiece_Smith_Pegs", fallback);
    case 135: return ModSeedImport_FixedItem(
        "fixed:KeyItemValues:StandingKey_Hera", fallback);
    }
  } else {
    switch (screen) {
    case 0x03:
      return ModSeedImport_FixedItem(link_x >= 1890 ?
          "fixed:HeartPieceOutdoorValues:HeartPiece_Spectacle" :
          "fixed:SpriteItemValues:EtherItem", fallback);
    case 0x05: return ModSeedImport_FixedItem(
        "fixed:HeartPieceOutdoorValues:HeartPiece_Mountain_Warp", fallback);
    case 0x28: return ModSeedImport_FixedItem(
        "fixed:HeartPieceOutdoorValues:HeartPiece_Maze", fallback);
    case 0x2A: return ModSeedImport_FixedItem(
        "fixed:HeartPieceOutdoorValues:HauntedGroveItem", fallback);
    case 0x30:
      return ModSeedImport_FixedItem(link_x < 512 ?
          "fixed:HeartPieceOutdoorValues:HeartPiece_Desert" :
          "fixed:SpriteItemValues:BombosItem", fallback);
    case 0x35: return ModSeedImport_FixedItem(
        "fixed:HeartPieceOutdoorValues:HeartPiece_Lake", fallback);
    case 0x3B: return ModSeedImport_FixedItem(
        "fixed:HeartPieceOutdoorValues:HeartPiece_Swamp", fallback);
    case 0x42:
    case 0x4A: return ModSeedImport_FixedItem(
        "fixed:HeartPieceOutdoorValues:HeartPiece_Cliffside", fallback);
    case 0x5B: return ModSeedImport_FixedItem(
        "fixed:HeartPieceOutdoorValues:HeartPiece_Pyramid", fallback);
    case 0x68: return ModSeedImport_FixedItem(
        "fixed:HeartPieceOutdoorValues:HeartPiece_Digging", fallback);
    case 0x81: return ModSeedImport_FixedItem(
        "fixed:HeartPieceOutdoorValues:HeartPiece_Zora", fallback);
    }
  }
  return fallback;
}

uint8 ModSeedImport_BossItem(uint16 room, uint8 fallback) {
  if (!g_active)
    return fallback;
  const char *field = NULL;
  switch (room) {
  case 200: field = "HeartContainer_ArmosKnights"; break;
  case 51: field = "HeartContainer_Lanmolas"; break;
  case 7: field = "HeartContainer_Moldorm"; break;
  case 90: field = "HeartContainer_HelmasaurKing"; break;
  case 6: field = "HeartContainer_Arrghus"; break;
  case 41: field = "HeartContainer_Mothula"; break;
  case 172: field = "HeartContainer_Blind"; break;
  case 222: field = "HeartContainer_Kholdstare"; break;
  case 144: field = "HeartContainer_Vitreous"; break;
  case 164: field = "HeartContainer_Trinexx"; break;
  }
  if (field == NULL)
    return fallback;
  char id[96];
  snprintf(id, sizeof(id), "fixed:HeartContainerBossValues:%s", field);
  return ModSeedImport_FixedItem(id, fallback);
}

uint8 ModSeedImport_PrizeItem(int dungeon_index, uint8 fallback) {
  if (!g_active || dungeon_index < 0 || dungeon_index >= 13)
    return fallback;
  char id[64];
  snprintf(id, sizeof(id), "prize:%s", kDungeonNames[dungeon_index]);
  return ModSeedImport_FixedItem(id, fallback);
}

uint8 ModSeedImport_BonkKeyItem(uint16 room, uint8 fallback) {
  if (!g_active)
    return fallback;
  if (room == 115)
    return ModSeedImport_FixedItem("fixed:KeyItemValues:BonkKey_Desert", fallback);
  if (room == 140)
    return ModSeedImport_FixedItem("fixed:KeyItemValues:BonkKey_GTower", fallback);
  return fallback;
}

uint8 ModSeedImport_StartingDoor(int index, uint8 fallback) {
  if (!g_active || index < 0 || index >= 7)
    return fallback;
  return g_z3r.starting_area_overworld_door[index];
}

static void CopyAsset(int index, const void *src, size_t size) {
  if (index < 0 || index >= kNumberOfAssets || g_asset_sizes[index] < size)
    return;
  memcpy((void*)g_asset_ptrs[index], src, size);
}

void ModSeedImport_ApplyStartingPoints(void) {
  if (!g_active || !g_has_start_points || g_z3r.inverted == 0)
    return;
  CopyAsset(28, g_start_points.rooms, sizeof(g_start_points.rooms));
  CopyAsset(29, g_start_points.relative_coords,
            sizeof(g_start_points.relative_coords));
  CopyAsset(30, g_start_points.scroll_x, sizeof(g_start_points.scroll_x));
  CopyAsset(31, g_start_points.scroll_y, sizeof(g_start_points.scroll_y));
  CopyAsset(32, g_start_points.player_x, sizeof(g_start_points.player_x));
  CopyAsset(33, g_start_points.player_y, sizeof(g_start_points.player_y));
  CopyAsset(34, g_start_points.camera_x, sizeof(g_start_points.camera_x));
  CopyAsset(35, g_start_points.camera_y, sizeof(g_start_points.camera_y));
  CopyAsset(36, g_start_points.blockset, sizeof(g_start_points.blockset));
  CopyAsset(37, g_start_points.floor, sizeof(g_start_points.floor));
  CopyAsset(38, g_start_points.palace, sizeof(g_start_points.palace));
  CopyAsset(40, g_start_points.starting_bg, sizeof(g_start_points.starting_bg));
  CopyAsset(41, g_start_points.quadrant1, sizeof(g_start_points.quadrant1));
  CopyAsset(42, g_start_points.quadrant2, sizeof(g_start_points.quadrant2));
  CopyAsset(43, g_start_points.door_settings,
            sizeof(g_start_points.door_settings));
}

void ModSeedImport_ApplyPalettes(void) {
  if (!g_active || g_z3r.inverted == 0)
    return;
  if (g_has_ow_bg_main_palette)
    CopyAsset(87, g_ow_bg_main_palette, sizeof(g_ow_bg_main_palette));
  if (g_has_ow_bg_aux12_palette)
    CopyAsset(88, g_ow_bg_aux12_palette, sizeof(g_ow_bg_aux12_palette));
}

void ModSeedImport_ApplySignText(void) {
  if (!g_active || g_z3r.inverted == 0 || !g_has_hc_sign_text)
    return;
  // $87:F52D/$F52F are word indexes 8/9 of the overworld sign table.
  kOverworld_SignText[8] = g_hc_sign_left;
  kOverworld_SignText[9] = g_hc_sign_right;
}

uint8 ModSeedImport_TileAttribute(uint16 index, uint8 fallback) {
  if (!g_active || !g_has_tr_tile_attributes || index >= 512)
    return fallback;
  return g_tr_tile_attributes[index];
}

bool ModSeedImport_MirrorBonk(uint16 x, uint16 y) {
  if (!g_active || g_z3r.inverted == 0)
    return false;
  for (int i = 0; i < g_mirror_bonk_count; i++) {
    const uint16 *r = g_mirror_bonk[i];
    if (x > r[0] && x <= r[1] && y > r[2] && y <= r[3])
      return true;
  }
  return false;
}

bool ModSeedImport_ShouldStartAtExit(int choice) {
  if (!g_active || choice < 0 || choice >= 3)
    return false;
  return g_z3r.should_start_at_exit[choice] != 0;
}

uint8 ModSeedImport_NpcItem(int npc, uint8 fallback) {
  if (!g_active || npc < 0 || npc >= 4 || !g_has_npc_items[npc])
    return fallback;
  return g_npc_items[npc];
}

bool ModSeedImport_ExtraHole(uint16 pos, uint16 area, uint8 *entrance) {
  if (!g_active)
    return false;
  for (int i = 0; i < 16; i++) {
    if (g_z3r.extra_hole_map16[i] == 0xffff)
      continue;
    if (g_z3r.extra_hole_map16[i] == pos &&
        g_z3r.extra_hole_area[i] == area) {
      if (entrance != NULL)
        *entrance = g_z3r.extra_hole_entrance[i];
      return true;
    }
  }
  return false;
}

static void ApplyWrites(const uint16 *addrs, const uint16 *values, int count) {
  for (int i = 0; i < count; i++) {
    uint16 addr = addrs[i];
    if (addr + 1 >= 0x20000)
      continue;
    g_ram[addr] = (uint8)values[i];
    g_ram[addr + 1] = (uint8)(values[i] >> 8);
  }
}

void ModSeedImport_ApplyOverworldPatches(uint16 screen) {
  if (!g_active || g_overworld_patch_count == 0)
    return;
  for (int i = 0; i < g_overworld_patch_count; i++) {
    const ModSeedOverworldPatch *patch = &g_overworld_patches[i];
    if (patch->screen != screen)
      continue;
    ApplyWrites(patch->addrs, patch->values, patch->write_count);
    if (patch->hole_count != 0 &&
        (save_ow_event_info[0x5B] & 0x20) == 0)
      ApplyWrites(patch->hole_addrs, patch->hole_values, patch->hole_count);
    ApplyWrites(patch->no_hole_addrs, patch->no_hole_values,
                patch->no_hole_count);
    if (patch->has_warp) {
      uint16 value = sram_progress_indicator < 3 ?
        patch->warp_low : patch->warp_high;
      if (patch->warp_addr + 1 < 0x20000) {
        g_ram[patch->warp_addr] = (uint8)value;
        g_ram[patch->warp_addr + 1] = (uint8)(value >> 8);
      }
    }
    return;
  }
}

bool ModSeedImport_InitNewSave(uint8 *sram, size_t size) {
  if (!g_active || !g_has_save_template || size < 0x500)
    return false;
  memcpy(sram, g_save_template, 0x3D8);
  memcpy(sram + 0x3E3, g_save_template + 0x3E3, 0x10C);
  // z3r stores raw bomb/arrow capacities at $F370/$F371, but the US engine
  // uses upgrade level indexes there.
  static const uint8 kBombCap[8] = {10, 15, 20, 25, 30, 35, 40, 50};
  static const uint8 kArrowCap[8] = {30, 35, 40, 45, 50, 55, 60, 70};
  int bombs = 0, arrows = 0;
  while (bombs < 7 && sram[0x370] >= kBombCap[bombs + 1])
    bombs++;
  while (arrows < 7 && sram[0x371] >= kArrowCap[arrows + 1])
    arrows++;
  sram[0x370] = (uint8)bombs;
  sram[0x371] = (uint8)arrows;
  // The z3r template's instant post-aga marker is resolved by the caller.
  if (sram[0x3C5] & 0x80) {
    sram[0x3C5] = 0;
    sram[0x282] = 0;
  }
  return true;
}

void ModSeedImport_SetReceiptDungeon(int dungeon) {
  g_receipt_dungeon = dungeon;
}

int ModSeedImport_TakeReceiptDungeon(void) {
  int dungeon = g_receipt_dungeon;
  g_receipt_dungeon = -1;
  return dungeon;
}

void ModSeedImport_SetSpecialItem(uint8 item) {
  g_special_item = item;
}

uint8 ModSeedImport_TakeSpecialItem(void) {
  uint8 item = g_special_item;
  g_special_item = 0;
  return item;
}

void ModSeedImport_SetPrizeItem(uint8 item) {
  g_prize_item = item;
}

uint8 ModSeedImport_TakePrizeItem(void) {
  uint8 item = g_prize_item;
  g_prize_item = 0;
  return item;
}

static void AddBombCapacity(int levels) {
  int level = link_bomb_upgrades + levels;
  if (level > 7)
    level = 7;
  link_bomb_upgrades = (uint8)level;
  link_bomb_filler = (uint8)(levels == 1 ? g_z3r.upgrade_5_bombs_refill
                                         : g_z3r.upgrade_10_bombs_refill);
}

static void AddArrowCapacity(int levels) {
  int level = link_arrow_upgrades + levels;
  if (level > 7)
    level = 7;
  link_arrow_upgrades = (uint8)level;
  link_arrow_filler = (uint8)(levels == 1 ? g_z3r.upgrade_5_arrows_refill
                                          : g_z3r.upgrade_10_arrows_refill);
}

void ModSeedImport_ApplySpecialItem(uint8 item) {
  switch (item) {
  case 0x4C:
    link_bomb_upgrades = 7;
    link_bomb_filler = 50;
    break;
  case 0x4D:
    link_arrow_upgrades = 7;
    link_arrow_filler = 70;
    break;
  case 0x4E:
    if (link_magic_consumption < 2)
      link_magic_consumption++;
    link_magic_filler = 0x80;
    break;
  case 0x4F:
    link_magic_consumption = 2;
    link_magic_filler = 0x80;
    break;
  case 0x51: AddBombCapacity(1); break;
  case 0x52: AddBombCapacity(2); break;
  case 0x53: AddArrowCapacity(1); break;
  case 0x54: AddArrowCapacity(2); break;
  case 0x59: {
    uint16 rupees = link_rupees_actual;
    int deduction = g_z3r.rupoor_deduction;
    link_rupees_actual = rupees > deduction ? rupees - deduction : 0;
    break;
  }
  default:
    break;
  }
}

static uint8 ProgressiveSword(void) {
  if (link_sword_type == 0 || link_sword_type == 0xFF)
    return 0x49;
  if (link_sword_type >= g_z3r.progressive_sword_limit)
    return (uint8)g_z3r.progressive_sword_replacement;
  return (uint8)link_sword_type;
}

static uint8 ProgressiveShield(void) {
  if (link_shield_type == 0)
    return 0x04;
  if (link_shield_type >= g_z3r.progressive_shield_limit)
    return (uint8)g_z3r.progressive_shield_replacement;
  return (uint8)(0x04 + link_shield_type);
}

static uint8 ProgressiveArmor(void) {
  if (link_armor == 0)
    return 0x22;
  if (link_armor >= g_z3r.progressive_armor_limit)
    return (uint8)g_z3r.progressive_armor_replacement;
  return (uint8)(0x22 + link_armor);
}

static uint8 ProgressiveGlove(void) {
  if (link_item_gloves == 0)
    return 0x1B;
  if (link_item_gloves == 1)
    return 0x1C;
  return 0x47;
}

static uint8 ProgressiveBow(void) {
  if (link_item_bow == 0)
    return 0x0B;
  // z3r compares (BowEquipment + 1) / 2 with ProgressiveBowLimit; values
  // 1/2 are the normal bow tier and 3/4 are the silver tier.
  if (((link_item_bow + 1) >> 1) >= g_z3r.progressive_bow_limit)
    return (uint8)g_z3r.progressive_bow_replacement;
  return 0x3B;
}

uint8 ModSeedImport_MapItem(uint8 item) {
  if (!g_active || item <= 0x4B)
    return item;

  if (ModSeedImport_Z3rRulesActive() && (item == 0x6B || item == 0x6C)) {
    // z3r goalitem.asm: Power Star / Triforce Piece increment GoalCounter.
    if (WORD(g_ram[0xF418]) != 0xFFFF)
      WORD(g_ram[0xF418])++;
    return 0x47;
  }

  if (item >= 0x5E && item <= 0x65) {
    switch (item) {
    case 0x5E: return ProgressiveSword();
    case 0x5F: return ProgressiveShield();
    case 0x60: return ProgressiveArmor();
    case 0x61: return ProgressiveGlove();
    case 0x64:
    case 0x65: return ProgressiveBow();
    default: return 0x47;
    }
  }

  if (item >= 0x70 && item <= 0x7F) {
    int dungeon = kMapDungeon[item - 0x70];
    if (dungeon >= 0)
      ModSeedImport_SetReceiptDungeon(dungeon);
    return 0x33;
  }
  if (item >= 0x80 && item <= 0x8F) {
    int dungeon = kCompassDungeon[item - 0x80];
    if (dungeon >= 0)
      ModSeedImport_SetReceiptDungeon(dungeon);
    return 0x25;
  }
  if (item >= 0x92 && item <= 0x9F) {
    int dungeon = kBigKeyDungeon[item - 0x92];
    if (dungeon >= 0)
      ModSeedImport_SetReceiptDungeon(dungeon);
    return 0x32;
  }
  if (item >= 0xA0 && item <= 0xAF) {
    int dungeon = kSmallKeyDungeon[item - 0xA0];
    if (dungeon >= 0 && !ModSeedImport_GenericKeysActive())
      ModSeedImport_SetReceiptDungeon(dungeon);
    return 0x24;
  }

  if (ModSeedImport_Z3rRulesActive()) {
    switch (item) {
    case 0x4C:
    case 0x51:
    case 0x52:
      ModSeedImport_SetSpecialItem(item);
      return 0x31;
    case 0x4D:
    case 0x53:
    case 0x54:
      ModSeedImport_SetSpecialItem(item);
      return 0x44;
    case 0x4E:
    case 0x4F:
      ModSeedImport_SetSpecialItem(item);
      return 0x45;
    case 0x59:
      // z3r maps the Rupoor receipt through the generic 0x24 receipt and
      // applies the deduction itself; 0x34 would add one rupee back.
      ModSeedImport_SetSpecialItem(item);
      return 0x24;
    case 0x5A:
      return 0x47;
    case 0x5B:
    case 0x5C:
    case 0x5D:
      return 0x47;
    case 0x62:
    case 0x63:
      return 0x47;
    case 0x90:
      return 0x24;
    case 0x91:
      return 0x47;
    default:
      break;
    }
  }

  switch (item) {
  case 0x4C:
  case 0x51:
  case 0x52: return 0x31;
  case 0x4D:
  case 0x53:
  case 0x54: return 0x44;
  case 0x4E:
  case 0x4F: return 0x45;
  case 0x50: return 0x01;
  case 0x58: return 0x3B;
  case 0x59: return 0x34;
  case 0x5A: return 0x47;
  case 0x5B:
  case 0x5C:
  case 0x5D: return 0x47;
  case 0x62:
  case 0x63: return 0x47;
  case 0x66: case 0x67: case 0x68: case 0x69:
  case 0x6A: case 0x6B: case 0x6C: case 0x6D:
  case 0x6E: case 0x6F: return 0x47;
  case 0x90: return 0x24;
  case 0x91: return 0x47;
  default: return 0x47;
  }
}
