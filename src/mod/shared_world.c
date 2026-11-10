#include "shared_world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../snes/ppu.h"
#include "../ancilla.h"
#include "../config.h"
#include "../misc.h"
#include "../nmi.h"
#include "../player.h"
#include "../player_oam.h"
#include "../sprite.h"
#include "../tile_detect.h"
#include "../variables.h"
#include "../zelda_rtl.h"
#include "events.h"
#include "frame_desc.h"
#include "local_coop.h"

// Model B first slice. Player 1 keeps the normal game loop; player 2 is a
// copy of the Link-owned RAM regions that is swapped into g_ram for one
// Link_Main()/Ancilla_Main()/LinkOam_Main() pass per frame. World state
// (room, sprites, camera, progress) is shared and only stepped once.
//
// This table is the answer to "what belongs to a Link". It deliberately
// leaves out the camera/room/tilemap blocks, the sprite arrays, the SRAM
// progress block, the HUD and the module/submodule state. See
// docs/engine/COOP-MODELO-B.md for the rationale and the known gaps.

typedef struct SharedRegion {
  uint16 offset;
  uint16 len;
} SharedRegion;

static const SharedRegion kPlayerRegions[] = {
  { 0x0020, 0x004F },  // position, direction, velocity, collision, animation
  { 0x0071, 0x0009 },  // tile interaction scratch, spin attack
  { 0x00A9, 0x0002 },  // link quadrant x/y
  { 0x00EE, 0x0002 },  // lower level / room transition bits used by Link
  { 0x00F0, 0x000C },  // joypad history and filtered edges (per controller)
  { 0x0100, 0x000A },  // Link DMA graphics selection
  { 0x0114, 0x0002 },  // tile below
  { 0x0280, 0x0032 },  // ancilla objprio/U/Z arrays
  { 0x02C6, 0x0033 },  // recoil, swimming, receive-item, bunny, sword timers
  { 0x0300, 0x0080 },  // item in hand, state bits, spin dash, cape, hooks
  { 0x0380, 0x0044 },  // ancilla K/L/A/B/G, boomerang/hookshot scratch
  { 0x03C4, 0x0039 },  // ancilla H/T/R/floor/attr, flute, sword-up
  { 0x0728, 0x0033 },  // ancilla door debris/22/25/26
  { 0x0AC0, 0x001C },  // Link CHR DMA source addresses
  { 0x0BF0, 0x00AA },  // ancilla position, velocity, type, step, timer, OAM
  { 0x0FC2, 0x0004 },  // previous x/y (transitions, recoil)
  { 0xF36D, 0x0002 },  // health current, magic power (each player has own)
};

// Variables that are world control but can be scribbled by Link_Main while
// player 2 is active. They are restored after player 2's pass so that only
// player 1 owns room changes, menus and camera transitions. A player-2 death
// (main_module_index 18) is the one exception: the game-over state is world.
static const SharedRegion kControlRegions[] = {
  { 0x0010, 0x0002 },  // main_module_index, submodule_index
  { 0x001B, 0x0001 },  // player_is_indoors
  { 0x008A, 0x0002 },  // overworld_screen_index
  { 0x00A0, 0x0005 },  // dungeon_room_index/prev, current floor
  { 0x00A6, 0x0003 },  // room quadrant layout/size
  { 0x00B0, 0x0001 },  // subsubmodule_index
  { 0x0600, 0x0010 },  // room bounds / overworld scroll limits
  { 0x00EF, 0x0001 },  // room_transitioning_flags
  { 0x010C, 0x0001 },  // saved_module_for_menu
  { 0x0126, 0x0001 },  // transition_counter
  { 0x0200, 0x0001 },  // overworld_map_state
  { 0x040A, 0x0002 },  // overworld_area_index
  { 0x0410, 0x0002 },  // overworld_screen_trans_dir_bits
  { 0x0416, 0x0002 },  // overworld_screen_trans_dir_bits2
  { 0x0418, 0x0001 },  // overworld_screen_transition
  { 0x048E, 0x0002 },  // dungeon_room_index2
  { 0x04C6, 0x0001 },  // trigger_special_entrance
  { 0x0696, 0x0002 },  // ow_entrance_value
  { 0x069A, 0x0001 },  // ow_countdown_transition
  { 0x0700, 0x0002 },  // current_area_of_player
};

// Player 2's Link OAM is built in the secondary window used by the game when
// sort_sprites_setting is 1. It is never written to VRAM: its entries are
// copied to free OAM slots after player 1's own OAM is restored.
enum {
  kP2OamBase = 0xe0 >> 2,
  kLinkOamSlots = 28,
  kGfx4bppSize = 0x3000,
  kRespawnDelay = 120,
  kRespawnInvulnerability = 60,
  kControlRegionSize = 64,
};

// Player 2's HUD is drawn into cells that are blank in both the vanilla and
// the Redux HUD: rows 3..4, columns 13..24 for the hearts (12 per row, enough
// for the 20-heart maximum) and row 4, columns 27..30 for a four-segment
// magic meter. Columns 25..26 of rows 3..4 belong to the dungeon floor
// indicator, so they are left alone. The hearts use palette 0 (the HUD's
// non-red palette) to tell them apart from player 1's red hearts.
enum {
  kP2HudHeartX = 13,
  kP2HudHeartRowWidth = 12,
  kP2HudHeartRows = 2,
  kP2HudMagicX = 27,
  kP2HudMagicSegments = 4,
  kP2HeartEmpty = 0x20A2,
  kP2HeartHalf = 0x20A1,
  kP2HeartFull = 0x20A0,
  kP2MagicEmpty = 0x3C4F,
  kP2MagicFull = 0x3C5E,
};

#define HUDXY(x, y) ((x) + (y) * 32)

// Automatic split-screen. The shared camera is the midpoint of the two Links,
// clamped to the room/area scroll limits. The two bodies have to fit in one
// 256x224 view with a small safety margin; a wider margin is required to merge
// again (hysteresis). Dungeon thresholds are a little tighter than overworld
// ones because the camera stops at walls and rooms are small. See
// docs/engine/COOP-MODELO-B.md for the exact numbers and rationale.
enum {
  kSplitMarginX = 8,
  kSplitMarginY = 8,
  kMergeMarginX = 40,
  kMergeMarginY = 32,
  kLinkBodyWidth = 16,
  kLinkBodyHeight = 24,
};

