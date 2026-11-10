#include "frame_desc.h"
#include "frame_desc_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../zelda_rtl.h"
#include "../variables.h"
#include "../assets.h"
#include "../tile_detect.h"
#include "../overworld.h"
#include "shared_world.h"
#include "../../snes/ppu.h"
#include "../../snes/dma.h"

static bool s_probe_enabled;
static bool s_capture_enabled;
static FrameDesc s_active_desc[kFrameDesc_MaxPanes];
static int s_capture_pane;
static int s_capture_pane_count = 1;

static Ppu *s_scratch_ppu;
static Ppu *s_saved_ppu;
static uint8 *s_check_ref;
static uint8 *s_check_desc;
static size_t s_check_size;
static uint8 *s_draw_scratch;
static size_t s_draw_scratch_size;

uint32 FrameDesc_CurrentVersion(void) {
  return kFrameDescVersion;
}

bool FrameDesc_IsCompatibleVersion(uint32 version) {
  return version == kFrameDescVersion;
}

typedef struct FrameDescOwnerHint {
  uint32 frame;
  uint16 base;     // OAM sprite number
  uint8 count;     // OAM sprites
  uint8 type;
  bool valid;
} FrameDescOwnerHint;

static FrameDescOwnerHint s_sprite_hint[16];
static FrameDescOwnerHint s_ancilla_hint[10];

typedef struct FrameDescOwnerMapEntry {
  uint8 kind;
  uint8 index;
  uint8 type;
} FrameDescOwnerMapEntry;

static FrameDescOwnerMapEntry s_owner_map[128];

void FrameDesc_SetProbeEnabled(bool enabled) {
  s_probe_enabled = enabled;
  if (!enabled) {
    memset(s_sprite_hint, 0, sizeof(s_sprite_hint));
    memset(s_ancilla_hint, 0, sizeof(s_ancilla_hint));
  }
}

bool FrameDesc_ProbeEnabled(void) {
  return s_probe_enabled;
}

void FrameDesc_SetCaptureEnabled(bool enabled) {
  s_capture_enabled = enabled;
}

bool FrameDesc_CaptureEnabled(void) {
  return s_capture_enabled;
}

void FrameDesc_SetCapturePane(int pane, int pane_count) {
  if (pane_count < 1)
    pane_count = 1;
  if (pane_count > kFrameDesc_MaxPanes)
    pane_count = kFrameDesc_MaxPanes;
  if (pane < 0 || pane >= pane_count)
    pane = 0;
  s_capture_pane = pane;
  s_capture_pane_count = pane_count;
}

int FrameDesc_CapturePane(void) {
  return s_capture_pane;
}

int FrameDesc_CapturePaneCount(void) {
  return s_capture_pane_count;
}

FrameDesc *FrameDesc_Active(void) {
  return &s_active_desc[s_capture_pane];
}

FrameDesc *FrameDesc_GetDesc(int pane) {
  if (pane < 0 || pane >= kFrameDesc_MaxPanes)
    return NULL;
  return &s_active_desc[pane];
}

