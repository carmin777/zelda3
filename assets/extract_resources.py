from ast import literal_eval as make_tuple
import json
import sys
import text_compression
import util
from util import get_bytes, get_words, get_byte, get_word, get_int8, get_int16, cache
import tables
import yaml
import extract_music
import os
import sprite_sheets
import overworld_tables

# Tables that a hack may move are located through the operand of the original
# routine instead of the US address.  The fingerprints below are the bytes
# around the instruction in the US ROM; the operand itself is wildcarded.
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

SANITIZE_REPORT = []
OVERWORLD_REPORT = []
def _report(action, detail):
  SANITIZE_REPORT.append('%s: %s' % (action, detail))

def _ow_report(action, detail):
  OVERWORLD_REPORT.append('%s: %s' % (action, detail))

def _fallback_room(room):
  directory = os.environ.get('ZELDA3_DUNGEON_FALLBACK')
  if not directory:
    return None
  path = os.path.join(directory, 'dungeon-%d.yaml' % room)
  if not os.path.isfile(path):
    return None
  return open(path, 'r', encoding='utf8').read()

@cache
def dungeon_room_object_table():
  low = _operand24(0x81874b, 0xbf, (0x85, 0xb8), (0x85, 0xb7),
                   'dungeon room object pointer table')
  high = _operand24(0x818745, 0xbf, (0x30, 0xae, 0x10, 0x01),
                    (0x85, 0xb8), 'dungeon room object pointer table')
  if high != low + 1:
    raise Exception(
      'dungeon room object pointer loads disagree: $%06x and $%06x' % (
        low, high))
  return low

@cache
def dungeon_header_table():
  base = _operand24(0x81b5dc, 0xbf, (0xa5, 0xa0, 0x0a, 0xaa),
                    (0x85, 0x0d, 0xe2, 0x20), 'dungeon room header table')
  values = [get_word(base + i * 2) for i in range(320)]
  count = 320
  # The hack tables are a contiguous run of 14-byte headers.  The US table
  # reuses records, so its offsets do not step by 14 and stays at 320.
  if len(values) > 1 and values[0] and values[1] == values[0] + 14:
    count = 1
    while (count < 320 and
           values[count] == values[0] + count * 14):
      count += 1
    _report('header table', '%s has %d valid entries; rooms %d..319 use the US room' % (
      '$%06x' % base, count, count))
  return base, count

@cache
def dungeon_sprite_table():
  return _indexed_table(0x89c297, 0xb9, (0x8e, 0x04, 0x0a, 0xa8),
                        (0x85, 0x00, 0xad), 'dungeon sprite pointer table')

@cache
def dungeon_secret_table():
  return _operand24(0x81e6c1, 0xbf, (0xa5, 0xa0, 0x0a, 0xaa),
                    (0x85, 0x00, 0xa9), 'dungeon secret pointer table')

@cache
def dungeon_chest_table():
  first = _operand24(0x81ebfa, 0xbf, (0xf8, 0x01, 0xf0, 0xe6),
                     (0x29, 0xff, 0x7f), 'dungeon chest table')
  second = _operand24(0x81ec0f, 0xbf, (0xe9, 0x01, 0x85, 0x0c),
                      (0x0a, 0x90, 0x24), 'dungeon chest table')
  if first != second:
    raise Exception('dungeon chest table loads disagree: $%06x and $%06x' % (
      first, second))
  return first

@cache
def dungeon_telemsg_table():
  return _indexed_table(0x87b4ff, 0xb9, (0xa5, 0xa0, 0x0a, 0xa8),
                        (0x80, 0x18, 0xaf), 'dungeon tele message table')

@cache
def dungeon_pits_table():
  return _operand24(0x8794aa, 0xdf, (0x70, 0x00, 0xa5, 0xa0),
                    (0xf0, 0x52, 0xca), 'pits that hurt player table')

@cache
def dungeon_default_table():
  low = _operand24(0x81882c, 0xbf, (0x85, 0xb8),
                   (0x85, 0xb7), 'dungeon default layout table')
  high = _operand24(0x818826, 0xbf, (0x18, 0x65, 0x00, 0xaa),
                    (0x85, 0xb8), 'dungeon default layout table')
  if high != low + 1:
    raise Exception('dungeon default table loads disagree: $%06x and $%06x' % (
      low, high))
  return low

@cache
def dungeon_overlay_table():
  low = _operand24(0x81b856, 0xbf, (0x85, 0xb8),
                   (0x85, 0xb7), 'dungeon overlay table')
  high = _operand24(0x81b850, 0xbf, (0x6d, 0xba, 0x04, 0xaa),
                    (0x85, 0xb8), 'dungeon overlay table')
  if high != low + 1:
    raise Exception('dungeon overlay table loads disagree: $%06x and $%06x' % (
      low, high))
  return low