static bool DebugLogging(void) {
  static int enabled = -1;
  if (enabled < 0)
    enabled = getenv("ZELDA3_SHARED_WORLD_DEBUG") != NULL;
  return enabled != 0;
}

static uint8 *s_player[2];
static uint8 *s_gfx[2];
static bool s_started;
static bool s_p2_ready;
static bool s_split;
static bool s_dead[2];
static uint16 s_respawn_timer[2];
static int s_live_player;
static int s_active_player;
static uint16 s_input2;
static bool s_follow_p2;
static uint32 s_last_room;
static bool s_last_room_valid;
static bool s_dump_active;
static long s_debug_frame;

// Player 2 hits are applied by Sprite_CheckDamageFromLink() as pending
// damage; the sprite's own step subtracts the health on the next world frame.
// Remember which slots player 2 hit so the real health drop can be reported as
// a Player2SpriteDamage event instead of guessing on every sprite.
static bool s_p2_hit_pending[16];
static uint8 s_p2_hit_health[16];
static uint8 s_p2_hit_type[16];
static uint8 s_p2_hit_wait[16];

static void FinishPlayer2Leader(void);

static uint16 ReadU16(const uint8 *buf, uint16 offset) {
  return (uint16)(buf[offset] | (uint16)buf[offset + 1] << 8);
}

static uint32 HashBytes(const uint8 *p, int n) {
  uint32 h = 2166136261u;
  while (n--)
    h = (h ^ *p++) * 16777619u;
  return h;
}

static void WriteU16(uint8 *buf, uint16 offset, uint16 value) {
  buf[offset] = (uint8)value;
  buf[offset + 1] = (uint8)(value >> 8);
}

static void CopyRegions(uint8 *dst, const uint8 *src, const SharedRegion *regions,
                        int count) {
  for (int i = 0; i < count; i++)
    memcpy(dst + regions[i].offset, src + regions[i].offset, regions[i].len);
}

static void CopyPlayer(uint8 *dst, const uint8 *src) {
  CopyRegions(dst, src, kPlayerRegions, (int)countof(kPlayerRegions));
}

static void SaveControl(uint8 *dst) {
  for (int i = 0; i < (int)countof(kControlRegions); i++) {
    memcpy(dst, g_ram + kControlRegions[i].offset, kControlRegions[i].len);
    dst += kControlRegions[i].len;
  }
}

static void RestoreControl(const uint8 *src) {
  for (int i = 0; i < (int)countof(kControlRegions); i++) {
    memcpy(g_ram + kControlRegions[i].offset, src, kControlRegions[i].len);
    src += kControlRegions[i].len;
  }
}

bool SharedWorld_IsEnabled(void) {
  return g_config.coop_shared_world;
}

bool SharedWorld_IsActive(void) {
  return s_started && g_config.coop_shared_world && !LocalCoop_IsActive();
}

void SharedWorld_Start(void) {
  if (s_started || !g_config.coop_shared_world)
    return;
  for (int i = 0; i < 2; i++) {
    s_player[i] = (uint8 *)calloc(0x20000, 1);
    s_gfx[i] = (uint8 *)calloc(kGfx4bppSize, 1);
    if (s_player[i] == NULL || s_gfx[i] == NULL) {
      fprintf(stderr, "shared world: out of memory for player %d\n", i + 1);
      for (int j = 0; j <= i; j++) {
        free(s_player[j]);
        free(s_gfx[j]);
        s_player[j] = NULL;
        s_gfx[j] = NULL;
      }
      return;
    }
  }
  s_started = true;
  s_p2_ready = false;
  s_split = false;
  s_dead[0] = s_dead[1] = false;
  s_respawn_timer[0] = s_respawn_timer[1] = 0;
  s_live_player = 0;
  s_active_player = 0;
  memset(s_p2_hit_pending, 0, sizeof(s_p2_hit_pending));
  memset(s_p2_hit_wait, 0, sizeof(s_p2_hit_wait));
  fprintf(stderr, "shared world: two Links, one world\n");
}

void SharedWorld_Stop(void) {
  if (g_zenv.ppu != NULL)
    memset(g_zenv.ppu->oamIsPlayer2, 0, sizeof(g_zenv.ppu->oamIsPlayer2));
  for (int i = 0; i < 2; i++) {
    free(s_player[i]);
    free(s_gfx[i]);
    s_player[i] = NULL;
    s_gfx[i] = NULL;
  }
  s_started = false;
  s_p2_ready = false;
  s_split = false;
  s_dead[0] = s_dead[1] = false;
  s_respawn_timer[0] = s_respawn_timer[1] = 0;
  s_live_player = 0;
  s_dump_active = false;
  s_active_player = 0;
  memset(s_p2_hit_pending, 0, sizeof(s_p2_hit_pending));
  memset(s_p2_hit_wait, 0, sizeof(s_p2_hit_wait));
}

void SharedWorld_SetInput2(uint16 input) {
  s_input2 = input;
}

uint16 SharedWorld_GetInput2(void) {
  return s_input2;
}

void SharedWorld_ResetFromCurrent(void) {
  s_p2_ready = false;
  s_split = false;
  s_dead[0] = s_dead[1] = false;
  s_respawn_timer[0] = s_respawn_timer[1] = 0;
  s_live_player = 0;
  s_follow_p2 = false;
  s_last_room_valid = false;
  memset(s_p2_hit_pending, 0, sizeof(s_p2_hit_pending));
  memset(s_p2_hit_wait, 0, sizeof(s_p2_hit_wait));
}

static bool IsNormalGameplay(void) {
  return animated_tile_data_src != 0 &&
         (main_module_index == 7 || main_module_index == 9) &&
         submodule_index == 0;
}

static uint32 CurrentRoomId(void) {
  if (player_is_indoors)
    return 0x10000u | dungeon_room_index | ((uint32)dung_cur_floor << 24) |
           ((uint32)BYTE(cur_palace_index_x2) << 16);
  return overworld_screen_index | ((uint32)(savegame_is_darkworld & 0x40) << 24);
}

static int16_t S16(uint16 v) {
  return (int16_t)v;
}

static int ClampInt(int v, int lo, int hi) {
  if (lo > hi) {
    int t = lo;
    lo = hi;
    hi = t;
  }
  return v < lo ? lo : v > hi ? hi : v;
}