static void CapturePpuState(FrameDescPpuState *dst, const Ppu *ppu) {
  memset(dst, 0, sizeof(*dst));
  dst->mode = ppu->mode;
  dst->brightness = ppu->brightness;
  dst->forced_blank = ppu->forcedBlank;
  dst->emu_rom_mode = ppu->emuRomMode;

  dst->screen_enabled[0] = ppu->screenEnabled[0];
  dst->screen_enabled[1] = ppu->screenEnabled[1];
  dst->screen_windowed[0] = ppu->screenWindowed[0];
  dst->screen_windowed[1] = ppu->screenWindowed[1];

  dst->mosaic_enabled = ppu->mosaicEnabled;
  dst->mosaic_size = ppu->mosaicSize;
  dst->obj_tile_adr1 = ppu->objTileAdr1;
  dst->obj_tile_adr2 = ppu->objTileAdr2;
  dst->obj_size = ppu->objSize;

  dst->window1left = ppu->window1left;
  dst->window1right = ppu->window1right;
  dst->window2left = ppu->window2left;
  dst->window2right = ppu->window2right;
  dst->windowsel = ppu->windowsel;

  dst->clip_mode = ppu->clipMode;
  dst->prevent_math_mode = ppu->preventMathMode;
  dst->add_subscreen = ppu->addSubscreen;
  dst->subtract_color = ppu->subtractColor;
  dst->half_color = ppu->halfColor;
  dst->math_enabled = ppu->mathEnabled;
  dst->fixed_color_r = ppu->fixedColorR;
  dst->fixed_color_g = ppu->fixedColorG;
  dst->fixed_color_b = ppu->fixedColorB;

  dst->extra_left_right = ppu->extraLeftRight;
  dst->extra_left_cur = ppu->extraLeftCur;
  dst->extra_right_cur = ppu->extraRightCur;
  dst->extra_bottom_cur = ppu->extraBottomCur;

  dst->mode7_perspective_low = ppu->mode7PerspectiveLow;
  dst->mode7_perspective_high = ppu->mode7PerspectiveHigh;

  for (int i = 0; i < 4; i++) {
    dst->bg[i].hscroll = ppu->bgLayer[i].hScroll;
    dst->bg[i].vscroll = ppu->bgLayer[i].vScroll;
    dst->bg[i].tilemap_adr = ppu->bgLayer[i].tilemapAdr;
    dst->bg[i].tile_adr = ppu->bgLayer[i].tileAdr;
    dst->bg[i].wider = ppu->bgLayer[i].tilemapWider;
    dst->bg[i].higher = ppu->bgLayer[i].tilemapHigher;
  }
  dst->scroll_prev = ppu->scrollPrev;
  dst->scroll_prev2 = ppu->scrollPrev2;
  dst->vram_pointer = ppu->vramPointer;
  dst->vram_increment = ppu->vramIncrement;
  dst->vram_increment_on_high = ppu->vramIncrementOnHigh;
  dst->cgram_pointer = ppu->cgramPointer;
  dst->cgram_second_write = ppu->cgramSecondWrite;
  dst->cgram_buffer = ppu->cgramBuffer;
  dst->oam_adr = ppu->oamAdr;
  dst->oam_second_write = ppu->oamSecondWrite;
  dst->oam_buffer = ppu->oamBuffer;

  memcpy(dst->m7matrix, ppu->m7matrix, sizeof(dst->m7matrix));
  dst->m7prev = ppu->m7prev;
  dst->m7large_field = ppu->m7largeField;
  dst->m7char_fill = ppu->m7charFill;
  dst->m7x_flip = ppu->m7xFlip;
  dst->m7y_flip = ppu->m7yFlip;
  dst->m7ext_bg = ppu->m7extBg_always_zero;
  dst->m7start_x = ppu->m7startX;
  dst->m7start_y = ppu->m7startY;
}

