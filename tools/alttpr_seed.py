#!/usr/bin/env python3
# Build an ALttPR seed ROM from a JP 1.0 source ROM, the z3randomizer base
# BPS and the per-seed JSON patch, and read the result back.
#
# Nothing here downloads or runs binaries: the BPS format is implemented in
# this file so the only inputs are the three user-provided files.
#
# Usage:
#   tools/alttpr_seed.py build --jp-rom japanese.sfc --base base.bps \
#       --seed seed.json -o seed.sfc
#   tools/alttpr_seed.py info seed.sfc [--json seed.json] [--all-items]
#
# The seed JSON's "patch" entries are file offsets, not SNES addresses.  This
# matches the base build (bank $B0:8000 is PC 0x180000) and the header title
# at PC 0x7FC0.

import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JP10_CRC32 = 0x3322EFFC
SEEDHASH_OFF = 0x180215
SEEDHASH_LEN = 5

# Item ids documented in z3r-tables.asm (the byte written to item tables).
ITEM_NAMES = {
  0x00: "Fighter's Sword + Shield",
  0x01: "Master Sword",
  0x02: "Tempered Sword",
  0x03: "Golden Sword",
  0x04: "Fighter's Shield",
  0x05: "Red Shield",
  0x06: "Mirror Shield",
  0x07: "Fire Rod",
  0x08: "Ice Rod",
  0x09: "Hammer",
  0x0A: "Hookshot",
  0x0B: "Bow",
  0x0C: "Boomerang",
  0x0D: "Magic Powder",
  0x0E: "Bee",
  0x0F: "Bombos",
  0x10: "Ether",
  0x11: "Quake",
  0x12: "Lamp",
  0x13: "Shovel",
  0x14: "Flute",
  0x15: "Cane of Somaria",
  0x16: "Bottle",
  0x17: "Heart Piece",
  0x18: "Cane of Byrna",
  0x19: "Magic Cape",
  0x1A: "Magic Mirror",
  0x1B: "Power Glove",
  0x1C: "Titan Mitts",
  0x1D: "Book of Mudora",
  0x1E: "Zora's Flippers",
  0x1F: "Moon Pearl",
  0x20: "Crystal",
  0x21: "Bug Net",
  0x22: "Blue Mail",
  0x23: "Red Mail",
  0x24: "Small Key",
  0x25: "Compass",
  0x26: "Heart Container",
  0x27: "Bomb",
  0x28: "3 Bombs",
  0x29: "Mushroom",
  0x2A: "Red Boomerang",
  0x2B: "Red Potion (bottle)",
  0x2C: "Green Potion (bottle)",
  0x2D: "Blue Potion (bottle)",
  0x2E: "Red Potion",
  0x2F: "Green Potion",
  0x30: "Blue Potion",
  0x31: "10 Bombs",
  0x32: "Big Key",
  0x33: "Map",
  0x34: "1 Rupee",
  0x35: "5 Rupees",
  0x36: "20 Rupees",
  0x37: "Pendant of Courage",
  0x38: "Pendant of Power",
  0x39: "Pendant of Wisdom",
  0x3A: "Bow and Arrows",
  0x3B: "Bow and Silver Arrows",
  0x3C: "Bee",
  0x3D: "Fairy",
  0x3E: "Boss Heart",
  0x3F: "Sanctuary Heart",
  0x40: "100 Rupees",
  0x41: "50 Rupees",
  0x42: "Heart",
  0x43: "Arrow",
  0x44: "10 Arrows",
  0x45: "Magic",
  0x46: "300 Rupees",
  0x47: "20 Rupees",
  0x48: "Gold Bee",
  0x49: "Fighter's Sword",
  0x4A: "Flute",
  0x4B: "Pegasus Boots",
  0x4C: "Max Bombs",
  0x4D: "Max Arrows",
  0x4E: "Half Magic",
  0x4F: "Quarter Magic",
  0x50: "Master Sword",
  0x51: "+5 Bombs",
  0x52: "+10 Bombs",
  0x53: "+5 Arrows",
  0x54: "+10 Arrows",
  0x55: "Programmable Item 1",
  0x56: "Programmable Item 2",
  0x57: "Programmable Item 3",
  0x58: "Upgrade-Only Silver Arrows",
  0x59: "Rupoor",
  0x5A: "Null Item",
  0x5B: "Red Clock",
  0x5C: "Blue Clock",
  0x5D: "Green Clock",
  0x5E: "Progressive Sword",
  0x5F: "Progressive Shield",
  0x60: "Progressive Armor",
  0x61: "Progressive Lifting Glove",
  0x62: "RNG Pool Item (Single)",
  0x63: "RNG Pool Item (Multi)",
  0x64: "Progressive Bow",
  0x65: "Progressive Bow (Alt)",
  0x6A: "Goal Item (Single)",
  0x6B: "Goal Item (Multi)",
  0x6D: "Server Request Item",
  0x6E: "Server Request Item (Dungeon Drop)",
}

for i in range(16):
  ITEM_NAMES[0x70 + i] = "Map of " + [
    "Light World", "Dark World", "Ganon's Tower", "Turtle Rock",
    "Thieves' Town", "Tower of Hera", "Ice Palace", "Skull Woods",
    "Misery Mire", "Dark Palace", "Swamp Palace", "Agahnim's Tower",
    "Desert Palace", "Eastern Palace", "Hyrule Castle", "Sewers",
  ][i]
  ITEM_NAMES[0x80 + i] = "Compass of " + [
    "Light World", "Dark World", "Ganon's Tower", "Turtle Rock",
    "Thieves' Town", "Tower of Hera", "Ice Palace", "Skull Woods",
    "Misery Mire", "Dark Palace", "Swamp Palace", "Agahnim's Tower",
    "Desert Palace", "Eastern Palace", "Hyrule Castle", "Sewers",
  ][i]
  ITEM_NAMES[0x92 + i] = "Big Key of " + [
    "Ganon's Tower", "Turtle Rock", "Thieves' Town", "Tower of Hera",
    "Ice Palace", "Skull Woods", "Misery Mire", "Dark Palace",
    "Swamp Palace", "Agahnim's Tower", "Desert Palace", "Eastern Palace",
    "Hyrule Castle", "Sewers",
  ][i] if i < 14 else ITEM_NAMES.get(0x92 + i, "Big Key")
ITEM_NAMES[0x90] = "Skull Key"
ITEM_NAMES[0x91] = "Reserved"

SMALL_KEY_DUNGEONS = [
  "Sewers", "Hyrule Castle", "Eastern Palace", "Desert Palace",
  "Agahnim's Tower", "Swamp Palace", "Palace of Darkness", "Misery Mire",
  "Skull Woods", "Ice Palace", "Tower of Hera", "Thieves' Town",
  "Turtle Rock", "Ganon's Tower",
]
for i, name in enumerate(SMALL_KEY_DUNGEONS):
  ITEM_NAMES[0xA0 + i] = "Small Key of " + name
ITEM_NAMES[0xAE] = "Reserved"
ITEM_NAMES[0xAF] = "Generic Small Key"

# 32-entry alphabet used by z3randomizer's file-select hash display
# (hashalphabet.asm: ALPHA_BOW ... ALPHA_KEY).
HASH_ICON_NAMES = [
  "Bow", "Boomerang", "Hookshot", "Bomb", "Mushroom", "Powder", "Rod",
  "Pendant", "Bombos", "Ether", "Quake", "Lamp", "Hammer", "Shovel",
  "Flute", "Net", "Book", "Bottle", "Potion", "Cane", "Cape", "Mirror",
  "Boots", "Gloves", "Flippers", "Moon Pearl", "Shield", "Tunic", "Heart",
  "Map", "Compass", "Key",
]

DUNGEON_NAMES = [
  "Ganon's Tower", "Hyrule Castle", "Eastern Palace", "Desert Palace",
  "Agahnim's Tower", "Swamp Palace", "Palace of Darkness", "Misery Mire",
  "Skull Woods", "Ice Palace", "Tower of Hera", "Thieves' Town",
  "Turtle Rock",
]

# Tables documented in z3r-tables.asm.  Each entry is
# (pc offset, table name, [field names], record size, byte order).
FIXED_TABLES = [
  (0x180000, "HeartPieceIndoorValues", [
    "HeartPiece_Forest_Thieves", "HeartPiece_Lumberjack_Tree",
    "HeartPiece_Spectacle_Cave", "HeartPiece_Circle_Bushes",
    "HeartPiece_Graveyard_Warp", "HeartPiece_Mire_Warp",
    "HeartPiece_Smith_Pegs"], 1, "u8"),
  (0x180010, "SpriteItemValues", [
    "RupeeNPC_MoldormCave", "RupeeNPC_NortheastDarkSwampCave",
    "LibraryItem", "MushroomItem", "WitchItem", "MagicBatItem",
    "EtherItem", "BombosItem"], 1, "u8"),
  (0x180140, "HeartPieceOutdoorValues", [
    "HeartPiece_Spectacle", "HeartPiece_Mountain_Warp", "HeartPiece_Maze",
    "HeartPiece_Desert", "HeartPiece_Lake", "HeartPiece_Swamp",
    "HeartPiece_Cliffside", "HeartPiece_Pyramid", "HeartPiece_Digging",
    "HeartPiece_Zora", "HauntedGroveItem"], 1, "u8"),
  (0x180150, "HeartContainerBossValues", [
    "HeartContainer_ArmosKnights", "HeartContainer_Lanmolas",
    "HeartContainer_Moldorm", "HeartContainer_HelmasaurKing",
    "HeartContainer_Arrghus", "HeartContainer_Mothula",
    "HeartContainer_Blind", "HeartContainer_Kholdstare",
    "HeartContainer_Vitreous", "HeartContainer_Trinexx"], 1, "u8"),
  (0x180160, "KeyItemValues", [
    "BonkKey_Desert", "BonkKey_GTower", "StandingKey_Hera"], 1, "u8"),
]