// Camera scroll limits for the current room/area. In dungeons these are the
// room's quadrant bounds; outdoors they are the current area's scroll range.
static void CameraBounds(int *x0, int *x1, int *y0, int *y1) {
  if (player_is_indoors) {
    int qx = quadrant_fullsize_x >> 1;
    int qy = quadrant_fullsize_y >> 1;
    *x0 = S16(room_bounds_x.v[qx]);
    *x1 = S16(room_bounds_x.v[qx + 2]);
    *y0 = S16(room_bounds_y.v[qy]);
    *y1 = S16(room_bounds_y.v[qy + 2]);
  } else {
    *x0 = S16(ow_scroll_vars0.xstart);
    *x1 = S16(ow_scroll_vars0.xend);
    *y0 = S16(ow_scroll_vars0.ystart);
    *y1 = S16(ow_scroll_vars0.yend);
  }
}

// Camera position that puts the midpoint of the two Links at the centre of
// the view, clamped to the room/area limits.
static void MidpointCamera(int x1, int y1, int x2, int y2, int *out_x,
                           int *out_y) {
  int cx, cy, x0, x1b, y0, y1b;
  CameraBounds(&x0, &x1b, &y0, &y1b);
  cx = ClampInt((x1 + x2) / 2 - 120, x0, x1b);
  cy = ClampInt((y1 + y2) / 2 - 100, y0, y1b);
  *out_x = cx;
  *out_y = cy;
}

// Camera for one pane while split: that Link centred, still clamped to the
// room/area limits. Both panes can show the same scroll in a 256px-wide room.
static void PaneCamera(int player, int *out_x, int *out_y) {
  int x, y, cx, cy, x0, x1, y0, y1;
  if (player == 0) {
    x = S16(link_x_coord);
    y = S16(link_y_coord);
  } else {
    x = S16(ReadU16(s_player[1], 0x22));
    y = S16(ReadU16(s_player[1], 0x20));
  }
  CameraBounds(&x0, &x1, &y0, &y1);
  cx = ClampInt(x - 120, x0, x1);
  cy = ClampInt(y - 100, y0, y1);
  *out_x = cx;
  *out_y = cy;
}

static void ApplyCameraDelta(int dx, int dy) {
  camera_x_coord_scroll_low = (uint16)(camera_x_coord_scroll_low + dx);
  camera_x_coord_scroll_hi = (uint16)(camera_x_coord_scroll_hi + dx);
  camera_y_coord_scroll_low = (uint16)(camera_y_coord_scroll_low + dy);
  camera_y_coord_scroll_hi = (uint16)(camera_y_coord_scroll_hi + dy);
  BG2HOFS_copy2 = (uint16)(BG2HOFS_copy2 + dx);
  BG2HOFS_copy = (uint16)(BG2HOFS_copy + dx);
  BG1HOFS_copy2 = (uint16)(BG1HOFS_copy2 + dx);
  BG1HOFS_copy = (uint16)(BG1HOFS_copy + dx);
  BG2VOFS_copy2 = (uint16)(BG2VOFS_copy2 + dy);
  BG2VOFS_copy = (uint16)(BG2VOFS_copy + dy);
  BG1VOFS_copy2 = (uint16)(BG1VOFS_copy2 + dy);
  BG1VOFS_copy = (uint16)(BG1VOFS_copy + dy);
}

void SharedWorld_PreFrame(void) {
  if (!SharedWorld_IsActive())
    return;
  // Player 2 may be the live continuation of a room/area transition. Once
  // normal gameplay resumes, switch the world back to player 1 and put P1
  // next to P2 in the new room before the world step.
  if (s_live_player == 1 && IsNormalGameplay())
    FinishPlayer2Leader();
  // Player 2's CHR marks belong to one frame only. Clear them here (before
  // the world step rewrites OAM) so menus and transitions cannot inherit a
  // stale mark on a slot they reuse.
  memset(g_zenv.ppu->oamIsPlayer2, 0, sizeof(g_zenv.ppu->oamIsPlayer2));
  if (!s_p2_ready || s_split || !IsNormalGameplay())
    return;

  int cx, cy;
  if (s_dead[0] && !s_dead[1]) {
    // Player 1 is down: keep the camera on player 2 until the respawn.
    PaneCamera(1, &cx, &cy);
  } else if (s_dead[1]) {
    // Player 1 owns the normal camera while player 2 is down.
    return;
  } else {
    int x1 = S16(link_x_coord), y1 = S16(link_y_coord);
    int x2 = S16(ReadU16(s_player[1], 0x22));
    int y2 = S16(ReadU16(s_player[1], 0x20));
    MidpointCamera(x1, y1, x2, y2, &cx, &cy);
  }
  int dx = cx - S16(BG2HOFS_copy2);
  int dy = cy - S16(BG2VOFS_copy2);
  if (dx != 0 || dy != 0)
    ApplyCameraDelta(dx, dy);
}

static bool InsideRect(int x, int y, int margin_x, int margin_y) {
  return x >= margin_x && x + kLinkBodyWidth <= 256 - margin_x &&
         y >= margin_y && y + kLinkBodyHeight <= 224 - margin_y;
}

static void UpdateSplitState(void) {
  if (!s_p2_ready || s_dead[0] || s_dead[1]) {
    if (s_split) {
      s_split = false;
      MOD_EMIT(kModEv_CameraSplit, 0, 0, 0);
    }
    return;
  }
  int x1 = S16(link_x_coord), y1 = S16(link_y_coord);
  int x2 = S16(ReadU16(s_player[1], 0x22));
  int y2 = S16(ReadU16(s_player[1], 0x20));
  int dx = x2 > x1 ? x2 - x1 : x1 - x2;
  int dy = y2 > y1 ? y2 - y1 : y1 - y2;
  bool dungeon = player_is_indoors != 0;
  int split_x = dungeon ? 192 : 208;
  int split_y = dungeon ? 144 : 152;
  int merge_x = dungeon ? 128 : 144;
  int merge_y = dungeon ? 96 : 104;

  int cx, cy;
  MidpointCamera(x1, y1, x2, y2, &cx, &cy);
  int sx1 = x1 - cx, sy1 = y1 - cy;
  int sx2 = x2 - cx, sy2 = y2 - cy;
  bool fits = InsideRect(sx1, sy1, kSplitMarginX, kSplitMarginY) &&
              InsideRect(sx2, sy2, kSplitMarginX, kSplitMarginY);
  bool merged_fits = InsideRect(sx1, sy1, kMergeMarginX, kMergeMarginY) &&
                     InsideRect(sx2, sy2, kMergeMarginX, kMergeMarginY);

  // If the camera cannot move on either axis (a room that fits in one
  // screen), two panes would show the same view and help nobody.
  int bx0, bx1, by0, by1;
  CameraBounds(&bx0, &bx1, &by0, &by1);
  bool can_split = bx0 != bx1 || by0 != by1;

  bool want_split;
  if (!can_split)
    want_split = false;
  else if (!s_split)
    want_split = dx > split_x || dy > split_y || !fits;
  else
    want_split = !(dx < merge_x && dy < merge_y && merged_fits);

  if (want_split != s_split) {
    s_split = want_split;
    MOD_EMIT(kModEv_CameraSplit, want_split ? 1 : 0, dx, dy);
    if (DebugLogging())
      fprintf(stderr, "shared world: frame %ld %s (dx=%d dy=%d %s)\n",
              s_debug_frame, want_split ? "split on" : "split off", dx, dy,
              dungeon ? "dungeon" : "overworld");
  }
}

