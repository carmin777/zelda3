#ifndef ZELDA3_MOD_FRAME_DESC_H_
#define ZELDA3_MOD_FRAME_DESC_H_

// Versioned description of one rendered PPU frame.
//
// The description is captured only when explicitly requested (command line,
// [Graphics] FrameDescription or an active render plugin). The game
// simulation never reads it. Bump kFrameDescVersion whenever the layout or the
// meaning of a field changes.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../types.h"

// Public, stable layout. Plugins include render_plugin.h, which includes this
// header; it deliberately does not depend on the internal Ppu struct or SDL.
enum {
  kFrameDescVersion = 4,

  // Worst case for the draw loop: two HDMA channels writing 4 bytes per line,
  // plus the four BG3 scroll writes at line 128.
  kFrameDesc_MaxChanges = 2 * 4 * 241 + 16,

  // One entry per OAM sprite (the OAM holds at most 128).
  kFrameDesc_MaxSprites = 128,

  // 256 + 2 * kPpuExtraLeftRight pixels wide, 240 pixels high.
  kFrameDesc_MaxTileCols = (256 + kPpuExtraLeftRight * 2) / 8,
  kFrameDesc_MaxTileRows = 240 / 8,
  kFrameDesc_MaxTiles = kFrameDesc_MaxTileCols * kFrameDesc_MaxTileRows,

  // A dungeon room can be up to 2x2 screens (64x64 8x8 tiles); the overworld
  // area is 64x64 Map16 cells (128x128 8x8 tiles). The layer stores 128x128
  // entries and the consumer uses cols/rows.
  kFrameDesc_MaxRoomDim = 128,
  kFrameDesc_MaxRoomTiles = kFrameDesc_MaxRoomDim * kFrameDesc_MaxRoomDim,

  // SharedWorld split-screen draws two complete panes, side by side.
  kFrameDesc_MaxPanes = 2,

  // OAM is copied into FrameDesc because SharedWorld changes it between the
  // two pane captures. The other large arrays stay by reference.
  kFrameDesc_OamWords = 0x110,
};

typedef struct FrameDescBg {
  uint16 hscroll;
  uint16 vscroll;
  uint16 tilemap_adr;
  uint16 tile_adr;
  bool wider;
  bool higher;
} FrameDescBg;

// PPU state at the start of the frame (after PpuBeginDrawing and the
// per-frame side-space configuration). Register latches needed by later
// ppu_write() calls are included so that replaying the line changes gives the
// same pixels.
typedef struct FrameDescPpuState {
  uint8 mode;
  uint8 brightness;
  bool forced_blank;
  bool emu_rom_mode;

  uint8 screen_enabled[2];
  uint8 screen_windowed[2];

  uint8 mosaic_enabled;
  uint8 mosaic_size;
  uint16 obj_tile_adr1;
  uint16 obj_tile_adr2;
  uint8 obj_size;

  uint8 window1left, window1right;
  uint8 window2left, window2right;
  uint32 windowsel;

  uint8 clip_mode;
  uint8 prevent_math_mode;
  bool add_subscreen;
  bool subtract_color;
  bool half_color;
  uint8 math_enabled;
  uint8 fixed_color_r, fixed_color_g, fixed_color_b;

  uint8 extra_left_right;
  uint8 extra_left_cur;
  uint8 extra_right_cur;
  uint8 extra_bottom_cur;

  float mode7_perspective_low;
  float mode7_perspective_high;

  FrameDescBg bg[4];
  uint8 scroll_prev;
  uint8 scroll_prev2;

  // Register write latches (needed if an HDMA table ever touches the data
  // ports instead of only the scroll/window registers).
  uint16 vram_pointer;
  uint16 vram_increment;
  bool vram_increment_on_high;
  uint8 cgram_pointer;
  bool cgram_second_write;
  uint8 cgram_buffer;
  uint16 oam_adr;
  bool oam_second_write;
  uint8 oam_buffer;

  int16 m7matrix[8];
  uint8 m7prev;
  bool m7large_field;
  bool m7char_fill;
  bool m7x_flip;
  bool m7y_flip;
  bool m7ext_bg;
  int32 m7start_x;
  int32 m7start_y;
} FrameDescPpuState;