_ENTRANCE_CODE = {
  0: {
    'rooms': (0x82d9a3, 0xbd, (0xaa, 0x0a, 0x0a, 0xa8), (0x85, 0xa0)),
    'relativeCoords': (0x82da1a, 0xb9, (0x00, 0xaa, 0xe2, 0x20), (0x8d, 0x01)),
    'scrollX': (0x82d9b8, 0xbd, (0x01, 0x8d, 0x24, 0x01), (0x85, 0xe2)),
    'scrollY': (0x82d9ab, 0xbd, (0xa0, 0x8d, 0x8e, 0x04), (0x85, 0xe8)),
    'playerX': (0x82d9d0, 0xbd, (0x59, 0xcf, 0x85, 0x20), (0x85, 0x22)),
    'playerY': (0x82d9cb, 0xbd, (0xf3, 0x7e, 0xf0, 0x0a), (0x85, 0x20)),
    'cameraX': (0x82d9e0, 0xbd, (0x1a, 0x8d, 0x1a, 0x06), (0x8d, 0x1c)),
    'cameraY': (0x82d9d5, 0xbd, (0x63, 0xd0, 0x85, 0x22), (0x8d, 0x18)),
    'blockset': (0x82da74, 0xbd, (0xa9, 0x00, 0x85, 0x2f), (0x8d, 0xa1)),
    'floor': (0x82da91, 0xbd, (0x12, 0x8d, 0x32, 0x01), (0x85, 0xa4)),
    'palace': (0x82da96, 0xbd, (0x06, 0xd4, 0x85, 0xa4), (0x8d, 0x0c)),
    'doorwayOrientation': (0x82da9c, 0xbd, (0xd4, 0x8d, 0x0c, 0x04), (0x85, 0x6c)),
    'startingBg': (0x82daa1, 0xbd, (0x10, 0xd5, 0x85, 0x6c), (0x4a, 0x4a)),
    'quadrant1': (0x82dab2, 0xbd, (0x0f, 0x8d, 0x76, 0x04), (0x4a, 0x4a)),
    'quadrant2': (0x82dac2, 0xbd, (0x29, 0x0f, 0x85, 0xa7), (0x4a, 0x4a)),
    'doorSettings': (0x82d9f0, 0xbd, (0xf8, 0x01, 0x85, 0xec), (0x8d, 0x96)),
    'musicTrack': (0x82da7a, 0xbd, (0xd3, 0x8d, 0xa1, 0x0a), (0x8d, 0x32)),
  },
  1: {
    'entrance': (0x82dc61, 0xbd, (0xaa, 0x0a, 0x0a, 0xa8), (0x8d, 0x0e)),
    'rooms': (0x82dc67, 0xbd, (0xdc, 0x8d, 0x0e, 0x01), (0x85, 0xa0)),
    'relativeCoords': (0x82dcdc, 0xb9, (0x00, 0xaa, 0xe2, 0x20), (0x8d, 0x01)),
    'scrollX': (0x82dc7c, 0xbd, (0x01, 0x8d, 0x24, 0x01), (0x85, 0xe2)),
    'scrollY': (0x82dc6f, 0xbd, (0xa0, 0x8d, 0x8e, 0x04), (0x85, 0xe8)),
    'playerX': (0x82dc94, 0xbd, (0xd0, 0xdb, 0x85, 0x20), (0x85, 0x22)),
    'playerY': (0x82dc8f, 0xbd, (0xf3, 0x7e, 0xf0, 0x0a), (0x85, 0x20)),
    'cameraX': (0x82dca4, 0xbd, (0x1a, 0x8d, 0x1a, 0x06), (0x8d, 0x1c)),
    'cameraY': (0x82dc99, 0xbd, (0xde, 0xdb, 0x85, 0x22), (0x8d, 0x18)),
    'blockset': (0x82dd2a, 0xbd, (0xa9, 0x02, 0x85, 0x2f), (0x8d, 0xa1)),
    'floor': (0x82dd30, 0xbd, (0xdc, 0x8d, 0xa1, 0x0a), (0x85, 0xa4)),
    'palace': (0x82dd35, 0xbd, (0x0f, 0xdc, 0x85, 0xa4), (0x8d, 0x0c)),
    'startingBg': (0x82dd3d, 0xbd, (0x0c, 0x04, 0x64, 0x6c), (0x4a, 0x4a)),
    'quadrant1': (0x82dd4e, 0xbd, (0x0f, 0x8d, 0x76, 0x04), (0x4a, 0x4a)),
    'quadrant2': (0x82dd5e, 0xbd, (0x29, 0x0f, 0x85, 0xa7), (0x4a, 0x4a)),
    'doorSettings': (0x82dcb4, 0xbd, (0xf8, 0x01, 0x85, 0xec), (0x8d, 0x96)),
    'musicTrack': (0x82dd6e, 0xbd, (0x29, 0x0f, 0x85, 0xaa), (0x8d, 0x32)),
  },
}

@cache
def entrance_table(set, name):
  addr, opcode, _before, after = _ENTRANCE_CODE[set][name]
  # The bytes before these loads are often the operand of the previous table's
  # load.  Check the opcode and the instructions after it instead, so moving
  # one entrance table does not invalidate the fingerprint of the next one.
  return _indexed_table(addr, opcode, (), after, '%s %s table' % (
    'entrance' if set == 0 else 'starting point', name))

def print_map32_to_map16(f):
  tables = overworld_tables.map32_to_map16()
  for i in range(2218):
    def getit(ea):
      ov = [get_byte(ea + j) for j in range(6)]
      res=[0]*4
      res[0] = ov[0] | (ov[4] >> 4) << 8
      res[1] = ov[1] | (ov[4] & 0xf) << 8
      res[2] = ov[2] | (ov[5] >> 4) << 8
      res[3] = ov[3] | (ov[5] & 0xf) << 8
      return res
    t0 = getit(tables[0] + i * 6)
    t1 = getit(tables[1] + i * 6)
    t2 = getit(tables[2] + i * 6)
    t3 = getit(tables[3] + i * 6)
    for j in range(4):
      print('%5d: %4d, %4d, %4d, %4d' % (i * 4 + j, t0[j], t1[j], t2[j], t3[j]), file = f)

