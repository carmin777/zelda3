#include "seed_import.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../third_party/cjson/cJSON.h"
#include "../features.h"
#include "../util.h"
#include "../variables.h"
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
  int id;
  int room;
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
  g_receipt_dungeon = -1;
  g_special_item = 0;
  g_prize_item = 0;
  g_active = false;
  ModShop_UnregisterOwner(&g_shop_owner);
}

bool ModSeedImport_Active(void) {
  return g_active;
}

bool ModSeedImport_Z3rRulesActive(void) {
  return (enhanced_features0 & kFeatures0_Z3rRules) != 0;
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
                      int slot, int item, int price) {
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
      int shop, slot, room, item, price;
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
      if (AppendShop(shops, &shop_count, shop, room, slot, item, price) < 0) {
        SetError(err, err_size, "seed_data: invalid shop entry at locations[%d]",
                 location_index);
        goto fail;
      }
    }
    location_index++;
  }

  if (chest_count == 0 && fixed_count == 0 && uncle_item < 0 && shop_count == 0) {
    SetError(err, err_size, "seed_data: no locations");
    goto fail;
  }

  ModSeedImport_Reset();
  g_chests = chests;
  g_chest_count = chest_count;
  g_fixed = fixed;
  g_fixed_count = fixed_count;
  g_uncle_item = uncle_item;
  if (has_save_template) {
    memcpy(g_save_template, save_template, sizeof(g_save_template));
    g_has_save_template = true;
  }
  g_active = true;

  for (int i = 0; i < shop_count; i++) {
    const SeedShopGroup *group = &shops[i];
    if (!ModShop_Set(&g_shop_owner, group->room & 0xFF, group->entries,
                     group->count, err, err_size)) {
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
  static const uint8 kMax[8] = {10, 15, 20, 25, 30, 35, 40, 50};
  link_bomb_filler = kMax[level];
}

static void AddArrowCapacity(int levels) {
  int level = link_arrow_upgrades + levels;
  if (level > 7)
    level = 7;
  link_arrow_upgrades = (uint8)level;
  static const uint8 kMax[8] = {30, 35, 40, 45, 50, 55, 60, 70};
  link_arrow_filler = kMax[level];
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
    link_rupees_actual = rupees > 10 ? rupees - 10 : 0;
    break;
  }
  default:
    break;
  }
}

static uint8 ProgressiveSword(void) {
  if (link_sword_type == 0 || link_sword_type == 0xFF)
    return 0x49;
  if (link_sword_type == 1)
    return 0x01;
  if (link_sword_type == 2)
    return 0x02;
  if (link_sword_type == 3)
    return 0x03;
  return 0x47;
}

static uint8 ProgressiveShield(void) {
  if (link_shield_type == 0)
    return 0x04;
  if (link_shield_type == 1)
    return 0x05;
  if (link_shield_type == 2)
    return 0x06;
  return 0x47;
}

static uint8 ProgressiveArmor(void) {
  if (link_armor == 0)
    return 0x22;
  if (link_armor == 1)
    return 0x23;
  return 0x47;
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
  if (link_item_bow < 3)
    return 0x3B;
  return 0x47;
}

uint8 ModSeedImport_MapItem(uint8 item) {
  if (!g_active || item <= 0x4B)
    return item;

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
    if (dungeon >= 0)
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
      ModSeedImport_SetSpecialItem(item);
      return 0x34;
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
