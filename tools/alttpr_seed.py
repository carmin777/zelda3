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
import sys
import zlib

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
    if shop_id == 0xFF:
      break
    room = rom[off + 1] | (rom[off + 2] << 8)
    shops[shop_id] = {"room": room, "door": rom[off + 3]}

  locations = []
  per_shop = {}
  for i in range(SHOP_CONTENTS_MAX):
    off = SHOP_CONTENTS_OFF + i * 8
    shop_id = rom[off]
    value = rom[off + 1]
    if shop_id == 0xFF or value == 0xFF:
      break
    slot = per_shop.get(shop_id, 0)
    per_shop[shop_id] = slot + 1
    price = read_u16(rom, off + 2)
    room = shops.get(shop_id, {}).get("room", -1)
    loc_id = "shop:%d:%d" % (shop_id, slot)
    locations.append(make_location(
      "shop", loc_id, "shop %d slot %d%s" % (
        shop_id, slot, " room 0x%03X" % room if room >= 0 else ""),
      value, shop=shop_id, slot=slot, price=price, room=room))
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


def cmd_import(args):
  with open(args.rom, "rb") as f:
    rom = f.read()
  if len(rom) < 0x190000:
    die("ROM is too small to contain the z3randomizer tables")
  seed = load_seed(args.json) if args.json else None
  name = seed_name_from_rom(rom, seed)
  mod_name = safe_name(name)
  locations = collect_locations(rom)
  if args.only:
    wanted = {name.strip() for name in args.only.split(",") if name.strip()}
    locations = [loc for loc in locations if loc["type"] in wanted]
  if args.room is not None:
    locations = [loc for loc in locations if loc.get("room") == args.room]
  if not locations:
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

  print("seed-import: %s (title %s)" % (name, data["rom"]["title"]))
  print("seed-import: %d location(s): %s" %
        (len(locations), format_counts(counts)))
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
    needle = "seed-dungeon target=%d item=24" % target
    found = False
    if args.stderr:
      with open(args.stderr, "r", encoding="utf-8", errors="replace") as f:
        found = needle in f.read()
    if not found:
      print("seed-proof key: missing '%s' in stderr" % needle, file=sys.stderr)
      return 1
    print("seed-proof key: 0x%02X -> dungeon %d (current %d)" % (raw, target, current))
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
  m.add_argument("--only", help="comma-separated location types to keep")
  m.add_argument("--room", type=lambda value: int(value, 0),
                 help="keep only locations with this room/dungeon id")
  m.add_argument("--no-z3r-rules", action="store_true",
                 help="do not enable the Z3rRules feature in the mod")
  m.set_defaults(func=cmd_import)

  c = sub.add_parser("coverage",
                     help="compare a seed ROM with a generated mod")
  c.add_argument("--rom", required=True, help="seed ROM")
  c.add_argument("--mod", help="generated mod directory or seed.json")
  c.add_argument("--summary", action="store_true",
                 help="print only the summary lines")
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
                 choices=["npc", "prize", "shop", "key", "start"])
  p.add_argument("--trace", help="captured --mod-trace output")
  p.add_argument("--stderr", help="captured stderr")
  p.add_argument("--state", help="captured --dump-state output")
  p.set_defaults(func=cmd_verify_proof)
  args = ap.parse_args()
  args.func(args)


if __name__ == "__main__":
  main()