@cache
def get_exit_datas():
  r = {}
  t = overworld_tables.exit_tables()
  for i in range(79):
    room = get_word(t['room'] + i * 2)
    screen_index = get_byte(t['screen_index'] + i)
    if screen_index > 191:
      _ow_report('exit table', 'entry %d has invalid screen $%02x; skipped' % (
        i, screen_index))
      continue
    load_offs = get_word(t['map16_load'] + i * 2)
    scroll_y = get_word(t['scroll_y'] + i * 2)
    scroll_x = get_word(t['scroll_x'] + i * 2)
    pos_y = get_word(t['y'] + i * 2)
    pos_x = get_word(t['x'] + i * 2)
    camera_y = get_word(t['camera_y'] + i * 2)
    camera_x = get_word(t['camera_x'] + i * 2)
    unk1 = get_int8(t['unk1'] + i)
    unk3 = get_int8(t['unk3'] + i)
    ndoor = get_word(t['normal_door'] + i * 2)
    fdoor = get_word(t['fancy_door'] + i * 2)
    base_x = (screen_index & 7) << 9
    base_y = (screen_index & 56) << 6
    y = {
      'index' : i,
      'room' : room,
      'xy' : [pos_x - base_x, pos_y - base_y],
      'scroll_xy' : [scroll_x - base_x, scroll_y - base_y],
      'camera_xy' : [camera_x - base_x, camera_y - base_y],
    }
    y['load_xy'] = [((load_offs >> 1) - (y['scroll_xy'][0] >> 4)) & 0x3f, (load_offs >> 7) - (y['scroll_xy'][1] >> 4) & 0x3f]
    y['unk'] = [unk1, unk3]
    def get_special_exit_info(room_index):
      return {
        'dir' : get_byte(0x82E801  + room_index - 0x180) >> 1,
        'spr_gfx' : get_byte(0x82E811  + room_index - 0x180),
        'aux_gfx' : get_byte(0x82E821  + room_index - 0x180),
        'pal_bg' : get_byte(0x82E831  + room_index - 0x180),
        'pal_spr' : get_byte(0x82E841  + room_index - 0x180),
        'top' : get_word(0x82e6e1 + (room_index - 0x180) * 2),
        'bottom' : get_word(0x82e701 + (room_index - 0x180) * 2),
        'left' : get_word(0x82e721 + (room_index - 0x180) * 2),
        'right' : get_word(0x82e741 + (room_index - 0x180) * 2),
        'left_edge_of_map' : get_word(0x82E7E1 + (room_index - 0x180) * 2),
        'unk4' : get_int16(0x82e761 + (room_index - 0x180) * 2),
        'unk6' : get_int16(0x82e781 + (room_index - 0x180) * 2),
        'unk5' : get_int16(0x82e7a1 + (room_index - 0x180) * 2),
        'unk7' : get_int16(0x82e7c1 + (room_index - 0x180) * 2),
      }

    if room >= 0x180 and room < 0x190:
      y['special_exit'] = get_special_exit_info(room)

    if ndoor != 0:
      assert fdoor == 0
      y['door'] = ['bombable' if ndoor & 0x8000 else 'wooden', (ndoor & 0x7e) >> 1, (ndoor & 0x3f80) >> 7]
    if fdoor != 0:
      y['door'] = ['palace' if fdoor & 0x8000 else 'sanctuary', (fdoor & 0x7e) >> 1, (fdoor & 0x3f80) >> 7]
    r.setdefault(screen_index, []).append(y)
  return r

def get_loadoffs(c, d):
  x, y = c[0] >> 4, c[1] >> 4
  x += d[0]
  y += d[1]
  return (y&0x3f) << 7 | (x&0x3f) << 1

@cache
def get_ow_travel_infos():
  r = {}
  t = overworld_tables.travel_tables()
  for i in range(17):
    screen_index = get_word(t['screen_index'] + i * 2)
    if screen_index > 191:
      _ow_report('travel table', 'entry %d has invalid screen $%04x; skipped' % (
        i, screen_index))
      continue
    load_offs = get_word(t['map16_load'] + i * 2)
    scroll_y = get_word(t['scroll_y'] + i * 2)
    scroll_x = get_word(t['scroll_x'] + i * 2)
    pos_y = get_word(t['y'] + i * 2)
    pos_x = get_word(t['x'] + i * 2)
    camera_y = get_word(t['camera_y'] + i * 2)
    camera_x = get_word(t['camera_x'] + i * 2)
    unk1 = get_int8(t['unk1'] + i * 2)
    unk3 = get_int8(t['unk3'] + i * 2)
    base_x = (screen_index & 7) << 9
    base_y = (screen_index & 56) << 6
    y = {}
    if i < 9:
      y['bird_travel_id'] = i
    else:
      y['whirlpool_src_area'] = get_word(t['whirlpool'] + (i - 9) * 2)
    y['xy'] = [pos_x - base_x, pos_y - base_y]
    y['scroll_xy'] = [scroll_x - base_x, scroll_y - base_y]
    y['camera_xy'] = [camera_x - base_x, camera_y - base_y]
    y['load_xy'] = [((load_offs >> 1) - (y['scroll_xy'][0] >> 4)) & 0x3f, (load_offs >> 7) - (y['scroll_xy'][1] >> 4) & 0x3f]

    t0 = get_loadoffs(y['scroll_xy'], y['load_xy'])
    assert t0 == load_offs, (t0 & 0x7f, load_offs & 0x7f)
    
    y['unk'] = [unk1, unk3]
    r.setdefault(screen_index, []).append(y)
  return r