static void ApplyPpuState(const FrameDesc *desc, Ppu *ppu) {
  const FrameDescPpuState *src = &desc->ppu;
  memset(ppu, 0, sizeof(*ppu));

  ppu->emuRomMode = src->emu_rom_mode;
  ppu->mode = src->mode;
  ppu->brightness = src->brightness;
  ppu->forcedBlank = src->forced_blank;

  ppu->screenEnabled[0] = src->screen_enabled[0];
  ppu->screenEnabled[1] = src->screen_enabled[1];
  ppu->screenWindowed[0] = src->screen_windowed[0];
  ppu->screenWindowed[1] = src->screen_windowed[1];

  ppu->mosaicEnabled = src->mosaic_enabled;
  ppu->mosaicSize = src->mosaic_size;
  ppu->objTileAdr1 = src->obj_tile_adr1;
  ppu->objTileAdr2 = src->obj_tile_adr2;
  ppu->objSize = src->obj_size;

  ppu->window1left = src->window1left;
  ppu->window1right = src->window1right;
  ppu->window2left = src->window2left;
  ppu->window2right = src->window2right;
  ppu->windowsel = src->windowsel;

  ppu->clipMode = src->clip_mode;
  ppu->preventMathMode = src->prevent_math_mode;
  ppu->addSubscreen = src->add_subscreen;
  ppu->subtractColor = src->subtract_color;
  ppu->halfColor = src->half_color;
  ppu->mathEnabled = src->math_enabled;
  ppu->fixedColorR = src->fixed_color_r;
  ppu->fixedColorG = src->fixed_color_g;
  ppu->fixedColorB = src->fixed_color_b;

  ppu->extraLeftRight = src->extra_left_right;
  ppu->extraLeftCur = src->extra_left_cur;
  ppu->extraRightCur = src->extra_right_cur;
  ppu->extraBottomCur = src->extra_bottom_cur;

  ppu->mode7PerspectiveLow = src->mode7_perspective_low;
  ppu->mode7PerspectiveHigh = src->mode7_perspective_high;

  for (int i = 0; i < 4; i++) {
    ppu->bgLayer[i].hScroll = src->bg[i].hscroll;
    ppu->bgLayer[i].vScroll = src->bg[i].vscroll;
    ppu->bgLayer[i].tilemapAdr = src->bg[i].tilemap_adr;
    ppu->bgLayer[i].tileAdr = src->bg[i].tile_adr;
    ppu->bgLayer[i].tilemapWider = src->bg[i].wider;
    ppu->bgLayer[i].tilemapHigher = src->bg[i].higher;
  }
  ppu->scrollPrev = src->scroll_prev;
  ppu->scrollPrev2 = src->scroll_prev2;
  ppu->vramPointer = src->vram_pointer;
  ppu->vramIncrement = src->vram_increment;
  ppu->vramIncrementOnHigh = src->vram_increment_on_high;
  ppu->cgramPointer = src->cgram_pointer;
  ppu->cgramSecondWrite = src->cgram_second_write;
  ppu->cgramBuffer = src->cgram_buffer;
  ppu->oamAdr = src->oam_adr;
  ppu->oamSecondWrite = src->oam_second_write;
  ppu->oamBuffer = src->oam_buffer;

  memcpy(ppu->m7matrix, src->m7matrix, sizeof(ppu->m7matrix));
  ppu->m7prev = src->m7prev;
  ppu->m7largeField = src->m7large_field;
  ppu->m7charFill = src->m7char_fill;
  ppu->m7xFlip = src->m7x_flip;
  ppu->m7yFlip = src->m7y_flip;
  ppu->m7extBg_always_zero = src->m7ext_bg;
  ppu->m7startX = src->m7start_x;
  ppu->m7startY = src->m7start_y;

  // Force PpuBeginDrawing to rebuild its caches from the captured CGRAM and
  // brightness instead of inheriting stale values.
  ppu->lastBrightnessMult = 0xff;
  ppu->lastMosaicModulo = 0xff;

  if (desc->vram != NULL && desc->vram_words != 0)
    memcpy(ppu->vram, desc->vram, desc->vram_words * sizeof(uint16));
  if (desc->cgram != NULL && desc->cgram_words != 0)
    memcpy(ppu->cgram, desc->cgram, desc->cgram_words * sizeof(uint16));
  if (desc->oam != NULL && desc->oam_words != 0)
    memcpy(ppu->oam, desc->oam, desc->oam_words * sizeof(uint16));
  if (desc->link_chr != NULL)
    memcpy(ppu->linkChr, desc->link_chr, sizeof(ppu->linkChr));
  if (desc->oam_is_player2 != NULL)
    memcpy(ppu->oamIsPlayer2, desc->oam_is_player2,
           sizeof(ppu->oamIsPlayer2));
}