SINGLE_VALUES = [
  (0x180028, "FairySword", 1, "item"),
  (0x180029, "SmithItemMode", 1, "u8"),
  (0x18002A, "SmithItem", 1, "item"),
  (0x180165, "GoalItemIcon", 2, "u16"),
  (0x180167, "GoalItemRequirement", 2, "u16"),
  (0x180169, "AgahnimDoorStyle", 1, "u8"),
  (0x18016A, "FreeItemText", 1, "u8"),
  (0x180171, "GanonPyramidRespawn", 1, "u8"),
  (0x180172, "GenericKeys", 1, "u8"),
  (0x180174, "FixFakeWorld", 1, "u8"),
  (0x18004A, "InvertedMode", 1, "u8"),
  (0x18004B, "QuickSwapFlag", 1, "u8"),
  (0x180048, "MenuSpeed", 1, "u8"),
  (0x18004F, "ByrnaInvulnerability", 1, "u8"),
  (0x1801A8, "GanonVulnerableMode", 2, "u16"),
  (0x18019A, "GanonsTowerOpenTarget", 2, "u16"),
  (0x18019C, "GanonsTowerOpenMode", 2, "u16"),
  (0x180210, "RandomizerSeedType", 1, "u8"),
  (0x180211, "GameType", 1, "u8"),
  (0x180212, "WarningFlags", 1, "u8"),
  (0x180213, "TournamentSeed", 1, "u8"),
  (0x180214, "TournamentSeedInverse", 1, "u8"),
  (0x18021A, "NoBGM", 1, "u8"),
  (0x18021B, "FastFanfare", 1, "u8"),
  (0x289B0, "PedestalSword", 1, "item"),
  (0x3355C, "SmithSword", 1, "item"),
  (0x348FF, "WaterfallPotion", 1, "item"),
]

# Data and behavior flags in the z3randomizer $B0:8000 block. Only a subset
# is consumed by the C importer; the rest is recorded in seed.json so the
# coverage/docs can name what this seed asks for.
Z3R_SETTINGS = [
  ("digging_game_rng", 0x180020, 1),
  ("chest_game_rng", 0x180021, 1),
  ("mire_required_medallion", 0x180022, 1),
  ("trock_required_medallion", 0x180023, 1),
  ("big_fairy_health", 0x180024, 1),
  ("big_fairy_magic", 0x180025, 1),
  ("spawn_npc_health", 0x180026, 1),
  ("spawn_npc_magic", 0x180027, 1),
  ("enable_easter_eggs", 0x180031, 1),
  ("heart_beep", 0x180033, 1),
  ("rupoor_deduction", 0x180036, 2),
  ("lamp_cone_sewers", 0x180038, 1),
  ("item_counter_hud", 0x180039, 1),
  ("map_hud_mode", 0x18003A, 1),
  ("map_mode", 0x18003B, 1),
  ("compass_mode", 0x18003C, 1),
  ("persistent_floodgate", 0x18003D, 1),
  ("hammerable_ganon", 0x18003F, 1),
  ("allow_swordless_medallion_use", 0x180041, 1),
  ("permit_sq_from_bosses", 0x180042, 1),
  ("allow_hammer_tablets", 0x180044, 1),
  ("hud_dungeon_items", 0x180045, 1),
  ("menu_speed", 0x180048, 1),
  ("menu_collapse", 0x180049, 1),
  ("inverted", 0x18004A, 1),
  ("quick_swap", 0x18004B, 1),
  ("smith_travels_freely", 0x18004C, 1),
  ("escape_assist", 0x18004D, 1),
  ("uncle_refill", 0x18004E, 1),
  ("byrna_invulnerability", 0x18004F, 1),
  ("upgrade_5_bombs_refill", 0x180080, 1),
  ("upgrade_10_bombs_refill", 0x180081, 1),
  ("upgrade_5_arrows_refill", 0x180082, 1),
  ("upgrade_10_arrows_refill", 0x180083, 1),
  ("potion_health_refill", 0x180084, 1),
  ("potion_magic_refill", 0x180085, 1),
  ("ganon_agah_rng", 0x180086, 1),
  ("is_encrypted", 0x180087, 2),
  ("turtle_rock_auto_open_fix", 0x180089, 1),
  ("block_castle_doors_in_rain", 0x18008A, 1),
  ("fake_boots", 0x18008E, 1),
  ("progressive_sword_limit", 0x180090, 1),
  ("progressive_sword_replacement", 0x180091, 1),
  ("progressive_shield_limit", 0x180092, 1),
  ("progressive_shield_replacement", 0x180093, 1),
  ("progressive_armor_limit", 0x180094, 1),
  ("progressive_armor_replacement", 0x180095, 1),
  ("bottle_limit", 0x180096, 1),
  ("bottle_replacement", 0x180097, 1),
  ("progressive_bow_limit", 0x180098, 1),
  ("progressive_bow_replacement", 0x180099, 1),
  ("one_mind_player_count", 0x18009A, 1),
  ("one_mind_timer", 0x18009B, 2),
  ("dungeon_map_icons", 0x18009D, 1),
  ("bugfix_mirrorless_sq", 0x1800A0, 1),
  ("bugfix_swamp_water_level", 0x1800A1, 1),
  ("bugfix_pre_aga_dw_dungeon_death", 0x1800A2, 1),
  ("bugfix_set_world_on_agahnim_death", 0x1800A3, 1),
  ("bugfix_pod_eg", 0x1800A4, 1),
  ("goal_item_icon", 0x180165, 2),
  ("goal_item_requirement", 0x180167, 2),
  ("agahnim_door_style", 0x180169, 1),
  ("free_item_text", 0x18016A, 1),
  ("ganon_pyramid_respawn", 0x180171, 1),
  ("generic_keys", 0x180172, 1),
  ("fix_fake_world", 0x180174, 1),
  ("server_request_mode", 0x180193, 1),
  ("turn_in_goal_items", 0x180194, 1),
  ("byrna_cave_spike_damage", 0x180195, 1),
  ("total_item_count", 0x180196, 2),
  ("ganons_tower_open_target", 0x18019A, 2),
  ("ganons_tower_open_mode", 0x18019C, 2),
  ("ped_pull_target", 0x1801A0, 2),
  ("ped_check_mode", 0x1801A2, 2),
  ("ganon_vulnerable_target", 0x1801A6, 2),
  ("ganon_vulnerable_mode", 0x1801A8, 2),
  ("randomizer_seed_type", 0x180210, 1),
  ("game_type", 0x180211, 1),
  ("warning_flags", 0x180212, 1),
  ("tournament_seed", 0x180213, 1),
  ("tournament_seed_inverse", 0x180214, 1),
  ("no_bgm", 0x18021A, 1),
  ("fast_fanfare", 0x18021B, 1),
]
Z3R_STARTING_AREA_EXIT_OFFSET_OFF = 0x180240
Z3R_STARTING_AREA_OVERWORLD_DOOR_OFF = 0x180247
Z3R_STARTING_AREA_EXIT_TABLE_OFF = 0x180250
Z3R_EXTRA_HOLE_MAP16_OFF = 0x180300
Z3R_EXTRA_HOLE_AREA_OFF = 0x180320
Z3R_EXTRA_HOLE_ENTRANCE_OFF = 0x180340
Z3R_SHOULD_START_AT_EXIT_OFF = 0x180350
Z3R_CRYSTAL_PENDANT_FLAGS_OFF = 0x180050

# z3r starting-point tables in the JP 1.0 base. The block is contiguous and
# starts at $82:D8D2 (the table read by AllowStartFromExit); the seed patch
# writes the inverted start rows here.
Z3R_STARTING_POINTS_OFF = 0x158D2
Z3R_STARTING_POINT_TABLE_LAYOUT = (
  ("rooms", 7, "u16"),
  ("relative_coords", 56, "u8"),
  ("scroll_x", 7, "u16"),
  ("scroll_y", 7, "u16"),
  ("player_y", 7, "u16"),
  ("player_x", 7, "u16"),
  ("camera_y", 7, "u16"),
  ("camera_x", 7, "u16"),
  ("blockset", 7, "u8"),
  ("floor", 7, "i8"),
  ("palace", 7, "u8"),
  ("starting_bg", 7, "u8"),
  ("quadrant1", 7, "u8"),
  ("quadrant2", 7, "u8"),
  ("door_settings", 7, "u16"),
)

# The inverted overworld tile patches live in the z3r bank $A4. The hook at
# $82:ED51 is a JSL to Overworld_LoadNewTiles; the routine loads the pointer
# table with LDA.l. Both the hook and the table are located from the ROM so
# a future z3r build with different addresses still imports correctly.
Z3R_INVERTED_MAP_HOOK_OFF = 0x16D51
Z3R_INVERTED_MAP_BANK = 0xA4

CHEST_TABLE_OFF = 0xE96E
CHEST_TABLE_RECORDS = 168

# The seed ROM also carries the other item locations in fixed tables. These
# are the ones the first importer slice understands; the list is intentionally
# explicit so the coverage report can name every location type.
UNCLE_ITEM_OFF = 0x2DF45
UNCLE_SIG_BEFORE = 0xA0
UNCLE_SIG_AFTER = bytes([0x9C, 0xE9, 0x02, 0x22, 0x24, 0x88, 0xA4])
SHOP_TABLE_OFF = 0x184800
SHOP_CONTENTS_OFF = 0x184900
SHOP_CONTENTS_MAX = 224
DUNGEON_PRIZE_OFF = 0xC6FC
PYRAMID_POTION_OFF = 0x3493B
# Dungeon index order used by the game (and by z3r CrystalPendantFlags_2):
# 0 Ganon's Tower, 1 Hyrule Castle, 2 Eastern, ... 12 Turtle Rock.
PRIZE_DUNGEON_INDICES = [2, 3, 5, 6, 7, 8, 9, 10, 11, 12]


def die(msg):
  print("alttpr_seed: " + msg, file=sys.stderr)
  sys.exit(1)


def u32(data, off):
  return struct.unpack_from("<I", data, off)[0]


def s32(data, off):
  return struct.unpack_from("<i", data, off)[0]


def read_varint(data, pos):
  value = 0
  shift = 1
  while True:
    if pos >= len(data):
      die("truncated BPS varint")
    b = data[pos]
    pos += 1
    value += (b & 0x7F) * shift
    if b & 0x80:
      return value, pos
    shift <<= 7
    value += shift


def read_signed(data, pos):
  value, pos = read_varint(data, pos)
  if value & 1:
    return -(value >> 1), pos
  return value >> 1, pos