@cache
def get_ow_entrance_info():
  r = {}
  t = overworld_tables.entrance_tables()
  for i in range(129):
    area = get_word(t['area'] + i * 2)
    pos = get_word(t['pos'] + i * 2)
    entrance_id = get_byte(t['entrance_id'] + i)
    if area > 191:
      _ow_report('entrance table', 'entry %d has invalid area $%04x; skipped' % (
        i, area))
      continue
    r.setdefault(area, []).append({'index' : i, 'x' : (pos >> 1) & 0x3f, 'y' : (pos >> 7) & 0x3f, 'entrance_id' : entrance_id})
  return r

@cache
def get_hole_infos():
  r = {}
  t = overworld_tables.hole_tables()
  for i in range(19):
    pos = get_word(t['pos'] + i * 2) + 0x400
    area = get_word(t['area'] + i * 2)
    entrance_id = get_byte(t['entrance_id'] + i)
    if area > 191:
      _ow_report('hole table', 'entry %d has invalid area $%04x; skipped' % (
        i, area))
      continue
    r.setdefault(area, []).append({'x' : (pos >> 1) & 0x3f, 'y' : (pos >> 7) & 0x3f, 'entrance_id' : entrance_id})
  return r

def _secret_name(value):
  name = tables.kSecretNames.get(value)
  if name is None:
    raise Exception('no name for secret type $%02x' % value)
  return name

def print_overworld_area(overworld_area):
  is_small = get_bytes(overworld_tables.map_is_small(), 192)
  y = {}

  def get_music(ambient):
    def fn(x):
      if ambient:
        name = tables.kAmbientSoundName.get(x >> 4)
        if name is None:
          _ow_report('music table', 'area %d has unknown ambient $%02x; using None' % (
            overworld_area, x))
          return tables.kAmbientSoundName[0]
        return name
      else:
        return tables.kMusicNames[x & 0xf]
    if overworld_area < 64:
      music = overworld_tables.music_sets()
      return {
        'beginning' : fn(get_byte(music + overworld_area)),
        'zelda' : fn(get_byte(music + overworld_area + 64)),
        'sword' : fn(get_byte(music + overworld_area + 128)),
        'agahnim' : fn(get_byte(music + overworld_area + 192)),
      }
    else:
      assert overworld_area < 64 + 96
      music2 = overworld_tables.music_sets2()
      return {'agahnim' : fn(get_byte(music2 + overworld_area - 64)) }

  def get_items():
    if overworld_area >= 128: return []
    offsets = overworld_tables.secrets_offsets()
    offset = get_word(offsets + overworld_area * 2)
    if offset < 0x8000:
      _ow_report('secret table', 'area %d has invalid offset $%04x; items ignored' % (
        overworld_area, offset))
      return []
    ea = 0x9b0000 | offset
    xs = []
    while True:
      pos = get_word(ea)
      if pos == 0xffff:
        break
      if pos & 1 or (pos >> 7) >= 64:
        _ow_report('secret table', 'area %d has invalid position $%04x; items ignored' % (
          overworld_area, pos))
        return []
      typ = get_byte(ea + 2)
      if typ not in tables.kKnownSecretTypes:
        _ow_report('secret table', 'area %d keeps unknown secret type $%02x at $%06x' % (
          overworld_area, typ, ea))
      x, y = pos//2%64, pos//2//64
      xs.append([x, y, _secret_name(typ)])
      ea += 3
      if (ea & 0xffff) < 0x8000:
        _ow_report('secret table', 'area %d overruns its data; items ignored' % (
          overworld_area))
        return []
    return xs

  header = {
    'name' : tables.kAreaNames[overworld_area],
    'size' : 'small' if is_small[overworld_area] else 'big',
    'gfx' : get_byte(overworld_tables.area_gfx() + overworld_area) if overworld_area < 128 else -1,
    'palette' : get_byte(overworld_tables.area_palettes() + overworld_area) if overworld_area < 136 else -1,
    'sign_text' : get_word(overworld_tables.sign_text() + overworld_area * 2) if overworld_area < 128 else -1,
    'music' : get_music(False),
    'ambient' : get_music(True),
  }
      
  y['Header'] = header
  y['Travel'] = get_ow_travel_infos().get(overworld_area, [])
  y['Entrances'] = get_ow_entrance_info().get(overworld_area, [])
  hole_infos = get_hole_infos()
  if overworld_area in hole_infos:
    y['Holes'] = hole_infos[overworld_area]
  y['Exits'] = get_exit_datas().get(overworld_area, [])
  y['Items'] = get_items()
  
  sprite_tables = overworld_tables.sprite_tables()
  def decode_sprites(base_addr):
    r = []
    ptr = get_word(base_addr + overworld_area * 2)
    if ptr == 0:
      return r
    if ptr < 0x8000:
      _ow_report('sprite table', 'area %d has invalid pointer $%04x; sprites ignored' % (
        overworld_area, ptr))
      return r
    ea = 0x890000 + ptr
    while get_byte(ea) != 0xff:
      if ((ea & 0x7fff) + 3) > 0x8000:
        _ow_report('sprite table', 'area %d sprite list overruns its bank; stopped' % (
          overworld_area))
        break
      y, x, w = get_byte(ea), get_byte(ea+1), get_byte(ea+2)
      r.append([x, y, tables.kSpriteNames[w]])
      ea += 3
    return r

  def get_info(stage):
    if overworld_area >= 128: return {}
    if overworld_area >= 64: stage = 3
    return {
      'gfx' : get_byte(overworld_tables.sprite_gfx() + (overworld_area & 63) + stage * 64),
      'palette' : get_byte(overworld_tables.sprite_palettes() + (overworld_area & 63) + stage * 64),
    }
  if overworld_area < 64:
    y['Sprites.Beginning'] = {
      'info' : get_info(0),
      'sprites' : decode_sprites(sprite_tables[0])
    }
    y['Sprites.FirstPart'] = {
      'info' : get_info(1),
      'sprites' : decode_sprites(sprite_tables[1])
    }
    y['Sprites.SecondPart'] = {
      'info' : get_info(2),
      'sprites' : decode_sprites(sprite_tables[2])
    }
  elif overworld_area < 144:
    y['Sprites'] = {
      'info' : get_info(2),
      'sprites' : decode_sprites(sprite_tables[2])
    }
    
  s = yaml.dump(y, default_flow_style=None, sort_keys=False)
  open('overworld/overworld-%d.yaml' % overworld_area, 'w').write(s)

