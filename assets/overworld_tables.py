"""Locate the overworld tables a hack may move.

Every table is found through the operand of the original routine that reads
it.  The bytes around each instruction are checked against the US ROM, so a
different layout fails loudly instead of reading unrelated data.  The US
result is the same address the extractor used before this module existed.
"""

import util
from util import cache


def _check_code(addr, opcode, before, after, what, operand_size):
  start = addr - len(before)
  actual = bytes(util.ROM.get_bytes(
    start, len(before) + 1 + operand_size + len(after)))
  expected = bytearray(before) + bytearray((opcode,)) + bytearray(
    operand_size) + bytearray(after)
  operand_start = len(before) + 1
  for i, exp in enumerate(expected):
    if operand_start <= i < operand_start + operand_size:
      continue
    if actual[i] != exp:
      raise Exception(
        'unrecognized %s code at $%06x: expected %s, found %s' % (
          what, start, expected.hex(' '), actual.hex(' ')))
  return actual


def _operand16(addr, opcode, before, after, what):
  _check_code(addr, opcode, before, after, what, 2)
  return util.ROM.get_word(addr + 1)


def _operand24(addr, opcode, before, after, what):
  _check_code(addr, opcode, before, after, what, 3)
  return util.ROM.get_24(addr + 1)


def _indexed_table(addr, opcode, before, after, what):
  # Absolute indexed loads use the data bank of the routine.
  return (addr & 0xff0000) | _operand16(addr, opcode, before, after, what)


def _same_table(first, second, what):
  if first != second:
    raise Exception('%s loads disagree: $%06x and $%06x' % (
      what, first, second))
  return first


@cache
def area_heads():
  """kOverworldAreaHeads, read by OverworldHandleTransitions.

  A Link to the Islands replaces the load with a JSL to a new routine that
  still reads the same 64-byte table.  Follow that hook instead of assuming
  the US instruction is present.
  """
  addr = 0x82aa7d
  first = bytes(util.ROM.get_bytes(addr, 4))
  if first == bytes((0xbf, 0xec, 0xa5, 0x02)):
    table = _operand24(
      addr, 0xbf, (), (0x0f, 0xca, 0xf3, 0x7e),
      'overworld area head table')
  elif first[0] == 0x22:
    target = util.ROM.get_24(addr + 1)
    prefix = bytes(util.ROM.get_bytes(target, 10))
    if prefix[:8] != bytes.fromhex('eaeac93ff008bfec'):
      raise Exception(
        'unrecognized overworld area head hook at $%06x -> $%06x: %s' % (
          addr, target, prefix.hex(' ')))
    table = util.ROM.get_24(target + 7)
    if table & 0xffff < 0x8000:
      raise Exception(
        'overworld area head table $%06x is not in a LoROM window' % table)
  else:
    raise Exception(
      'unrecognized overworld area head load at $%06x: %s' % (
        addr, first.hex(' ')))
  values = list(util.ROM.get_bytes(table, 64))
  if any(value > 0x3f for value in values):
    raise Exception(
      'overworld area head table $%06x has an entry above $3f' % table)
  return table


@cache
def area_gfx():
  return _operand24(0x82ab14, 0xbf, (0xfc, 0x7e, 0x8d, 0xa3, 0x0a),
                    (0x8d, 0xa2, 0x0a), 'overworld area gfx table')


@cache
def area_palettes():
  return _operand24(0x82aafc, 0xbf, (0x40, 0xfd, 0x7e, 0x85, 0x00),
                    (0x22, 0xa8, 0xd5), 'overworld area palette table')


@cache
def sign_text():
  return _indexed_table(0x87b519, 0xb9, (0x07, 0xa5, 0x8a, 0x0a, 0xa8),
                        (0x8d, 0xf0, 0x1c), 'overworld sign text table')


@cache
def music_sets():
  return _indexed_table(0x82c497, 0xb9, (0xb0, 0x03, 0xa0, 0x00, 0x00),
                        (0x9f, 0x00, 0x5b, 0x7f), 'overworld music table')


