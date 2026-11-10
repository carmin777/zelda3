#!/usr/bin/env python3
"""Build binary asset overrides for zelda3 mods from editable sources.

A mod keeps its sources under ``content/`` using the same relative layout as
the extracted ``assets/`` directory (dungeon/*.yaml, overworld/*.yaml,
dialogue*.txt, font*.png, sprites/*.png, linksprite.png, ...).  Dungeon and
overworld YAML files may contain only the top-level keys the mod changes, and
dialogue files may contain only the changed ``<id>: <text>`` messages; the tool
merges both with the extracted base.  A full extracted YAML still replaces the
whole document.  The tool extracts a base tree from the ROM once, compiles both
the base and the mod version with the same compiler and languages, and writes
only the assets that actually changed to ``<mod>/build/<asset>.bin``.

The generated entries are tracked by a top-level ``generated_overrides`` list
in ``mod.json`` so a later build can remove entries it no longer needs without
touching overrides added by hand.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from collections import OrderedDict
from pathlib import Path

import yaml

TOOL_VERSION = "1.3.1"

ROOT = Path(__file__).resolve().parent.parent
ASSET_DIR = ROOT / "assets"
OTHER_DIR = ROOT / "other"
CACHE_DIR = ROOT / ".modkit" / "base"
CACHE_FILES = CACHE_DIR / "files"
CACHE_MANIFEST = CACHE_DIR / "manifest.json"

# Files needed to run the resource compiler/extractor.  The generated content
# lives in the cache, not in the repository.
ASSET_CODE_FILES = (
  "compile_music.py",
  "compile_resources.py",
  "decode_music.py",
  "extract_music.py",
  "extract_resources.py",
  "overworld_tables.py",
  "palette_usage.bin",
  "restool.py",
  "sprite_sheet_info.py",
  "sprite_sheets.py",
  "tables.py",
  "text_compression.py",
  "util.py",
)
ASSET_OTHER_FILES = ("3x5_font.png",)

EXTRA_LANGUAGES = ("de", "fr", "fr-c", "en", "es", "pl", "pt", "redux", "nl", "sv")
LANGUAGE_ROM_NAMES = {
  "de": ("german.sfc", "german.smc"),
  "fr": ("french.sfc", "french.smc"),
  "es": ("spanish.sfc", "spanish.smc"),
  "pl": ("polish.sfc", "polish.smc"),
  "pt": ("portuguese.sfc", "portuguese.smc"),
  "nl": ("dutch.sfc", "dutch.smc"),
  "sv": ("swedish.sfc", "swedish.smc"),
  "en": ("english.sfc", "english.smc"),
  "redux": ("redux.sfc", "redux.smc"),
}

DAT_MAGIC = b"Zelda3_v0     \n\0"

# Palettes exposed by src/mod/asset_names.c. import-rom imports all of them
# when they differ from the US base.
PALETTE_ASSETS = frozenset((
  "kPalette_DungBgMain",
  "kPalette_MainSpr",
  "kPalette_ArmorAndGloves",
  "kPalette_Sword",
  "kPalette_Shield",
  "kPalette_SpriteAux3",
  "kPalette_MiscSprite_Indoors",
  "kPalette_SpriteAux1",
  "kPalette_OverworldBgMain",
  "kPalette_OverworldBgAux12",
  "kPalette_OverworldBgAux3",
  "kPalette_PalaceMapBg",
  "kPalette_PalaceMapSpr",
  "kHudPalData",
  "kOverworldMapPaletteData",
))
MAP_ASSETS = frozenset((
  "kOverworldMapGfx",
  "kLightOverworldTilemap",
  "kDarkOverworldTilemap",
  "kDungMap_FloorLayout",
  "kDungMap_Tiles",
))
# Dungeon rooms, headers, chests, sprites, secrets, entrances and starting
# points are extracted from the translated/hack ROM, compiled and imported
# when they differ from the US base.
DUNGEON_ASSETS = frozenset((
  "kDungeonRoom",
  "kDungeonRoomOffs",
  "kDungeonRoomDoorOffs",
  "kDungeonRoomHeaders",
  "kDungeonRoomHeadersOffs",
  "kDungeonRoomChests",
  "kDungeonRoomTeleMsg",
  "kDungeonPitsHurtPlayer",
  "kDungeonRoomDefault",
  "kDungeonRoomDefaultOffs",
  "kDungeonRoomOverlay",
  "kDungeonRoomOverlayOffs",
  "kDungeonSecrets",
  "kDungeonSprites",
  "kDungeonSpriteOffs",
  "kEntranceData_rooms",
  "kEntranceData_relativeCoords",
  "kEntranceData_scrollX",
  "kEntranceData_scrollY",
  "kEntranceData_playerX",
  "kEntranceData_playerY",
  "kEntranceData_cameraX",
  "kEntranceData_cameraY",
  "kEntranceData_blockset",
  "kEntranceData_floor",
  "kEntranceData_palace",
  "kEntranceData_doorwayOrientation",
  "kEntranceData_startingBg",
  "kEntranceData_quadrant1",
  "kEntranceData_quadrant2",
  "kEntranceData_doorSettings",
  "kEntranceData_musicTrack",
  "kStartingPoint_rooms",
  "kStartingPoint_relativeCoords",
  "kStartingPoint_scrollX",
  "kStartingPoint_scrollY",
  "kStartingPoint_playerX",
  "kStartingPoint_playerY",
  "kStartingPoint_cameraX",
  "kStartingPoint_cameraY",
  "kStartingPoint_blockset",
  "kStartingPoint_floor",
  "kStartingPoint_palace",
  "kStartingPoint_doorwayOrientation",
  "kStartingPoint_startingBg",
  "kStartingPoint_quadrant1",
  "kStartingPoint_quadrant2",
  "kStartingPoint_doorSettings",
  "kStartingPoint_entrance",
  "kStartingPoint_musicTrack",
))
# Overworld areas, headers, music, entrances/exits, holes, secrets, sprites,
# map32->map16 and the per-area gfx/palette tables are extracted from the
# translated/hack ROM YAML and imported when they differ from the US base.
OVERWORLD_ASSETS = frozenset((
  "kOverworld_Hibytes_Comp",
  "kOverworld_Lobytes_Comp",
  "kOverworldMapIsSmall",
  "kOverworldAuxTileThemeIndexes",
  "kOverworldBgPalettes",
  "kOverworld_SignText",
  "kOwMusicSets",
  "kOwMusicSets2",
  "kBirdTravel_ScreenIndex",
  "kBirdTravel_Map16LoadSrcOff",
  "kBirdTravel_ScrollX",
  "kBirdTravel_ScrollY",
  "kBirdTravel_LinkXCoord",
  "kBirdTravel_LinkYCoord",
  "kBirdTravel_CameraXScroll",
  "kBirdTravel_CameraYScroll",
  "kBirdTravel_Unk1",
  "kBirdTravel_Unk3",
  "kWhirlpoolAreas",
  "kOverworld_Entrance_Area",
  "kOverworld_Entrance_Pos",
  "kOverworld_Entrance_Id",
  "kFallHole_Area",
  "kFallHole_Pos",
  "kFallHole_Entrances",
  "kExitData_ScreenIndex",
  "kExitDataRooms",
  "kExitData_Map16LoadSrcOff",
  "kExitData_ScrollX",
  "kExitData_ScrollY",
  "kExitData_XCoord",
  "kExitData_YCoord",
  "kExitData_CameraXScroll",
  "kExitData_CameraYScroll",
  "kExitData_NormalDoor",
  "kExitData_FancyDoor",
  "kExitData_Unk1",
  "kExitData_Unk3",
  "kSpExit_Top",
  "kSpExit_Bottom",
  "kSpExit_Left",
  "kSpExit_Right",
  "kSpExit_Tab4",
  "kSpExit_Tab5",
  "kSpExit_Tab6",
  "kSpExit_Tab7",
  "kSpExit_LeftEdgeOfMap",
  "kSpExit_Dir",
  "kSpExit_SprGfx",
  "kSpExit_AuxGfx",
  "kSpExit_PalBg",
  "kSpExit_PalSpr",
  "kOverworldSecrets_Offs",
  "kOverworldSecrets",
  "kOverworldSpriteOffs",
  "kOverworldSprites",
  "kOverworldSpriteGfx",
  "kOverworldSpritePalettes",
  "kMap32ToMap16_0",
  "kMap32ToMap16_1",
  "kMap32ToMap16_2",
  "kMap32ToMap16_3",
  "kMap16ToMap8",
  "kMap8DataToTileAttr",
  "kSomeTileAttr",
))
# Ending credits and epilogue scene text have their own layout parsers but are
# imported by default like the other text content.
CREDITS_ASSETS = frozenset(("kEnding_Credits_Text", "kEnding_Credits_Offs"))
EPILOGUE_ASSETS = frozenset(("kEnding0_Data", "kEnding0_Offs"))
TEXT_ASSETS = CREDITS_ASSETS | EPILOGUE_ASSETS
# Dialogue/font and Link graphics come from the translated ROM itself. Known
# translations keep their curated asset set; hacks (and Redux) import them by
# default when they differ from the US base.
DIALOGUE_ASSETS = frozenset(("kDialogue", "kDialogueFont"))
LINK_ASSETS = frozenset(("kLinkGraphics",))
# Assets imported by default by ``import-rom``: graphics, tilemaps, palettes,
# maps, dungeon/overworld content, ending credits and epilogue scene text.
HACK_ONLY_ASSETS = frozenset((
  # Known translations already ship a curated asset set that keeps the
  # reference .dat unchanged. Hack map16 IDs differ, so this table is needed
  # there; it can still be forced with --assets.
  "kMap16ToMap8",
))
DEFAULT_IMPORT_ASSETS = (
  "kSprGfx",
  "kBgGfx",
  "kBgTilemap_0",
  "kBgTilemap_1",
  "kBgTilemap_2",
  "kBgTilemap_3",
  "kBgTilemap_4",
  "kBgTilemap_5",
) + tuple(sorted(MAP_ASSETS)) + tuple(sorted(PALETTE_ASSETS)) + tuple(
  sorted(DUNGEON_ASSETS)) + tuple(sorted(OVERWORLD_ASSETS)) + tuple(
  sorted(TEXT_ASSETS)) + tuple(sorted(DIALOGUE_ASSETS)) + tuple(
  sorted(LINK_ASSETS))
INTERFACE_ASSETS = frozenset((
  "kSprGfx",
  "kBgGfx",
  "kBgTilemap_0",
  "kBgTilemap_1",
  "kBgTilemap_2",
  "kBgTilemap_3",
  "kBgTilemap_4",
  "kBgTilemap_5",
))
GAME_TABLES_DEF = ROOT / "src" / "mod" / "game_tables.def"
# Corrections for specific translated ROMs, keyed by the exact SHA-1 of the
# source ROM. Each entry lists fixed-layout tables and the order of their
# 8-word rows. These are documented data fixes for known translations; the
# generic importer and the C code never guess from ROM contents.
KNOWN_ROM_CORRECTIONS = {
  "d0d09ed41f9c373fe6afdccafbf0da8c88d3d90d": (
    OrderedDict((
      ("table", "kHudBowItemText"),
      # Original PT-BR rows: blank/"Arco e", "Flecha"/"Arco",
      # "Arco e"/"Flecha de Prata". The first two entries are one line off.
      ("description", "texto do arco"),
      ("rows", (1, 2, 0, 3, 4, 5)),
    )),
  ),
}
ENDING_CREDITS_COUNT = 394
# Start of Credits_AddNextAttribution in the US ROM (SNES $8EBE24). The bytes
# are unchanged in the PT-BR translation, so they locate the routine there too.
ENDING_CREDITS_ROUTINE = bytes.fromhex(
  "8b4babc230ae0010a5c8eb9d0210a9403e9d0410")
ENDING_EPILOGUE_COUNT = 17
# Start of Credits_AddEndingSequenceText (SNES $8EC303). The prefix is shared
# by the US and PT-BR routines; the data operands after it are read from the
# translated ROM instead of assuming the US addresses.
ENDING_EPILOGUE_ROUTINE = bytes.fromhex(
  "8b4babc230a960008d0210a947fe8d0410")


class ModkitError(Exception):
  pass


def log(message):
  print("modkit: %s" % message, flush=True)


def sha1_bytes(data):
  return hashlib.sha1(data).hexdigest()


def sha1_file(path):
  h = hashlib.sha1()
  with open(path, "rb") as f:
    for chunk in iter(lambda: f.read(1 << 20), b""):
      h.update(chunk)
  return h.hexdigest()


def known_rom_hashes():
  """Hashes of the translations the asset tools already know about."""
  text = (ASSET_DIR / "util.py").read_text(encoding="utf8")
  return {value.lower() for value in re.findall(r"'([0-9A-Fa-f]{40})'", text)}


def known_rom_languages():
  """Map known translation SHA-1s to their language code in assets/util.py.

  Hacks are absent from this table. Redux is kept as a hack-like source so its
  dialogue still follows the default import path, while curated translations
  (pt, de, fr, ...) keep the old behavior unless their assets are requested
  explicitly.
  """
  text = (ASSET_DIR / "util.py").read_text(encoding="utf8")
  return {
    match.group(1).lower(): match.group(2)
    for match in re.finditer(
      r"'([0-9A-Fa-f]{40})'\s*:\s*\(\s*'([^']+)'", text)
  }


def read_rom_bytes(path):
  data = Path(path).read_bytes()
  if (len(data) & 0xfffff) == 0x200:
    data = data[0x200:]
  return data


def snes_to_file_offset(address):
  return ((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff)


def read_snes_bytes(data, address, size, what):
  offset = snes_to_file_offset(address)
  if size < 0 or offset + size > len(data):
    raise ModkitError("truncated %s at SNES $%06x" % (what, address))
  return data[offset:offset + size]


def read_overworld_area_heads(rom_path):
  """Return kOverworldAreaHeads bytes, following the routine that reads it.

  The US code loads the 64-byte table with LDA $02A5EC,X. A Link to the
  Islands replaces that instruction with a JSL to a new routine that still
  reads the same table; follow the hook instead of assuming the US address.
  """
  data = read_rom_bytes(rom_path)
  code = snes_to_file_offset(0x82AA7D)
  first = data[code:code + 4]
  if first == b"\xbf\xec\xa5\x02":
    table = 0x02A5EC
  elif first[:1] == b"\x22":
    target = data[code + 1] | data[code + 2] << 8 | data[code + 3] << 16
    target_offset = snes_to_file_offset(target)
    prefix = data[target_offset:target_offset + 12]
    if prefix[:8] != bytes.fromhex("eaeac93ff008bfec"):
      raise ModkitError(
        "unrecognized overworld area head hook at $82AA7D -> $%06X: %s" % (
          target, prefix.hex(" ")))
    table = (data[target_offset + 7] |
             data[target_offset + 8] << 8 |
             data[target_offset + 9] << 16)
  else:
    raise ModkitError(
      "unrecognized overworld area head load at $82AA7D: %s" % first.hex(" "))
  values = read_snes_bytes(data, table, 64, "overworld area head table")
  if any(value > 0x3F for value in values):
    raise ModkitError(
      "overworld area head table $%06X has an entry above $3F" % table)
  return values


def read_game_tables():
  """Read the table registry shared with src/mod/game_tables.c.

  The .def file is included by the C registry and parsed here, so names,
  sizes and ROM addresses have a single source of truth. Direct tables are
  read from a fixed US ROM address; derived tables are built by decoding the
  translated ROM's game-over routines.
  """
  if not GAME_TABLES_DEF.is_file():
    raise ModkitError("missing table registry: %s" % GAME_TABLES_DEF)
  tables = []
  seen = set()
  for line_no, line in enumerate(
      GAME_TABLES_DEF.read_text(encoding="utf8").splitlines(), 1):
    line = line.split("//", 1)[0].strip()
    if not line:
      continue
    match = re.fullmatch(
      r"MOD_TABLE\(\s*(\w+)\s*,\s*(\d+)\s*,\s*(0x[0-9a-fA-F]+)\s*\)", line)
    is_derived = False
    if match:
      name = match.group(1)
      count = int(match.group(2))
      size = count * 2
      address = int(match.group(3), 16)
      unit = "word"
    else:
      match = re.fullmatch(
        r"MOD_TABLE_BYTES\(\s*(\w+)\s*,\s*(\d+)\s*,\s*(0x[0-9a-fA-F]+)\s*\)",
        line)
      if match:
        name = match.group(1)
        count = int(match.group(2))
        size = count
        address = int(match.group(3), 16)
        unit = "byte"
      else:
        match = re.fullmatch(
          r"MOD_TABLE_DERIVED\(\s*(\w+)\s*,\s*(\d+)\s*\)", line)
        if not match:
          raise ModkitError("%s:%d: invalid table entry" % (
            GAME_TABLES_DEF, line_no))
        name = match.group(1)
        count = int(match.group(2))
        size = count
        address = None
        unit = "byte"
        is_derived = True
    if count <= 0 or size > 0x20000:
      raise ModkitError("%s:%d: invalid %s count for '%s'" % (
        GAME_TABLES_DEF, line_no, unit, name))
    if name in seen:
      raise ModkitError("%s:%d: duplicate table '%s'" % (
        GAME_TABLES_DEF, line_no, name))
    seen.add(name)
    tables.append(OrderedDict((
      ("name", name),
      ("size", size),
      ("rom_addr", address),
      ("derived", is_derived),
    )))
  if not tables:
    raise ModkitError("%s: no table entries" % GAME_TABLES_DEF)
  return tables


def apply_known_rom_corrections(rom_sha1, name, data):
  """Apply documented row reorderings for a known translated ROM.

  Corrections are selected only by the exact SHA-1 of the source ROM, so an
  unknown hack or a modified translation keeps its own bytes. A row is the
  fixed 8-word line used by the HUD item-name tables.
  """
  applied = []
  for correction in KNOWN_ROM_CORRECTIONS.get(rom_sha1, ()):
    if correction["table"] != name:
      continue
    row_words = 8
    row_bytes = row_words * 2
    row_count = len(data) // row_bytes
    rows = correction["rows"]
    if (len(data) % row_bytes or len(rows) != row_count or
        sorted(rows) != list(range(row_count))):
      raise ModkitError(
        "known correction for '%s' needs %d rows of %d words, got %d bytes" % (
          name, len(rows), row_words, len(data)))
    data = b"".join(
      data[row * row_bytes:(row + 1) * row_bytes] for row in rows)
    applied.append(correction)
  return data, applied


def read_ending_credits_addresses(rom_path):
  """Find the ending-credit text and offset tables in a ROM.

  Credits_AddNextAttribution loads them with two absolute,Y instructions. The
  routine start is the same in the US and PT-BR ROMs, so read the operands
  instead of assuming the US addresses.
  """
  data = read_rom_bytes(rom_path)
  index = data.find(ENDING_CREDITS_ROUTINE)
  if index < 0:
    return None
  bank = ((index // 0x8000) & 0x7f) | 0x80
  match = re.search(rb"\xB9(..)\xA8\xB9(..)", data[index:index + 0x60], re.S)
  if match is None:
    return None

  def absolute(operand):
    value = operand[0] | operand[1] << 8
    return (bank << 16) | value

  return absolute(match.group(1)), absolute(match.group(2))


def read_ending_credits_assets(rom_path):
  """Read the translated credits text/offsets, following the ROM's pointers.

  Two entry layouts share the same offsets table:

  - ``map_index`` (US): scroll, count, then ``(count + 1) >> 1`` bytes, each
    an index into ``kEnding_MapData``.
  - ``raw_words`` (PT-BR): scroll, count, one high byte, then
    ``(count + 1) >> 1`` low bytes; each glyph is ``(high << 8) | low`` and
    is written straight to VRAM.

  In both layouts a non-empty entry ends exactly where the next distinct
  offset starts. Count how many entries fit each layout instead of guessing
  from the payload bytes: the PT-BR low bytes include values below 160, so a
  byte heuristic would misclassify some entries.
  """
  addresses = read_ending_credits_addresses(rom_path)
  if addresses is None:
    return None
  offs_addr, text_addr = addresses
  data = read_rom_bytes(rom_path)
  offs = read_snes_bytes(data, offs_addr, ENDING_CREDITS_COUNT * 2,
                         "ending credits offsets")
  values = struct.unpack_from("<%dH" % ENDING_CREDITS_COUNT, offs)
  text_offset = snes_to_file_offset(text_addr)
  available = data[text_offset:]
  unique = sorted(set(value for value in values if value < len(available)))
  map_fits = raw_fits = 0
  for i, offset in enumerate(unique):
    if offset + 2 > len(available) or available[offset] == 0xff:
      continue
    count = (available[offset + 1] + 1) >> 1
    next_offset = unique[i + 1] if i + 1 < len(unique) else len(available)
    if offset + 2 + count == next_offset:
      map_fits += 1
    if offset + 2 + 1 + count == next_offset:
      raw_fits += 1
  if map_fits == 0 and raw_fits == 0:
    raise ModkitError(
      "unrecognized ending credits entry layout at SNES $%06x" % text_addr)
  raw_words = raw_fits > map_fits
  size = 0
  for offset in unique:
    if offset + 2 > len(available) or available[offset] == 0xff:
      continue
    count = (available[offset + 1] + 1) >> 1
    end = offset + 2 + (1 + count if raw_words else count)
    size = max(size, end)
  if size == 0 or text_offset + size > len(data):
    raise ModkitError("invalid ending credits text at SNES $%06x" % text_addr)
  return OrderedDict((
    ("text", bytes(data[text_offset:text_offset + size])),
    ("offs", bytes(offs)),
    ("text_addr", text_addr),
    ("offs_addr", offs_addr),
    ("format", "raw_words" if raw_words else "map_index"),
  ))


def read_ending_epilogue_assets(rom_path):
  """Read the translated epilogue offset/data tables, following the ROM code.

  ``Credits_AddEndingSequenceText`` starts with a byte prefix shared by the US
  and PT-BR ROMs. After it, the routine loads the offset table with two
  absolute,Y instructions and then either reads the data in place (US) or
  jumps to a relocated loader (PT-BR) that sets a long pointer to the data.

  Each scene is a sequence of 4-byte VRAM headers. In the US layout every
  payload byte is one index into ``kEnding_MapData``. In the PT-BR layout the
  payload starts with one high byte and each following byte is a low byte, so
  each tilemap word is ``(high << 8) | low``. Count how many entries tile
  exactly in each layout instead of guessing from byte values.
  """
  data = read_rom_bytes(rom_path)
  index = data.find(ENDING_EPILOGUE_ROUTINE)
  if index < 0:
    return None
  bank = ((index // 0x8000) & 0x7f) | 0x80
  start = index + len(ENDING_EPILOGUE_ROUTINE)
  match = re.search(rb"\xB9(..)\x85\x04\xB9(..)\xA8\xA2\x00\x00",
                    data[start:start + 0x40], re.S)
  if match is None:
    return None

  def absolute(operand):
    return (bank << 16) | (operand[0] | operand[1] << 8)

  end_addr = absolute(match.group(1))
  offs_addr = absolute(match.group(2))
  if end_addr != offs_addr + 2:
    raise ModkitError(
      "ending epilogue offsets use different tables: $%06x and $%06x" % (
        offs_addr, end_addr))

  source = data[start + match.end():start + match.end() + 4]
  if source[:1] == b"\xb9":
    data_addr = (bank << 16) | (source[1] | source[2] << 8)
  elif source[:1] == b"\x5c":
    target = source[1] | source[2] << 8 | source[3] << 16
    target_offset = snes_to_file_offset(target)
    loader = data[target_offset:target_offset + 16]
    pointer = re.search(rb"\xA9(..)\x85\x66\xA9(..)\x85\x68", loader, re.S)
    if pointer is None:
      raise ModkitError(
        "unrecognized ending epilogue loader at SNES $%06x" % target)
    low = pointer.group(1)[0] | pointer.group(1)[1] << 8
    high = pointer.group(2)[0] | pointer.group(2)[1] << 8
    data_addr = high << 16 | low
  else:
    raise ModkitError(
      "unrecognized ending epilogue data load at SNES $%06x" % (
        (bank << 16) | (start + match.end())))

  offs = read_snes_bytes(data, offs_addr, ENDING_EPILOGUE_COUNT * 2,
                         "ending epilogue offsets")
  values = struct.unpack_from("<%dH" % ENDING_EPILOGUE_COUNT, offs)
  if list(values) != sorted(values) or values[-1] == 0:
    raise ModkitError(
      "invalid ending epilogue offset table at SNES $%06x" % offs_addr)

  data_offset = snes_to_file_offset(data_addr)
  size = values[-1]
  if data_offset + size > len(data):
    raise ModkitError(
      "ending epilogue data at SNES $%06x is truncated" % data_addr)
  text = bytes(data[data_offset:data_offset + size])

  unique = sorted(set(value for value in values if value < size))
  map_fits = raw_fits = 0
  for i, start_offset in enumerate(unique):
    end_offset = unique[i + 1] if i + 1 < len(unique) else size
    if start_offset >= end_offset:
      continue
    for layout in (0, 1):
      pos = start_offset
      fits = True
      while pos < end_offset:
        if pos + 4 > size:
          fits = False
          break
        count = ((text[pos + 2] | text[pos + 3] << 8) >> 9 & 0x7f) + 1
        step = 4 + count + layout
        if pos + step > size or pos + step > end_offset:
          fits = False
          break
        pos += step
      if fits and pos == end_offset:
        if layout:
          raw_fits += 1
        else:
          map_fits += 1
  if map_fits == 0 and raw_fits == 0:
    raise ModkitError(
      "unrecognized ending epilogue entry layout at SNES $%06x" % data_addr)
  raw_words = raw_fits > map_fits
  return OrderedDict((
    ("data", text),
    ("offs", bytes(offs)),
    ("data_addr", data_addr),
    ("offs_addr", offs_addr),
    ("format", "raw_words" if raw_words else "map_index"),
  ))


def read_gfx_pointer_tables(rom_path):
  """Read the translated ROM's decompression pointer tables.

  The SNES code in Decomp_spr/Decomp_bg loads three parallel byte tables
  (bank, high byte, low byte) to build a 24-bit pointer. The table addresses
  are operands of those routines, so read them instead of hardcoding them.
  """
  data = read_rom_bytes(rom_path)

  def table_addresses(code_address, label):
    offset = snes_to_file_offset(code_address)
    b = data[offset:offset + 15]
    if (len(b) != 15 or b[0] != 0xb9 or b[3] != 0x85 or b[4] != 0xca or
        b[5] != 0xb9 or b[8] != 0x85 or b[9] != 0xc9 or
        b[10] != 0xb9 or b[13] != 0x85 or b[14] != 0xc8):
      raise ModkitError(
        "cannot find the graphics pointer tables: expected the %s routine at "
        "SNES $%06x" % (label, code_address))
    return b[1] | b[2] << 8, b[6] | b[7] << 8, b[11] | b[12] << 8

  def read_pointers(addresses, count, label):
    bank, high, low = addresses
    result = []
    for i in range(count):
      values = []
      for table in (bank, high, low):
        offset = snes_to_file_offset(table + i)
        if offset >= len(data):
          raise ModkitError("truncated %s graphics pointer table" % label)
        values.append(data[offset])
      result.append(values[0] << 16 | values[1] << 8 | values[2])
    for pointer in result:
      if snes_to_file_offset(pointer) >= len(data):
        raise ModkitError(
          "invalid %s graphics pointer $%06x in translated ROM" % (label, pointer))
    return result

  return OrderedDict((
    ("spr", read_pointers(table_addresses(0x80e772, "Decomp_spr"), 108, "sprite")),
    ("bg", read_pointers(table_addresses(0x80e78f, "Decomp_bg"), 115, "background")),
  ))


def unpack_packed_arrays(data):
  """Split the format written by pack_arrays() into its individual blobs."""
  if not data:
    return []
  if len(data) < 2:
    raise ModkitError("truncated packed asset")
  last = struct.unpack_from("<H", data, len(data) - 2)[0]
  if last >= 8192:
    count = last - 8192 + 1
    offset_size, offset_format = 4, "<I"
  else:
    count = last + 1
    offset_size, offset_format = 2, "<H"
  offsets_end = offset_size * (count - 1)
  if offsets_end + 2 > len(data):
    raise ModkitError("truncated packed asset offset table")
  offsets = [struct.unpack_from(offset_format, data, i * offset_size)[0]
             for i in range(count - 1)]
  payload_start = offsets_end
  payload_end = len(data) - 2
  boundaries = [0] + offsets + [payload_end - payload_start]
  return [data[payload_start + boundaries[i]:payload_start + boundaries[i + 1]]
          for i in range(count)]


def pack_packed_arrays(arr):
  """Write the same packed-array format as assets/compile_resources.py."""
  if not arr:
    return b""
  offsets = []
  total = 0
  for item in arr[:-1]:
    total += len(item)
    offsets.append(total)
  if total < 65536 and len(arr) <= 8192:
    return (b"".join(struct.pack("<H", value) for value in offsets) +
            b"".join(arr) + struct.pack("<H", len(arr) - 1))
  return (b"".join(struct.pack("<I", value) for value in offsets) +
          b"".join(arr) + struct.pack("<H", 8192 + len(arr) - 1))


def decompress_gfx(data):
  """Decompress one graphics pack (the ROM's 0xff-terminated format)."""
  out = bytearray()
  i = 0
  n = len(data)
  while True:
    if i >= n:
      raise ModkitError("truncated compressed graphics")
    cmd = data[i]
    i += 1
    if cmd == 0xff:
      return bytes(out)
    if (cmd & 0xe0) != 0xe0:
      length = (cmd & 0x1f) + 1
      cmd &= 0xe0
    else:
      if i >= n:
        raise ModkitError("truncated compressed graphics")
      length = ((cmd & 3) << 8) + data[i] + 1
      i += 1
      cmd = (cmd << 3) & 0xe0
    if cmd == 0:
      if i + length > n:
        raise ModkitError("truncated compressed graphics literal")
      out.extend(data[i:i + length])
      i += length
    elif cmd & 0x80:
      if i + 2 > n:
        raise ModkitError("truncated compressed graphics back-reference")
      # Graphics packs store the back-reference offset little-endian.
      offset = data[i] | data[i + 1] << 8
      i += 2
      for _ in range(length):
        if offset >= len(out):
          raise ModkitError("invalid compressed graphics back-reference")
        out.append(out[offset])
        offset += 1
    elif (cmd & 0x40) == 0:
      if i >= n:
        raise ModkitError("truncated compressed graphics memset")
      out.extend(bytes((data[i],)) * length)
      i += 1
    elif (cmd & 0x20) == 0:
      if i + 2 > n:
        raise ModkitError("truncated compressed graphics memset16")
      b1, b2 = data[i], data[i + 1]
      i += 2
      for j in range(length):
        out.append(b1 if (j & 1) == 0 else b2)
    else:
      if i >= n:
        raise ModkitError("truncated compressed graphics incr")
      b = data[i]
      i += 1
      for _ in range(length):
        out.append(b)
        b = (b + 1) & 0xff


def changed_gfx_packs(base_data, translated_data, raw_prefix):
  base_packs = unpack_packed_arrays(base_data)
  translated_packs = unpack_packed_arrays(translated_data)
  if len(base_packs) != len(translated_packs):
    return []
  changed = []
  for i, (a, b) in enumerate(zip(base_packs, translated_packs)):
    if i < raw_prefix:
      same = a == b
    else:
      try:
        same = decompress_gfx(a) == decompress_gfx(b)
      except ModkitError:
        same = a == b
    if not same:
      changed.append(i)
  return changed


def read_json(path):
  with open(path, "r", encoding="utf8") as f:
    return json.load(f, object_pairs_hook=OrderedDict)


def write_json(path, value):
  path.parent.mkdir(parents=True, exist_ok=True)
  with open(path, "w", encoding="utf8", newline="\n") as f:
    json.dump(value, f, indent=2, ensure_ascii=False)
    f.write("\n")


def run_tool(command, cwd, what, env=None):
  result = subprocess.run(
    command, cwd=str(cwd), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    text=True, errors="replace", env=env)
  if result.returncode != 0:
    output = result.stdout.strip().splitlines()
    tail = "\n".join(output[-40:])
    raise ModkitError("%s failed (exit %d)\n%s" % (what, result.returncode, tail))
  return result.stdout


def code_fingerprint():
  h = hashlib.sha1()
  for name in ASSET_CODE_FILES:
    path = ASSET_DIR / name
    if not path.is_file():
      raise ModkitError("missing asset compiler file: %s" % path)
    h.update(name.encode("utf8"))
    h.update(b"\0")
    h.update(path.read_bytes())
  for name in ASSET_OTHER_FILES:
    path = OTHER_DIR / name
    if not path.is_file():
      raise ModkitError("missing asset support file: %s" % path)
    h.update(name.encode("utf8"))
    h.update(b"\0")
    h.update(path.read_bytes())
  return h.hexdigest()


def copy_compiler_files(workspace):
  assets = workspace / "assets"
  assets.mkdir(parents=True, exist_ok=True)
  for name in ASSET_CODE_FILES:
    shutil.copy2(ASSET_DIR / name, assets / name)
  (assets / "sprites").mkdir(exist_ok=True)
  other = workspace / "other"
  other.mkdir(parents=True, exist_ok=True)
  for name in ASSET_OTHER_FILES:
    shutil.copy2(OTHER_DIR / name, other / name)
  return assets


def resolve_rom(value):
  path = Path(value)
  candidates = [path] if path.is_absolute() else [Path.cwd() / path, ROOT / path]
  for candidate in candidates:
    if candidate.is_file():
      return candidate.resolve()
  raise ModkitError("ROM not found: %s" % value)


def resolve_base_dat(value):
  """Find the zelda3_assets.dat whose language slots the mod must match."""
  if value:
    path = Path(value)
    candidates = [path] if path.is_absolute() else [Path.cwd() / path, ROOT / path]
    for candidate in candidates:
      if candidate.is_file():
        return candidate.resolve()
    raise ModkitError("base assets file not found: %s" % value)
  default = ROOT / "zelda3_assets.dat"
  return default.resolve() if default.is_file() else None


def cache_is_valid(rom, manifest):
  if not manifest:
    return False
  if manifest.get("rom_sha1") != sha1_file(rom):
    return False
  if manifest.get("assets_sha1") != code_fingerprint():
    return False
  return (CACHE_FILES / "dialogue.txt").is_file()


def extract_base(rom):
  log("extracting base assets from %s" % rom)
  base_tmp = CACHE_DIR.with_name(CACHE_DIR.name + ".tmp")
  shutil.rmtree(base_tmp, ignore_errors=True)
  with tempfile.TemporaryDirectory(prefix="modkit-extract-") as tmpdir:
    workspace = Path(tmpdir)
    copy_compiler_files(workspace)
    run_tool(
      [sys.executable, "assets/restool.py", "--extract-from-rom", "-r", str(rom),
       "--no-build"],
      workspace, "base extraction")
    ignore = shutil.ignore_patterns(*ASSET_CODE_FILES, "__pycache__", ".gitignore")
    shutil.copytree(workspace / "assets", base_tmp / "files", ignore=ignore)
    write_json(base_tmp / "manifest.json", OrderedDict([
      ("tool_version", TOOL_VERSION),
      ("assets_sha1", code_fingerprint()),
      ("rom", str(rom)),
      ("rom_sha1", sha1_file(rom)),
      ("languages", ["us"]),
    ]))
  shutil.rmtree(CACHE_DIR, ignore_errors=True)
  base_tmp.rename(CACHE_DIR)


def ensure_base(rom):
  manifest = None
  if CACHE_MANIFEST.is_file():
    try:
      manifest = read_json(CACHE_MANIFEST)
    except (OSError, ValueError):
      manifest = None
  if cache_is_valid(rom, manifest):
    return manifest
  extract_base(rom)
  return read_json(CACHE_MANIFEST)


def parse_languages(value):
  if not value:
    return []
  languages = []
  for part in value.split(","):
    lang = part.strip()
    if not lang:
      raise ModkitError("empty language in --languages")
    if lang == "us":
      raise ModkitError("'us' is always built and must not be passed to --languages")
    if lang not in EXTRA_LANGUAGES:
      raise ModkitError(
        "unknown language '%s' (valid: %s)" % (lang, ", ".join(EXTRA_LANGUAGES)))
    if lang not in languages:
      languages.append(lang)
  return languages


def language_dialogue_name(lang):
  if lang == "us":
    return "dialogue.txt"
  return "dialogue_%s.txt" % lang.replace("-", "_")


def language_font_name(lang):
  if lang == "us":
    return "font.png"
  return "font_%s.png" % lang.replace("-", "_")


def language_from_dialogue_name(name):
  if name == "dialogue.txt":
    return "us"
  if name.startswith("dialogue_") and name.endswith(".txt"):
    return name[len("dialogue_"):-len(".txt")].replace("_", "-")
  return None


def language_from_font_name(name):
  if name == "font.png":
    return "us"
  if name.startswith("font_") and name.endswith(".png"):
    return name[len("font_"):-len(".png")].replace("_", "-")
  return None


def parse_language_roms(values):
  result = {}
  for value in values or []:
    if "=" not in value:
      raise ModkitError("--language-rom expects LANG=PATH, got '%s'" % value)
    lang, path = value.split("=", 1)
    lang = lang.strip()
    if lang not in EXTRA_LANGUAGES:
      raise ModkitError("unknown language '%s' in --language-rom" % lang)
    result[lang] = resolve_rom(path.strip())
  return result


def find_language_rom(lang, explicit):
  if lang in explicit:
    return explicit[lang]
  names = []
  names.extend(LANGUAGE_ROM_NAMES.get(lang, ()))
  names.extend((
    "zelda3_%s.sfc" % lang,
    "zelda3_%s.smc" % lang,
    "zelda3-%s.sfc" % lang,
    "%s.sfc" % lang,
    "%s.smc" % lang,
  ))
  for directory in (Path.cwd(), ROOT):
    for name in names:
      path = directory / name
      if path.is_file():
        return path.resolve()
  return None


def ensure_language(lang, explicit_roms):
  if lang == "us":
    return
  manifest = read_json(CACHE_MANIFEST)
  has_files = (
    (CACHE_FILES / language_dialogue_name(lang)).is_file() and
    (CACHE_FILES / language_font_name(lang)).is_file())
  if has_files and lang in manifest.get("languages", []):
    return

  rom = find_language_rom(lang, explicit_roms)
  if rom is None:
    raise ModkitError(
      "language '%s' needs its translated ROM (or an existing %s/%s). "
      "Pass --language-rom %s=PATH." % (
        lang, language_dialogue_name(lang), language_font_name(lang), lang))

  log("extracting '%s' dialogue/font from %s" % (lang, rom))
  with tempfile.TemporaryDirectory(prefix="modkit-lang-") as tmpdir:
    workspace = Path(tmpdir)
    assets = copy_compiler_files(workspace)
    shutil.copytree(CACHE_FILES, assets, dirs_exist_ok=True)
    script = """
import sys
import util

rom = util.load_rom(sys.argv[1], True)
wanted = sys.argv[2]
if rom.language != wanted:
  raise SystemExit("ROM language is '%s', expected '%s'" % (rom.language, wanted))
import text_compression
import sprite_sheets

name = text_compression.dialogue_filename(rom.language)
with open(name, "w", encoding="utf8") as f:
  text_compression.print_strings(rom, file=f)
sprite_sheets.decode_font()
"""
    run_tool([sys.executable, "-c", script, str(rom), lang], assets,
             "extracting language '%s'" % lang)
    for name in (language_dialogue_name(lang), language_font_name(lang)):
      source = assets / name
      if not source.is_file():
        raise ModkitError("language extraction did not produce %s" % name)
      shutil.copy2(source, CACHE_FILES / name)

  manifest = read_json(CACHE_MANIFEST)
  languages = list(manifest.get("languages", ["us"]))
  if lang not in languages:
    languages.append(lang)
  manifest["languages"] = languages
  write_json(CACHE_MANIFEST, manifest)


def content_files(content_dir):
  if not content_dir.is_dir():
    return []
  files = []
  for root, dirs, names in os.walk(content_dir):
    dirs.sort()
    for name in sorted(names):
      path = Path(root) / name
      if not path.is_file():
        continue
      files.append(path.relative_to(content_dir).as_posix())
  return files


def parse_dialogue_lines(text, label):
  entries = []
  seen = set()
  for line_no, line in enumerate(text.splitlines(), 1):
    if line == "":
      raise ModkitError("%s:%d: empty line, expected '<id>: <text>'" % (label, line_no))
    if ": " not in line:
      raise ModkitError("%s:%d: expected '<id>: <text>'" % (label, line_no))
    id_text, body = line.split(": ", 1)
    if not id_text.isdigit():
      raise ModkitError("%s:%d: dialogue id must be an integer" % (label, line_no))
    ident = int(id_text)
    if ident in seen:
      raise ModkitError("%s:%d: duplicate dialogue id %d" % (label, line_no, ident))
    seen.add(ident)
    entries.append((ident, body))
  return entries


def merge_dialogue_text(base_text, overlay_text, label):
  base_entries = parse_dialogue_lines(base_text, label + " (base)")
  overlay_entries = parse_dialogue_lines(overlay_text, label)
  merged = {ident: body for ident, body in base_entries}
  for ident, body in overlay_entries:
    if ident not in merged:
      raise ModkitError("%s: unknown dialogue id %d" % (label, ident))
    merged[ident] = body
  return "".join("%d: %s\n" % (ident, merged[ident]) for ident, _ in base_entries)


def is_partial_yaml(rel):
  parts = rel.split("/")
  return (len(parts) == 2 and parts[0] in ("dungeon", "overworld") and
          parts[1].endswith(".yaml"))


def merge_yaml_text(base_text, overlay_text, label):
  base = yaml.safe_load(base_text)
  overlay = yaml.safe_load(overlay_text)
  if not isinstance(base, dict):
    raise ModkitError("%s: base YAML is not a mapping" % label)
  if not isinstance(overlay, dict):
    raise ModkitError("%s: overlay YAML must be a mapping" % label)
  # A full extracted file has every top-level key of the base and replaces the
  # whole document. Anything smaller is a partial overlay of top-level keys.
  if set(overlay.keys()) >= set(base.keys()):
    merged = overlay
  else:
    merged = dict(base)
    for key, value in overlay.items():
      merged[key] = value
  return yaml.safe_dump(merged, default_flow_style=None, sort_keys=False)


def overlay_content(content_dir, assets, active_languages=None):
  copied = []
  for rel in content_files(content_dir):
    source = content_dir / rel
    target = assets / rel
    try:
      target.resolve().relative_to(assets.resolve())
    except ValueError:
      raise ModkitError("content path escapes the asset tree: %s" % rel)
    target.parent.mkdir(parents=True, exist_ok=True)

    dialogue_lang = language_from_dialogue_name(rel) if "/" not in rel else None
    if dialogue_lang is not None:
      if (dialogue_lang != "us" and active_languages is not None and
          dialogue_lang not in active_languages):
        continue
      base = CACHE_FILES / rel
      if not base.is_file():
        raise ModkitError("no base dialogue for %s; build with --languages %s" % (
          rel, dialogue_lang))
      merged = merge_dialogue_text(
        base.read_text(encoding="utf8"), source.read_text(encoding="utf8"), rel)
      target.write_text(merged, encoding="utf8", newline="\n")
    elif is_partial_yaml(rel) and (CACHE_FILES / rel).is_file():
      merged = merge_yaml_text(
        (CACHE_FILES / rel).read_text(encoding="utf8"),
        source.read_text(encoding="utf8"), rel)
      target.write_text(merged, encoding="utf8", newline="\n")
    else:
      shutil.copy2(source, target)
    copied.append(rel)
  return copied


def prepare_workspace(workspace, content_dir=None, active_languages=None):
  assets = copy_compiler_files(workspace)
  shutil.copytree(CACHE_FILES, assets, dirs_exist_ok=True)
  if content_dir is not None and content_dir.is_dir():
    overlay_content(content_dir, assets, active_languages)
  return workspace


def compile_assets(workspace, rom, languages, sprites_from_png):
  command = [sys.executable, "assets/restool.py", "-r", str(rom)]
  if languages:
    command.extend(["--languages", ",".join(languages)])
  if sprites_from_png:
    command.append("--sprites-from-png")
  run_tool(command, workspace, "asset compilation")
  dat = workspace / "zelda3_assets.dat"
  if not dat.is_file():
    raise ModkitError("asset compilation did not write zelda3_assets.dat")
  return dat


IMPORT_COMPILER = "_modkit_import_rom.py"


def compile_assets_import_rom(workspace, rom, gfx_tables,
                              skip_dungeon_content=False,
                              skip_overworld_content=False,
                              allow_unknown=False):
  """Compile assets from a translated ROM with its moved graphics tables.

  The translation workspace uses the same skip flags as the base used for
  comparison, so the two .dat files keep the same asset list. The translated
  ROM may be an unrecognized hack; only the explicit import path allows that.
  """
  assets = workspace / "assets"
  spec_path = workspace / "import-spec.json"
  write_json(spec_path, OrderedDict((
    ("spr", gfx_tables["spr"]),
    ("bg", gfx_tables["bg"]),
    ("allow_unknown", allow_unknown),
    ("skip_dungeon_content", skip_dungeon_content),
    ("skip_overworld_content", skip_overworld_content),
  )))
  wrapper = assets / IMPORT_COMPILER
  wrapper.write_text(
    "import json, os, sys\n"
    "sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))\n"
    "import util\n"
    "with open(sys.argv[2], 'r', encoding='utf8') as f:\n"
    "  spec = json.load(f)\n"
    "rom = util.load_rom(sys.argv[1], True, spec['allow_unknown'])\n"
    "import tables\n"
    "tables.kCompSpritePtrs = spec['spr']\n"
    "tables.kCompBgPtrs = spec['bg']\n"
    "import compile_resources\n"
    "class Args:\n"
    "  sprites_from_png = False\n"
    "  languages = None\n"
    "  print_assets_header = False\n"
    "  skip_dungeon_content = spec['skip_dungeon_content']\n"
    "  skip_overworld_content = spec['skip_overworld_content']\n"
    "compile_resources.main(Args())\n",
    encoding="utf8", newline="\n")
  output = run_tool(
    [sys.executable, IMPORT_COMPILER, str(rom), str(spec_path)],
    assets, "translated asset compilation")
  # run_tool only forwards the captured output on failure. Surface the
  # unknown-ROM warning from util.load_rom on the explicit import path.
  for line in output.splitlines():
    if line.startswith("Warning:"):
      log(line)
  dat = workspace / "zelda3_assets.dat"
  if not dat.is_file():
    raise ModkitError("asset compilation did not write zelda3_assets.dat")
  return dat


DUNGEON_EXTRACTOR = "_modkit_extract_dungeon.py"


def extract_dungeon_content(workspace, rom, report_path):
  """Extract the 320 room YAMLs from the translated/hack ROM.

  The workspace already has the US dungeon tree from the base cache.  Keep a
  copy as the fallback for rooms whose hack table entry is missing or invalid,
  then let extract_resources write the hack rooms over the working tree.
  """
  assets = workspace / "assets"
  fallback = assets / "dungeon_us"
  shutil.rmtree(fallback, ignore_errors=True)
  shutil.copytree(assets / "dungeon", fallback)
  wrapper = assets / DUNGEON_EXTRACTOR
  wrapper.write_text(
    "import os, sys\n"
    "sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))\n"
    "import util\n"
    "util.load_rom(sys.argv[1], True, True)\n"
    "import extract_resources\n"
    "extract_resources.make_directories()\n"
    "extract_resources.print_all_dungeon_rooms()\n"
    "extract_resources.print_default_rooms()\n"
    "extract_resources.print_overlay_rooms()\n",
    encoding="utf8", newline="\n")
  env = os.environ.copy()
  env["ZELDA3_DUNGEON_FALLBACK"] = str(fallback)
  env["ZELDA3_DUNGEON_REPORT"] = str(report_path)
  output = run_tool(
    [sys.executable, DUNGEON_EXTRACTOR, str(rom)], assets,
    "dungeon extraction", env=env)
  for line in output.splitlines():
    if line.startswith("Warning:"):
      log(line)
  if not report_path.is_file():
    raise ModkitError("dungeon extraction did not write %s" % report_path)
  return read_json(report_path)


OVERWORLD_EXTRACTOR = "_modkit_extract_overworld.py"


def extract_overworld_content(workspace, rom, report_path):
  """Extract the overworld YAMLs from the translated/hack ROM.

  The workspace already has the US overworld tree from the base cache.  The
  extractor writes every area that the hack marks as an area head; the
  compiler later uses the same table, so the two sides stay in sync.
  """
  assets = workspace / "assets"
  wrapper = assets / OVERWORLD_EXTRACTOR
  wrapper.write_text(
    "import os, sys\n"
    "sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))\n"
    "import util\n"
    "util.load_rom(sys.argv[1], True, True)\n"
    "import extract_resources\n"
    "extract_resources.make_directories()\n"
    "extract_resources.print_all_overworld_areas()\n"
    "extract_resources.print_map32_to_map16(open('map32_to_map16.txt', 'w'))\n",
    encoding="utf8", newline="\n")
  env = os.environ.copy()
  env["ZELDA3_OVERWORLD_REPORT"] = str(report_path)
  output = run_tool(
    [sys.executable, OVERWORLD_EXTRACTOR, str(rom)], assets,
    "overworld extraction", env=env)
  for line in output.splitlines():
    if line.startswith("Warning:"):
      log(line)
  if not report_path.is_file():
    raise ModkitError("overworld extraction did not write %s" % report_path)
  return read_json(report_path)


DIALOGUE_LINK_EXTRACTOR = "_modkit_extract_dialogue_link.py"
DIALOGUE_LINK_EXTRACTOR_CODE = r'''import hashlib, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import util

rom = util.load_rom(sys.argv[1], True, True)
with open(sys.argv[2], "r", encoding="utf8") as f:
  spec = json.load(f)
report = {
  "rom_sha1": hashlib.sha1(rom.ROM).hexdigest(),
  "language": rom.language,
  "dialogue": None,
  "link": None,
}
import sprite_sheets
import text_compression

# Tolerant version of text_compression.decode_strings_generic. Unknown or
# reserved command bytes are collected instead of being guessed, so a hack
# with an unsupported text command is reported and skipped.
def decode_tolerant(lang):
  info = text_compression.kLanguages[lang]
  p, rom_idx = info.rom_addrs[0], 1
  result, unknown = [], []
  steps = 0
  while True:
    s = ""
    while True:
      steps += 1
      if steps > 200000:
        unknown.append({"address": "0x%06x" % p, "byte": None,
                        "kind": "runaway"})
        return result, unknown
      addr = p
      c = rom.get_byte(p)
      command_index = c - info.COMMAND_START
      if info.COMMAND_START <= c < info.SWITCH_BANK:
        if command_index < 0 or command_index >= len(info.command_lengths):
          unknown.append({"address": "0x%06x" % addr, "byte": c,
                          "kind": "command"})
          length = 1
        else:
          length = info.command_lengths[command_index]
      else:
        length = 1
      p += length
      if c == 0x7f:  # EndMessage
        break
      if c < info.COMMAND_START:
        if c == info.ESCAPE_CHARACTER:
          c = rom.get_byte(p)
          p += 1
        if c >= len(info.alphabet):
          unknown.append({"address": "0x%06x" % addr, "byte": c,
                          "kind": "character"})
        else:
          s += info.alphabet[c]
      elif c < info.SWITCH_BANK:
        if command_index >= len(info.command_names):
          unknown.append({"address": "0x%06x" % addr, "byte": c,
                          "kind": "command"})
        elif length == 2:
          param = rom.get_byte(p - 1)
          s += "[%s %.2d]" % (info.command_names[command_index], param)
        else:
          s += "[%s]" % info.command_names[command_index]
      elif c == info.FINISH:
        return result, unknown
      elif c == info.SWITCH_BANK:
        if rom_idx >= len(info.rom_addrs):
          unknown.append({"address": "0x%06x" % addr, "byte": c,
                          "kind": "bank_switch"})
          return result, unknown
        p = info.rom_addrs[rom_idx]
        rom_idx += 1
        s = ""
      elif c < info.SWITCH_BANK + 8:
        unknown.append({"address": "0x%06x" % addr, "byte": c,
                        "kind": "reserved"})
      else:
        index = c - info.DICT_BASE_DEC
        if index < 0 or index >= len(info.dictionary):
          unknown.append({"address": "0x%06x" % addr, "byte": c,
                          "kind": "dictionary"})
        else:
          s += info.dictionary[index]
    result.append(s)
    if len(result) >= 397 and lang == "pt":
      return result, unknown


def skipped(reason, unknown=None):
  value = {"status": "skipped", "reason": reason}
  if unknown:
    value["unknown"] = unknown[:50]
    if len(unknown) > 50:
      value["unknown_truncated"] = len(unknown) - 50
  return value


if spec.get("dialogue"):
  try:
    lang = rom.language if rom.language in text_compression.kLanguages else "us"
    if text_compression.uses_new_format(lang):
      raise ValueError(
        "new-format dialogue (EU encoder) is not supported by import-rom")
    texts, unknown = decode_tolerant(lang)
    if unknown:
      report["dialogue"] = skipped(
        "unknown text command(s) in the ROM", unknown)
    elif len(texts) not in (396, 397):
      report["dialogue"] = skipped(
        "unexpected message count %d (expected 397)" % len(texts))
    else:
      if len(texts) == 396:
        extra = ("[Speed 00]0- [Number 00]. 1- [Number 01][2]"
                 "2- [Number 02]. 3- [Number 03]")
        texts = texts[:4] + [extra] + texts[4:]
      try:
        text_compression.compress_strings(texts, "us")
      except Exception as e:
        report["dialogue"] = skipped("cannot compile the decoded text: %s" % e)
      else:
        dialogue_path = "dialogue.txt"
        font_path = "font.png"
        old_dialogue = open(dialogue_path, "rb").read()
        old_font = open(font_path, "rb").read()
        try:
          with open(dialogue_path, "w", encoding="utf8", newline="\n") as f:
            for i, text in enumerate(texts, 1):
              f.write("%d: %s\n" % (i, text))
          old_language = rom.language
          rom.language = "us"
          try:
            sprite_sheets.decode_font()
          finally:
            rom.language = old_language
          report["dialogue"] = {
            "status": "extracted",
            "messages": len(texts),
            "source_language": lang,
          }
        except Exception as e:
          open(dialogue_path, "wb").write(old_dialogue)
          open(font_path, "wb").write(old_font)
          report["dialogue"] = skipped("font/dialogue extraction failed: %s" % e)
  except Exception as e:
    report["dialogue"] = skipped("dialogue decode failed: %s" % e)

if spec.get("link"):
  try:
    nmi = rom.get_bytes(0x8089E0, 0x26)
    if (nmi[:7] != bytes.fromhex("c210a9808d1521") or
        nmi[0x21] != 0xA9 or nmi[0x23:0x26] != bytes.fromhex("8d0443")):
      raise ValueError("unrecognized NMI_DoUpdates Link DMA setup")
    bank = nmi[0x22]
    source = (bank << 16) | 0x8000
    height = 448
    data = rom.get_bytes(source, 0x800 * height // 32)
    dst = bytearray(128 * height)
    for i in range(16 * height // 8):
      x, y = i % 16, i // 16
      offs, toffs = i * 32, x * 8 + y * 8 * 128
      for yy in range(8):
        d0 = data[offs + yy * 2 + 0]
        d1 = data[offs + yy * 2 + 1]
        d2 = data[offs + yy * 2 + 16]
        d3 = data[offs + yy * 2 + 17]
        for xx in range(8):
          t = (((d0 >> xx) & 1) | (((d1 >> xx) & 1) << 1) |
               (((d2 >> xx) & 1) << 2) | (((d3 >> xx) & 1) << 3))
          dst[toffs + yy * 128 + (7 - xx)] = t
    palette = [0, 0x7fff, 0x237e, 0x11b7, 0x369e, 0x14a5, 0x1ff, 0x1078,
               0x599d, 0x3647, 0x3b68, 0xa4a, 0x12ef, 0x2a5c, 0x1571, 0x7a18]
    sprite_sheets.save_as_png(
      (128, height), dst, "linksprite.png",
      sprite_sheets.convert_snes_palette(palette))
    report["link"] = {
      "status": "extracted",
      "source": "0x%06x" % source,
      "size": 0x7000,
    }
  except Exception as e:
    report["link"] = {"status": "skipped", "reason": str(e)}

with open(sys.argv[3], "w", encoding="utf8") as f:
  json.dump(report, f, indent=2)
  f.write("\n")
'''


def extract_dialogue_link(workspace, rom, report_path, want_dialogue, want_link):
  """Extract hack dialogue/font and Link graphics into the workspace.

  ``dialogue.txt``/``font.png`` become the US language slot; the C loader
  already picks kDialogue/kDialogueFont from the active assets, so no engine
  change is needed. The Link sheet is the 0x7000-byte block used by the DMA
  tables, read from the bank selected in NMI_DoUpdates.
  """
  if not want_dialogue and not want_link:
    return None
  assets = workspace / "assets"
  spec_path = workspace / "dialogue-link-spec.json"
  write_json(spec_path, OrderedDict((
    ("dialogue", bool(want_dialogue)),
    ("link", bool(want_link)),
  )))
  wrapper = assets / DIALOGUE_LINK_EXTRACTOR
  wrapper.write_text(DIALOGUE_LINK_EXTRACTOR_CODE, encoding="utf8", newline="\n")
  backups = {}
  for name in ("dialogue.txt", "font.png", "linksprite.png"):
    path = assets / name
    backups[name] = path.read_bytes() if path.is_file() else None

  def restore():
    for name, data in backups.items():
      if data is not None:
        (assets / name).write_bytes(data)

  try:
    output = run_tool(
      [sys.executable, DIALOGUE_LINK_EXTRACTOR, str(rom), str(spec_path),
       str(report_path)],
      assets, "dialogue/Link extraction")
  except ModkitError as e:
    restore()
    log("warning: dialogue/Link extraction failed: %s; skipping them" % e)
    return None
  for line in output.splitlines():
    if line.startswith("Warning:"):
      log(line)
  if not report_path.is_file():
    restore()
    log("warning: dialogue/Link extraction wrote no report; skipping them")
    return None
  report = read_json(report_path)
  if report.get("dialogue"):
    status = report["dialogue"].get("status")
    if status == "extracted":
      log("dialogue: %d message(s) from %s" % (
        report["dialogue"]["messages"], report["dialogue"]["source_language"]))
    else:
      log("warning: dialogue not imported: %s" % report["dialogue"].get("reason"))
      unknown = report["dialogue"].get("unknown") or []
      for item in unknown[:20]:
        log("warning:   text byte %s at %s (%s)" % (
          item.get("byte"), item.get("address"), item.get("kind")))
      if len(unknown) > 20:
        log("warning:   ... and %d more unknown text byte(s)" %
            (len(unknown) - 20))
  if report.get("link"):
    status = report["link"].get("status")
    if status == "extracted":
      log("Link graphics: %d bytes from %s" % (
        report["link"]["size"], report["link"]["source"]))
    else:
      log("warning: Link graphics not imported: %s" % report["link"].get("reason"))
  return report


def read_assets_dat(path):
  data = path.read_bytes()
  if len(data) < 88 or data[:16] != DAT_MAGIC:
    raise ModkitError("invalid assets file: %s" % path)
  count, key_size = struct.unpack_from("<II", data, 80)
  key_start = 88 + count * 4
  key_end = key_start + key_size
  if key_end > len(data):
    raise ModkitError("truncated assets file: %s" % path)
  names = data[key_start:key_end].split(b"\0")
  if names and names[-1] == b"":
    names.pop()
  if len(names) != count:
    raise ModkitError("assets file has %d names, expected %d" % (len(names), count))

  assets = OrderedDict()
  offset = key_end
  for i, raw_name in enumerate(names):
    size = struct.unpack_from("<I", data, 88 + i * 4)[0]
    offset = (offset + 3) & ~3
    if offset + size > len(data):
      raise ModkitError("truncated asset '%s' in %s" % (raw_name.decode("utf8"), path))
    assets[raw_name.decode("utf8")] = data[offset:offset + size]
    offset += size
  return assets


def read_dat_dialogue_layout(assets):
  """Read the language -> slot mapping of a zelda3_assets.dat.

  kDialogue and kDialogueFont are packed arrays with one entry per language.
  kDialogueMap points at the entries (dialogue index, font index, flags). The
  C loader (ZeldaSetLanguage) uses those indexes directly, so a mod that
  overrides either asset must keep the same number of entries and the same
  language order as the .dat the game is using.
  """
  for name in ("kDialogue", "kDialogueFont", "kDialogueMap"):
    if name not in assets:
      raise ModkitError("'.dat' has no '%s'" % name)
  maps = unpack_packed_arrays(assets["kDialogueMap"])
  dialogues = unpack_packed_arrays(assets["kDialogue"])
  fonts = unpack_packed_arrays(assets["kDialogueFont"])
  languages = []
  us_dialogue = us_font = None
  for entry in maps:
    parts = unpack_packed_arrays(entry)
    if len(parts) != 2:
      raise ModkitError("invalid kDialogueMap entry")
    try:
      name = parts[0].decode("utf8")
    except UnicodeDecodeError:
      raise ModkitError("invalid language name in kDialogueMap")
    conf = parts[1]
    if len(conf) != 3:
      raise ModkitError("invalid kDialogueMap index/flag entry for '%s'" % name)
    dialogue_index, font_index = conf[0], conf[1]
    if dialogue_index >= len(dialogues) or font_index >= len(fonts):
      raise ModkitError("kDialogueMap entry '%s' points outside the assets" % name)
    if name in languages:
      raise ModkitError("duplicate language '%s' in kDialogueMap" % name)
    languages.append(name)
    if name == "us":
      us_dialogue, us_font = dialogue_index, font_index
  if us_dialogue is None:
    raise ModkitError("'.dat' kDialogueMap has no 'us' entry")
  return OrderedDict((
    ("languages", languages),
    ("dialogues", dialogues),
    ("fonts", fonts),
    ("us_dialogue", us_dialogue),
    ("us_font", us_font),
  ))


def merge_hack_dialogue_assets(base_assets, hack_assets):
  """Keep the base .dat languages and replace only the US dialogue/font.

  import-rom compiles the hack as a US-only .dat. This merges that US entry
  into the language layout of the .dat the game loads, so kDialogueMap keeps
  pointing at valid slots for every language while the hack text/font wins in
  the US slot.
  """
  layout = read_dat_dialogue_layout(base_assets)
  hack_dialogues = unpack_packed_arrays(hack_assets["kDialogue"])
  hack_fonts = unpack_packed_arrays(hack_assets["kDialogueFont"])
  if len(hack_dialogues) != 1 or len(hack_fonts) != 1:
    raise ModkitError("hack dialogue/font compile is not a single US slot")
  dialogues = list(layout["dialogues"])
  fonts = list(layout["fonts"])
  dialogues[layout["us_dialogue"]] = hack_dialogues[0]
  fonts[layout["us_font"]] = hack_fonts[0]
  return (pack_packed_arrays(dialogues), pack_packed_arrays(fonts), layout)


def update_mod_manifest(mod_dir, changed, base_sizes, old_generated,
                        tracking_key="generated_overrides", force_resize=()):
  manifest_path = mod_dir / "mod.json"
  if not manifest_path.is_file():
    raise ModkitError("missing mod.json in %s" % mod_dir)
  try:
    manifest = read_json(manifest_path)
  except ValueError as e:
    raise ModkitError("mod.json is not valid JSON: %s" % e)
  if not isinstance(manifest, dict):
    raise ModkitError("mod.json must contain a JSON object")

  old_generated = list(old_generated)
  overrides = manifest.get("asset_overrides", OrderedDict())
  if not isinstance(overrides, dict):
    raise ModkitError("'asset_overrides' in mod.json must be an object")

  changed_names = list(changed)
  for name in changed_names:
    if name in overrides and name not in old_generated:
      raise ModkitError(
        "'%s' already has an override in mod.json not tracked by '%s'; remove "
        "it before letting modkit generate that asset" % (name, tracking_key))

  build_dir = mod_dir / "build"
  build_dir.mkdir(parents=True, exist_ok=True)
  new_generated = []
  for name in changed_names:
    data = changed[name]
    filename = "%s.bin" % name
    (build_dir / filename).write_bytes(data)
    rel = "build/%s" % filename
    if len(data) == base_sizes[name] and name not in force_resize:
      overrides[name] = rel
    else:
      overrides[name] = OrderedDict((("file", rel), ("allow_resize", True)))
    new_generated.append(name)

  changed_set = set(changed_names)
  for name in old_generated:
    if name in changed_set:
      continue
    overrides.pop(name, None)
    stale = build_dir / ("%s.bin" % name)
    if stale.is_file():
      stale.unlink()

  if overrides:
    manifest["asset_overrides"] = overrides
  else:
    manifest.pop("asset_overrides", None)
  if new_generated:
    manifest[tracking_key] = new_generated
  else:
    manifest.pop(tracking_key, None)
  write_json(manifest_path, manifest)
  return manifest


def update_table_manifest(mod_dir, changed, old_imported):
  manifest_path = mod_dir / "mod.json"
  if not manifest_path.is_file():
    raise ModkitError("missing mod.json in %s" % mod_dir)
  try:
    manifest = read_json(manifest_path)
  except ValueError as e:
    raise ModkitError("mod.json is not valid JSON: %s" % e)
  if not isinstance(manifest, dict):
    raise ModkitError("mod.json must contain a JSON object")

  old_imported = list(old_imported)
  overrides = manifest.get("table_overrides", OrderedDict())
  if not isinstance(overrides, dict):
    raise ModkitError("'table_overrides' in mod.json must be an object")
  for name in changed:
    if name in overrides and name not in old_imported:
      raise ModkitError(
        "'%s' already has a table override in mod.json not tracked by "
        "'imported_tables'; remove it before letting modkit import that table"
        % name)

  build_dir = mod_dir / "build"
  build_dir.mkdir(parents=True, exist_ok=True)
  new_imported = []
  for name, data in changed.items():
    filename = "%s.bin" % name
    (build_dir / filename).write_bytes(data)
    overrides[name] = "build/%s" % filename
    new_imported.append(name)

  changed_set = set(changed)
  for name in old_imported:
    if name in changed_set:
      continue
    overrides.pop(name, None)
    stale = build_dir / ("%s.bin" % name)
    if stale.is_file():
      stale.unlink()

  if overrides:
    manifest["table_overrides"] = overrides
  else:
    manifest.pop("table_overrides", None)
  if new_imported:
    manifest["imported_tables"] = new_imported
  else:
    manifest.pop("imported_tables", None)
  write_json(manifest_path, manifest)
  return manifest


def build_mod(args):
  mod_dir = Path(args.mod_dir).resolve()
  if not mod_dir.is_dir():
    raise ModkitError("mod directory not found: %s" % mod_dir)
  content_dir = mod_dir / "content"
  if content_dir.exists() and not content_dir.is_dir():
    raise ModkitError("content path is not a directory: %s" % content_dir)

  languages = parse_languages(args.languages)
  rom = resolve_rom(args.rom)
  explicit_roms = parse_language_roms(args.language_rom)

  ensure_base(rom)
  for lang in languages:
    ensure_language(lang, explicit_roms)

  files = content_files(content_dir)
  for rel in files:
    name = Path(rel).name
    lang = language_from_dialogue_name(name)
    if lang is None:
      lang = language_from_font_name(name)
    if lang and lang != "us" and lang in EXTRA_LANGUAGES and lang not in languages:
      log("warning: content has %s but --languages does not include '%s'; "
          "the file will be ignored" % (rel, lang))

  sprites_from_png = any(
    rel.startswith("sprites/") and rel.lower().endswith(".png") for rel in files)

  manifest_path = mod_dir / "mod.json"
  if not manifest_path.is_file():
    raise ModkitError("missing mod.json in %s" % mod_dir)
  try:
    module_manifest = read_json(manifest_path)
  except ValueError as e:
    raise ModkitError("mod.json is not valid JSON: %s" % e)
  if not isinstance(module_manifest, dict):
    raise ModkitError("mod.json must contain a JSON object")
  old_generated = module_manifest.get("generated_overrides", [])
  if old_generated is None:
    old_generated = []
  if not isinstance(old_generated, list) or not all(
      isinstance(name, str) for name in old_generated):
    raise ModkitError("'generated_overrides' in mod.json must be an array of strings")

  with tempfile.TemporaryDirectory(prefix="modkit-build-") as tmpdir:
    tmp = Path(tmpdir)
    log("compiling base assets (%d language%s)" % (
      len(languages) + 1, "" if not languages else "s"))
    base_ws = prepare_workspace(tmp / "base")
    base_dat = compile_assets(base_ws, rom, languages, sprites_from_png)
    base_assets = read_assets_dat(base_dat)

    log("compiling mod content")
    active_languages = set(["us"] + languages)
    mod_ws = prepare_workspace(tmp / "mod", content_dir, active_languages)
    mod_dat = compile_assets(mod_ws, rom, languages, sprites_from_png)
    mod_assets = read_assets_dat(mod_dat)

    if list(base_assets) != list(mod_assets):
      raise ModkitError("base and mod assets files have different asset tables")

    changed = OrderedDict()
    base_sizes = {name: len(data) for name, data in base_assets.items()}
    for name in base_assets:
      if base_assets[name] != mod_assets[name]:
        changed[name] = mod_assets[name]

    update_mod_manifest(mod_dir, changed, base_sizes, old_generated)

    build_info = OrderedDict()
    build_info["tool"] = "modkit"
    build_info["version"] = TOOL_VERSION
    build_info["languages"] = ["us"] + languages
    build_info["base_dat_sha1"] = sha1_file(base_dat)
    base_hashes = OrderedDict()
    for name in changed:
      data = base_assets[name]
      base_hashes[name] = OrderedDict((
        ("sha1", sha1_bytes(data)), ("size", len(data))))
    build_info["base_assets"] = base_hashes
    generated = OrderedDict()
    for name, data in changed.items():
      generated[name] = OrderedDict((
        ("file", "build/%s.bin" % name),
        ("base_size", len(base_assets[name])),
        ("size", len(data)),
        ("sha1", sha1_bytes(data)),
      ))
    build_info["generated"] = generated
    write_json(mod_dir / "build" / "modkit.json", build_info)

  if changed:
    print("modkit: %d asset(s) changed:" % len(changed), flush=True)
    for name, data in changed.items():
      print("  %s: %d -> %d bytes -> build/%s.bin" % (
        name, len(base_assets[name]), len(data), name), flush=True)
  else:
    print("modkit: no differences; zero overrides", flush=True)
  return 0


def parse_asset_list(value):
  if not value:
    return None
  result = []
  for part in value.split(","):
    name = part.strip()
    if not name:
      raise ModkitError("empty asset name in --assets")
    if name not in result:
      result.append(name)
  return result


def is_interface_asset(name):
  return name in INTERFACE_ASSETS or name.startswith("kBgTilemap_")


def asset_category(name):
  if name in PALETTE_ASSETS:
    return "palette"
  if name in MAP_ASSETS:
    return "map"
  if name in DUNGEON_ASSETS:
    return "dungeon"
  if name in OVERWORLD_ASSETS:
    return "overworld"
  if is_interface_asset(name):
    return "interface"
  if name in DIALOGUE_ASSETS:
    return "text"
  if name in LINK_ASSETS:
    return "interface"
  if name in TEXT_ASSETS:
    return "text"
  return "other"


GAME_OVER_SWEEP_LEFT = 0x88F4F6
GAME_OVER_UNFURL_RIGHT = 0x88F56D
GAME_OVER_DRAW = 0x88F5C4
GAME_OVER_TABLES = (
  "kGameOverTextLetterCount",
  "kGameOverTextTab1",
  "kGameOverTextTab2",
  "kGameOverTextTiles",
)


def read_snes_byte(data, address, what):
  return read_snes_bytes(data, address, 1, what)[0]


def read_snes_word(data, address, what):
  value = read_snes_bytes(data, address, 2, what)
  return value[0] | value[1] << 8


def read_snes_u24(data, address, what):
  value = read_snes_bytes(data, address, 3, what)
  return value[0] | value[1] << 8 | value[2] << 16


def expect_rom_bytes(data, address, expected_hex, what):
  expected = bytes.fromhex(expected_hex)
  actual = read_snes_bytes(data, address, len(expected), what)
  if actual != expected:
    raise ModkitError(
      "unrecognized %s at $%06x: expected %s, found %s" % (
        what, address, expected.hex(" "), actual.hex(" ")))


def read_long_table(data, address, size, what):
  bank = address >> 16
  offset = address & 0xffff
  value = read_snes_bytes(data, (bank << 16) | offset, size, what)
  return value


def pad_table(data, size):
  if len(data) > size:
    raise ModkitError("table is %d bytes, expected at most %d" % (
      len(data), size))
  return data + bytes(size - len(data))


def detect_game_over_tables(data):
  """Decode the game-over letter layout from a ROM without using a hash.

  The US code has eight inline letters. The PT-BR translation keeps the same
  routine addresses but uses nine letters, a moved Tab1, and JML trampolines
  for Tab2 and for the tile/attribute table in bank $A0. We verify the
  instruction stream around the operands before trusting any address.
  """
  # Sweep-left routine: the fixed instructions around the operands that the
  # translation changes (letter count, Tab1 address, last/second-to-last
  # indexes).
  expect_rom_bytes(data, 0x88F4F6, "ae5f038ea00fa080", "game-over sweep")
  expect_rom_bytes(data, 0x88F504, "989d2c0c208090", "game-over sweep")
  expect_rom_bytes(data, 0x88F50B, "bd180cd0", "game-over sweep")
  expect_rom_bytes(data, 0x88F510, "bd040cdd", "game-over sweep")
  tab1_offset = read_snes_word(data, 0x88F514, "game-over Tab1 operand")
  expect_rom_bytes(data, 0x88F51B, "9d040ce88e5f03e0", "game-over sweep")
  letter_count = read_snes_byte(data, 0x88F523, "game-over letter count")
  if not 2 <= letter_count <= 10:
    raise ModkitError(
      "game-over letter count %d is outside 2..10" % letter_count)
  expect_rom_bytes(data, 0x88F526, "a9", "game-over sweep")
  if read_snes_byte(data, 0x88F527, "game-over last index") != letter_count - 1:
    raise ModkitError("game-over last-letter index does not match the count")
  expect_rom_bytes(data, 0x88F528, "8d5f03ee4a0c9c9d03a9268d2f01",
                   "game-over sweep")
  expect_rom_bytes(data, 0x88F538, "e0", "game-over sweep")
  if read_snes_byte(data, 0x88F539, "game-over second-to-last index") != letter_count - 1:
    raise ModkitError("game-over second-to-last index does not match the count")
  expect_rom_bytes(data, 0x88F53A, "d0", "game-over sweep")
  expect_rom_bytes(data, 0x88F53C, "a0", "game-over sweep")
  if read_snes_byte(data, 0x88F53D, "game-over back-fill index") != letter_count - 2:
    raise ModkitError("game-over back-fill index does not match the count")
  expect_rom_bytes(data, 0x88F53E, "cc9d03f0", "game-over sweep")
  expect_rom_bytes(data, 0x88F54F, "bd180cd0", "game-over sweep")
  expect_rom_bytes(data, 0x88F554, "bd040cae9d03dd", "game-over sweep")
  if read_snes_word(data, 0x88F55B, "game-over Tab1 operand") != tab1_offset:
    raise ModkitError("game-over sweep uses two different Tab1 addresses")
  expect_rom_bytes(data, 0x88F55D, "b003ce9d03", "game-over sweep")
  expect_rom_bytes(data, 0x88F562, "825f00", "game-over sweep")

  tab1_address = (GAME_OVER_SWEEP_LEFT & 0xff0000) | tab1_offset
  tab1 = read_snes_bytes(data, tab1_address, letter_count, "game-over Tab1")

  # Unfurl-right: the US code compares against an inline Tab2; the translated
  # patch jumps to a routine that switches the data bank and compares there.
  expect_rom_bytes(data, 0x88F57B, "ac9d03", "game-over unfurl")
  unfurl_opcode = read_snes_byte(data, 0x88F57E, "game-over unfurl")
  if unfurl_opcode == 0xBD:
    expect_rom_bytes(data, 0x88F57E, "bd040cd9", "game-over unfurl")
    if letter_count > 8:
      raise ModkitError("US game-over code cannot hold %d Tab2 entries" % letter_count)
    tab2_offset = read_snes_word(data, 0x88F582, "game-over Tab2 operand")
    tab2_address = (GAME_OVER_UNFURL_RIGHT & 0xff0000) | tab2_offset
    tab2 = read_snes_bytes(data, tab2_address, letter_count, "game-over Tab2")
  elif unfurl_opcode == 0x5C:
    tab2_code = read_snes_u24(data, 0x88F57F, "game-over Tab2 JML")
    routine = read_snes_bytes(data, tab2_code, 11, "game-over Tab2 routine")
    if (routine[0] != 0x8B or routine[1] != 0xA9 or routine[3] != 0x48 or
        routine[4] != 0xAB or routine[5:9] != b"\xbd\x04\x0c\xd9"):
      raise ModkitError(
        "unrecognized game-over Tab2 routine at $%06x" % tab2_code)
    tab2_address = (routine[2] << 16) | routine[9] | routine[10] << 8
    tab2 = read_snes_bytes(data, tab2_address, letter_count, "game-over Tab2")
  else:
    raise ModkitError(
      "unrecognized game-over unfurl opcode $%02x at $%06x" % (
        unfurl_opcode, 0x88F57E))
  expect_rom_bytes(data, 0x88F592, "c9", "game-over unfurl")
  if read_snes_byte(data, 0x88F593, "game-over unfurl count") != letter_count:
    raise ModkitError("game-over unfurl count does not match the letter count")

  # Draw routine: the US code reads two inline tile bytes and hardcodes
  # attribute $3C; the translated patch jumps to long-table lookups that
  # return top tile/attribute and bottom tile/attribute.
  expect_rom_bytes(data, 0x88F5C4, "a9008590a9088591a9208592a90a8593ae5f03a000da",
                   "game-over draw")
  expect_rom_bytes(data, 0x88F5DA, "a95785006401bd040c8502bd180c850320e1f68a0a",
                   "game-over draw")
  draw_opcode = read_snes_byte(data, 0x88F5EF, "game-over draw")
  if draw_opcode == 0x5C:
    expect_rom_bytes(data, 0x88F601, "20e1f65c", "game-over draw")
    top_code = read_snes_u24(data, 0x88F5F0, "game-over top-tile JML")
    bottom_code = read_snes_u24(data, 0x88F605, "game-over bottom-tile JML")
    top = read_snes_bytes(data, top_code, 13, "game-over top-tile routine")
    if (top[0] != 0x0A or top[1] != 0xAA or top[2] != 0xBF or
        top[6:9] != b"\x91\x90\xc8" or top[9] != 0xBF):
      raise ModkitError(
        "unrecognized game-over top-tile routine at $%06x" % top_code)
    tile_base = top[3] | top[4] << 8 | top[5] << 16
    if (top[10] | top[11] << 8 | top[12] << 16) != tile_base + 1:
      raise ModkitError("game-over top-tile routine does not use adjacent bytes")
    bottom = read_snes_bytes(data, bottom_code, 11, "game-over bottom-tile routine")
    if (bottom[0] != 0xBF or bottom[4:7] != b"\x91\x90\xc8" or
        bottom[7] != 0xBF):
      raise ModkitError(
        "unrecognized game-over bottom-tile routine at $%06x" % bottom_code)
    bottom_base = bottom[1] | bottom[2] << 8 | bottom[3] << 16
    if (bottom_base != tile_base + 2 or
        (bottom[8] | bottom[9] << 8 | bottom[10] << 16) != bottom_base + 1):
      raise ModkitError("game-over bottom-tile routine does not use adjacent bytes")
    tiles = read_long_table(data, tile_base, letter_count * 4,
                            "game-over tile/attribute table")
  elif draw_opcode == 0xAA:
    expect_rom_bytes(data, 0x88F5EF, "aabd", "game-over draw")
    expect_rom_bytes(data, 0x88F601, "20e1f6bd", "game-over draw")
    if letter_count > 8:
      raise ModkitError("US game-over code cannot hold %d tile entries" % letter_count)
    top_offset = read_snes_word(data, 0x88F5F1, "game-over top tile operand")
    bottom_offset = read_snes_word(data, 0x88F605, "game-over bottom tile operand")
    if bottom_offset != top_offset + 1:
      raise ModkitError("game-over US tile table is not interleaved")
    char_pairs = read_snes_bytes(
      data, (GAME_OVER_DRAW & 0xff0000) | top_offset, letter_count * 2,
      "game-over tile table")
    tiles = bytearray()
    for i in range(letter_count):
      tiles.extend((char_pairs[i * 2], 0x3c,
                    char_pairs[i * 2 + 1], 0x3c))
    tiles = bytes(tiles)
  else:
    raise ModkitError(
      "unrecognized game-over draw opcode $%02x at $%06x" % (
        draw_opcode, 0x88F5EF))

  return OrderedDict((
    ("kGameOverTextLetterCount", bytes((letter_count,))),
    ("kGameOverTextTab1", pad_table(tab1, 10)),
    ("kGameOverTextTab2", pad_table(tab2, 10)),
    ("kGameOverTextTiles", pad_table(tiles, 40)),
  ))


def read_mod_manifest(path, what):
  path = Path(path).resolve()
  manifest_path = path / "mod.json"
  if not manifest_path.is_file():
    raise ModkitError("%s has no mod.json: %s" % (what, path))
  try:
    manifest = read_json(manifest_path)
  except ValueError as e:
    raise ModkitError("%s mod.json is not valid JSON: %s" % (what, e))
  if not isinstance(manifest, dict):
    raise ModkitError("%s mod.json must contain a JSON object" % what)
  return path, manifest


def merge_import_features(target, feature_dirs):
  """Merge the 'features' object from one or more preset mods."""
  merged = target.get("features", OrderedDict())
  if merged is None:
    merged = OrderedDict()
  if not isinstance(merged, dict):
    raise ModkitError("'features' in mod.json must be an object")
  for feature_dir in feature_dirs:
    _, preset = read_mod_manifest(feature_dir, "features preset")
    features = preset.get("features", OrderedDict())
    if features is None:
      features = OrderedDict()
    if not isinstance(features, dict):
      raise ModkitError("'features' in %s/mod.json must be an object" % feature_dir)
    for name, value in features.items():
      if not isinstance(value, bool):
        raise ModkitError(
          "feature '%s' in %s/mod.json must be a boolean" % (name, feature_dir))
      merged[name] = value
  if merged:
    target["features"] = merged
  else:
    target.pop("features", None)


def merge_import_script(target, mod_dir, script_dir):
  """Copy the Lua script (and permissions) declared by another mod."""
  source_dir, source = read_mod_manifest(script_dir, "scripts mod")
  script = source.get("script")
  if not isinstance(script, str) or not script:
    raise ModkitError("scripts mod %s has no 'script' entry" % source_dir)
  rel = Path(script)
  if rel.is_absolute() or ".." in rel.parts or not rel.parts:
    raise ModkitError("scripts mod has an unsafe script path: %s" % script)
  source_file = source_dir / rel
  if not source_file.is_file():
    raise ModkitError("script file not found: %s" % source_file)
  target_file = mod_dir / rel
  target_file.parent.mkdir(parents=True, exist_ok=True)
  shutil.copy2(source_file, target_file)
  target["script"] = rel.as_posix()

  permissions = source.get("permissions", [])
  if permissions is None:
    permissions = []
  if not isinstance(permissions, list) or not all(
      isinstance(item, str) for item in permissions):
    raise ModkitError("'permissions' in %s/mod.json must be an array of strings" % source_dir)
  if permissions:
    current = target.get("permissions", [])
    if not isinstance(current, list):
      raise ModkitError("'permissions' in mod.json must be an array of strings")
    merged = list(current)
    for item in permissions:
      if item not in merged:
        merged.append(item)
    target["permissions"] = merged


def import_rom(args):
  mod_dir = Path(args.mod_dir).resolve()
  if not mod_dir.is_dir():
    raise ModkitError("mod directory not found: %s" % mod_dir)
  manifest_path = mod_dir / "mod.json"
  if not manifest_path.is_file():
    raise ModkitError("missing mod.json in %s" % mod_dir)
  try:
    module_manifest = read_json(manifest_path)
  except ValueError as e:
    raise ModkitError("mod.json is not valid JSON: %s" % e)
  if not isinstance(module_manifest, dict):
    raise ModkitError("mod.json must contain a JSON object")

  def read_name_list(key):
    value = module_manifest.get(key, [])
    if value is None:
      value = []
    if not isinstance(value, list) or not all(
        isinstance(name, str) for name in value):
      raise ModkitError("'%s' in mod.json must be an array of strings" % key)
    return value

  old_imported = read_name_list("imported_overrides")
  old_imported_tables = read_name_list("imported_tables")

  base_rom = resolve_rom(args.rom)
  translated_rom = resolve_rom(args.translated_rom)
  if base_rom == translated_rom:
    raise ModkitError("the translated ROM must be different from the base ROM")
  translated_rom_sha1 = sha1_file(translated_rom)

  ensure_base(base_rom)
  base_gfx_tables = read_gfx_pointer_tables(base_rom)
  gfx_tables = read_gfx_pointer_tables(translated_rom)
  requested = parse_asset_list(args.assets)

  # Hacks and Redux bring their own dialogue/font and Link graphics. Curated
  # translations keep the historical import set unless the caller asks for
  # these assets explicitly, so e.g. the PT-BR import stays byte-identical.
  translated_language = known_rom_languages().get(translated_rom_sha1)
  hack_like_source = translated_language is None or translated_language == "redux"
  if requested is None:
    want_dialogue = hack_like_source
    want_link = hack_like_source
  else:
    want_dialogue = any(name in DIALOGUE_ASSETS for name in requested)
    want_link = any(name in LINK_ASSETS for name in requested)

  # The game loads zelda3_assets.dat from its working directory. If it exists,
  # use its language layout when the hack dialogue/font is imported; otherwise
  # fall back to the US-only layout compiled below.
  target_base_dat = resolve_base_dat(args.base_dat)

  # Game tables are fixed-size byte arrays registered in the C code. Direct
  # tables live at the same address in both ROMs; derived tables are built
  # from the translated game-over code below.
  table_changes = OrderedDict()
  applied_corrections = []
  tables = read_game_tables()
  table_by_name = {table["name"]: table for table in tables}
  base_rom_data = read_rom_bytes(base_rom)
  translated_rom_data = read_rom_bytes(translated_rom)
  for table in tables:
    if table["derived"]:
      continue
    name = table["name"]
    if name == "kOverworldAreaHeads":
      base_bytes = read_overworld_area_heads(base_rom)
      translated_bytes = read_overworld_area_heads(translated_rom)
    else:
      base_bytes = read_snes_bytes(
        base_rom_data, table["rom_addr"], table["size"], "table '%s'" % name)
      translated_bytes = read_snes_bytes(
        translated_rom_data, table["rom_addr"], table["size"],
        "table '%s'" % name)
    translated_bytes, corrections = apply_known_rom_corrections(
      translated_rom_sha1, name, translated_bytes)
    for correction in corrections:
      log("aplicando correção conhecida da tradução PT de 2014: %s (%s)" % (
        correction["description"], name))
      applied_corrections.append(OrderedDict((
        ("rom_sha1", translated_rom_sha1),
        ("table", name),
        ("description", correction["description"]),
        ("rows", list(correction["rows"])),
      )))
    if base_bytes != translated_bytes:
      table_changes[name] = translated_bytes

  # The game-over letters are not a fixed-address table: the translated ROM
  # changes the routine operands and may put the data in another bank. Decode
  # both ROMs and import only the layouts that differ from the US defaults.
  try:
    base_game_over = detect_game_over_tables(base_rom_data)
    translated_game_over = detect_game_over_tables(translated_rom_data)
  except ModkitError as e:
    log("warning: cannot decode game-over letter tables: %s; skipping them" % e)
  else:
    base_count = base_game_over["kGameOverTextLetterCount"][0]
    if base_count != 8:
      log("warning: base ROM has %d game-over letters, expected 8; skipping them"
          % base_count)
    else:
      log("game-over letter tables: %d letter(s) in translated ROM" %
          translated_game_over["kGameOverTextLetterCount"][0])
      for name in GAME_OVER_TABLES:
        table = table_by_name.get(name)
        if table is None or not table["derived"]:
          log("warning: table registry has no derived '%s'; skipping it" % name)
          continue
        data = translated_game_over[name]
        if len(data) != table["size"]:
          log("warning: decoded '%s' has %d bytes, expected %d; skipping it" %
              (name, len(data), table["size"]))
          continue
        if base_game_over[name] != data:
          table_changes[name] = data

  credits = None
  try:
    credits = read_ending_credits_assets(translated_rom)
  except ModkitError as e:
    log("warning: cannot read ending credits: %s" % e)
  if credits is None:
    log("warning: cannot locate ending credits in %s; skipping them"
        % translated_rom)

  epilogue = None
  try:
    epilogue = read_ending_epilogue_assets(translated_rom)
  except ModkitError as e:
    log("warning: cannot read ending epilogue text: %s" % e)
  if epilogue is None:
    log("warning: cannot locate ending epilogue text in %s; skipping it"
        % translated_rom)

  with tempfile.TemporaryDirectory(prefix="modkit-import-") as tmpdir:
    tmp = Path(tmpdir)
    log("compiling US base assets")
    base_ws = prepare_workspace(tmp / "base")
    base_dat = compile_assets(base_ws, base_rom, [], False)
    base_assets = read_assets_dat(base_dat)

    # Compare against a US compile that skips the same categories as the
    # translated compile, so both .dat files have exactly the same asset list.
    # The canonical full base_dat above stays the recorded base hash.
    log("compiling US base assets for comparison (overworld included)")
    base_cmp_ws = prepare_workspace(tmp / "base-compare")
    base_cmp_dat = compile_assets_import_rom(
      base_cmp_ws, base_rom, base_gfx_tables,
      skip_dungeon_content=False, skip_overworld_content=False)
    base_cmp_assets = read_assets_dat(base_cmp_dat)

    log("extracting dungeon rooms from %s" % translated_rom)
    translated_ws = prepare_workspace(tmp / "translated")
    dialogue_link_report = extract_dialogue_link(
      translated_ws, translated_rom, tmp / "dialogue-link-import.json",
      want_dialogue, want_link)
    dungeon_report = extract_dungeon_content(
      translated_ws, translated_rom, tmp / "dungeon-import.json")
    for name, value in dungeon_report["tables"].items():
      log("dungeon table %s: %s" % (name, value))
    for line in dungeon_report["sanitized"]:
      log("dungeon sanitize: %s" % line)

    log("extracting overworld areas from %s" % translated_rom)
    overworld_report = extract_overworld_content(
      translated_ws, translated_rom, tmp / "overworld-import.json")
    for name, value in overworld_report["tables"].items():
      if isinstance(value, dict):
        for sub_name, sub_value in value.items():
          log("overworld table %s.%s: %s" % (name, sub_name, sub_value))
      elif isinstance(value, list):
        log("overworld table %s: %s" % (name, ", ".join(value)))
      else:
        log("overworld table %s: %s" % (name, value))
    for line in overworld_report["sanitized"]:
      log("overworld sanitize: %s" % line)

    log("compiling translated assets from %s" % translated_rom)
    translated_dat = compile_assets_import_rom(
      translated_ws, translated_rom, gfx_tables,
      skip_dungeon_content=False, skip_overworld_content=False,
      allow_unknown=True)
    translated_assets = read_assets_dat(translated_dat)

    if list(base_cmp_assets) != list(translated_assets):
      raise ModkitError("base and translated assets files have different asset tables")

    dialogue_merge = None
    dialogue_extracted = bool(
      dialogue_link_report and dialogue_link_report.get("dialogue") and
      dialogue_link_report["dialogue"].get("status") == "extracted")
    if want_dialogue and dialogue_extracted and target_base_dat is not None:
      try:
        target_assets = read_assets_dat(target_base_dat)
        merged_dialogue, merged_font, layout = merge_hack_dialogue_assets(
          target_assets, translated_assets)
      except ModkitError as e:
        log("warning: cannot keep the languages of %s: %s; "
            "using the US-only dialogue/font layout" % (target_base_dat, e))
      else:
        translated_assets["kDialogue"] = merged_dialogue
        translated_assets["kDialogueFont"] = merged_font
        base_cmp_assets["kDialogue"] = target_assets["kDialogue"]
        base_cmp_assets["kDialogueFont"] = target_assets["kDialogueFont"]
        dialogue_merge = OrderedDict((
          ("base_dat", str(target_base_dat)),
          ("base_dat_sha1", sha1_file(target_base_dat)),
          ("languages", layout["languages"]),
          ("us_dialogue_index", layout["us_dialogue"]),
          ("us_font_index", layout["us_font"]),
        ))
        log("dialogue: keeping %d language slot(s) from %s: %s" % (
          len(layout["languages"]), target_base_dat,
          ", ".join(layout["languages"])))
    elif want_dialogue and dialogue_extracted:
      log("warning: no zelda3_assets.dat found; dialogue/font use the US-only layout")

    if credits is not None:
      for name, key in (("kEnding_Credits_Text", "text"),
                        ("kEnding_Credits_Offs", "offs")):
        if name not in translated_assets:
          raise ModkitError("translated assets file has no '%s'" % name)
        translated_assets[name] = credits[key]

    if epilogue is not None:
      for name, key in (("kEnding0_Data", "data"),
                        ("kEnding0_Offs", "offs")):
        if name not in translated_assets:
          raise ModkitError("translated assets file has no '%s'" % name)
        translated_assets[name] = epilogue[key]

    changed = OrderedDict()
    for name in base_cmp_assets:
      if base_cmp_assets[name] != translated_assets[name]:
        changed[name] = translated_assets[name]

    if credits is None:
      for name in CREDITS_ASSETS:
        changed.pop(name, None)
    if epilogue is None:
      for name in EPILOGUE_ASSETS:
        changed.pop(name, None)

    if requested is None:
      selected_names = [name for name in DEFAULT_IMPORT_ASSETS if name in base_cmp_assets]
      if translated_rom_sha1 in known_rom_hashes():
        selected_names = [name for name in selected_names
                          if name not in HACK_ONLY_ASSETS]
      if credits is None:
        selected_names = [name for name in selected_names if name not in CREDITS_ASSETS]
      if epilogue is None:
        selected_names = [name for name in selected_names if name not in EPILOGUE_ASSETS]
    else:
      for name in requested:
        if name not in base_cmp_assets:
          raise ModkitError("unknown asset '%s'" % name)
        if name in CREDITS_ASSETS and credits is None:
          raise ModkitError(
            "cannot import '%s': ending credits were not found in the "
            "translated ROM" % name)
        if name in EPILOGUE_ASSETS and epilogue is None:
          raise ModkitError(
            "cannot import '%s': ending epilogue text was not found in the "
            "translated ROM" % name)
      selected_names = requested
    selected_set = set(selected_names)

    imported = OrderedDict()
    other = OrderedDict()
    for name, data in changed.items():
      if name in selected_set:
        imported[name] = data
      else:
        other[name] = data

    base_sizes = {name: len(data) for name, data in base_cmp_assets.items()}
    dialogue_force_resize = DIALOGUE_ASSETS if dialogue_merge is not None else ()
    update_mod_manifest(mod_dir, imported, base_sizes, old_imported,
                        tracking_key="imported_overrides",
                        force_resize=dialogue_force_resize)
    update_table_manifest(mod_dir, table_changes, old_imported_tables)

    if args.features_from or args.scripts_from:
      try:
        manifest = read_json(manifest_path)
      except ValueError as e:
        raise ModkitError("mod.json is not valid JSON: %s" % e)
      if not isinstance(manifest, dict):
        raise ModkitError("mod.json must contain a JSON object")
      if args.features_from:
        merge_import_features(manifest, args.features_from)
        print("modkit: merged features from %s" %
              ", ".join(args.features_from), flush=True)
      if args.scripts_from:
        merge_import_script(manifest, mod_dir, args.scripts_from)
        print("modkit: copied script from %s" % args.scripts_from, flush=True)
      write_json(manifest_path, manifest)

    pack_changes = OrderedDict()
    if "kSprGfx" in changed:
      pack_changes["kSprGfx"] = changed_gfx_packs(
        base_cmp_assets["kSprGfx"], changed["kSprGfx"], 12)
    if "kBgGfx" in changed:
      pack_changes["kBgGfx"] = changed_gfx_packs(
        base_cmp_assets["kBgGfx"], changed["kBgGfx"], 0)

    build_info = OrderedDict()
    build_info["tool"] = "modkit-import-rom"
    build_info["version"] = TOOL_VERSION
    build_info["translated_rom"] = str(translated_rom)
    build_info["translated_rom_sha1"] = translated_rom_sha1
    build_info["base_rom"] = str(base_rom)
    build_info["base_rom_sha1"] = sha1_file(base_rom)
    build_info["base_dat_sha1"] = sha1_file(base_dat)
    build_info["known_corrections"] = applied_corrections
    imported_info = OrderedDict()
    for name, data in imported.items():
      imported_info[name] = OrderedDict((
        ("file", "build/%s.bin" % name),
        ("base_sha1", sha1_bytes(base_cmp_assets[name])),
        ("base_size", len(base_cmp_assets[name])),
        ("size", len(data)),
        ("sha1", sha1_bytes(data)),
      ))
    build_info["imported"] = imported_info
    other_info = OrderedDict()
    for name, data in other.items():
      other_info[name] = OrderedDict((
        ("base_sha1", sha1_bytes(base_cmp_assets[name])),
        ("base_size", len(base_cmp_assets[name])),
        ("size", len(data)),
        ("sha1", sha1_bytes(data)),
      ))
    build_info["not_imported"] = other_info
    table_info = OrderedDict()
    for name, data in table_changes.items():
      table = table_by_name[name]
      info = OrderedDict((
        ("file", "build/%s.bin" % name),
      ))
      if table["derived"]:
        info["source"] = "decoded game-over routine"
      else:
        info["rom_addr"] = "0x%06x" % table["rom_addr"]
      info["size"] = len(data)
      info["sha1"] = sha1_bytes(data)
      table_info[name] = info
    build_info["tables"] = table_info
    if credits is not None:
      build_info["ending_credits"] = OrderedDict((
        ("text_addr", "0x%06x" % credits["text_addr"]),
        ("offs_addr", "0x%06x" % credits["offs_addr"]),
        ("format", credits["format"]),
      ))
    else:
      build_info["ending_credits"] = None
    if epilogue is not None:
      build_info["ending_epilogue"] = OrderedDict((
        ("data_addr", "0x%06x" % epilogue["data_addr"]),
        ("offs_addr", "0x%06x" % epilogue["offs_addr"]),
        ("format", epilogue["format"]),
      ))
    else:
      build_info["ending_epilogue"] = None
    if dialogue_link_report is not None:
      build_info["dialogue_import"] = dialogue_link_report.get("dialogue")
      build_info["link_import"] = dialogue_link_report.get("link")
      if dialogue_merge is not None:
        build_info["dialogue_merge"] = dialogue_merge
    build_info["changed_packs"] = pack_changes
    if (dungeon_report["sanitized"] or
        any(name in DUNGEON_ASSETS for name in changed)):
      build_info["dungeon_extraction"] = dungeon_report
    if (overworld_report["sanitized"] or
        any(name in OVERWORLD_ASSETS for name in imported) or
        "kOverworldAreaHeads" in table_changes):
      build_info["overworld_extraction"] = overworld_report
    write_json(mod_dir / "build" / "modkit-import.json", build_info)

  print("modkit: translated ROM: %s" % translated_rom, flush=True)
  print("modkit: %d asset(s) differ from the US base:" % len(changed), flush=True)
  for name, data in changed.items():
    action = "import" if name in imported else "skip"
    kind = asset_category(name)
    print("  %-6s [%-9s] %-28s %7d -> %7d bytes" % (
      action, kind, name, len(base_cmp_assets[name]), len(data)), flush=True)
  if imported:
    print("modkit: %d asset(s) imported -> build/" % len(imported), flush=True)
  else:
    print("modkit: no selected asset differs; zero overrides", flush=True)
  if other:
    print("modkit: %d differing asset(s) not imported (use --assets to include):"
          % len(other), flush=True)
    for name, data in other.items():
      print("  %-28s %7d -> %7d bytes" % (
        name, len(base_cmp_assets[name]), len(data)), flush=True)
  if table_changes:
    print("modkit: %d table(s) differ from the US base:" % len(table_changes),
          flush=True)
    for name, data in table_changes.items():
      table = table_by_name[name]
      if table["derived"]:
        print("  import [%-9s] %-28s %7d bytes (game-over code)" % (
          "table", name, len(data)), flush=True)
      else:
        print("  import [%-9s] %-28s %7d bytes at $%06x" % (
          "table", name, len(data), table["rom_addr"]), flush=True)
  else:
    print("modkit: no table differs from the US base", flush=True)
  if credits is not None:
    print("modkit: ending credits: %s text at $%06x (%d bytes), offsets at "
          "$%06x" % (credits["format"], credits["text_addr"],
                     len(credits["text"]), credits["offs_addr"]),
          flush=True)
  if epilogue is not None:
    print("modkit: ending epilogue: %s data at $%06x (%d bytes), offsets at "
          "$%06x" % (epilogue["format"], epilogue["data_addr"],
                     len(epilogue["data"]), epilogue["offs_addr"]),
          flush=True)
  for name, indices in pack_changes.items():
    print("modkit: %s: %d pack(s) changed: %s" % (
      name, len(indices), ", ".join(str(i) for i in indices)), flush=True)
  return 0


def extract_asset(args):
  kind = args.kind
  identifier = args.id
  mod_dir = Path(args.mod_dir).resolve()
  mod_dir.mkdir(parents=True, exist_ok=True)
  rom = resolve_rom(args.rom)
  explicit_roms = parse_language_roms(args.language_rom)
  ensure_base(rom)

  if kind == "dungeon":
    room = identifier
    if room.startswith("dungeon-"):
      room = room[len("dungeon-"):]
    if room.endswith(".yaml"):
      room = room[:-len(".yaml")]
    try:
      room = int(room)
    except ValueError:
      raise ModkitError("dungeon id must be an integer, got '%s'" % identifier)
    source = CACHE_FILES / "dungeon" / ("dungeon-%d.yaml" % room)
    target = mod_dir / "content" / "dungeon" / ("dungeon-%d.yaml" % room)
  elif kind == "overworld":
    area = identifier
    if area.startswith("overworld-"):
      area = area[len("overworld-"):]
    if area.endswith(".yaml"):
      area = area[:-len(".yaml")]
    try:
      area = int(area)
    except ValueError:
      raise ModkitError("overworld id must be an integer, got '%s'" % identifier)
    source = CACHE_FILES / "overworld" / ("overworld-%d.yaml" % area)
    target = mod_dir / "content" / "overworld" / ("overworld-%d.yaml" % area)
  elif kind == "dialogue":
    if identifier.endswith(".txt"):
      name = identifier
      lang = language_from_dialogue_name(name)
      if lang is None:
        raise ModkitError("dialogue id must be a language or dialogue*.txt")
    else:
      lang = identifier
      name = language_dialogue_name(lang)
    if lang != "us":
      if lang not in EXTRA_LANGUAGES:
        raise ModkitError("unknown dialogue language '%s'" % lang)
      ensure_language(lang, explicit_roms)
    source = CACHE_FILES / name
    target = mod_dir / "content" / name
  else:
    raise ModkitError("unknown extract kind: %s" % kind)

  if not source.is_file():
    raise ModkitError("base file not found: %s" % source)
  target.parent.mkdir(parents=True, exist_ok=True)
  shutil.copy2(source, target)
  print("modkit: extracted %s -> %s" % (source, target), flush=True)
  return 0


def make_parser():
  parser = argparse.ArgumentParser(
    description="Build zelda3 asset overrides from editable mod content.")
  parser.add_argument("--version", action="version", version="modkit " + TOOL_VERSION)
  sub = parser.add_subparsers(dest="command", required=True)

  build = sub.add_parser("build", help="build a mod's content into asset overrides")
  build.add_argument("mod_dir", metavar="MOD_DIR")
  build.add_argument("--languages", metavar="L1,L2",
                     help="extra languages compiled into the assets (e.g. pt)")
  build.add_argument("--rom", default="zelda3.sfc", metavar="ROM")
  build.add_argument("--language-rom", action="append", default=[],
                     metavar="LANG=PATH",
                     help="translated ROM used to extract an extra language")
  build.set_defaults(func=build_mod)

  import_rom_parser = sub.add_parser(
    "import-rom", help="import graphics and tilemaps from a translated ROM")
  import_rom_parser.add_argument("translated_rom", metavar="TRANSLATED_ROM")
  import_rom_parser.add_argument("mod_dir", metavar="MOD_DIR")
  import_rom_parser.add_argument(
    "--assets", metavar="K1,K2",
    help="comma-separated asset names to import (default: interface graphics/tilemaps)")
  import_rom_parser.add_argument("--rom", default="zelda3.sfc", metavar="ROM")
  import_rom_parser.add_argument(
    "--base-dat", metavar="DAT",
    help="assets file whose dialogue/font language slots must be kept "
         "(default: zelda3_assets.dat in the repository root, if present)")
  import_rom_parser.add_argument(
    "--features-from", action="append", default=[], metavar="MOD_DIR",
    help="merge the 'features' preset from another mod (repeatable, later wins)")
  import_rom_parser.add_argument(
    "--scripts-from", metavar="MOD_DIR",
    help="copy the Lua script and permissions declared by another mod")
  import_rom_parser.set_defaults(func=import_rom)

  extract = sub.add_parser("extract", help="copy a base asset into a mod's content/")
  extract.add_argument("kind", choices=("dungeon", "overworld", "dialogue"))
  extract.add_argument("id", metavar="ID")
  extract.add_argument("mod_dir", metavar="MOD_DIR")
  extract.add_argument("--rom", default="zelda3.sfc", metavar="ROM")
  extract.add_argument("--language-rom", action="append", default=[],
                       metavar="LANG=PATH")
  extract.set_defaults(func=extract_asset)
  return parser


def main(argv=None):
  parser = make_parser()
  args = parser.parse_args(argv)
  try:
    return args.func(args)
  except ModkitError as e:
    print("modkit: error: %s" % e, file=sys.stderr)
    return 1
  except OSError as e:
    print("modkit: error: %s" % e, file=sys.stderr)
    return 1
  except KeyboardInterrupt:
    print("modkit: interrupted", file=sys.stderr)
    return 130


if __name__ == "__main__":
  sys.exit(main())