bool SharedWorld_IsSplit(void) {
  // Menus, transitions and cutscenes always use the single normal view; the
  // split is only meaningful while the shared world is stepping.
  return SharedWorld_IsActive() && s_split && IsNormalGameplay();
}

int SharedWorld_OutputWidth(int pane_width) {
  if (SharedWorld_IsSplit())
    return pane_width * 2;
  return pane_width;
}

// Put a Link next to the other one. Used when player 2 is first seeded, when
// player 1 changes room/area (player 2 follows), after a leader transition and
// when a dead Link respawns. Only the body/action state is reset; health and
// magic live in the player regions and are preserved.
static bool XFitsCurrentRoom(uint16 x) {
  if (!player_is_indoors)
    return true;
  int qx = quadrant_fullsize_x >> 1;
  int x0 = S16(room_bounds_x.v[qx]);
  int x1 = S16(room_bounds_x.v[qx + 2]);
  return x + 16 > x0 && x < x1;
}

static uint8 PlacementTileAt(uint16 x, uint16 y, uint8 floor) {
  if (player_is_indoors) {
    int t = (floor >= 1 ? 0x1000 : 0) + ((x & 0x1f8) >> 3) +
            ((y & 0x1f8) << 3);
    return dung_bg2_attr_table[t];
  }
  return Overworld_GetTileAttributeAtLocation(x >> 3, y);
}

static bool PlacementTileSolid(uint16 x, uint16 y, uint8 floor) {
  uint8 tile = PlacementTileAt(x, y, floor);
  switch (tile) {
  case 0x01: case 0x02: case 0x03:
  case 0x26: case 0x27: case 0x43: case 0x44: case 0x46: case 0x57:
    return true;
  default:
    break;
  }
  if (player_is_indoors)
    return tile == 0x04 || tile == 0x0b || (tile >= 0x6c && tile <= 0x6f);
  return false;
}

static bool PlacementCandidateOk(uint16 x, uint16 y, uint8 floor) {
  return XFitsCurrentRoom(x) &&
         !PlacementTileSolid(x + 8, y + 12, floor);
}

static void PlaceLinkNear(uint8 *dst, const uint8 *other, bool other_is_live) {
  uint16 x = other_is_live ? link_x_coord : ReadU16(other, 0x22);
  uint16 y = other_is_live ? link_y_coord : ReadU16(other, 0x20);
  uint8 dir_last = other_is_live ? link_direction_last : other[0x26];
  uint8 dir_facing = other_is_live ? link_direction_facing : other[0x2F];
  uint8 dir = other_is_live ? link_direction : other[0x67];
  uint8 floor = other_is_live ? link_is_on_lower_level : other[0xEE];
  uint8 qx = other_is_live ? link_quadrant_x : other[0xA9];
  uint8 qy = other_is_live ? link_quadrant_y : other[0xAA];
  uint16 lx = x & 0x1ff;
  uint16 right = lx > 0x1d0 ? (uint16)(lx - 24) : (uint16)(lx + 24);
  uint16 left = lx < 24 ? (uint16)(lx + 24) : (uint16)(lx - 24);
  uint16 candidate = (uint16)((x & ~0x1ff) | right);
  if (!PlacementCandidateOk(candidate, y, floor))
    candidate = (uint16)((x & ~0x1ff) | left);
  if (!PlacementCandidateOk(candidate, y, floor))
    candidate = x;

  uint8 *d = dst;
  WriteU16(d, 0x22, candidate);
  WriteU16(d, 0x20, y);
  WriteU16(d, 0x24, 0);  // z
  d[0x26] = dir_last;
  d[0x27] = d[0x28] = d[0x29] = 0;
  d[0x2A] = d[0x2B] = d[0x2C] = 0;
  d[0x30] = d[0x31] = 0;
  d[0x2F] = dir_facing;
  d[0x67] = dir;
  d[0x5D] = kPlayerState_Ground;
  d[0x46] = 0;
  d[0x4B] = 0;
  d[0x4D] = 0;
  d[0x3C] = 0;
  d[0x2E4] = 0;  // flag_is_link_immobilized
  d[0x301] = 0;
  d[0x304] = 0;
  d[0x308] = 0;
  d[0x31F] = 0;
  d[0x373] = 0;
  d[0x37A] = 0;
  d[0x37B] = 0;  // link_disable_sprite_damage
  d[0x364] = 0;
  d[0x365] = 0;
  d[0x362] = 0;
  d[0x363] = 0;
  d[0xEE] = floor;
  d[0xA9] = qx;
  d[0xAA] = qy;
  WriteU16(d, 0xFC2, ReadU16(d, 0x22));
  WriteU16(d, 0xFC4, ReadU16(d, 0x20));
}

// Seed player 2's external copy from the live player 1 state. This works on
// the slot directly; it does not disturb the live (player 1) Link.
static void SeedPlayer2(void) {
  CopyPlayer(s_player[1], g_ram);
  PlaceLinkNear(s_player[1], g_ram, true);
  memcpy(s_gfx[1], gfx_4bpp_buffer, kGfx4bppSize);
}

static uint16 OtherLinkX(void) {
  const uint8 *other = s_player[s_active_player == 0 ? 1 : 0];
  return ReadU16(other, 0x22);
}