@cache
def music_sets2():
  return _indexed_table(0x82c4a8, 0xb9, (0xd0, 0xf2, 0xa0, 0x00, 0x00),
                        (0x9f, 0x00, 0x5b, 0x7f), 'overworld music table 2')


@cache
def map_is_small():
  return _operand24(0x82ac3c, 0xbf, (0x16, 0x04, 0x60, 0xa6, 0x8a),
                    (0xf0, 0x0c, 0xae, 0x10, 0x04), 'overworld size table')


@cache
def sprite_gfx():
  return _indexed_table(0x80fc4c, 0xb9, (0xfe, 0x00, 0xa2, 0x3e, 0x00),
                        (0x9f, 0x00, 0xfd, 0x7e), 'overworld sprite gfx table')


@cache
def sprite_palettes():
  return _indexed_table(0x80fc53, 0xb9, (0xfa, 0x9f, 0x00, 0xfd, 0x7e),
                        (0x9f, 0x80, 0xfd, 0x7e),
                        'overworld sprite palette table')


@cache
def sprite_tables():
  """The three overworld sprite pointer tables (beginning/first/second)."""
  return (
    _indexed_table(0x89c4ef, 0xb9, (0x0e, 0xc9, 0x02, 0xf0, 0x14),
                   (0x85, 0x00, 0xb9, 0x82, 0xc8), 'overworld sprite stage 0'),
    _indexed_table(0x89c503, 0xb9, (0xb9, 0x22, 0xca, 0x80, 0x08),
                   (0x85, 0x00, 0xb9, 0x02, 0xc9), 'overworld sprite stage 1'),
    _indexed_table(0x89c4f9, 0xb9, (0xb9, 0x82, 0xc8, 0x80, 0x12),
                   (0x85, 0x00, 0xb9, 0x22, 0xca), 'overworld sprite stage 2'),
  )


@cache
def hibytes_comp():
  first = _operand24(0x82f59d, 0xbf, (0x69, 0x80, 0x00, 0x85, 0x03),
                     (0x85, 0xc8, 0xbf), 'overworld hi byte pointer table')
  second = _operand24(0x82f7e3, 0xbf, (0x69, 0x80, 0x00, 0x85, 0x03),
                      (0x85, 0xc8, 0xbf), 'overworld hi byte pointer table')
  return _same_table(first, second, 'overworld hi byte pointer table')


@cache
def lobytes_comp():
  first = _operand24(0x82f5c8, 0xbf, (0x30, 0x20, 0x38, 0xf6, 0xfa),
                     (0x85, 0xc8, 0xbf), 'overworld lo byte pointer table')
  second = _operand24(0x82f80e, 0xbf, (0x30, 0x20, 0x38, 0xf6, 0xfa),
                      (0x85, 0xc8, 0xbf), 'overworld lo byte pointer table')
  return _same_table(first, second, 'overworld lo byte pointer table')


@cache
def map32_to_map16():
  """The four 6-byte map32->map16 tables read by $82:f691."""
  return (
    _operand24(0x82f6ab, 0xbf, (0x42, 0x44, 0xaa, 0xe2, 0x20),
               (0x8d, 0x00, 0x44), 'map32->map16 table 0'),
    _operand24(0x82f6eb, 0xbf, (0x29, 0x0f, 0x8d, 0x07, 0x44),
               (0x8d, 0x10, 0x44), 'map32->map16 table 1'),
    _operand24(0x82f72b, 0xbf, (0x29, 0x0f, 0x8d, 0x17, 0x44),
               (0x8d, 0x20, 0x44), 'map32->map16 table 2'),
    _operand24(0x82f76b, 0xbf, (0x29, 0x0f, 0x8d, 0x27, 0x44),
               (0x8d, 0x30, 0x44), 'map32->map16 table 3'),
  )