def _overworld_table_report():
  return {
    'area_heads': '0x%06x' % overworld_tables.area_heads(),
    'area_gfx': '0x%06x' % overworld_tables.area_gfx(),
    'area_palettes': '0x%06x' % overworld_tables.area_palettes(),
    'sign_text': '0x%06x' % overworld_tables.sign_text(),
    'music_sets': '0x%06x' % overworld_tables.music_sets(),
    'music_sets2': '0x%06x' % overworld_tables.music_sets2(),
    'map_is_small': '0x%06x' % overworld_tables.map_is_small(),
    'sprite_gfx': '0x%06x' % overworld_tables.sprite_gfx(),
    'sprite_palettes': '0x%06x' % overworld_tables.sprite_palettes(),
    'sprite_tables': ['0x%06x' % x for x in overworld_tables.sprite_tables()],
    'hibytes_comp': '0x%06x' % overworld_tables.hibytes_comp(),
    'lobytes_comp': '0x%06x' % overworld_tables.lobytes_comp(),
    'map32_to_map16': ['0x%06x' % x for x in overworld_tables.map32_to_map16()],
    'map16_to_map8': '0x%06x' % overworld_tables.map16_to_map8(),
    'map8_data_to_tile_attr': '0x%06x' % overworld_tables.map8_data_to_tile_attr(),
    'some_tile_attr': '0x%06x' % overworld_tables.some_tile_attr(),
    'secrets_offsets': '0x%06x' % overworld_tables.secrets_offsets(),
    'exit_tables': {k: '0x%06x' % v for k, v in overworld_tables.exit_tables().items()},
    'travel_tables': {k: '0x%06x' % v for k, v in overworld_tables.travel_tables().items()},
    'entrance_tables': {k: '0x%06x' % v for k, v in overworld_tables.entrance_tables().items()},
    'hole_tables': {k: '0x%06x' % v for k, v in overworld_tables.hole_tables().items()},
  }

def print_all_overworld_areas():
  area_heads = get_bytes(overworld_tables.area_heads(), 64)
  
  for i in range(160):
    if i >= 128 or area_heads[i&63] == (i&63):
      print_overworld_area(i)

  report_path = os.environ.get('ZELDA3_OVERWORLD_REPORT')
  if report_path:
    report = {
      'tables': _overworld_table_report(),
      'sanitized': OVERWORLD_REPORT,
    }
    with open(report_path, 'w', encoding='utf8') as f:
      json.dump(report, f, indent=2)
      f.write('\n')

def print_dialogue():
  text_compression.print_strings(util.ROM, file = open(text_compression.dialogue_filename(util.ROM.language), 'w', encoding='utf8'))

def decode_room_objects(p):
  objs = []
  j = 0
  while True:
    p0, p1, p2 = get_byte(p), get_byte(p+1), get_byte(p+2)
    A = p0 | p1 << 8
    if A == 0xffff:
      return p + 2, objs, None
    if A == 0xfff0:
      p += 2
      break
    if (A & 0xfc) != 0xfc:
      index = p2
      Dst = (p1 >> 2) << 7
      Dst |= (p0 & 0xfc) >> 1
      X = (Dst >> 1) & 0x3f
      Y = (Dst >> 7) & 0x3f
      W = p0 & 3
      H = p1 & 3
      if index < 0xf8:
        objs.append({'x' : X, 'y' : Y, 's' : '%d*%d' % (W, H), 'n': tables.kType0Names[index]})
      else:
        index2 = (index & 7) << 4 | H << 2 | W
        objs.append({'x' : X, 'y' : Y, 'n': tables.kType1Names[index2]})
    else:
      # subtype 2: 111111xx xxxxyyyy yyiiiiii
      X = ((p0 << 4 | p1 >> 4) & 0x3f)
      Y = (p1 << 2 | p2 >> 6) & 0x3f
      index = p2 & 0x3f
      objs.append({'x' : X, 'y' : Y, 'n' : tables.kType2Names[index]})
    p += 3
    j += 1

  doors = []
  while True:
    A = get_byte(p) | get_byte(p+1) << 8
    if A == 0xffff:
      return p + 2, objs, doors
    doors.append({'type':get_byte(p + 1), 'pos' : get_byte(p) >> 4, 'dir' : A & 3})
    p += 2