static uint16 OtherLinkY(void) {
  const uint8 *other = s_player[s_active_player == 0 ? 1 : 0];
  return ReadU16(other, 0x20);
}

static uint32 DistSq(uint16 ax, uint16 ay, uint16 bx, uint16 by) {
  int dx = (int)ax - (int)bx;
  int dy = (int)ay - (int)by;
  return (uint32)(dx * dx + dy * dy);
}

bool SharedWorld_OtherLinkIsTarget(uint16 from_x, uint16 from_y) {
  if (!SharedWorld_IsActive() || !s_p2_ready)
    return false;
  // During a P2-led transition player 1's copy is still in the old room;
  // sprites must keep aiming at the live Link (P2) until the transition ends.
  if (s_live_player == 1)
    return false;
  int live = s_active_player;
  int other = live == 0 ? 1 : 0;
  if (s_dead[other])
    return false;
  if (s_dead[live])
    return true;
  uint16 ox = OtherLinkX(), oy = OtherLinkY();
  uint32 d_other = DistSq(from_x, from_y, ox, oy);
  uint32 d_live = DistSq(from_x, from_y, link_x_coord, link_y_coord);
  return d_other < d_live;
}

uint16 SharedWorld_OtherLinkX(void) {
  return OtherLinkX();
}

uint16 SharedWorld_OtherLinkY(void) {
  return OtherLinkY();
}

void SharedWorld_LinkPosFor(uint16 from_x, uint16 from_y, uint16 *x,
                            uint16 *y) {
  if (SharedWorld_OtherLinkIsTarget(from_x, from_y)) {
    *x = OtherLinkX();
    *y = OtherLinkY();
  } else {
    *x = link_x_coord;
    *y = link_y_coord;
  }
}

// Player 2 may lead room/area transitions, stairs and pits. Menus, game over,
// cutscenes and endings stay with player 1.
static bool IsTransitionControlAllowed(void) {
  switch (main_module_index) {
  case 5:   // load file (entering/exiting a save)
  case 7:   // dungeon player control / room transition
  case 8:   // overworld load
  case 9:   // overworld player control / screen transition
  case 11:  // falling entrance
  case 15:  // mirror warp from Agahnim
  case 17:  // pit/wallmaster return to entrance
  case 25:  // exiting a dungeon
    return true;
  default:
    return false;
  }
}

static void MarkLinkDead(uint8 *d) {
  d[0x27] = d[0x28] = d[0x29] = 0;
  d[0x2A] = d[0x2B] = d[0x2C] = 0;
  d[0x30] = d[0x31] = 0;
  d[0x46] = 0;
  d[0x4B] = 12;  // link_visibility_status: body and shadow hidden
  d[0x4D] = 0;
  d[0x2E4] = 1;  // flag_is_link_immobilized
  d[0x301] = 0;
  d[0x304] = 0;
  d[0x308] = 0;
  d[0x31F] = 0;
  d[0x373] = 0;
  d[0x37A] = 0;
  d[0x37B] = 1;  // link_disable_sprite_damage
  d[0xF36D] = 0;
}

static uint8 RespawnHealth(void) {
  int level = g_ram[0xF36C] >> 3;
  if (level < 0 || level > 20)
    level = 20;
  return kHealthAfterDeath[level];
}

static void RespawnPlayerNearOther(int player) {
  uint8 *d = player == 0 ? g_ram : s_player[1];
  const uint8 *other = player == 0 ? s_player[1] : g_ram;
  bool other_is_live = player != 0;
  PlaceLinkNear(d, other, other_is_live);
  d[0x37B] = kRespawnInvulnerability;
  d[0xF36D] = RespawnHealth();
  s_dead[player] = false;
  s_respawn_timer[player] = 0;
  if (player != 0)
    memcpy(s_gfx[1], gfx_4bpp_buffer, kGfx4bppSize);
  MOD_EMIT(kModEv_PlayerRespawn, player + 1, d[0xF36D], 0);
  if (DebugLogging())
    fprintf(stderr, "shared world: frame %ld player %d respawns at %d,%d\n",
            s_debug_frame, player + 1, ReadU16(d, 0x22), ReadU16(d, 0x20));
}

static void TickRespawnTimers(void) {
  for (int i = 0; i < 2; i++)
    if (s_dead[i] && s_respawn_timer[i] > 0)
      s_respawn_timer[i]--;
}

static void UpdateRespawns(void) {
  if (s_dead[0] && s_respawn_timer[0] == 0 && s_p2_ready && !s_dead[1])
    RespawnPlayerNearOther(0);
  if (s_dead[1] && s_respawn_timer[1] == 0 && !s_dead[0])
    RespawnPlayerNearOther(1);
}

static void PromotePlayer2Leader(void) {
  s_live_player = 1;
  s_active_player = 1;
  s_split = false;
  s_follow_p2 = false;
  s_last_room_valid = false;
  if (DebugLogging())
    fprintf(stderr, "shared world: frame %ld player 2 leads transition "
            "module %d sub %d\n", s_debug_frame, main_module_index,
            submodule_index);
}

// Called before a world step once player 2's transition finished. P2 becomes
// the external copy again and P1 is placed next to it in the new room.
static void FinishPlayer2Leader(void) {
  if (s_live_player != 1)
    return;
  uint32 room = CurrentRoomId();
  CopyPlayer(s_player[1], g_ram);
  memcpy(s_gfx[1], gfx_4bpp_buffer, kGfx4bppSize);
  memcpy(s_gfx[0], gfx_4bpp_buffer, kGfx4bppSize);
  if (!s_dead[0])
    PlaceLinkNear(s_player[0], s_player[1], false);
  CopyPlayer(g_ram, s_player[0]);
  s_live_player = 0;
  s_active_player = 0;
  s_last_room = room;
  s_last_room_valid = true;
  s_follow_p2 = false;
  s_split = false;
  if (DebugLogging())
    fprintf(stderr, "shared world: frame %ld player 2 transition done in "
            "room %08x\n", s_debug_frame, room);
}

static int FindFreeOamSlot(const uint8 *oam) {
  for (int j = 0; j < 128; j++)
    if (oam[j * 4 + 1] == 0xf0)
      return j;
  return -1;
}

static void SetOamExt(int slot, uint8 value) {
  value &= 3;
  bytewise_extended_oam[slot] = value;
  uint8 *e = &extended_oam[slot >> 2];
  int shift = (slot & 3) * 2;
  *e = (uint8)((*e & ~(3 << shift)) | (value << shift));
}