def apply_bps(source, patch):
  # Same action semantics as src/util.c:ApplyBps, which is already used for
  # zelda3_assets.bps.  Keep this in sync with that implementation.
  if patch[:4] != b"BPS1":
    die("base is not a BPS1 patch")
  if len(patch) < 16:
    die("base BPS is truncated")
  pos = 4
  source_size, pos = read_varint(patch, pos)
  target_size, pos = read_varint(patch, pos)
  metadata_size, pos = read_varint(patch, pos)
  pos += metadata_size
  if source_size != len(source):
    die("BPS source size %d does not match input size %d" %
        (source_size, len(source)))
  if pos + 8 > len(patch):
    die("base BPS is truncated")
  target = bytearray(target_size)
  out_pos = 0
  source_rel = 0
  target_rel = 0
  while out_pos < target_size:
    cmd, pos = read_varint(patch, pos)
    action = cmd & 3
    length = (cmd >> 2) + 1
    if action == 0:  # SourceRead: sequential bytes from the source.
      if out_pos + length > len(source):
        die("BPS SourceRead out of range")
      target[out_pos:out_pos + length] = source[out_pos:out_pos + length]
      out_pos += length
    elif action == 1:  # TargetRead: literal bytes from the patch.
      if pos + length > len(patch):
        die("BPS TargetRead out of range")
      target[out_pos:out_pos + length] = patch[pos:pos + length]
      pos += length
      out_pos += length
    elif action == 2:  # SourceCopy: relative to the source cursor.
      delta, pos = read_signed(patch, pos)
      source_rel += delta
      if source_rel < 0 or source_rel + length > len(source):
        die("BPS SourceCopy out of range")
      target[out_pos:out_pos + length] = source[source_rel:source_rel + length]
      source_rel += length
      out_pos += length
    else:  # TargetCopy: relative to the target cursor.
      delta, pos = read_signed(patch, pos)
      target_rel += delta
      if target_rel < 0 or target_rel + length > target_size:
        die("BPS TargetCopy out of range")
      for _ in range(length):
        target[out_pos] = target[target_rel]
        out_pos += 1
        target_rel += 1
      continue
  if pos + 12 != len(patch):
    die("BPS has trailing data or truncated checksums")
  if u32(patch, pos) != zlib.crc32(source) & 0xFFFFFFFF:
    die("BPS source CRC32 mismatch")
  if u32(patch, pos + 4) != zlib.crc32(target) & 0xFFFFFFFF:
    die("BPS target CRC32 mismatch")
  if u32(patch, pos + 8) != zlib.crc32(patch[:-4]) & 0xFFFFFFFF:
    die("BPS patch CRC32 mismatch")
  return bytes(target)


def md5_hex(data):
  return hashlib.md5(data).hexdigest()


def load_seed(path):
  try:
    with open(path, "r", encoding="utf-8") as f:
      seed = json.load(f)
  except (OSError, ValueError) as e:
    die("cannot read seed JSON %s: %s" % (path, e))
  if not isinstance(seed, dict) or not isinstance(seed.get("patch"), list):
    die("seed JSON has no patch list")
  return seed


def patch_seed_rom(rom, seed):
  rom = bytearray(rom)
  writes = 0
  for entry in seed["patch"]:
    if not isinstance(entry, dict):
      die("unexpected seed patch entry")
    for addr_s, values in entry.items():
      addr = int(addr_s)
      if not isinstance(values, list) or not values:
        die("empty seed patch value at 0x%X" % addr)
      if addr < 0 or addr + len(values) > len(rom):
        die("seed patch at 0x%X is outside the ROM" % addr)
      for i, value in enumerate(values):
        if not isinstance(value, int) or not 0 <= value <= 255:
          die("invalid byte in seed patch at 0x%X" % (addr + i))
        rom[addr + i] = value
      writes += len(values)
  return bytes(rom), writes


def expected_base_md5(seed_path, base_path, explicit):
  if explicit:
    return explicit.lower()
  seed_dir = os.path.dirname(os.path.abspath(seed_path))
  try:
    seed = load_seed(seed_path)
  except SystemExit:
    raise
  hash_name = seed.get("hash")
  if hash_name:
    hash_json = os.path.join(seed_dir, "h-%s.json" % hash_name)
    if os.path.exists(hash_json):
      try:
        with open(hash_json, "r", encoding="utf-8") as f:
          data = json.load(f)
        if data.get("md5"):
          return str(data["md5"]).lower()
      except (OSError, ValueError):
        pass
  name = os.path.basename(base_path)
  if name.startswith("base-") and name.endswith(".bps"):
    return name[5:-4].lower()
  return None


def cmd_build(args):
  with open(args.jp_rom, "rb") as f:
    jp = f.read()
  if len(jp) != 0x100000:
    die("JP ROM is %d bytes, expected 1048576" % len(jp))
  jp_crc = zlib.crc32(jp) & 0xFFFFFFFF
  if jp_crc != JP10_CRC32:
    die("JP ROM CRC32 is %08X, expected %08X" % (jp_crc, JP10_CRC32))
  with open(args.base, "rb") as f:
    base_patch = f.read()
  seed = load_seed(args.seed)

  base_rom = apply_bps(jp, base_patch)
  got = md5_hex(base_rom)
  want = expected_base_md5(args.seed, args.base, args.md5)
  if want is None:
    print("warning: no base md5 to check (pass --md5)", file=sys.stderr)
  elif got != want:
    die("patched base md5 is %s, expected %s" % (got, want))
  else:
    print("base: %s (md5 ok)" % got)

  rom, writes = patch_seed_rom(base_rom, seed)
  title = rom[0x7FC0:0x7FC0 + 21].decode("ascii", "replace").rstrip()
  with open(args.output, "wb") as f:
    f.write(rom)
  print("seed: %s" % seed.get("hash", "?"))
  print("title: %s" % title)
  print("output: %s" % args.output)
  print("size: %d" % len(rom))
  print("md5: %s" % md5_hex(rom))
  print("crc32: %08X" % (zlib.crc32(rom) & 0xFFFFFFFF))
  print("seed patch: %d entries, %d bytes" % (len(seed["patch"]), writes))


def read_u16(rom, off):
  return rom[off] | (rom[off + 1] << 8)


def read_z3r_settings(rom):
  values = {}
  for name, off, size in Z3R_SETTINGS:
    values[name] = rom[off] if size == 1 else read_u16(rom, off)
  values["starting_area_exit_offset"] = list(
    rom[Z3R_STARTING_AREA_EXIT_OFFSET_OFF:
        Z3R_STARTING_AREA_EXIT_OFFSET_OFF + 7])
  values["starting_area_overworld_door"] = list(
    rom[Z3R_STARTING_AREA_OVERWORLD_DOOR_OFF:
        Z3R_STARTING_AREA_OVERWORLD_DOOR_OFF + 7])
  values["starting_area_exit_table"] = list(
    rom[Z3R_STARTING_AREA_EXIT_TABLE_OFF:
        Z3R_STARTING_AREA_EXIT_TABLE_OFF + 80])
  values["extra_hole_map16"] = list(read_u16(rom, Z3R_EXTRA_HOLE_MAP16_OFF + i * 2)
                                    for i in range(16))
  values["extra_hole_area"] = list(read_u16(rom, Z3R_EXTRA_HOLE_AREA_OFF + i * 2)
                                   for i in range(16))
  values["extra_hole_entrance"] = list(
    rom[Z3R_EXTRA_HOLE_ENTRANCE_OFF:Z3R_EXTRA_HOLE_ENTRANCE_OFF + 16])
  values["should_start_at_exit"] = list(
    rom[Z3R_SHOULD_START_AT_EXIT_OFF:Z3R_SHOULD_START_AT_EXIT_OFF + 3])
  values["crystal_pendant_flags"] = list(
    rom[Z3R_CRYSTAL_PENDANT_FLAGS_OFF:
        Z3R_CRYSTAL_PENDANT_FLAGS_OFF + 13])
  return values


PALETTE_OVERWORLD_BG_MAIN_OFF = 0xDE6C8
PALETTE_OVERWORLD_BG_MAIN_SIZE = 420
PALETTE_OVERWORLD_BG_AUX12_OFF = 0xDE86C
PALETTE_OVERWORLD_BG_AUX12_SIZE = 840


def read_overworld_palettes(rom):
  """JP 1.0 overworld palette tables; they differ from the US assets."""
  if len(rom) < PALETTE_OVERWORLD_BG_AUX12_OFF + PALETTE_OVERWORLD_BG_AUX12_SIZE:
    return None
  return {
    "bg_main": rom[PALETTE_OVERWORLD_BG_MAIN_OFF:
                   PALETTE_OVERWORLD_BG_MAIN_OFF + PALETTE_OVERWORLD_BG_MAIN_SIZE].hex(),
    "bg_aux12": rom[PALETTE_OVERWORLD_BG_AUX12_OFF:
                    PALETTE_OVERWORLD_BG_AUX12_OFF + PALETTE_OVERWORLD_BG_AUX12_SIZE].hex(),
  }


SIGN_TEXT_HC_LEFT_PC = 0x3F52D
SIGN_TEXT_HC_RIGHT_PC = 0x3F52F


HC_SIGN_US_IDS = {0x190: 0x3C, 0x191: 0x3E}


def read_hc_sign_text(rom):
  """The two inverted Hyrule Castle sign ids from $87:F52D/F52F.

  The seed stores JP message ids $190/$191; the engine dialogue table has
  no such entries, so they are translated to the unused US sign ids $3C/$3E
  and the imported dialogue overrides those two entries.
  """
  if len(rom) < SIGN_TEXT_HC_RIGHT_PC + 2:
    return None
  left = read_u16(rom, SIGN_TEXT_HC_LEFT_PC)
  right = read_u16(rom, SIGN_TEXT_HC_RIGHT_PC)
  return {
    "hc_left": HC_SIGN_US_IDS.get(left, left),
    "hc_right": HC_SIGN_US_IDS.get(right, right),
  }


def read_starting_points(rom):
  out = {}
  off = Z3R_STARTING_POINTS_OFF
  for name, count, kind in Z3R_STARTING_POINT_TABLE_LAYOUT:
    size = 1 if kind in ("u8", "i8") else 2
    values = []
    for i in range(count):
      if kind == "u8":
        values.append(rom[off + i])
      elif kind == "i8":
        value = rom[off + i]
        values.append(value - 256 if value >= 128 else value)
      else:
        values.append(read_u16(rom, off + i * 2))
    out[name] = values
    off += count * size
  return out