typedef enum FrameDescChangeSource {
  kFrameDescChange_Hdma6 = 0,
  kFrameDescChange_Hdma7 = 1,
  kFrameDescChange_Irq = 2,
} FrameDescChangeSource;

typedef struct FrameDescChange {
  // The change is applied before ppu_runLine(line). HDMA writes performed
  // after line N are recorded with line N + 1; the line-128 irq split is
  // recorded on line 128.
  uint16 line;
  uint16 reg;      // 0x2100 .. 0x213f
  uint8 value;
  uint8 source;    // FrameDescChangeSource
} FrameDescChange;

typedef enum FrameDescOwnerKind {
  kFrameDescOwner_None = 0,
  kFrameDescOwner_Link = 1,
  kFrameDescOwner_Sprite = 2,
  kFrameDescOwner_Ancilla = 3,
} FrameDescOwnerKind;

typedef struct FrameDescSprite {
  uint8 oam_index;       // OAM entry (0, 2, 4, ...) this sprite starts at
  uint16 x_raw;          // 0..511, before SNES wrap adjustment
  int16 x;               // screen X after the extra-side-space adjustment
  uint8 y;
  uint8 tile;
  uint8 palette;
  uint8 priority;
  uint8 size;            // 8 or 16 (native game always uses one of these)
  bool flip_x;
  bool flip_y;
  uint8 owner_kind;      // FrameDescOwnerKind
  uint8 owner_index;     // Link/sprite slot/ancilla slot, or 0xff
  uint8 owner_type;      // sprite_type[] or ancilla_type[], 0 for Link
  uint8 floor;           // link/sprite/ancilla floor (0 = lower)
  int16 z;               // link_z_coord, sprite_z[] or ancilla_z[]; -1 unknown
} FrameDescSprite;

typedef enum FrameDescLayerSource {
  kFrameDescLayer_None = 0,
  kFrameDescLayer_DungeonBg1 = 1,  // upper floor
  kFrameDescLayer_DungeonBg2 = 2,  // lower floor
  kFrameDescLayer_Overworld = 3,   // BG2: map16 terrain
  kFrameDescLayer_OverworldBg1 = 4,  // BG1: trees/bridges/overlay
} FrameDescLayerSource;

typedef struct FrameDescTileLayer {
  bool present;
  uint8 source;           // FrameDescLayerSource
  uint8 floor;            // 0 = lower, 1 = upper
  int16 origin_tile_x;    // room/map-relative tile coordinate of column 0
  int16 origin_tile_y;
  uint16 cols;
  uint16 rows;
  uint8 valid[kFrameDesc_MaxTiles];
  uint8 attr[kFrameDesc_MaxTiles];    // collision tile attribute
  uint16 raw[kFrameDesc_MaxTiles];    // dungeon byte or overworld Map16 word
} FrameDescTileLayer;

// Whole current room/area, not just the part inside the SNES viewport.
// Dungeons expose the loaded room (up to 2x2 screens); the overworld exposes
// the loaded 64x64-Map16 area (128x128 8x8 tiles). Neighboring rooms/areas are
// never included. |origin_tile_x/y| is the room tile visible at pane
// column/row 0; |base_tile_x/y| is where this layer's cols x rows window
// starts inside the source table. Consumers draw tile (r,c) at
// ((base_x + c) - origin_x).
typedef struct FrameDescRoomLayer {
  bool present;
  uint8 source;           // FrameDescLayerSource
  uint8 cols, rows;       // 32..64, dense window starting at base_tile_x/y
  int16 origin_tile_x;    // room tile at pane column 0
  int16 origin_tile_y;    // room tile at pane row 0
  int16 screen_origin_tile_x;  // room tile at logical screen column 0
  int16 screen_origin_tile_y;  // room tile at logical screen row 0
  int16 base_tile_x;      // room/source-table tile at local column 0
  int16 base_tile_y;      // room/source-table tile at local row 0
  uint8 valid[kFrameDesc_MaxRoomTiles];
  uint8 attr[kFrameDesc_MaxRoomTiles];
  uint16 raw[kFrameDesc_MaxRoomTiles];
} FrameDescRoomLayer;