// Copy player 2's ancilla/effect OAM entries (they use the normal sprite CHR
// set) into free slots. OAM slots are compared against player 1's buffer, so
// stale entries left in the secondary Link window are ignored.
static void MergePlayer2EffectOam(const uint8 *oam_p1, const uint8 *ext_p1,
                                  const uint8 *oam_p2, const uint8 *ext_p2) {
  uint8 *oam = (uint8 *)oam_buf;
  for (int i = 0; i < 128; i++) {
    const uint8 *src = oam_p2 + i * 4;
    if (src[1] == 0xf0)
      continue;
    if (!memcmp(src, oam_p1 + i * 4, 4) && ext_p2[i] == ext_p1[i])
      continue;
    int dst = FindFreeOamSlot(oam);
    if (dst < 0)
      break;
    memcpy(oam + dst * 4, src, 4);
    SetOamExt(dst, ext_p2[i]);
  }
}

// Copy player 2's real Link OAM (body, sword, shield, shadow and item) from
// the secondary window into free slots. These entries are flagged so the PPU
// fetches their CHR from Ppu.linkChr (player 2's own animation) instead of the
// single VRAM Link set that player 1 owns.
static void MergePlayer2LinkOam(const uint8 *oam_p2, const uint8 *ext_p2) {
  uint8 *oam = (uint8 *)oam_buf;
  for (int i = kP2OamBase; i < kP2OamBase + kLinkOamSlots; i++) {
    const uint8 *src = oam_p2 + i * 4;
    if (src[1] == 0xf0)
      continue;
    int dst = FindFreeOamSlot(oam);
    if (dst < 0)
      break;
    memcpy(oam + dst * 4, src, 4);
    SetOamExt(dst, ext_p2[i]);
    g_zenv.ppu->oamIsPlayer2[dst] = 1;
  }
}

static void ShiftOam(Ppu *ppu, int dx, int dy) {
  if (dx == 0 && dy == 0)
    return;
  for (int i = 0; i < 128; i++) {
    uint16 *e = &ppu->oam[i * 2];
    int y = e[0] >> 8;
    if (y == 0xf0)
      continue;
    int shift = (i & 7) * 2;
    uint16 *hi = &ppu->oam[0x100 + (i >> 3)];
    int x = (e[0] & 0xff) | (((*hi >> shift) & 1) << 8);
    x = (x + dx) & 0x1ff;
    y = (y + dy) & 0xff;
    e[0] = (uint16)(y << 8 | (x & 0xff));
    *hi = (uint16)((*hi & ~(1u << shift)) | (((x >> 8) & 1) << shift));
  }
}

// Draw player 2's hearts and magic in the HUD strip. This runs after the
// world step (and after player 2's own pass) so it never changes what the
// vanilla HUD computes for player 1. It only touches the blank cells listed
// above and only while normal gameplay is running; menus, transitions and the
// shared-world-off case leave the original HUD untouched.
void SharedWorld_UpdateHud(void) {
  if (!SharedWorld_IsActive() || !s_p2_ready || s_live_player == 1 ||
      !IsNormalGameplay())
    return;

  uint8 *p2 = s_player[1];
  int capacity = link_health_capacity;
  int slots = (capacity + 7) >> 3;
  if (slots > kP2HudHeartRows * kP2HudHeartRowWidth)
    slots = kP2HudHeartRows * kP2HudHeartRowWidth;
  int filled = (p2[0xF36D] + 3) & ~3;

  for (int i = 0; i < slots; i++) {
    int x = kP2HudHeartX + (i < kP2HudHeartRowWidth
                                ? i
                                : i - kP2HudHeartRowWidth);
    int y = i < kP2HudHeartRowWidth ? 4 : 3;
    int health = filled - i * 8;
    uint16 tile = health >= 8 ? kP2HeartFull
                              : health > 0 ? kP2HeartHalf : kP2HeartEmpty;
    hud_tile_indices_buffer[HUDXY(x, y)] = tile;
  }

  int magic = (p2[0xF36E] + 31) >> 5;
  if (magic > kP2HudMagicSegments)
    magic = kP2HudMagicSegments;
  for (int i = 0; i < kP2HudMagicSegments; i++) {
    hud_tile_indices_buffer[HUDXY(kP2HudMagicX + i, 4)] =
        i < magic ? kP2MagicFull : kP2MagicEmpty;
  }
  flag_update_hud_in_nmi++;
}

void SharedWorld_DrawFrame(uint8 *buffer, size_t pitch, uint32 render_flags,
                           int pane_width) {
  Ppu *ppu = g_zenv.ppu;
  if (!SharedWorld_IsSplit()) {
    ZeldaDrawPpuFrame(buffer, pitch, render_flags);
    return;
  }

  BgLayer bg_save[4];
  int16_t m7_save[8];
  uint16 oam_save[0x110];
  uint8 scroll_prev = ppu->scrollPrev, scroll_prev2 = ppu->scrollPrev2;
  uint8 m7_prev = ppu->m7prev;
  int save_bg2x = BG2HOFS_copy2, save_bg2y = BG2VOFS_copy2;
  memcpy(bg_save, ppu->bgLayer, sizeof(bg_save));
  memcpy(m7_save, ppu->m7matrix, sizeof(m7_save));
  memcpy(oam_save, ppu->oam, sizeof(oam_save));

  int sim_x = S16(BG2HOFS_copy2), sim_y = S16(BG2VOFS_copy2);
  for (int pane = 0; pane < 2; pane++) {
    int cam_x, cam_y;
    PaneCamera(pane, &cam_x, &cam_y);
    int dx = sim_x - cam_x;
    int dy = sim_y - cam_y;

    memcpy(ppu->bgLayer, bg_save, sizeof(bg_save));
    ppu->bgLayer[0].hScroll = (uint16)(ppu->bgLayer[0].hScroll + dx);
    ppu->bgLayer[0].vScroll = (uint16)(ppu->bgLayer[0].vScroll + dy);
    ppu->bgLayer[1].hScroll = (uint16)(ppu->bgLayer[1].hScroll + dx);
    ppu->bgLayer[1].vScroll = (uint16)(ppu->bgLayer[1].vScroll + dy);
    ppu->scrollPrev = scroll_prev;
    ppu->scrollPrev2 = scroll_prev2;
    ppu->m7prev = m7_prev;
    memcpy(ppu->m7matrix, m7_save, sizeof(m7_save));
    if (ppu->mode == 7) {
      ppu->m7matrix[6] = (int16_t)(ppu->m7matrix[6] + dx);
      ppu->m7matrix[7] = (int16_t)(ppu->m7matrix[7] + dy);
    }

    memcpy(ppu->oam, oam_save, sizeof(oam_save));
    ShiftOam(ppu, dx, dy);

    // ConfigurePpuSideSpace() reads the live camera globals during rendering.
    BG2HOFS_copy2 = (uint16)cam_x;
    BG2VOFS_copy2 = (uint16)cam_y;
    if (FrameDesc_CaptureEnabled())
      FrameDesc_SetCapturePane(pane, 2);
    ZeldaDrawPpuFrame(buffer + (size_t)pane * pane_width * 4, pitch,
                      render_flags);
  }

  if (FrameDesc_CaptureEnabled())
    FrameDesc_SetCapturePane(0, 1);
  memcpy(ppu->bgLayer, bg_save, sizeof(bg_save));
  memcpy(ppu->m7matrix, m7_save, sizeof(m7_save));
  memcpy(ppu->oam, oam_save, sizeof(oam_save));
  ppu->scrollPrev = scroll_prev;
  ppu->scrollPrev2 = scroll_prev2;
  ppu->m7prev = m7_prev;
  BG2HOFS_copy2 = (uint16)save_bg2x;
  BG2VOFS_copy2 = (uint16)save_bg2y;
}