@cache
def get_chest_info():
  ea = dungeon_chest_table()
  all = {}
  for i in range(504//3):
    room = get_word(ea + i * 3)
    data = get_byte(ea + i * 3 + 2)
    if room == 0xffff or (room & 0x7fff) >= 320:
      _report('chest table', 'record %d has unused room $%04x; skipped' % (i, room))
      continue
    all.setdefault(room & 0x7fff, []).append((data, (room & 0x8000) != 0))
  return all

def _get_entrance_info_one(i, set):
  def table(name):
    return entrance_table(set, name)
  def get_exit_door(i):
    x = get_word(table('doorSettings') + i * 2)
    if x == 0:
      return ['none']
    if x == 0xffff:
      return ['none_0xffff']
    return ['bombable' if x & 0x8000 else 'wooden', (x & 0x7e) >> 1, (x & 0x3f80) >> 7]
  kQuadrantNames = { 0 : 'upper_left', 2 : 'lower_left', 16 : 'upper_right', 18 : 'lower_right' }
  room = get_word(table('rooms') + i * 2)
  def get_se(se_base_addr, xy, quds):
    base_x = (room & 0xf) * 2
    base_y = (room >> 4) * 2  
    ym = (xy[1] & 0x100) >> 8
    xm = (xy[0] & 0x100) >> 8
    #if room == 259: ym = 1 # possibly related to none_0xffff
    hu = get_byte(se_base_addr + i * 8 + 0) - base_y - ym
    fu = get_byte(se_base_addr + i * 8 + 1) - base_y
    hd = get_byte(se_base_addr + i * 8 + 2) - base_y - ym
    fd = get_byte(se_base_addr + i * 8 + 3) - base_y - 1
    #assert fu == 0 and fd == 0 and hd == 0 and fd == 0, (room, fu, fd, hd, fd)
    qqq = xm if room >= 242 and quds[0] == 'single_x' else 0
    hl = get_byte(se_base_addr + i * 8 + 4) - base_x - xm
    fl = get_byte(se_base_addr + i * 8 + 5) - base_x - qqq
    hr = get_byte(se_base_addr + i * 8 + 6) - base_x - xm
    fr = get_byte(se_base_addr + i * 8 + 7) - base_x - 1 - qqq
    return [hu, fu, hd, fd, hl, fl, hr, fr]#

  player_x = get_word(table('playerX') + i * 2) - ((room & 0x00f) << 9)
  player_y = get_word(table('playerY') + i * 2) - ((room & 0x1f0) << 5)

  quadrant_byte = get_byte(table('quadrant2') + i)
  if quadrant_byte not in kQuadrantNames:
    _report('entrance table', '%s %d has invalid quadrant $%02x; using upper_left' % (
      'entrance' if set == 0 else 'starting point', i, quadrant_byte))
    quadrant_byte = 0

  y = {
    ('entrance_index' if set == 0 else 'starting_point_index'): i,
    'name' : tables.kEntranceNames[i] if set == 0 else 'Starting Location %d' % i,
    'scroll_xy' : [
      get_word(table('scrollX') + i * 2) - ((room & 0x00f) << 9),
      get_word(table('scrollY') + i * 2) - ((room & 0x1f0) << 5),
    ],
    'player_xy' : [ player_x, player_y ],
    'camera_xy' : [
      get_word(table('cameraX') + i * 2),
      get_word(table('cameraY') + i * 2),
    ],
    'blockset' : get_byte(table('blockset') + i),
    'music' : tables.kMusicNames[get_byte(table('musicTrack') + i)],
    'palace' : tables.kPalaceNames[(get_int8(table('palace') + i) + 2) >> 1],
    'doorway_orientation' : get_int8(table('doorwayOrientation') + i) if set == 0 else 0,
    'plane' : get_byte(table('startingBg') + i) & 0xf,
    'ladder_level' : get_byte(table('startingBg') + i) >> 4,
    'quadrants' : [
      'double_x' if (get_byte(table('quadrant1') + i) & 0x20) != 0 else 'single_x',
      'double_y' if (get_byte(table('quadrant1') + i) & 0x2) else 'single_y',
      kQuadrantNames[quadrant_byte]
      ],
    'floor' : get_int8(table('floor') + i),
  }

  se = get_se(table('relativeCoords'), y['player_xy'], y['quadrants'])
  if se != [0, 0, 0, 0, 0, 0, 0, 0]:
    y['repair_scroll_bounds'] = se
  y['house_exit_door'] = get_exit_door(i)

#  quadrant_byte = (2 if player_y & 0x100 else 0) + (16 if player_x & 0x100 else 0)
#  if get_byte((0x82D69F, 0x82DC2B)[set] + i) != quadrant_byte:
#    print('Room %d has incorrect quadrant byte' % room, y['quadrants'])
  if set:
    y['associated_entrance_index'] = get_word(table('entrance') + i * 2)
  return room, y 

@cache
def get_entrance_info(set):
  r = {}
  for i in range(133 if set == 0 else 7):
    room, y = _get_entrance_info_one(i, set)
    r.setdefault(room, []).append(y)
  return r

@cache 
def pits_hurt_player():
  ea = dungeon_pits_table()
  result = set()
  for i in range(57):
    room = get_word(ea + i * 2)
    if room == 0xffff or room >= 320:
      _report('pits table', 'entry %d has unused room $%04x; skipped' % (i, room))
      continue
    result.add(room)
  return result

def _readable(ea, size=1):
  try:
    util.ROM.get_bytes(ea, size)
    return True
  except Exception:
    return False

def _fallback_room_or_raise(room_index, reason, fallback):
  if fallback is not None:
    _report('fallback', 'room %d: %s; using the US room' % (room_index, reason))
    return fallback
  raise Exception('room %d: %s (no US fallback configured)' % (room_index, reason))

def print_room(room_index):
  fallback = _fallback_room(room_index)
  obj_table = dungeon_room_object_table()
  p = obj_table + room_index * 3
  room_addr = get_byte(p) | get_byte(p+1) << 8 | get_byte(p+2) << 16
  if room_addr in (0, 0xffffff) or not _readable(room_addr, 2):
    return _fallback_room_or_raise(
      room_index, 'unusable room object pointer $%06x' % room_addr, fallback)

  header_table, header_count = dungeon_header_table()
  if room_index >= header_count:
    return _fallback_room_or_raise(
      room_index, 'no header entry in the %d-entry table' % header_count,
      fallback)
  header_off = get_word(header_table + room_index * 2)
  if header_off in (0, 0xffff):
    return _fallback_room_or_raise(
      room_index, 'unused header entry $%04x' % header_off, fallback)
  p = (header_table & 0xff0000) | header_off
  if p == 0x4FFEF:
    p = 0x82EDC5 # just some place with zeros
  if not _readable(p, 14):
    return _fallback_room_or_raise(
      room_index, 'unreadable header at $%06x' % p, fallback)

  sprite_ptr = get_word(dungeon_sprite_table() + room_index * 2)
  if sprite_ptr and not _readable(0x890000 | sprite_ptr):
    return _fallback_room_or_raise(
      room_index, 'unusable sprite pointer $%04x' % sprite_ptr, fallback)
  secret_ptr = get_word(dungeon_secret_table() + room_index * 2)
  if secret_ptr and not _readable(0x810000 | secret_ptr):
    return _fallback_room_or_raise(
      room_index, 'unusable secret pointer $%04x' % secret_ptr, fallback)

  floor, layout = get_byte(room_addr), get_byte(room_addr + 1)

  flags = get_byte(p + 0)
  p7, p8 = get_byte(p + 7), get_byte(p + 8)
  effect = get_byte(p + 4)
  tag0 = get_byte(p + 5)
  tag1 = get_byte(p + 6)
  if (effect >= len(tables.kEffectNames) or
      tag0 >= len(tables.kTagNames) or tag1 >= len(tables.kTagNames)):
    return _fallback_room_or_raise(
      room_index, 'invalid header effect/tag $%02x/$%02x/$%02x' % (
        effect, tag0, tag1), fallback)

  ea = 0x890000 | sprite_ptr
  sort_sprites_setting = get_byte(ea) if sprite_ptr else 0

  header = {
    'floor1': floor & 0xf,
    'floor2' : floor >> 4,
    'layout': layout >> 2,
    'start_quadrant' : layout & 3,
    'bg2' : tables.kBg2[flags >> 5],
    'collision' : tables.kCollisionNames[flags >> 2 & 7],
    'lights_out' : flags & 1,
    'palette' : get_byte(p + 1),
    'blockset' : get_byte(p + 2),
    'enemyblk' : get_byte(p + 3),
    'effect' : tables.kEffectNames[effect],
    'tag0' : tables.kTagNames[tag0],
    'tag1' : tables.kTagNames[tag1],
    'hole0_dest' : [get_byte(p+9), p7 & 3],
    'stair0_dest' : [get_byte(p+10), p7 >> 2 & 3],
    'stair1_dest' : [get_byte(p+11), p7 >> 4 & 3],
    'stair2_dest' : [get_byte(p+12), p7 >> 6 & 3],
    'stair3_dest' : [get_byte(p+13),p8 & 3],
    'tele_msg' : get_word(dungeon_telemsg_table() + room_index * 2),
    'sort_sprites' : sort_sprites_setting,
    'pits_hurt_player' : room_index in pits_hurt_player()
  }

  def get_sprites():
    if sprite_ptr == 0:
      return []
    ea = 0x890000 | sprite_ptr
    ea += 1
    r = []
    while get_byte(ea) != 0xff:
      y, x, type = get_byte(ea), get_byte(ea+1), get_byte(ea+2)
      if type == 0xe4:
        if y == 0xfe or y == 0xfd:
          r[-1].append('drop_key' if y == 0xfe else 'drop_big_key')
          ea += 3
          continue
      elif x >= 0xe0:
        floor = y >> 7
        index = type + 0x100
        if index >= len(tables.kSpriteNames):
          _report('sprite table', 'room %d has unknown sprite type $%02x; skipped' % (
            room_index, type))
          ea += 3
          continue
        r.append([x & 0x1f, y & 0x1f, 'lower' if floor else 'upper', tables.kSpriteNames[index]])
        ea += 3
        continue
      subtype = (x >> 5) | ((y >> 5) & 3) << 3
      floor = y >> 7
      if type >= len(tables.kSpriteNames):
        _report('sprite table', 'room %d has unknown sprite type $%02x; skipped' % (
          room_index, type))
        ea += 3
        continue
      name = tables.kSpriteNames[type]
      if subtype != 0:
        i = name.index('-')
        name = name[:i] + ('.%d' % subtype) + name[i:]
      r.append([x & 0x1f, y & 0x1f, 'lower' if floor else 'upper', name])
      ea += 3
    return r

  def get_secrets():
    if secret_ptr == 0:
      return []
    ea = 0x810000 | secret_ptr
    xs = []
    while get_word(ea) != 0xffff:
      pos = get_word(ea)
      if pos & 1:
        _report('secret table', 'room %d has odd secret position $%04x; skipped' % (
          room_index, pos))
        ea += 3
        continue
      secret_type = get_byte(ea+2)
      if secret_type not in tables.kSecretNames:
        _report('secret table', 'room %d has unknown secret type $%02x; skipped' % (
          room_index, secret_type))
        ea += 3
        continue
      x, y = pos//2%64, pos//2//64 
      xs.append([x, y, tables.kSecretNames[secret_type]])
      ea += 3
    return xs

  def get_chests():
    r = []
    for data, big in get_chest_info().get(room_index, []):
      if big:
        r.append('%d!' % data)
      else:
        r.append(data)
    return r
      
  sprites = get_sprites()
  secrets = get_secrets()

  data = {'Header' : header, 'Sprites' : sprites, 'Secrets' : secrets, 'Chests' : get_chests()}
  data['Entrances'] = get_entrance_info(0).get(room_index, [])
  if room_index in get_entrance_info(1):
    data['StartingPoints'] = get_entrance_info(1)[room_index]

  p = room_addr + 2
  p, objs, doors = decode_room_objects(p)
  data['Layer1'] = objs
  if doors: data['Layer1.doors'] = doors

  p, objs, doors = decode_room_objects(p)
  data['Layer2'] = objs
  if doors: data['Layer2.doors'] = doors

  p, objs, doors = decode_room_objects(p)
  data['Layer3'] = objs
  if doors: data['Layer3.doors'] = doors
  
  return yaml.dump(data, default_flow_style=None, sort_keys=False)

def print_all_dungeon_rooms():
  for i in range(320):
    s = print_room(i)
    open( 'dungeon/dungeon-%d.yaml' % i, 'w').write(s)
  report_path = os.environ.get('ZELDA3_DUNGEON_REPORT')
  if report_path:
    report = {
      'tables': {
        'room_objects': '0x%06x' % dungeon_room_object_table(),
        'headers': '0x%06x' % dungeon_header_table()[0],
        'header_count': dungeon_header_table()[1],
        'sprites': '0x%06x' % dungeon_sprite_table(),
        'secrets': '0x%06x' % dungeon_secret_table(),
        'chests': '0x%06x' % dungeon_chest_table(),
        'tele_msg': '0x%06x' % dungeon_telemsg_table(),
        'pits': '0x%06x' % dungeon_pits_table(),
        'default_rooms': '0x%06x' % dungeon_default_table(),
        'overlay_rooms': '0x%06x' % dungeon_overlay_table(),
      },
      'sanitized': SANITIZE_REPORT,
    }
    with open(report_path, 'w', encoding='utf8') as f:
      json.dump(report, f, indent=2)
      f.write('\n')

def print_default_rooms():
  def print_default_room(idx):
    p = dungeon_default_table() + idx * 3
    room_addr = get_byte(p) | get_byte(p+1) << 8 | get_byte(p+2) << 16
    p, objs, doors = decode_room_objects(room_addr)
    assert doors == None
    return objs
  default_rooms = {}
  for i in range(8):
    default_rooms['Default%d' % i] = print_default_room(i)
  s = yaml.dump(default_rooms, default_flow_style=None, sort_keys=False)
  open('dungeon/default_rooms.yaml', 'w').write(s)

def print_overlay_rooms():
  def print_overlay_room(idx):
    p = dungeon_overlay_table() + idx * 3
    room_addr = get_byte(p) | get_byte(p+1) << 8 | get_byte(p+2) << 16
    p, objs, doors = decode_room_objects(room_addr)
    assert doors == None
    return objs
  default_rooms = {}
  for i in range(19):
    default_rooms['Overlay%d' % i] = print_overlay_room(i)
  s = yaml.dump(default_rooms, default_flow_style=None, sort_keys=False)
  open('dungeon/overlay_rooms.yaml', 'w').write(s)

def make_directories():
  os.makedirs('img', exist_ok=True)
  os.makedirs('overworld', exist_ok=True)
  os.makedirs('dungeon', exist_ok=True)
  os.makedirs('sound', exist_ok=True)

def print_all_text_stuff():
  print_all_overworld_areas()
  print_all_dungeon_rooms()
  print_overlay_rooms()
  print_default_rooms()
  print_dialogue()
  print_map32_to_map16(open('map32_to_map16.txt', 'w'))

def main():
  make_directories()
  print_all_text_stuff()
  extract_music.extract_sound_data(util.ROM)
  sprite_sheets.decode_link_sprites()
  sprite_sheets.decode_sprite_sheets()
  sprite_sheets.decode_hud_icons()
  sprite_sheets.decode_font()
 
if __name__ == "__main__":
  util.load_rom(sys.argv[1] if len(sys.argv) >= 2 else None)
  main()