typedef struct FrameDescSemantics {
  bool indoors;
  bool mode7;
  uint16 room;            // dungeon_room_index2 / overworld_screen_index
  uint16 area;            // overworld_area_index
  uint8 floor;            // dung_cur_floor
  uint8 link_floor;       // link_is_on_lower_level
  int16 link_x, link_y, link_z;
  int16 camera_x, camera_y;
  FrameDescTileLayer layer[2];  // BG1 and BG2, indexed by PPU layer
  FrameDescRoomLayer room_layer[2];  // same layers, whole current room
} FrameDescSemantics;

// Where this description belongs in the output image. pane_x/pane_y are in
// logical SNES pixels (before scale); output_width/output_height are the whole
// frame in the same units. The renderer scales both by display.scale.
typedef struct FrameDescDisplay {
  uint32 scale;            // 1 normally, 4 for the 4x4 Mode 7 renderer
  uint32 pane_index;       // 0, or 0..1 for SharedWorld split-screen
  uint32 pane_count;       // 1 or 2
  int32 pane_x;
  int32 pane_y;
  uint32 output_width;
  uint32 output_height;

  // Effective side space after ConfigurePpuSideSpace(). The same values are
  // also inside ppu (extra_left_right etc.) for consumers that replay state.
  uint32 extra_left_right;
  uint32 extra_left_cur;
  uint32 extra_right_cur;
  uint32 extra_bottom_cur;
} FrameDescDisplay;

typedef struct FrameDesc {
  uint32 version;
  uint32 frame_index;
  uint32 width;           // visible width including extra left/right
  uint32 height;          // 224 or 240 (the draw loop bound)
  uint32 render_flags;
  FrameDescDisplay display;

  FrameDescPpuState ppu;

  // References into the live PPU. They stay valid until the next PPU write.
  // oam points at oam_snapshot below, which is a per-capture copy because the
  // split-screen path rewrites OAM between panes.
  const uint16 *vram;
  const uint16 *cgram;
  const uint16 *oam;
  const uint16 *link_chr;
  const uint8 *oam_is_player2;
  uint32 vram_words;
  uint32 cgram_words;
  uint32 oam_words;
  uint16 oam_snapshot[kFrameDesc_OamWords];

  uint32 change_count;
  FrameDescChange changes[kFrameDesc_MaxChanges];

  uint32 sprite_count;
  FrameDescSprite sprites[kFrameDesc_MaxSprites];

  FrameDescSemantics semantics;
  bool complete;
} FrameDesc;

typedef struct FrameDescCheckStats {
  uint64 frames;
  uint64 pixels;
  uint64 diff_pixels;
  uint64 split_frames;
  uint64 mode7_frames;
  uint64 mode7_4x4_frames;
  long first_frame;
  int first_x;
  int first_y;
  uint32 first_ref_pixel;
  uint32 first_desc_pixel;
} FrameDescCheckStats;

// Version compatibility for plugin loading. The layout is only compatible
// when the numbers are equal; there is no field-by-field migration yet.
uint32 FrameDesc_CurrentVersion(void);
bool FrameDesc_IsCompatibleVersion(uint32 version);

// Probe toggles. When off, the owner-tracking hooks return immediately.
void FrameDesc_SetProbeEnabled(bool enabled);
bool FrameDesc_ProbeEnabled(void);

// Capture toggle. ZeldaDrawPpuFrame captures only while this is on.
void FrameDesc_SetCaptureEnabled(bool enabled);
bool FrameDesc_CaptureEnabled(void);