def _z3r_pc(bank, addr):
  return (bank & 0x7f) * 0x8000 + (addr & 0x7fff)


def _read_inverted_map_stream(rom, base, start, stop=None):
  """Interpret the z3r overworld tile patch command stream."""
  out = []
  customs = []
  p = start
  for _ in range(20000):
    if stop is not None and p >= stop:
      break
    word = read_u16(rom, p)
    p += 2
    if word == 0xFFFF:  # !OWW_END
      break
    if word & 0x8000:
      command = word & 0x7f
      increment = 0x80 if (word & 0x80) else 2
      if command == 0x00:  # stripe
        cursor = read_u16(rom, p)
        p += 2
        while True:
          tile = read_u16(rom, p)
          p += 2
          if tile & 0x8000:
            if tile == 0xFFFF:  # !OWW_SKIP
              continue
            out.append([cursor, tile & 0x7fff])
            break
          out.append([cursor, tile])
          cursor += increment
      elif command in (0x01, 0x02):  # stripe RLE / RLE increment
        count = (word >> 8) & 0x7f
        tile = read_u16(rom, p)
        cursor = read_u16(rom, p + 2)
        p += 4
        for i in range(count):
          out.append([cursor, tile + i if command == 0x02 else tile])
          cursor += increment
      elif command == 0x03:  # arbitrary tile copy
        tile = read_u16(rom, p)
        p += 2
        while True:
          addr = read_u16(rom, p)
          p += 2
          if addr & 0x8000:
            out.append([addr & 0x7fff, tile])
            break
          out.append([addr, tile])
      elif command == 0x0d:  # skip if inverted
        target = read_u16(rom, p)
        p = base + (target & 0x7fff)
      elif command == 0x0e:  # skip if not inverted (inverted: fall through)
        p += 2
      elif command == 0x0f:  # inverted only
        pass
      elif command == 0x10:  # custom command
        vector = read_u16(rom, p)
        p += 2
        vector_pc = base + (vector & 0x7fff)
        no_hole = None
        for i in range(vector_pc, vector_pc + 16):
          if rom[i] == 0xA0:  # LDY #no_hole
            no_hole = read_u16(rom, i + 1)
            break
        customs.append([vector, no_hole, p])
        if no_hole is not None:
          hole, _, _, _ = _read_inverted_map_stream(
              rom, base, p, base + (no_hole & 0x7fff))
          tail, _, _, _ = _read_inverted_map_stream(
              rom, base, base + (no_hole & 0x7fff))
          return out, hole, tail, customs
      else:
        return out, None, None, customs
    else:
      tile = word
      pos = read_u16(rom, p)
      p += 2
      out.append([pos, tile])
  return out, None, None, customs


def read_mirror_bonk(rom):
  """MirrorBonk rectangles from the z3r bank $A4 ($87:A9AC hook)."""
  hook = 0x3A9AC
  if rom[hook] != 0x5C:
    return None
  target = rom[hook + 1] | rom[hook + 2] << 8 | rom[hook + 3] << 16
  target_pc = _z3r_pc(target >> 16, target & 0xffff)
  table = None
  for i in range(target_pc, target_pc + 0x60):
    if rom[i] == 0xBF and rom[i + 3] == Z3R_INVERTED_MAP_BANK:
      addr = rom[i + 1] | rom[i + 2] << 8
      if table is None or addr < table:
        table = addr
  if table is None:
    return None
  base = _z3r_pc(Z3R_INVERTED_MAP_BANK, 0)
  table_pc = base + (table & 0x7fff)
  # The build's table ends at $B8 (23 entries); the 24th slot is data that
  # belongs to the next table.
  rects = []
  for i in range(23):
    rects.append([read_u16(rom, table_pc + i * 8 + 2 * j) for j in range(4)])
  return rects


TR_TILE_ATTRIBUTES_PC = 0x121E1C
TR_TILE_ATTRIBUTES_SIZE = 512


def read_tr_tile_attributes(rom):
  """Inverted Turtle Rock collision table from the z3r bank $A4."""
  if len(rom) < TR_TILE_ATTRIBUTES_PC + TR_TILE_ATTRIBUTES_SIZE:
    return None
  return rom[TR_TILE_ATTRIBUTES_PC:
             TR_TILE_ATTRIBUTES_PC + TR_TILE_ATTRIBUTES_SIZE].hex()


def read_inverted_map_patches(rom):
  if len(rom) <= Z3R_INVERTED_MAP_HOOK_OFF + 3:
    return None
  hook = rom[Z3R_INVERTED_MAP_HOOK_OFF:Z3R_INVERTED_MAP_HOOK_OFF + 4]
  if hook[0] != 0x22:
    return None
  target = hook[1] | hook[2] << 8 | hook[3] << 16
  target_pc = _z3r_pc(target >> 16, target & 0xffff)
  table = None
  for i in range(target_pc, target_pc + 0x40):
    if rom[i] == 0xBF and rom[i + 3] == Z3R_INVERTED_MAP_BANK:
      table = rom[i + 1] | rom[i + 2] << 8 | rom[i + 3] << 16
      break
  if table is None:
    return None
  bank = table >> 16
  base = _z3r_pc(bank, 0)
  table_pc = _z3r_pc(bank, table & 0xffff)
  screens = []
  for screen in range(128):
    pointer = read_u16(rom, table_pc + screen * 2)
    if pointer == 0:
      continue
    writes, hole, tail, customs = _read_inverted_map_stream(
        rom, base, base + (pointer & 0x7fff))
    entry = {"screen": screen, "writes": writes}
    if hole is not None:
      entry["hole"] = hole
      entry["no_hole"] = tail
    for vector, no_hole, _ in customs:
      if screen == 0x5B and no_hole is None:
        entry["warp"] = {"addr": 0x3BBE, "low": 0x34, "high": 0x212}
    screens.append(entry)
  return {"format": 1, "screens": screens}


# z3r keeps its English script in the JP raw-text format at $9C:8000. The
# bytes are direct glyph indexes (not the vanilla US compression), with $FB
# closing each message and $F6..$FF acting as commands. The character map is
# the one shipped as data/textbytecharmap.txt in z3randomizer.
Z3R_TEXT_PC = 0xE0000
Z3R_TEXT_END = 0xF356
Z3R_ADVANCE_PC = 0xE7261   # $9C:F261
Z3R_EXTRA_PC = 0xE72F9     # $9C:F2F9
Z3R_CHARMAP = {
  0x9D: "'", 0xAA: "A", 0xAB: "B", 0xAC: "C", 0xAD: "D", 0xAE: "E",
  0xAF: "F", 0xB0: "G", 0xB1: "H", 0xB2: "I", 0xB3: "J", 0xB4: "K",
  0xB5: "L", 0xB6: "M", 0xB7: "N", 0xB8: "O", 0xB9: "P", 0xBA: "Q",
  0xBB: "R", 0xBC: "S", 0xBD: "T", 0xBE: "U", 0xBF: "V", 0xC0: "W",
  0xC1: "X", 0xC2: "Y", 0xC3: "Z", 0xC6: "?", 0xC7: "!", 0xC8: ",",
  0xC9: "-", 0xCD: ".", 0xCE: "-", 0xEA: "-", 0xD0: "a", 0xD1: "b",
  0xD2: "c", 0xD3: "d", 0xD4: "e", 0xD5: "f", 0xD6: "g", 0xD7: "h",
  0xD8: "i", 0xD9: "j", 0xDA: "k", 0xDB: "l", 0xDC: "m", 0xDD: "n",
  0xDE: "o", 0xDF: "p", 0xE0: "q", 0xE1: "r", 0xE2: "s", 0xE3: "t",
  0xE4: "u", 0xE5: "v", 0xE6: "w", 0xE7: "x", 0xE8: "y", 0xE9: "z",
}
for _digit in range(10):
  Z3R_CHARMAP[0xA0 + _digit] = str(_digit)

US_DIALOGUE_COUNT = 397
_text_compression = None


def text_compression_module():
  global _text_compression
  if _text_compression is None:
    sys.path.insert(0, os.path.join(ROOT, "assets"))
    import text_compression
    _text_compression = text_compression
  return _text_compression


def us_to_jp_message_id(us_id):
  # Kept in sync with SeedOracle_JpMessageId() in src/mod/seed_oracle.c.
  if us_id == 0x16:
    return 0x17
  if 0x2F <= us_id <= 0x183:
    return us_id - 2
  if us_id in (0x184, 0x185):
    return 0x182
  return us_id


def read_jp_text_table(rom):
  if len(rom) < Z3R_TEXT_PC + (Z3R_TEXT_END - 0x8000):
    return []
  table = [0x8000]
  ptr = 0x8000
  for _ in range(0x10000):
    old = ptr
    c = rom[Z3R_TEXT_PC + old - 0x8000]
    y = c if c >= 0xF6 else 0xF5
    ptr += rom[Z3R_ADVANCE_PC + y]
    if y == 0xFE:
      y2 = rom[Z3R_TEXT_PC + old + 1 - 0x8000]
      if rom[Z3R_EXTRA_PC + y2] & 0x80:
        ptr += 1
    if y == 0xFB:
      table.append(ptr)
      if ptr == Z3R_TEXT_END:
        return table
  return table


def decode_jp_message(rom, offset):
  """Decode one z3r raw-text message into modkit dialogue syntax.

  Returns None when the message uses a glyph/command that has no safe US
  counterpart, so the base US message stays untouched.
  """
  end = Z3R_TEXT_PC + Z3R_TEXT_END - 0x8000
  pos = Z3R_TEXT_PC + offset - 0x8000
  out = []
  while pos < end:
    c = rom[pos]
    if c == 0xFB:
      text = "".join(out)
      while "[Scroll][Scroll]" in text:
        text = text.replace("[Scroll][Scroll]", "[Scroll]")
      return text
    if c in Z3R_CHARMAP:
      out.append(Z3R_CHARMAP[c])
      pos += 1
    elif c == 0xFF:
      out.append(" ")
      pos += 1
    elif c in (0xF7, 0xF8, 0xF9):
      out.append({0xF7: "[1]", 0xF8: "[2]", 0xF9: "[3]"}[c])
      pos += 1
    elif c in (0xF6, 0xFA):
      out.append("[Scroll]")
      pos += 1
    elif c == 0x9F:
      out.append("...")
      pos += 1
    elif c == 0xFC:
      if pos + 1 >= end or rom[pos + 1] > 0x0F:
        return None
      out.append("[Speed %02d]" % rom[pos + 1])
      pos += 2
    elif c == 0xFD:
      # Two-byte icon/glyph. Drop the icon but keep the readable text.
      pos += 2
    elif c == 0xFE:
      if pos + 1 >= end:
        return None
      sub = rom[pos + 1]
      param = rom[pos + 2] if pos + 2 < end else 0
      if sub == 0x6B and param <= 0x0F:
        out.append("[Speed %02d]" % param)
        pos += 3
      elif sub == 0x6C and param <= 0x0F:
        out.append("[Number %02d]" % param)
        pos += 3
      elif sub == 0x68:
        out.append("[Choose]")
        pos += 2
      else:
        return None
    else:
      return None
  return None