void FrameDesc_BeginCapture(FrameDesc *desc, Ppu *ppu, uint32 render_flags,
                            int width, int height) {
  memset(desc, 0, sizeof(*desc));
  desc->version = kFrameDescVersion;
  desc->frame_index = (uint32)frame_ctr_dbg;
  desc->width = (uint32)width;
  desc->height = (uint32)height;
  desc->render_flags = render_flags;

  desc->display.scale = (uint32)PpuGetCurrentRenderScale(ppu, render_flags);
  desc->display.pane_index = (uint32)s_capture_pane;
  desc->display.pane_count = (uint32)s_capture_pane_count;
  desc->display.pane_x = s_capture_pane * width;
  desc->display.pane_y = 0;
  desc->display.output_width = (uint32)(width * s_capture_pane_count);
  desc->display.output_height = (uint32)height;
  desc->display.extra_left_right = ppu->extraLeftRight;
  desc->display.extra_left_cur = ppu->extraLeftCur;
  desc->display.extra_right_cur = ppu->extraRightCur;
  desc->display.extra_bottom_cur = ppu->extraBottomCur;

  CapturePpuState(&desc->ppu, ppu);

  desc->vram = ppu->vram;
  desc->cgram = ppu->cgram;
  memcpy(desc->oam_snapshot, ppu->oam, sizeof(desc->oam_snapshot));
  desc->oam = desc->oam_snapshot;
  desc->link_chr = ppu->linkChr;
  desc->oam_is_player2 = ppu->oamIsPlayer2;
  desc->vram_words = 0x8000;
  desc->cgram_words = 0x100;
  desc->oam_words = kFrameDesc_OamWords;

  desc->change_count = 0;
  desc->sprite_count = 0;
  desc->complete = false;
}

void FrameDesc_RecordRegister(FrameDesc *desc, int line, uint16 reg,
                              uint8 value, uint8 source) {
  if (desc == NULL || desc->change_count >= kFrameDesc_MaxChanges)
    return;
  FrameDescChange *c = &desc->changes[desc->change_count++];
  c->line = (uint16)line;
  c->reg = reg;
  c->value = value;
  c->source = source;
}

void FrameDesc_NoteSpriteOam(int slot, uint16 oam_ptr) {
  if (!s_probe_enabled || slot < 0 || slot >= (int)countof(s_sprite_hint))
    return;
  FrameDescOwnerHint *h = &s_sprite_hint[slot];
  h->frame = (uint32)frame_ctr_dbg;
  h->base = (uint16)((oam_ptr - 0x800u) >> 2);
  h->count = (uint8)((sprite_flags2[slot] & 0x1f) + 1);
  h->type = sprite_type[slot];
  h->valid = true;
}

void FrameDesc_NoteAncillaOam(int slot, uint16 oam_ptr) {
  if (!s_probe_enabled || slot < 0 || slot >= (int)countof(s_ancilla_hint))
    return;
  FrameDescOwnerHint *h = &s_ancilla_hint[slot];
  h->frame = (uint32)frame_ctr_dbg;
  h->base = (uint16)((oam_ptr - 0x800u) >> 2);
  h->count = (uint8)(ancilla_numspr[slot] >> 2);
  if (h->count == 0)
    h->count = 1;
  h->type = ancilla_type[slot];
  h->valid = true;
}

static void MarkOwnerRange(FrameDescOwnerMapEntry *map, int base, int count,
                           uint8 kind, uint8 index, uint8 type) {
  if (base < 0)
    return;
  for (int i = 0; i < count && base + i < 128; i++) {
    FrameDescOwnerMapEntry *e = &map[base + i];
    if (e->kind == kFrameDescOwner_None) {
      e->kind = kind;
      e->index = index;
      e->type = type;
    }
  }
}