// Selects which pane the next capture goes to. SharedWorld_DrawFrame uses this
// around each pane; the default is pane 0 of 1. pane_count is clamped to
// kFrameDesc_MaxPanes.
void FrameDesc_SetCapturePane(int pane, int pane_count);
int FrameDesc_CapturePane(void);
int FrameDesc_CapturePaneCount(void);

// Pane storage for split-screen. GetDesc returns NULL when out of range.
FrameDesc *FrameDesc_Active(void);
FrameDesc *FrameDesc_GetDesc(int pane);

// Capture hooks (BeginCapture/RecordRegister/EndCapture) live in
// frame_desc_capture.h, which is engine-internal.

// Replays the captured scroll-register writes (HDMA/IRQ) up to |line| and
// returns the 10-bit BG scroll in effect for that line. Consumers that sample
// the semantic layer per tile row should use this instead of the initial
// ppu.bg[layer].hscroll/vscroll; the overworld mirror warp and other HDMA
// effects change the scroll mid-frame.
static inline void FrameDesc_GetBgScrollAtLine(const FrameDesc *desc,
                                               int layer, int line,
                                               uint16 *hscroll,
                                               uint16 *vscroll) {
  if (desc == NULL || layer < 0 || layer >= 4) {
    if (hscroll != NULL)
      *hscroll = 0;
    if (vscroll != NULL)
      *vscroll = 0;
    return;
  }
  uint16 h = desc->ppu.bg[layer].hscroll;
  uint16 v = desc->ppu.bg[layer].vscroll;
  int hreg = 0x210d + layer * 2;
  int vreg = hreg + 1;
  bool h_high = false;
  bool v_high = false;
  for (uint32 i = 0; i < desc->change_count; i++) {
    const FrameDescChange *c = &desc->changes[i];
    if ((int)c->line > line)
      continue;
    if (c->reg == hreg) {
      if (h_high)
        h = (uint16)((h & 0x00ff) | (c->value << 8));
      else
        h = (uint16)((h & 0xff00) | c->value);
      h_high = !h_high;
    } else if (c->reg == vreg) {
      if (v_high)
        v = (uint16)((v & 0x00ff) | (c->value << 8));
      else
        v = (uint16)((v & 0xff00) | c->value);
      v_high = !v_high;
    }
  }
  if (hscroll != NULL)
    *hscroll = h & 0x3ff;
  if (vscroll != NULL)
    *vscroll = v & 0x3ff;
}

// Re-renders the frame described by |desc| into |buffer| using the normal ppu.c
// line renderer. The live PPU is not touched.
bool FrameDesc_Render(const FrameDesc *desc, uint8 *buffer, size_t pitch);

// Draws the current PPU state with the normal renderer, optionally capturing
// the description, then restores the PPU/DMA/irq latch so the simulation is
// unchanged. |pixels| may be NULL when only capturing.
bool FrameDesc_DrawCurrentFrame(uint32 render_flags, int width, int height,
                                bool capture, uint8 *pixels, size_t pitch);

// Per-frame pixel check used by --frame-check. Returns true when the
// description renderer and the normal renderer match exactly.
bool FrameDesc_CheckFrame(long frame, uint32 render_flags, int width,
                          int height, FrameDescCheckStats *stats);

// Dumps the current frame as JSON plus one semantic PNG per layer under
// .agents/shots/. Requires headless mode.
bool FrameDesc_DumpCurrentFrame(long frame, uint32 render_flags, int width,
                                int height);

// Owner hints recorded while the game builds OAM.
void FrameDesc_NoteSpriteOam(int slot, uint16 oam_ptr);
void FrameDesc_NoteAncillaOam(int slot, uint16 oam_ptr);

// Dump helpers (implemented in frame_desc_dump.c).
bool FrameDesc_WriteJson(const char *path, const FrameDesc *desc);
bool FrameDesc_WriteSemanticPng(const char *path, const FrameDesc *desc,
                                int layer_index);
bool FrameDesc_WritePngRgb(const char *path, int width, int height,
                           const uint8 *rgb);

#endif  // ZELDA3_MOD_FRAME_DESC_H_