def read_jp_dialogue(rom):
  table = read_jp_text_table(rom)
  return [decode_jp_message(rom, off) for off in table]


def seed_dialogue_overlay(rom):
  """Map C 0-based message indexes to seed text for modkit dialogue.txt ids."""
  messages = read_jp_dialogue(rom)
  tc = text_compression_module()
  overlay = {}
  for us_index in range(US_DIALOGUE_COUNT):
    jp_index = us_to_jp_message_id(us_index)
    if jp_index >= len(messages):
      continue
    text = messages[jp_index]
    if not text or not any(ch.isalnum() for ch in text):
      continue
    try:
      tc.compress_strings([text], "us")
    except Exception:
      continue
    overlay[us_index + 1] = text
  # The inverted Hyrule Castle signs point at JP messages $190/$191, which
  # are beyond the US message count. Keep those ids so the engine can use the
  # imported text directly.
  if rom[0x18004A]:
    for jp_id, us_id in HC_SIGN_US_IDS.items():
      if jp_id >= len(messages):
        continue
      text = messages[jp_id]
      if not text or not any(ch.isalnum() for ch in text):
        continue
      try:
        tc.compress_strings([text], "us")
      except Exception:
        continue
      overlay[us_id] = text
  return overlay


NPC_ITEM_IDS = {
  "MoonPearl": 0x1F,
  "Bottle": 0x16,
  "TenArrows": 0x44,
  "PieceOfHeart": 0x17,
  "ThreeBombs": 0x28,
  "TwentyRupees": 0x42,
}


def read_npc_items(seed):
  """The four NPC rewards that live outside the fixed z3r tables."""
  if not seed:
    return None
  spoiler = seed.get("spoiler", {})
  entries = {}
  for region in spoiler.values():
    if isinstance(region, dict):
      entries.update(region)
  out = {}
  for key, name in (("old_man", "Old Man:1"),
                    ("sick_kid", "Sick Kid:1"),
                    ("sahasrahla", "Sahasrahla:1"),
                    ("catfish", "Catfish:1")):
    value = entries.get(name)
    if not isinstance(value, str):
      continue
    item_name = value.rsplit(":", 1)[0]
    item = NPC_ITEM_IDS.get(item_name)
    if item is not None:
      out[key] = item
  return out or None


def fnv1a32(data):
  value = 2166136261
  for byte in data:
    value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
  return value


def item_name(value):
  return ITEM_NAMES.get(value, "unknown (0x%02X)" % value)


def is_major_item(value):
  if value <= 0x1F:
    return True
  if value in (0x3A, 0x3B, 0x50, 0x5E, 0x5F, 0x60, 0x61, 0x64, 0x65, 0x6A, 0x6B):
    return True
  return False


def decode_hex_byte(value):
  if value == 0x00:
    return "pendant"
  if value == 0x40:
    return "crystal"
  if value == 0x80:
    return "none"
  if value == 0x01:
    return "Agahnim 1"
  if value == 0x02:
    return "Agahnim 2"
  return "0x%02X" % value


def decode_prize_bits(value):
  parts = []
  for bit, name in ((0, "Pendant 3"), (1, "Pendant 2"), (2, "Pendant 1"),
                    (3, "Crystal 1"), (4, "Crystal 2"), (5, "Crystal 3"),
                    (6, "Crystal 4"), (7, "Crystal 5")):
    if value & (1 << bit):
      parts.append(name)
  return ", ".join(parts) if parts else "nothing"


def read_patch_values(seed):
  values = {}
  for entry in seed.get("patch", []):
    for addr_s, data in entry.items():
      addr = int(addr_s)
      for i, value in enumerate(data):
        values[addr + i] = value
  return values


def item_rule(value):
  if value <= 0x4B:
    return "direct"
  if value in (0x5E, 0x5F, 0x60, 0x61, 0x64, 0x65):
    return "progressive"
  if 0x70 <= value <= 0x7F:
    return "map"
  if 0x80 <= value <= 0x8F:
    return "compass"
  if 0x92 <= value <= 0x9F:
    return "bigkey"
  if 0xA0 <= value <= 0xAF:
    return "smallkey"
  return "approx"


def make_location(kind, loc_id, name, value, **extra):
  entry = {
    "id": loc_id,
    "type": kind,
    "name": name,
    "item": value,
    "item_name": item_name(value),
  }
  entry.update(extra)
  return entry


def read_uncle_item(rom):
  before = rom[UNCLE_ITEM_OFF - 1]
  after = rom[UNCLE_ITEM_OFF + 1:UNCLE_ITEM_OFF + 1 + len(UNCLE_SIG_AFTER)]
  if before != UNCLE_SIG_BEFORE or after != UNCLE_SIG_AFTER:
    die("uncle item signature not found at 0x%06X" % UNCLE_ITEM_OFF)
  return rom[UNCLE_ITEM_OFF]


def read_chest_locations(rom):
  records = []
  for record in range(CHEST_TABLE_RECORDS):
    off = CHEST_TABLE_OFF + record * 3
    item = rom[off]
    room = rom[off + 1] | ((rom[off + 2] & 0x7F) << 8)
    big = bool(rom[off + 2] & 0x80)
    if room >= 320:
      continue
    records.append({
      "item": item,
      "room": room,
      "big": big,
      "record": record,
    })

  # The game numbers chests in room-object order; the table keeps the same
  # order, so the ordinal inside each room is the chest index.
  counts = {}
  for rec in records:
    index = counts.get(rec["room"], 0)
    counts[rec["room"]] = index + 1
    rec["index"] = index
    rec["id"] = "chest:%03X:%d" % (rec["room"], index)
    rec["type"] = "chest"
    rec["name"] = "chest room 0x%03X #%d%s" % (
      rec["room"], index, " (big)" if rec["big"] else "")
    rec["item_name"] = item_name(rec["item"])
  return records


def read_fixed_locations(rom):
  locations = []
  for off, table, fields, rec_size, kind in FIXED_TABLES:
    for i, field in enumerate(fields):
      value = rom[off + i * rec_size]
      locations.append(make_location(
        "fixed", "fixed:%s:%s" % (table, field), field, value,
        table=table, offset=off + i * rec_size))
  for off, name, size, kind in SINGLE_VALUES:
    if kind != "item" or name == "SmithSword":
      continue
    value = rom[off]
    locations.append(make_location(
      "fixed", "fixed:%s" % name, name, value, table=name, offset=off))
  value = rom[PYRAMID_POTION_OFF]
  locations.append(make_location(
    "fixed", "fixed:PyramidPotion", "PyramidPotion", value,
    table="PyramidPotion", offset=PYRAMID_POTION_OFF))
  return locations


def read_prize_locations(rom):
  locations = []
  for index in PRIZE_DUNGEON_INDICES:
    name = DUNGEON_NAMES[index]
    value = rom[DUNGEON_PRIZE_OFF + index]
    locations.append(make_location(
      "prize", "prize:%s" % name, name, value,
      dungeon=name, dungeon_index=index))
  return locations


def read_shop_locations(rom):
  shops = {}
  for i in range(32):
    off = SHOP_TABLE_OFF + i * 8
    shop_id = rom[off]
    room = rom[off + 1] | (rom[off + 2] << 8)
    # 0xFF is a valid shop id (the tenth shop in this build); the table
    # terminator is the first empty record after it.
    if shop_id == 0 and room == 0 and rom[off + 3] == 0:
      break
    shops[shop_id] = {
      "room": room,
      "door": rom[off + 3],
      # ShopTable+5 bit 0x40: ignore the overworld door when matching.
      "any_door": bool(rom[off + 5] & 0x40),
    }

  locations = []
  per_shop = {}
  for i in range(SHOP_CONTENTS_MAX):
    off = SHOP_CONTENTS_OFF + i * 8
    shop_id = rom[off]
    value = rom[off + 1]
    if value == 0xFF:
      break
    slot = per_shop.get(shop_id, 0)
    per_shop[shop_id] = slot + 1
    price = read_u16(rom, off + 2)
    room = shops.get(shop_id, {}).get("room", -1)
    door = shops.get(shop_id, {}).get("door", 0)
    any_door = shops.get(shop_id, {}).get("any_door", True)
    loc_id = "shop:%d:%d" % (shop_id, slot)
    locations.append(make_location(
      "shop", loc_id, "shop %d slot %d%s%s" % (
        shop_id, slot,
        " room 0x%03X" % room if room >= 0 else "",
        " door 0x%02X" % door if door else ""),
      value, shop=shop_id, slot=slot, price=price, room=room,
      door=door, any_door=any_door))
  return locations


def collect_locations(rom):
  _, title = find_title(rom)
  if not title.startswith("VT "):
    die("not an ALttPR seed ROM (title '%s' does not start with 'VT ')" % title)
  locations = []
  locations.append(make_location(
    "uncle", "uncle", "uncle (wounded)", read_uncle_item(rom)))
  locations.extend(read_chest_locations(rom))
  locations.extend(read_fixed_locations(rom))
  locations.extend(read_prize_locations(rom))
  locations.extend(read_shop_locations(rom))
  return locations


def location_counts(locations):
  counts = {}
  for loc in locations:
    counts[loc["type"]] = counts.get(loc["type"], 0) + 1
  return counts


def format_counts(counts):
  return ", ".join("%s %d" % (key, counts[key]) for key in sorted(counts))


def seed_name_from_rom(rom, seed):
  if seed and seed.get("hash"):
    return str(seed["hash"])
  _, title = find_title(rom)
  if title.startswith("VT "):
    return title[3:].strip()
  return "".join("%02X" % value for value in
                 rom[SEEDHASH_OFF:SEEDHASH_OFF + SEEDHASH_LEN])