static void BuildOwnerMap(void) {
  memset(s_owner_map, 0, sizeof(s_owner_map));

  for (int k = 0; k < (int)countof(s_sprite_hint); k++) {
    FrameDescOwnerHint *h = &s_sprite_hint[k];
    if (!h->valid || h->frame != (uint32)frame_ctr_dbg || sprite_state[k] == 0)
      continue;
    MarkOwnerRange(s_owner_map, h->base, h->count, kFrameDescOwner_Sprite,
                   (uint8)k, h->type);
  }

  for (int k = 0; k < (int)countof(s_ancilla_hint); k++) {
    FrameDescOwnerHint *h = &s_ancilla_hint[k];
    if (!h->valid || h->frame != (uint32)frame_ctr_dbg || ancilla_type[k] == 0)
      continue;
    MarkOwnerRange(s_owner_map, h->base, h->count, kFrameDescOwner_Ancilla,
                   (uint8)k, h->type);
  }

  // Link writes a fixed 16-entry block. Fill it last because some of those
  // slots overlap the generic sprite regions depending on sort_sprites_setting.
  int link_base = sort_sprites_offset_into_oam_buffer >> 2;
  if (link_base != 0) {
    for (int i = 0; i < 16 && link_base + i < 128; i++) {
      FrameDescOwnerMapEntry *e = &s_owner_map[link_base + i];
      e->kind = kFrameDescOwner_Link;
      e->index = 0;
      e->type = 0;
    }
  }
}

static void BuildSprites(FrameDesc *desc, const Ppu *ppu) {
  BuildOwnerMap();
  int extra = ppu->extraLeftRight;
  for (int word = 0; word < 256; word += 2) {
    uint16 ent = ppu->oam[word];
    int y = ent >> 8;
    if (y == 0xf0)
      continue;

    uint16 ext = ppu->oam[0x100 + (word >> 4)];
    int hi = (ext >> (word & 15)) & 3;
    uint16 ent2 = ppu->oam[word + 1];
    int x_raw = (ent & 0xff) | ((hi & 1) << 8);
    int x = x_raw;
    if (x >= 256 + extra)
      x -= 512;

    int size = (hi & 2) ? 16 : 8;
    int sprite_no = word >> 1;
    FrameDescOwnerMapEntry *owner = &s_owner_map[sprite_no];

    FrameDescSprite *s = &desc->sprites[desc->sprite_count++];
    memset(s, 0, sizeof(*s));
    s->oam_index = (uint8)sprite_no;
    s->x_raw = (uint16)x_raw;
    s->x = (int16)x;
    s->y = (uint8)y;
    s->tile = ent2 & 0xff;
    s->palette = (ent2 >> 9) & 7;
    s->priority = (ent2 >> 12) & 3;
    s->size = (uint8)size;
    s->flip_x = (ent2 & 0x4000) != 0;
    s->flip_y = (ent2 & 0x8000) != 0;
    s->owner_kind = owner->kind;
    s->owner_index = owner->kind ? owner->index : 0xff;
    s->owner_type = owner->type;
    s->floor = 0xff;
    s->z = -1;

    if (owner->kind == kFrameDescOwner_Link) {
      s->floor = link_is_on_lower_level ? 1 : 0;
      s->z = (int16)link_z_coord;
    } else if (owner->kind == kFrameDescOwner_Sprite) {
      s->floor = sprite_floor[owner->index] ? 1 : 0;
      s->z = (int16)sprite_z[owner->index];
    } else if (owner->kind == kFrameDescOwner_Ancilla) {
      s->floor = ancilla_floor[owner->index] ? 1 : 0;
      s->z = (int16)ancilla_z[owner->index];
    }
  }
}

static void BuildDungeonLayer(FrameDescTileLayer *tl, const FrameDesc *desc,
                              const FrameDescBg *bg, int layer, int cols,
                              int rows) {
  tl->source = layer == 0 ? kFrameDescLayer_DungeonBg1
                          : kFrameDescLayer_DungeonBg2;
  tl->floor = layer == 0 ? 1 : 0;
  int x0 = -(int)desc->ppu.extra_left_right;
  tl->origin_tile_x = (int16)(((bg->hscroll + x0) & 0x1f8) >> 3);
  tl->origin_tile_y = (int16)(((bg->vscroll & 0x1f8) >> 3));
  const uint8 *table = tl->floor ? dung_bg1_attr_table : dung_bg2_attr_table;
  for (int row = 0; row < rows; row++) {
    uint16 hscroll, vscroll;
    FrameDesc_GetBgScrollAtLine(desc, layer, row * 8, &hscroll, &vscroll);
    for (int col = 0; col < cols; col++) {
      int idx = row * kFrameDesc_MaxTileCols + col;
      int world_x = x0 + col * 8 + hscroll;
      int world_y = row * 8 + vscroll;
      int tx = (world_x & 0x1f8) >> 3;
      int ty = (world_y & 0x1f8) >> 3;
      uint8 attr = table[ty * 64 + tx];
      tl->attr[idx] = attr;
      tl->raw[idx] = attr;
    }
  }
}