@cache
def map16_to_map8():
  return _operand24(0x808864, 0xbf, (0x00, 0x05, 0x06, 0x0a, 0xaa),
                    (0x85, 0x06, 0x29, 0xff), 'map16->map8 table')


@cache
def map8_data_to_tile_attr():
  return _operand24(0x80886e, 0xbf, (0x06, 0x29, 0xff, 0x01, 0xaa),
                    (0xe2, 0x30, 0xc9, 0x10), 'map8 data attribute table')


@cache
def some_tile_attr():
  return _operand24(0x85fac3, 0xbf, (0xaa,),
                    (0xe2, 0x30, 0x6b, 0x8b), 'some tile attribute table')


@cache
def secrets_offsets():
  return _operand24(0x9bc8b8, 0xbf, (0x00, 0xb0, 0x7d, 0x0a, 0xaa),
                    (0x85, 0x00, 0xa9, 0x1b, 0x00),
                    'overworld secret offset table')


@cache
def exit_tables():
  """All 13 parallel tables used by LoadOverworldFromDungeon."""
  return {
    'room': _indexed_table(
      0x82e4d8, 0xdd, (0xe5, 0xa2, 0x9e, 0xca, 0xca),
      (0xd0, 0xf9), 'overworld exit room table'),
    'screen_index': _indexed_table(
      0x82e549, 0xbd, (0x8a, 0x4a, 0xaa, 0xe2, 0x20),
      (0x85, 0x8a, 0x8d, 0x0a, 0x04), 'overworld exit screen table'),
    'map16_load': _indexed_table(
      0x82e501, 0xbd, (0x85, 0x22),
      (0x85, 0x84, 0x38, 0xe9), 'overworld exit load table'),
    'scroll_x': _indexed_table(
      0x82e4ea, 0xbd, (0x22, 0x01, 0x8d, 0x24, 0x01),
      (0x85, 0xe0, 0x85, 0xe2), 'overworld exit scroll x table'),
    'scroll_y': _indexed_table(
      0x82e4dd, 0xbd, (0xd0, 0xf9),
      (0x85, 0xe6, 0x85, 0xe8), 'overworld exit scroll y table'),
    'x': _indexed_table(
      0x82e4fc, 0xbd, (0x85, 0x20),
      (0x85, 0x22), 'overworld exit x table'),
    'y': _indexed_table(
      0x82e4f7, 0xbd, (0x1e, 0x01, 0x8d, 0x20, 0x01),
      (0x85, 0x20), 'overworld exit y table'),
    'camera_x': _indexed_table(
      0x82e528, 0xbd, (0x3a, 0x3a, 0x8d, 0x1a, 0x06),
      (0x8d, 0x1c, 0x06), 'overworld exit camera x table'),
    'camera_y': _indexed_table(
      0x82e51d, 0xbd, (0x3e, 0x00, 0x4a, 0x85, 0x86),
      (0x8d, 0x18, 0x06), 'overworld exit camera y table'),
    'normal_door': _indexed_table(
      0x82e538, 0xbd, (0xa9, 0x02, 0x00, 0x85, 0x2f),
      (0x8d, 0x96, 0x06), 'overworld exit normal door table'),
    'fancy_door': _indexed_table(
      0x82e53e, 0xbd, (0x8d, 0x96, 0x06),
      (0x8d, 0x98, 0x06), 'overworld exit fancy door table'),
    'unk1': _indexed_table(
      0x82e556, 0xbd, (0x64, 0x8b, 0x9c, 0x0b, 0x04),
      (0x8d, 0x24, 0x06, 0x9c, 0x25), 'overworld exit unk1 table'),
    'unk3': _indexed_table(
      0x82e565, 0xbd, (0x90, 0x03, 0xce, 0x25, 0x06),
      (0x8d, 0x28, 0x06, 0x9c, 0x29), 'overworld exit unk3 table'),
  }