def safe_name(value):
  return "".join(c if c.isalnum() or c in "-_" else "_" for c in value)


NEWFONT_INVERTED_PC = 0x19AC00
NEWFONT_INVERTED_SIZE = 4096


def write_inverted_font(rom, out_path):
  """Write the seed's NewFontInverted glyphs into the modkit font.png."""
  if len(rom) < NEWFONT_INVERTED_PC + NEWFONT_INVERTED_SIZE:
    return False
  template = os.path.join(ROOT, ".modkit", "base", "files", "font.png")
  if not os.path.isfile(template):
    return False
  try:
    from PIL import Image
  except Exception:
    return False
  im = Image.open(template).convert("P")
  data = rom[NEWFONT_INVERTED_PC:NEWFONT_INVERTED_PC + NEWFONT_INVERTED_SIZE]
  w = 128 + 15
  for i in range(256):
    x, y = i % 16, i // 16
    base = x * 9 + (y * 8 + (y >> 1)) * w
    gx = (base + w) % w
    gy = (base + w) // w
    for yy in range(8):
      d0 = data[i * 16 + yy * 2]
      d1 = data[i * 16 + yy * 2 + 1]
      for xx in range(8):
        t = ((d0 >> xx) & 1) | (((d1 >> xx) & 1) << 1)
        im.putpixel((gx + 7 - xx, gy + yy), 96 + t)
  im.save(out_path, optimize=True)
  return True