// Overworld BG2 is the terrain (Map16 in dung_bg2/overworld_tileattr) and has
// proper collision attributes. BG1 is a separate overlay map (trees, bridges,
// rain, ...) built into dung_bg1. It has no collision table of its own, but
// kSomeTileAttr maps its Map16 ids to the same attribute space used by the
// height profile, so each layer gets its own height instead of borrowing the
// terrain below it.
static void BuildOverworldLayer(FrameDescTileLayer *tl, const FrameDesc *desc,
                                const FrameDescBg *bg, int layer, int cols,
                                int rows) {
  tl->source = layer == 0 ? kFrameDescLayer_OverworldBg1
                          : kFrameDescLayer_Overworld;
  tl->floor = 0;
  int x0 = -(int)desc->ppu.extra_left_right;
  tl->origin_tile_x = (int16)(((x0 + bg->hscroll) >> 3));
  tl->origin_tile_y = (int16)(bg->vscroll >> 3);
  const uint16 *map = layer == 0 ? dung_bg1 : overworld_tileattr;
  for (int row = 0; row < rows; row++) {
    uint16 hscroll, vscroll;
    FrameDesc_GetBgScrollAtLine(desc, layer, row * 8, &hscroll, &vscroll);
    for (int col = 0; col < cols; col++) {
      int idx = row * kFrameDesc_MaxTileCols + col;
      int world_x = x0 + col * 8 + hscroll;
      int world_y = row * 8 + vscroll;
      int tile_x = world_x >> 3;
      int map16_x = (((tile_x - overworld_offset_base_x) &
                      overworld_offset_mask_x) >> 1);
      int map16_y = (((world_y - overworld_offset_base_y) &
                      overworld_offset_mask_y) >> 4);
      int map16_idx = (map16_y << 6) + map16_x;
      uint16 raw = 0;
      if (map16_idx >= 0 && map16_idx < 0x1000)
        raw = map[map16_idx];
      tl->raw[idx] = raw;
      if (layer == 1) {
        tl->attr[idx] = raw < 0xea8
            ? Overworld_GetTileAttributeAtLocation((uint16)tile_x,
                                                   (uint16)world_y)
            : 0;
      } else {
        tl->attr[idx] = raw < 0xea8 ? kSomeTileAttr[raw] : 0;
      }
    }
  }
}