void SharedWorld_FrameStep(void) {
  if (!SharedWorld_IsActive())
    return;
  // Player 2 is the live Link while it leads a transition. PreFrame switches
  // back to player 1 as soon as normal gameplay resumes.
  if (s_live_player == 1) {
    if (!IsNormalGameplay())
      s_last_room_valid = false;
    return;
  }
  // The world step can enter the game-over module from normal gameplay or
  // from a transition (for example a pit landing). Intercept it before the
  // normal-gameplay check so P1 does not lose the game while P2 is alive.
  if (main_module_index == 18 && link_health_current == 0 && !s_dead[0]) {
    s_dead[0] = true;
    s_respawn_timer[0] = kRespawnDelay;
    MOD_EMIT(kModEv_PlayerDeath, 1, 0, 0);
    if (DebugLogging())
      fprintf(stderr, "shared world: frame %ld player 1 died\n", s_debug_frame);
    if (!s_p2_ready || s_dead[1])
      return;  // no living P2: let the original game over run
    uint8 restore = saved_module_for_menu;
    if (restore != 7 && restore != 8 && restore != 9)
      restore = player_is_indoors ? 7 : 9;
    main_module_index = restore;
    submodule_index = 0;
    subsubmodule_index = 0;
    countdown_for_blink = 0;
    link_hearts_filler = 0;
    MarkLinkDead(g_ram);
    if (!IsNormalGameplay()) {
      s_last_room_valid = false;
      return;
    }
  }
  if (!IsNormalGameplay()) {
    s_last_room_valid = false;
    return;
  }
  s_debug_frame++;
  TickRespawnTimers();

  uint32 room = CurrentRoomId();
  if (!s_p2_ready) {
    SeedPlayer2();
    s_p2_ready = true;
    s_follow_p2 = true;
    s_split = false;
    s_last_room = room;
    s_last_room_valid = true;
    s_active_player = 0;
  } else {
    if (!s_last_room_valid || room != s_last_room) {
      if (DebugLogging())
        fprintf(stderr, "shared world: frame %ld room change %08x -> %08x\n",
                s_debug_frame, s_last_room, room);
      if (!s_dead[1])
        s_follow_p2 = true;
      s_split = false;
    }
    s_last_room = room;
    s_last_room_valid = true;
  }

  UpdateRespawns();
  if (s_dead[0] && s_dead[1])
    return;

  // Player 2's hit from the previous frame is now real damage: the sprite's
  // own step subtracted the pending amount during the world step. Report the
  // actual health drop, with the type saved at hit time in case the slot was
  // reused before this check.
  for (int k = 0; k < 16; k++) {
    if (!s_p2_hit_pending[k])
      continue;
    if (sprite_health[k] < s_p2_hit_health[k]) {
      int damage = s_p2_hit_health[k] - sprite_health[k];
      s_p2_hit_pending[k] = false;
      MOD_EMIT(kModEv_Player2SpriteDamage, k, s_p2_hit_type[k], damage);
      if (DebugLogging())
        fprintf(stderr, "shared world: frame %ld player 2 damaged sprite %d "
                "type %02x hp %d -> %d\n", s_debug_frame, k, s_p2_hit_type[k],
                s_p2_hit_health[k], sprite_health[k]);
    } else if (--s_p2_hit_wait[k] == 0) {
      // The hit was rejected or the sprite never applied the pending damage;
      // forget it instead of attributing a later health change to player 2.
      s_p2_hit_pending[k] = false;
    }
  }

  memset(g_zenv.ppu->oamIsPlayer2, 0, sizeof(g_zenv.ppu->oamIsPlayer2));

  // Save player 1 out of the live RAM. Its health/magic and the dead state
  // stay in the copy while player 2 is swapped in.
  CopyPlayer(s_player[0], g_ram);

  if (s_dead[1]) {
    s_active_player = 0;
    UpdateSplitState();
    return;
  }

  // Keep one 4bpp buffer per player. Player 2 starts from the current world
  // graphics plus the item-hand slices from its own buffer, so the item it
  // holds does not overwrite player 1's item in the NMI upload.
  memcpy(s_gfx[0], gfx_4bpp_buffer, kGfx4bppSize);
  memcpy(gfx_4bpp_buffer, s_gfx[0], kGfx4bppSize);
  memcpy(gfx_4bpp_buffer + 0x2d40, s_gfx[1] + 0x2d40, 0x80);

  // Swap player 2 into the live RAM.
  CopyPlayer(g_ram, s_player[1]);
  s_active_player = 1;

  // Read controller 2 with the same NMI edge detector the game uses. This
  // updates joypad1*_last and filtered_joypad_* inside player 2's region.
  NMI_ReadJoypads(s_input2);

  if (s_follow_p2) {
    PlaceLinkNear(g_ram, s_player[0], false);
    s_follow_p2 = false;
  }

  uint8 control[kControlRegionSize];
  SaveControl(control);
  uint8 old_main_module = main_module_index;
  uint8 old_submodule = submodule_index;
  uint8 old_indoors = player_is_indoors;

  if (link_give_damage && DebugLogging())
    fprintf(stderr, "shared world: frame %ld player 2 takes %d damage\n",
            s_debug_frame, link_give_damage);

  uint8 oam_p1[0x200], ext_p1[128];
  memcpy(oam_p1, oam_buf, sizeof(oam_p1));
  memcpy(ext_p1, bytewise_extended_oam, sizeof(ext_p1));

  Link_Main();
  Ancilla_Main();

  // Effects and projectiles produced by player 2's pass. They use the normal
  // sprite CHR, so their OAM entries can be merged as-is.
  uint8 oam_eff[0x200], ext_eff[128];
  memcpy(oam_eff, oam_buf, sizeof(oam_eff));
  memcpy(ext_eff, bytewise_extended_oam, sizeof(ext_eff));

  // Build player 2's Link OAM in the secondary window. Clear the window first
  // so player 1's stale entries are not mistaken for player 2 parts.
  memcpy(oam_buf, oam_p1, sizeof(oam_p1));
  memcpy(bytewise_extended_oam, ext_p1, sizeof(ext_p1));
  for (int i = kP2OamBase; i < kP2OamBase + kLinkOamSlots; i++) {
    oam_buf[i].y = 0xf0;
    bytewise_extended_oam[i] = 0;
  }
  uint8 sort_setting = sort_sprites_setting;
  sort_sprites_setting = 1;
  LinkOam_Main();
  sort_sprites_setting = sort_setting;

  // LinkOam_Main set player 2's Link DMA indices. Compute its CHR source
  // addresses and fill the extra CHR space while player 2 is still live;
  // player 1's addresses and VRAM stay untouched.
  NMI_UpdateLinkDmaSources();
  NMI_LoadPlayer2LinkChr(g_zenv.ppu->linkChr);

  uint8 oam_link[0x200], ext_link[128];
  memcpy(oam_link, oam_buf, sizeof(oam_link));
  memcpy(ext_link, bytewise_extended_oam, sizeof(ext_link));

  // Player 2's buffer is now complete. Give the real NMI player 1's buffer so
  // the item each Link holds is uploaded to its own CHR slots.
  memcpy(s_gfx[1], gfx_4bpp_buffer, kGfx4bppSize);
  memcpy(gfx_4bpp_buffer, s_gfx[0], kGfx4bppSize);
  if (DebugLogging() && (s_debug_frame % 60) == 0) {
    uint32 h1 = HashBytes(s_gfx[0] + 0x2d40, 0x80);
    uint32 h2 = HashBytes(s_gfx[1] + 0x2d40, 0x80);
    if (h1 != h2)
      fprintf(stderr, "shared world: frame %ld item buffers differ "
              "p1=%08x p2=%08x\n", s_debug_frame, h1, h2);
  }

  // Back to player 1's OAM, then add player 2's effects and body.
  memcpy(oam_buf, oam_p1, sizeof(oam_p1));
  memcpy(bytewise_extended_oam, ext_p1, sizeof(ext_p1));
  MergePlayer2EffectOam(oam_p1, ext_p1, oam_eff, ext_eff);
  MergePlayer2LinkOam(oam_link, ext_link);

  // Minimal interaction sweep: player 2's sword/item hitbox damages enemies,
  // and enemies that reach player 2 can damage it. Target selection inside
  // Sprite_CheckDamageToLink follows the nearest Link.
  for (int k = 15; k >= 0; k--) {
    if (sprite_state[k] < 9)
      continue;
    uint8 give_before = sprite_give_damage[k];
    uint8 health_before = sprite_health[k];
    Sprite_CheckDamageToLink(k);
    if (sprite_state[k] < 9)
      continue;
    bool hit = Sprite_CheckDamageFromLink(k) != 0;
    if (hit && DebugLogging())
      fprintf(stderr, "shared world: frame %ld player 2 hit sprite %d type %02x\n",
              s_debug_frame, k, sprite_type[k]);
    if (sprite_give_damage[k] != give_before ||
        sprite_health[k] < health_before) {
      s_p2_hit_pending[k] = true;
      s_p2_hit_health[k] = health_before;
      s_p2_hit_type[k] = sprite_type[k];
      s_p2_hit_wait[k] = 32;
    }
  }

  bool player2_died = main_module_index == 18;
  if (player2_died) {
    MarkLinkDead(g_ram);
    s_dead[1] = true;
    s_respawn_timer[1] = kRespawnDelay;
    MOD_EMIT(kModEv_PlayerDeath, 2, 0, 0);
    if (DebugLogging())
      fprintf(stderr, "shared world: frame %ld player 2 died\n", s_debug_frame);
  }
  CopyPlayer(s_player[1], g_ram);

  if (!player2_died) {
    bool p2_transition = main_module_index != old_main_module ||
                         submodule_index != old_submodule ||
                         player_is_indoors != old_indoors ||
                         CurrentRoomId() != room;
    if (p2_transition && IsTransitionControlAllowed()) {
      // Player 2 crossed a door/border or took stairs. It becomes the live
      // Link for the transition; P1's copy waits and is placed next to P2
      // when the transition finishes.
      PromotePlayer2Leader();
      return;
    }
    RestoreControl(control);
  } else if (!s_dead[0]) {
    // Player 2 is down but P1 survived: ordinary gameplay continues.
    RestoreControl(control);
  }

  CopyPlayer(g_ram, s_player[0]);
  s_active_player = 0;
  UpdateSplitState();
}

void SharedWorld_DumpSelect(int player) {
  if (!SharedWorld_IsActive())
    return;
  if (player == 1) {
    if (!s_dump_active) {
      CopyPlayer(s_player[0], g_ram);
      CopyPlayer(g_ram, s_player[1]);
      s_active_player = 1;
      s_dump_active = true;
    }
  } else if (s_dump_active) {
    CopyPlayer(s_player[1], g_ram);
    CopyPlayer(g_ram, s_player[0]);
    s_active_player = 0;
    s_dump_active = false;
  }
}

void SharedWorld_DumpDone(void) {
  if (s_dump_active) {
    CopyPlayer(s_player[1], g_ram);
    CopyPlayer(g_ram, s_player[0]);
    s_active_player = 0;
    s_dump_active = false;
  }
}