def cmd_import(args):
  with open(args.rom, "rb") as f:
    rom = f.read()
  if len(rom) < 0x190000:
    die("ROM is too small to contain the z3randomizer tables")
  seed = load_seed(args.json) if args.json else None
  name = seed_name_from_rom(rom, seed)
  mod_name = safe_name(name)
  locations = collect_locations(rom)
  settings_only = args.only is not None and args.only.strip() == "settings"
  if args.only:
    wanted = {name.strip() for name in args.only.split(",") if name.strip()}
    locations = [] if settings_only else [
      loc for loc in locations if loc["type"] in wanted]
  if args.room is not None:
    locations = [loc for loc in locations if loc.get("room") == args.room]
  if not locations and not settings_only:
    die("no locations left after the import filters")
  counts = location_counts(locations)

  meta = seed.get("spoiler", {}).get("meta", {}) if seed else {}
  settings = {
    "mode": meta.get("mode"),
    "goal": meta.get("goal"),
    "logic": meta.get("logic"),
    "randomizer_seed_type": rom[0x180210],
    "game_type": rom[0x180211],
    "warning_flags": rom[0x180212],
    "inverted": rom[0x18004A],
    "tournament": rom[0x180213],
    "seed_hash": list(rom[SEEDHASH_OFF:SEEDHASH_OFF + SEEDHASH_LEN]),
    "ganons_tower_open_target": read_u16(rom, 0x18019A),
    "ganons_tower_open_mode": read_u16(rom, 0x18019C),
    "ganon_vulnerable_mode": read_u16(rom, 0x1801A8),
    "entry_crystals_ganon": meta.get("entry_crystals_ganon"),
    "entry_crystals_tower": meta.get("entry_crystals_tower"),
  }

  data = {
    "format": 1,
    "seed": name,
    "rom": {
      "md5": md5_hex(rom),
      "crc32": "%08X" % (zlib.crc32(rom) & 0xFFFFFFFF),
      "title": find_title(rom)[1],
    },
    "settings": settings,
    "z3r_settings": read_z3r_settings(rom),
    "starting_points": read_starting_points(rom),
    "hc_sign_text": read_hc_sign_text(rom),
    "npc_items": read_npc_items(seed),
    "overworld_palettes": read_overworld_palettes(rom),
    "overworld_patches": read_inverted_map_patches(rom),
    "tr_tile_attributes": read_tr_tile_attributes(rom),
    "mirror_bonk": read_mirror_bonk(rom),
    "save_template": rom[0x183000:0x183500].hex(),
    "locations": locations,
  }

  out_dir = args.output or os.path.join("mods-seeds", mod_name)
  os.makedirs(out_dir, exist_ok=True)
  manifest = {
    "id": "seed-" + mod_name,
    "name": "ALttPR seed " + name,
    "version": "1.0.0",
    "api": 1,
    "enabled": True,
    "features": {} if args.no_z3r_rules else {"Z3rRules": True},
    "seed_data": "seed.json",
  }
  with open(os.path.join(out_dir, "mod.json"), "w", encoding="utf-8") as f:
    json.dump(manifest, f, indent=2, ensure_ascii=False)
    f.write("\n")
  with open(os.path.join(out_dir, "seed.json"), "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
    f.write("\n")

  dialogue_count = 0
  font_written = False
  if not args.no_dialogue:
    overlay = seed_dialogue_overlay(rom)
    if overlay:
      content_dir = os.path.join(out_dir, "content")
      os.makedirs(content_dir, exist_ok=True)
      with open(os.path.join(content_dir, "dialogue.txt"), "w",
                encoding="utf-8", newline="\n") as f:
        for ident in sorted(overlay):
          f.write("%d: %s\n" % (ident, overlay[ident]))
      dialogue_count = len(overlay)
      if rom[0x18004A]:
        font_written = write_inverted_font(
            rom, os.path.join(content_dir, "font.png"))
      if args.build:
        command = [sys.executable, os.path.join(ROOT, "tools", "modkit.py"),
                   "build", out_dir]
        result = subprocess.run(command, cwd=ROOT)
        if result.returncode != 0:
          die("modkit build failed for %s" % out_dir)

  print("seed-import: %s (title %s)" % (name, data["rom"]["title"]))
  print("seed-import: %d location(s): %s" %
        (len(locations), format_counts(counts)))
  if dialogue_count:
    print("seed-import: %d dialogue message(s) from the seed ROM%s" % (
      dialogue_count, " (built)" if args.build else ""))
  if font_written:
    print("seed-import: inverted font imported from NewFontInverted")
  print("seed-import: mod written to %s" % out_dir)


def load_mod_locations(path):
  json_path = path
  if os.path.isdir(path):
    candidate = os.path.join(path, "seed.json")
    if os.path.exists(candidate):
      json_path = candidate
    else:
      candidates = [
        os.path.join(path, name, "seed.json")
        for name in sorted(os.listdir(path))
        if os.path.isdir(os.path.join(path, name))
        and os.path.exists(os.path.join(path, name, "seed.json"))
      ]
      if len(candidates) != 1:
        die("mod directory has no unique seed.json under %s" % path)
      json_path = candidates[0]
  try:
    with open(json_path, "r", encoding="utf-8") as f:
      data = json.load(f)
  except (OSError, ValueError) as e:
    die("cannot read mod seed data %s: %s" % (json_path, e))
  if not isinstance(data, dict) or not isinstance(data.get("locations"), list):
    die("mod seed data %s has no locations list" % json_path)
  return data, json_path


def cmd_coverage(args):
  with open(args.rom, "rb") as f:
    rom = f.read()
  locations = collect_locations(rom)
  mod_locations = None
  mod_path = None
  if args.mod:
    mod_data, mod_path = load_mod_locations(args.mod)
    mod_locations = {}
    for loc in mod_data["locations"]:
      loc_id = loc.get("id")
      if loc_id in mod_locations:
        die("mod seed data has duplicate location '%s'" % loc_id)
      mod_locations[loc_id] = loc

  applied_types = {"chest", "uncle", "fixed", "prize", "shop"}
  by_type = {}
  match_count = 0
  approximate = 0
  for loc in locations:
    kind = loc["type"]
    stats = by_type.setdefault(kind, {"total": 0, "covered": 0, "match": 0})
    stats["total"] += 1
    covered = kind in applied_types
    if covered:
      stats["covered"] += 1
    rule = item_rule(loc["item"])
    if covered and rule == "approx":
      approximate += 1
    mod_item = None
    if mod_locations is not None:
      mod_loc = mod_locations.get(loc["id"])
      mod_item = mod_loc.get("item") if mod_loc else None
      if mod_item == loc["item"]:
        stats["match"] += 1
        match_count += 1

  covered_count = sum(stats["covered"] for stats in by_type.values())
  total = len(locations)
  print("seed-import: coverage: %d of %d locations applied (%s)" % (
    covered_count, total,
    ", ".join("%s %d/%d" % (kind, by_type[kind]["covered"], by_type[kind]["total"])
              for kind in sorted(by_type))))
  if mod_locations is not None:
    print("seed-import: mod table matches seed: %d/%d" % (match_count, total))
  rules = {}
  for loc in locations:
    if loc["type"] in applied_types:
      rule = item_rule(loc["item"])
      rules[rule] = rules.get(rule, 0) + 1
  print("seed-import: item rules in applied locations: %s%s" % (
    format_counts(rules),
    " (%d approximate)" % approximate if approximate else ""))

  if args.json:
    spoiler = load_seed(args.json).get("spoiler", {})
    spoiler_locations = 0
    for region, entries in spoiler.items():
      if isinstance(entries, dict) and region not in (
          "Equipped", "Special", "Bosses", "meta", "playthrough"):
        spoiler_locations += len(entries)
    for shop in spoiler.get("Shops", []):
      for i in range(3):
        if shop.get("item_%d" % i):
          spoiler_locations += 1
    special = len(spoiler.get("Special", {}))
    print("seed-import: spoiler: %d item entries + %d special" % (
      spoiler_locations, special))
    if spoiler_locations != covered_count:
      print("seed-import: spoiler/ROM difference: %d location(s) outside "
            "the z3r tables" % (spoiler_locations - covered_count))

  if not args.summary:
    print("covered type      id                              seed  mod   rule")
    for loc in locations:
      mod_item = "-" if mod_locations is None else "%02X" % (
        mod_locations.get(loc["id"], {}).get("item", -1) & 0xFF)
      print("%-7s %-9s %-31s %02X    %-4s  %s" % (
        "yes" if loc["type"] in applied_types else "no",
        loc["type"], loc["id"], loc["item"], mod_item,
        item_rule(loc["item"])))

  missing = {kind: stats["total"] - stats["covered"]
             for kind, stats in by_type.items() if stats["total"] > stats["covered"]}
  if missing:
    print("seed-import: missing by type: %s" % format_counts(missing))

  if mod_locations is not None and match_count != total:
    for loc in locations:
      mod_loc = mod_locations.get(loc["id"])
      mod_item = mod_loc.get("item") if mod_loc else None
      if mod_item != loc["item"]:
        print("seed-import: mismatch %s: seed 0x%02X, mod %s" % (
          loc["id"], loc["item"],
          "missing" if mod_item is None else "0x%02X" % mod_item))
    return 1
  return 0


def cmd_verify_proof(args):
  with open(args.rom, "rb") as f:
    rom = f.read()

  def read_trace(path):
    events = []
    if not path:
      return events
    with open(path, "r", encoding="utf-8", errors="replace") as f:
      for line in f:
        parts = line.split()
        if len(parts) == 6 and parts[0] == "ev":
          events.append((int(parts[1]), parts[2], int(parts[3]),
                         int(parts[4]), int(parts[5])))
    return events

  def read_state(path):
    state = {}
    if not path:
      return state
    with open(path, "r", encoding="utf-8", errors="replace") as f:
      for line in f:
        parts = line.split()
        if len(parts) == 3 and parts[0] == "state":
          state[parts[1]] = int(parts[2])
    return state

  events = read_trace(args.trace)
  state = read_state(args.state)

  if args.proof == "npc":
    expected = rom[0x180010 + 3]  # SpriteItemValues.MushroomItem
    got = [ev[2] for ev in events if ev[1] == "ItemReceive" and ev[4] == 0]
    if expected not in got:
      print("seed-proof npc: expected ItemReceive %02X (%s), got %s" % (
        expected, item_name(expected), ", ".join("%02X" % v for v in got)),
        file=sys.stderr)
      return 1
    print("seed-proof npc: ItemReceive %02X (%s)" % (expected, item_name(expected)))
    return 0

  if args.proof == "prize":
    expected = rom[DUNGEON_PRIZE_OFF + 2]  # Eastern Palace
    got = [ev[2] for ev in events if ev[1] == "ItemReceive" and ev[4] == 3]
    if expected not in got:
      print("seed-proof prize: expected ItemReceive %02X (%s), got %s" % (
        expected, item_name(expected), ", ".join("%02X" % v for v in got)),
        file=sys.stderr)
      return 1
    print("seed-proof prize: ItemReceive %02X (%s)" % (expected, item_name(expected)))
    return 0

  if args.proof == "shop":
    shops = read_shop_locations(rom)
    expected = None
    for loc in shops:
      if loc.get("room", -1) & 0xff == 0x1f and loc.get("slot") == 1:
        expected = loc["item"]
    if expected is None:
      print("seed-proof shop: no room 0x1f slot 1 in the seed", file=sys.stderr)
      return 1
    got = [ev[2] for ev in events if ev[1] == "ItemReceive" and ev[4] == 0]
    if expected not in got:
      print("seed-proof shop: expected ItemReceive %02X (%s), got %s" % (
        expected, item_name(expected), ", ".join("%02X" % v for v in got)),
        file=sys.stderr)
      return 1
    print("seed-proof shop: room 0x1f slot 1 -> %02X (%s)" % (
      expected, item_name(expected)))
    return 0

  if args.proof == "key":
    chest = None
    for loc in read_chest_locations(rom):
      if loc["room"] == 170 and loc["index"] == 0:
        chest = loc
    if chest is None:
      print("seed-proof key: chest room 170 index 0 is missing", file=sys.stderr)
      return 1
    raw = chest["item"]
    target = None
    if 0xA0 <= raw <= 0xAD:
      target = [13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0][raw - 0xA0]
    if target is None:
      print("seed-proof key: room 170 does not hold a dungeon key (0x%02X)" % raw,
            file=sys.stderr)
      return 1
    current = state.get("cur_palace_index_x2", -1) >> 1
    if current == target:
      print("seed-proof key: target %d is the current dungeon" % target,
            file=sys.stderr)
      return 1
    found = any(ev[1] == "SeedItem" and ev[2] == 0x24 and ev[3] == target
                for ev in events)
    if not found:
      print("seed-proof key: missing SeedItem 36 -> %d in the trace" % target,
            file=sys.stderr)
      return 1
    print("seed-proof key: 0x%02X -> dungeon %d (current %d)" % (raw, target, current))
    return 0

  if args.proof == "shopdoor":
    shops = read_shop_locations(rom)
    expected = None
    for loc in shops:
      if loc.get("room") == 287 and loc.get("slot") == 0:
        expected = loc.get("door")
    if expected is None:
      print("seed-proof shopdoor: shop 0 has no door in the seed", file=sys.stderr)
      return 1
    door = state.get("previous_overworld_door", -1)
    if door != expected:
      print("seed-proof shopdoor: expected door 0x%02X, got %s" % (
        expected, door), file=sys.stderr)
      return 1
    bought = any(ev[1] == "ItemReceive" and ev[2] == 66 for ev in events)
    if not bought:
      print("seed-proof shopdoor: shop 0 did not receive item 0x42", file=sys.stderr)
      return 1
    print("seed-proof shopdoor: room 0x11F door 0x%02X -> item 0x42" % door)
    return 0

  if args.proof == "text":
    if args.message_id is None:
      print("seed-proof text: --message-id is required", file=sys.stderr)
      return 1
    jp_index = us_to_jp_message_id(args.message_id)
    messages = read_jp_dialogue(rom)
    if jp_index >= len(messages) or not messages[jp_index]:
      print("seed-proof text: message %d has no safe seed text" % args.message_id,
            file=sys.stderr)
      return 1
    text = messages[jp_index]
    encoded = text_compression_module().compress_strings([text], "us")[0]
    expected = fnv1a32(encoded)
    found = any(ev[1] == "DialogueText" and ev[2] == args.message_id and
                (ev[3] & 0xFFFFFFFF) == expected and ev[4] == len(encoded)
                for ev in events)
    if not found:
      got = [(ev[3], ev[4]) for ev in events
             if ev[1] == "DialogueText" and ev[2] == args.message_id]
      print("seed-proof text: message %d expected %08X/%d, got %s" % (
        args.message_id, expected, len(encoded), got), file=sys.stderr)
      return 1
    print("seed-proof text: message %d = %s (%08X)" % (
      args.message_id, text, expected))
    return 0

  if args.proof == "start":
    template = rom[0x183000:0x183500]
    bomb_caps = [10, 15, 20, 25, 30, 35, 40, 50]
    arrow_caps = [30, 35, 40, 45, 50, 55, 60, 70]
    bombs = max(i for i, cap in enumerate(bomb_caps) if template[0x370] >= cap)
    arrows = max(i for i, cap in enumerate(arrow_caps) if template[0x371] >= cap)
    progress = template[0x3C5]
    if progress & 0x80:
      progress = 0
    expected = {
      "link_bomb_upgrades": bombs,
      "link_arrow_upgrades": arrows,
      "sram_progress_indicator": progress,
      "sram_progress_flags": template[0x3C6],
      "link_health_capacity": template[0x36C],
      "link_health_current": template[0x36D],
      "link_sword_type": template[0x359],
      "link_shield_type": template[0x35A],
      "link_armor": template[0x35B],
    }
    for name, value in expected.items():
      if state.get(name) != value:
        print("seed-proof start: %s expected %d, got %s" % (
          name, value, state.get(name)), file=sys.stderr)
        return 1
    print("seed-proof start: %d field(s) match the save template" % len(expected))
    return 0

  print("seed-proof: unknown proof", file=sys.stderr)
  return 1


def cmd_verify_replay(args):
  with open(args.rom, "rb") as f:
    rom = f.read()
  locations = {loc["id"]: loc for loc in collect_locations(rom)}
  expected_chests = [
    (locations["chest:104:0"], True),
    (locations["chest:055:0"], False),
  ]
  expected_uncle = locations["uncle"]["item"]

  events = []
  with open(args.trace, "r", encoding="utf-8", errors="replace") as f:
    trace_lines = f.readlines()
  for line in trace_lines:
    parts = line.split()
    if len(parts) == 6 and parts[0] == "ev":
      events.append((int(parts[1]), parts[2], int(parts[3]),
                     int(parts[4]), int(parts[5])))

  if not any(line.strip() == "compare: off" for line in trace_lines):
    print("seed-import: replay has no 'compare: off'", file=sys.stderr)
    return 1

  checked_chests = []
  for chest, required in expected_chests:
    room = chest["room"]
    chest_at = None
    for i, ev in enumerate(events):
      if ev[1] == "ChestOpen" and ev[2] == room:
        chest_at = i
        break
    if chest_at is None:
      if required:
        print("seed-import: replay never opened chest room 0x%03X" % room,
              file=sys.stderr)
        return 1
      continue
    receive = None
    for ev in events[chest_at:]:
      if ev[1] == "ItemReceive" and ev[4] == 1:
        receive = ev
        break
    if receive is None or receive[2] != chest["item"]:
      print("seed-import: chest room 0x%03X gave %s, expected 0x%02X (%s)" % (
        room, "nothing" if receive is None else "0x%02X" % receive[2],
        chest["item"], item_name(chest["item"])), file=sys.stderr)
      return 1
    checked_chests.append("chest 0x%03X -> 0x%02X" % (room, chest["item"]))

  if not any(ev[1] == "ItemReceive" and ev[4] == 0 and ev[2] == expected_uncle
             for ev in events):
    print("seed-import: replay did not receive uncle item 0x%02X (%s)" % (
      expected_uncle, item_name(expected_uncle)), file=sys.stderr)
    return 1

  for ev in events:
    if ev[1] == "ItemReceive" and 0x4C <= ev[2] < 0x80:
      print("seed-import: unsafe z3r item 0x%02X leaked into ItemReceive" % ev[2],
            file=sys.stderr)
      return 1

  print("seed-import: replay ok: %s, uncle -> 0x%02X" % (
    ", ".join(checked_chests), expected_uncle))
  return 0


def find_title(rom):
  # The seed patch writes the LoROM title at PC 0x7FC0.  Also accept the
  # PC 0xFFC0 header for a headered dump.
  for off in (0x7FC0, 0xFFC0):
    if off + 21 <= len(rom):
      title = rom[off:off + 21].decode("ascii", "replace").rstrip("\x00 ")
      if title.startswith("VT "):
        return off, title
  off = 0x7FC0 if 0x7FC0 + 21 <= len(rom) else 0xFFC0
  return off, rom[off:off + 21].decode("ascii", "replace").rstrip("\x00 ")


def cmd_info(args):
  with open(args.rom, "rb") as f:
    rom = f.read()
  if len(rom) < 0x190000:
    die("ROM is too small to contain the z3randomizer tables")
  seed = load_seed(args.json) if args.json else None
  meta = seed.get("spoiler", {}).get("meta", {}) if seed else {}
  values = read_patch_values(seed) if seed else {}

  header_off, title = find_title(rom)
  print("ROM: %s" % args.rom)
  print("size: %d" % len(rom))
  print("md5: %s" % md5_hex(rom))
  print("crc32: %08X" % (zlib.crc32(rom) & 0xFFFFFFFF))
  print("title: %s" % title)
  if header_off != 0x7FC0:
    print("header: 0x%X" % header_off)

  seed_hash = list(rom[SEEDHASH_OFF:SEEDHASH_OFF + SEEDHASH_LEN])
  icons = [HASH_ICON_NAMES[v] if v < len(HASH_ICON_NAMES) else "unknown(0x%02X)" % v
           for v in seed_hash]
  print("seed hash: %s" % " ".join("%02X" % v for v in seed_hash))
  print("seed icons: %s" % ", ".join(icons))
  if seed:
    print("json hash: %s" % seed.get("hash", "?"))

  seed_type = rom[0x180210]
  game_type = rom[0x180211]
  warning = rom[0x180212]
  inverted = rom[0x18004A]
  print("randomizer: type=%d game_flags=0x%02X warning=0x%02X" %
        (seed_type, game_type, warning))
  flags = []
  for bit, name in ((3, "room"), (2, "item"), (1, "door/entrance"), (0, "enemy")):
    if game_type & (1 << bit):
      flags.append(name)
  print("randomized: %s" % (", ".join(flags) if flags else "nothing"))
  print("mode: %s" % ("inverted" if inverted else "normal (open/standard not stored in ROM)"))
  if meta:
    print("json mode: %s" % meta.get("mode", "?"))
    print("json goal: %s" % meta.get("goal", "?"))
    print("json logic: %s" % meta.get("logic", "?"))

  crystal_flags = rom[0x180050:0x180050 + 13]
  crystals = [DUNGEON_NAMES[i] for i, v in enumerate(crystal_flags) if v == 0x40]
  pendants = [DUNGEON_NAMES[i] for i, v in enumerate(crystal_flags) if v == 0x00]
  aga = [DUNGEON_NAMES[i] for i, v in enumerate(crystal_flags) if v in (0x01, 0x02)]
  print("crystals: %d%s" % (len(crystals), (": " + ", ".join(crystals)) if crystals else ""))
  print("pendants: %d%s" % (len(pendants), (": " + ", ".join(pendants)) if pendants else ""))
  if aga:
    print("agahnim slots: %s" % ", ".join(aga))
  if meta:
    print("json entry crystals: ganon=%s tower=%s" %
          (meta.get("entry_crystals_ganon", "?"), meta.get("entry_crystals_tower", "?")))

  print("dungeon prizes:")
  for i, name in enumerate(DUNGEON_NAMES):
    print("  %-17s %s" % (name, decode_hex_byte(crystal_flags[i])))
  print("dungeon prize receipts:")
  for i, name in enumerate(DUNGEON_NAMES):
    receipt = rom[0xC6FC + i]
    print("  %-17s %s (0x%02X)" % (name, item_name(receipt), receipt))

  print("fixed item tables:")
  for off, name, fields, rec_size, kind in FIXED_TABLES:
    for i, field in enumerate(fields):
      pos = off + i * rec_size
      if kind == "u16":
        value = read_u16(rom, pos)
        text = "0x%04X" % value
      elif kind == "flags":
        value = rom[pos]
        text = decode_hex_byte(value)
      elif kind == "prize_bits":
        value = rom[pos]
        parts = []
        for bit, name in ((0, "Pendant 3"), (1, "Pendant 2"), (2, "Pendant 1"),
                          (3, "Crystal 1"), (4, "Crystal 2"), (5, "Crystal 3"),
                          (6, "Crystal 4"), (7, "Crystal 5")):
          if value & (1 << bit):
            parts.append(name)
        text = "0x%02X (%s)" % (value, ", ".join(parts) if parts else "nothing")
      else:
        value = rom[pos]
        text = "%s (0x%02X)" % (item_name(value), value)
      print("  %-36s %s" % (field, text))
  for off, name, size, kind in SINGLE_VALUES:
    if kind == "u16":
      value = read_u16(rom, off)
      text = "0x%04X" % value
    elif kind == "item":
      value = rom[off]
      text = "%s (0x%02X)" % (item_name(value), value)
    else:
      value = rom[off]
      text = "%d (0x%02X)" % (value, value)
    print("  %-36s %s" % (name, text))

  # The 3-byte dungeon chest table is the one the z3randomizer rewrites
  # (item byte first, then the 15-bit room id and the big-chest bit).
  chest_major = []
  chest_other = 0
  for rec in range(CHEST_TABLE_RECORDS):
    off = CHEST_TABLE_OFF + rec * 3
    item = rom[off]
    room = rom[off + 1] | ((rom[off + 2] & 0x7F) << 8)
    big = bool(rom[off + 2] & 0x80)
    if room >= 320:
      continue
    if is_major_item(item):
      chest_major.append((room, big, item))
    else:
      chest_other += 1
  print("dungeon chests: %d records (%d non-major)" %
        (len(chest_major) + chest_other, chest_other))
  for room, big, item in chest_major:
    print("  room 0x%03X %-4s %s (0x%02X)" %
          (room, "big" if big else "small", item_name(item), item))

  # Other item-like writes in the seed patch are useful as a cross-check even
  # when the location names are not documented in the tables we have.
  if args.all_items and seed:
    known_prefixes = (0x180000, 0x184000, 0x184800, 0x184900, 0x187000, 0xE96E)
    other = []
    for addr, value in sorted(values.items()):
      if any(addr >= p and addr < p + 0x100 for p in known_prefixes):
        continue
      if is_major_item(value):
        other.append((addr, value))
    print("other major-item bytes in seed patch: %d" % len(other))
    for addr, value in other:
      print("  0x%06X %s (0x%02X)" % (addr, item_name(value), value))

  if seed:
    # Cross-check twelve documented addresses against the seed JSON patch.
    checks = [0x180000, 0x180010, 0x180020, 0x180028, 0x180052,
              0x180090, 0x180140, 0x180150, 0x180160, 0x180165,
              0x180215, 0x187000]
    ok = 0
    checked = 0
    for addr in checks:
      if addr in values:
        checked += 1
        if rom[addr] == values[addr]:
          ok += 1
    print("json/rom cross-check: %d/%d documented bytes match" % (ok, checked))
    spoiler = seed.get("spoiler", {})
    locations = spoiler.get("locations") if isinstance(spoiler, dict) else None
    if locations:
      print("spoiler locations: %d" % len(locations))
    else:
      print("spoiler locations: absent (only meta was published for this seed)")


def main():
  ap = argparse.ArgumentParser(description=__doc__)
  sub = ap.add_subparsers(dest="command", required=True)
  b = sub.add_parser("build", help="build a seed ROM")
  b.add_argument("--jp-rom", required=True, help="Japanese v1.0 ROM (CRC32 3322EFFC)")
  b.add_argument("--base", required=True, help="z3randomizer base BPS")
  b.add_argument("--seed", required=True, help="seed JSON from alttpr.com")
  b.add_argument("-o", "--output", required=True, help="output ROM")
  b.add_argument("--md5", help="expected md5 of the patched base ROM")
  b.set_defaults(func=cmd_build)

  i = sub.add_parser("info", help="read a seed ROM")
  i.add_argument("rom", help="seed ROM")
  i.add_argument("--json", help="optional seed JSON for meta and cross-checks")
  i.add_argument("--all-items", action="store_true",
                 help="also list major-item bytes outside the named tables")
  i.set_defaults(func=cmd_info)

  m = sub.add_parser("import", help="write a zelda3 mod from a seed ROM")
  m.add_argument("--rom", required=True, help="seed ROM")
  m.add_argument("--json", help="optional seed JSON for settings")
  m.add_argument("-o", "--output",
                 help="mod directory (default: mods-seeds/<seed>)")
  m.add_argument("--only", help="comma-separated location types to keep, or 'settings'")
  m.add_argument("--room", type=lambda value: int(value, 0),
                 help="keep only locations with this room/dungeon id")
  m.add_argument("--no-z3r-rules", action="store_true",
                 help="do not enable the Z3rRules feature in the mod")
  m.add_argument("--no-dialogue", action="store_true",
                 help="do not extract the z3r dialogue into content/dialogue.txt")
  m.add_argument("--build", action="store_true",
                 help="run modkit.py build after writing the dialogue content")
  m.set_defaults(func=cmd_import)

  c = sub.add_parser("coverage",
                     help="compare a seed ROM with a generated mod")
  c.add_argument("--rom", required=True, help="seed ROM")
  c.add_argument("--mod", help="generated mod directory or seed.json")
  c.add_argument("--summary", action="store_true",
                 help="print only the summary lines")
  c.add_argument("--json", help="seed JSON for the spoiler count check")
  c.set_defaults(func=cmd_coverage)

  v = sub.add_parser("verify-replay",
                     help="check a --mod-trace log against the seed ROM")
  v.add_argument("--rom", required=True, help="seed ROM")
  v.add_argument("--trace", required=True, help="captured trace file")
  v.set_defaults(func=cmd_verify_replay)

  p = sub.add_parser("verify-proof",
                     help="check one feature proof against the seed ROM")
  p.add_argument("--rom", required=True, help="seed ROM")
  p.add_argument("--proof", required=True,
                 choices=["npc", "prize", "shop", "shopdoor", "key", "start", "text"])
  p.add_argument("--trace", help="captured --mod-trace output")
  p.add_argument("--stderr", help="captured stderr")
  p.add_argument("--state", help="captured --dump-state output")
  p.add_argument("--message-id", type=lambda value: int(value, 0),
                 help="C dialogue message index for --proof text")
  p.set_defaults(func=cmd_verify_proof)
  args = ap.parse_args()
  args.func(args)


if __name__ == "__main__":
  main()