static void BuildSemantics(FrameDesc *desc) {
  const FrameDescPpuState *ppu = &desc->ppu;
  FrameDescSemantics *sem = &desc->semantics;
  memset(sem, 0, sizeof(*sem));
  sem->indoors = player_is_indoors != 0;
  sem->mode7 = ppu->mode == 7;
  sem->room = sem->indoors ? dungeon_room_index2 : overworld_screen_index;
  sem->area = overworld_area_index;
  sem->floor = dung_cur_floor;
  sem->link_floor = link_is_on_lower_level;
  sem->link_x = (int16)link_x_coord;
  sem->link_y = (int16)link_y_coord;
  sem->link_z = (int16)link_z_coord;
  sem->camera_x = (int16)ppu->bg[1].hscroll;
  sem->camera_y = (int16)ppu->bg[1].vscroll;

  int cols = (int)((desc->width + 7) / 8);
  int rows = (int)((desc->height + 7) / 8);
  if (cols > kFrameDesc_MaxTileCols)
    cols = kFrameDesc_MaxTileCols;
  if (rows > kFrameDesc_MaxTileRows)
    rows = kFrameDesc_MaxTileRows;

  int x0 = -(int)ppu->extra_left_right;
  int left_clip = -(int)ppu->extra_left_cur;
  int right_clip = 256 + ppu->extra_right_cur;
  for (int layer = 0; layer < 2; layer++) {
    FrameDescTileLayer *tl = &sem->layer[layer];
    memset(tl, 0, sizeof(*tl));
    tl->cols = (uint16)cols;
    tl->rows = (uint16)rows;
    if (sem->mode7)
      continue;

    const FrameDescBg *bg = &ppu->bg[layer];
    if (sem->indoors) {
      if (layer > 1)
        continue;
      BuildDungeonLayer(tl, desc, bg, layer, cols, rows);
    } else {
      if (layer > 1)
        continue;
      BuildOverworldLayer(tl, desc, bg, layer, cols, rows);
    }

    tl->present = (ppu->screen_enabled[0] & (1 << layer)) != 0;
    for (int row = 0; row < rows; row++) {
      for (int col = 0; col < cols; col++) {
        int idx = row * kFrameDesc_MaxTileCols + col;
        int sx = x0 + col * 8;
        tl->valid[idx] = sx + 8 > left_clip && sx < right_clip;
      }
    }
  }
}

void FrameDesc_EndCapture(FrameDesc *desc) {
  if (desc == NULL)
    return;
  BuildSprites(desc, g_zenv.ppu);
  BuildSemantics(desc);
  desc->complete = true;
}

bool FrameDesc_Render(const FrameDesc *desc, uint8 *buffer, size_t pitch) {
  if (desc == NULL || desc->version != kFrameDescVersion ||
      !desc->complete || buffer == NULL)
    return false;

  if (s_scratch_ppu == NULL) {
    s_scratch_ppu = (Ppu *)malloc(sizeof(Ppu));
    if (s_scratch_ppu == NULL)
      return false;
  }
  Ppu *ppu = s_scratch_ppu;
  ApplyPpuState(desc, ppu);
  PpuBeginDrawing(ppu, buffer, pitch, desc->render_flags);

  uint32 change = 0;
  for (int line = 0; line <= (int)desc->height; line++) {
    while (change < desc->change_count &&
           desc->changes[change].line == (uint16)line) {
      uint16 reg = desc->changes[change].reg;
      if (reg >= 0x2100 && reg <= 0x213f)
        ppu_write(ppu, (uint8)(reg - 0x2100), desc->changes[change].value);
      change++;
    }
    ppu_runLine(ppu, line);
  }
  return true;
}

static bool EnsureCheckBuffers(size_t size) {
  if (s_check_size >= size)
    return true;
  uint8 *a = (uint8 *)realloc(s_check_ref, size);
  if (a == NULL)
    return false;
  s_check_ref = a;
  uint8 *b = (uint8 *)realloc(s_check_desc, size);
  if (b == NULL)
    return false;
  s_check_desc = b;
  s_check_size = size;
  return true;
}

static bool EnsureDrawBuffer(size_t size) {
  if (s_draw_scratch_size >= size)
    return true;
  uint8 *p = (uint8 *)realloc(s_draw_scratch, size);
  if (p == NULL)
    return false;
  s_draw_scratch = p;
  s_draw_scratch_size = size;
  return true;
}