@cache
def travel_tables():
  """Bird-travel and whirlpool tables read by the $82:ec34 routine."""
  return {
    'screen_index': _indexed_table(
      0x82ec8b, 0xbd, (0x28, 0x06, 0x8d, 0x2a, 0x06),
      (0x85, 0x8a, 0x8d, 0x0a, 0x04), 'bird travel screen table'),
    'map16_load': _indexed_table(
      0x82ec93, 0xbd, (0x85, 0x8a, 0x8d, 0x0a, 0x04),
      (0x85, 0x84, 0x38, 0xe9), 'bird travel load table'),
    'scroll_x': _indexed_table(
      0x82ec54, 0xbd, (0x22, 0x01, 0x8d, 0x24, 0x01),
      (0x85, 0xe0, 0x85, 0xe2), 'bird travel scroll x table'),
    'scroll_y': _indexed_table(
      0x82ec47, 0xbd, (0xf0, 0x1a, 0xae, 0xf0, 0x1a),
      (0x85, 0xe6, 0x85, 0xe8), 'bird travel scroll y table'),
    'x': _indexed_table(
      0x82ec66, 0xbd, (0x85, 0x20),
      (0x85, 0x22), 'bird travel x table'),
    'y': _indexed_table(
      0x82ec61, 0xbd, (0x1e, 0x01, 0x8d, 0x20, 0x01),
      (0x85, 0x20), 'bird travel y table'),
    'camera_x': _indexed_table(
      0x82ecba, 0xbd, (0x3a, 0x3a, 0x8d, 0x1a, 0x06),
      (0x8d, 0x1c, 0x06), 'bird travel camera x table'),
    'camera_y': _indexed_table(
      0x82ecaf, 0xbd, (0x3e, 0x00, 0x4a, 0x85, 0x86),
      (0x8d, 0x18, 0x06), 'bird travel camera y table'),
    'unk1': _indexed_table(
      0x82ec6b, 0xbd, (0x85, 0x22),
      (0x8d, 0x24, 0x06), 'bird travel unk1 table'),
    'unk3': _indexed_table(
      0x82ec7b, 0xbd, (0x24, 0x06, 0x8d, 0x26, 0x06),
      (0x8d, 0x28, 0x06), 'bird travel unk3 table'),
    'whirlpool': _operand24(
      0x82ed13, 0xdf, (0x10, 0xa5, 0x8a, 0xca, 0xca),
      (0xd0, 0xf8, 0x8a), 'whirlpool source table'),
  }


@cache
def entrance_tables():
  return {
    'area': _operand24(
      0x9bbd19, 0xdf, (0xd0, 0xf6, 0xad, 0x0a, 0x04),
      (0xd0, 0xeb, 0xaf), 'overworld entrance area table'),
    'pos': _operand24(
      0x9bbd10, 0xdf, (0x00, 0xca, 0xca, 0x30, 0x67),
      (0xd0, 0xf6, 0xad), 'overworld entrance position table'),
    'entrance_id': _operand24(
      0x9bbd5f, 0xbf, (0x8a, 0x4a, 0xaa, 0xe2, 0x20),
      (0x8d, 0x0e, 0x01), 'overworld entrance id table'),
  }


@cache
def hole_tables():
  return {
    'pos': _indexed_table(
      0x9bb893, 0xdd, (0xa2, 0x24, 0x00, 0xa5, 0x00),
      (0xd0, 0x08, 0xad, 0x0a), 'overworld hole position table'),
    'area': _indexed_table(
      0x9bb89b, 0xdd, (0xd0, 0x08, 0xad, 0x0a, 0x04),
      (0xf0, 0x0f, 0xca, 0xca), 'overworld hole area table'),
    'entrance_id': _indexed_table(
      0x9bb8b4, 0xbd, (0xe2, 0x30, 0x8a, 0x4a, 0xaa),
      (0x8d, 0x0e, 0x01, 0x9c, 0x0f), 'overworld hole entrance table'),
  }