// Captures one output frame (one pane, or both SharedWorld panes). When
// |pixels| is NULL the normal renderer still has to run so the PPU state and
// register changes can be observed; a reusable scratch buffer holds its
// output. The live PPU/DMA/irq latch are restored before returning, so the
// simulation sees exactly what it saw before the call.
bool FrameDesc_DrawCurrentFrame(uint32 render_flags, int width, int height,
                                bool capture, uint8 *pixels, size_t pitch) {
  int pane_count = SharedWorld_IsSplit() ? 2 : 1;
  int out_width = width * pane_count;
  size_t needed = (size_t)out_width * 4 * (size_t)height;
  if (pixels == NULL) {
    if (!EnsureDrawBuffer(needed))
      return false;
    pixels = s_draw_scratch;
    pitch = (size_t)out_width * 4;
  }

  Ppu *saved_ppu = s_saved_ppu;
  if (saved_ppu == NULL) {
    saved_ppu = (Ppu *)malloc(sizeof(Ppu));
    if (saved_ppu == NULL)
      return false;
    s_saved_ppu = saved_ppu;
  }
  memcpy(saved_ppu, g_zenv.ppu, sizeof(Ppu));
  Dma saved_dma = *g_zenv.dma;
  uint8 saved_irq = irq_flag;

  bool old_capture = s_capture_enabled;
  s_capture_enabled = capture;
  FrameDesc_SetCapturePane(0, pane_count);

  if (pane_count == 2)
    SharedWorld_DrawFrame(pixels, pitch, render_flags, width);
  else
    ZeldaDrawPpuFrame(pixels, pitch, render_flags);

  s_capture_enabled = old_capture;
  memcpy(g_zenv.ppu, saved_ppu, sizeof(Ppu));
  *g_zenv.dma = saved_dma;
  irq_flag = saved_irq;

  if (!capture)
    return true;
  for (int pane = 0; pane < pane_count; pane++) {
    FrameDesc *desc = FrameDesc_GetDesc(pane);
    if (desc == NULL || !desc->complete)
      return false;
  }
  return true;
}

bool FrameDesc_CheckFrame(long frame, uint32 render_flags, int width,
                          int height, FrameDescCheckStats *stats) {
  int pane_count = SharedWorld_IsSplit() ? 2 : 1;
  int out_width = width * pane_count;
  size_t pitch = (size_t)out_width * 4;
  size_t size = pitch * (size_t)height;
  if (!EnsureCheckBuffers(size))
    return false;

  memset(s_check_ref, 0, size);
  memset(s_check_desc, 0, size);
  bool captured = FrameDesc_DrawCurrentFrame(render_flags, width, height, true,
                                             s_check_ref, pitch);
  if (!captured) {
    fprintf(stderr, "frame-check: failed to capture frame %ld\n", frame);
    return false;
  }
  for (int pane = 0; pane < pane_count; pane++) {
    FrameDesc *desc = FrameDesc_GetDesc(pane);
    if (desc == NULL ||
        !FrameDesc_Render(desc, s_check_desc + (size_t)pane * width * 4,
                          pitch)) {
      fprintf(stderr, "frame-check: failed to render frame %ld pane %d\n",
              frame, pane);
      return false;
    }
  }

  const uint32 *ref = (const uint32 *)s_check_ref;
  const uint32 *got = (const uint32 *)s_check_desc;
  uint32 count = (uint32)out_width * (uint32)height;
  uint64 diff = 0;
  for (uint32 i = 0; i < count; i++) {
    if (ref[i] != got[i]) {
      if (diff == 0 && stats->diff_pixels == 0) {
        stats->first_frame = frame;
        stats->first_x = (int)(i % (uint32)out_width);
        stats->first_y = (int)(i / (uint32)out_width);
        stats->first_ref_pixel = ref[i];
        stats->first_desc_pixel = got[i];
      }
      diff++;
    }
  }
  stats->frames++;
  stats->pixels += count;
  stats->diff_pixels += diff;
  if (pane_count == 2)
    stats->split_frames++;
  FrameDesc *first = FrameDesc_GetDesc(0);
  if (first != NULL && first->ppu.mode == 7) {
    stats->mode7_frames++;
    if ((render_flags & kPpuRenderFlags_4x4Mode7) != 0)
      stats->mode7_4x4_frames++;
    if (stats->mode7_frames == 1)
      printf("frame-check: first mode7 frame %ld (4x4=%d)\n", frame,
             (render_flags & kPpuRenderFlags_4x4Mode7) != 0);
  }
  return diff == 0;
}
