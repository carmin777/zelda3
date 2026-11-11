#include "frame_desc.h"
#include "hd_tile_hash.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define FrameDesc_Mkdir(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define FrameDesc_Mkdir(path) mkdir(path, 0755)
#endif

#include "../zelda_rtl.h"

static uint32 FrameDesc_Crc32(const uint8 *data, size_t size) {
  static uint32 table[256];
  static bool initialized;
  if (!initialized) {
    for (uint32 i = 0; i < 256; i++) {
      uint32 c = i;
      for (int k = 0; k < 8; k++)
        c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    initialized = true;
  }
  uint32 c = 0xffffffffu;
  for (size_t i = 0; i < size; i++)
    c = table[(c ^ data[i]) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}

static uint32 FrameDesc_Adler32(const uint8 *data, size_t size) {
  uint32 a = 1, b = 0;
  for (size_t i = 0; i < size; i++) {
    a = (a + data[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

static void Put32BE(uint8 *p, uint32 v) {
  p[0] = (uint8)(v >> 24);
  p[1] = (uint8)(v >> 16);
  p[2] = (uint8)(v >> 8);
  p[3] = (uint8)v;
}

static bool WriteChunk(FILE *f, const char type[4], const uint8 *data,
                       size_t size) {
  uint8 buf[4];
  Put32BE(buf, (uint32)size);
  if (fwrite(buf, 1, 4, f) != 4)
    return false;
  if (fwrite(type, 1, 4, f) != 4)
    return false;
  if (size != 0 && fwrite(data, 1, size, f) != size)
    return false;
  uint32 crc = FrameDesc_Crc32((const uint8 *)type, 4);
  if (size != 0) {
    // The CRC covers type + data. Compute it without an extra allocation.
    crc = 0xffffffffu;
    static uint32 table[256];
    static bool initialized;
    if (!initialized) {
      for (uint32 i = 0; i < 256; i++) {
        uint32 c = i;
        for (int k = 0; k < 8; k++)
          c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
      }
      initialized = true;
    }
    for (int i = 0; i < 4; i++)
      crc = table[(crc ^ type[i]) & 0xff] ^ (crc >> 8);
    for (size_t i = 0; i < size; i++)
      crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    crc ^= 0xffffffffu;
  }
  Put32BE(buf, crc);
  return fwrite(buf, 1, 4, f) == 4;
}

bool FrameDesc_WritePngRgb(const char *path, int width, int height,
                           const uint8 *rgb) {
  if (width <= 0 || height <= 0 || rgb == NULL)
    return false;

  FILE *f = fopen(path, "wb");
  if (f == NULL)
    return false;

  bool ok = true;
  static const uint8 kSignature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  if (fwrite(kSignature, 1, sizeof(kSignature), f) != sizeof(kSignature))
    ok = false;

  uint8 ihdr[13];
  Put32BE(ihdr + 0, (uint32)width);
  Put32BE(ihdr + 4, (uint32)height);
  ihdr[8] = 8;   // bit depth
  ihdr[9] = 2;   // truecolor RGB
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  if (ok)
    ok = WriteChunk(f, "IHDR", ihdr, sizeof(ihdr));

  size_t row_size = (size_t)width * 3 + 1;
  size_t raw_size = row_size * (size_t)height;
  uint8 *raw = (uint8 *)malloc(raw_size);
  if (raw == NULL) {
    fclose(f);
    return false;
  }
  for (int y = 0; y < height; y++) {
    uint8 *dst = raw + (size_t)y * row_size;
    dst[0] = 0;
    memcpy(dst + 1, rgb + (size_t)y * (size_t)width * 3,
           (size_t)width * 3);
  }

  size_t z_size = 2 + (raw_size / 65535 + 1) * 5 + raw_size + 4;
  uint8 *z = (uint8 *)malloc(z_size);
  if (z == NULL) {
    free(raw);
    fclose(f);
    return false;
  }
  size_t zpos = 0;
  z[zpos++] = 0x78;
  z[zpos++] = 0x01;
  size_t pos = 0;
  while (pos < raw_size) {
    size_t chunk = raw_size - pos;
    if (chunk > 65535)
      chunk = 65535;
    bool final = pos + chunk == raw_size;
    z[zpos++] = final ? 1 : 0;
    z[zpos++] = (uint8)chunk;
    z[zpos++] = (uint8)(chunk >> 8);
    z[zpos++] = (uint8)~chunk;
    z[zpos++] = (uint8)(~chunk >> 8);
    memcpy(z + zpos, raw + pos, chunk);
    zpos += chunk;
    pos += chunk;
  }
  uint32 adler = FrameDesc_Adler32(raw, raw_size);
  z[zpos++] = (uint8)(adler >> 24);
  z[zpos++] = (uint8)(adler >> 16);
  z[zpos++] = (uint8)(adler >> 8);
  z[zpos++] = (uint8)adler;

  if (ok)
    ok = WriteChunk(f, "IDAT", z, zpos);
  if (ok)
    ok = WriteChunk(f, "IEND", NULL, 0);
  if (fclose(f) != 0)
    ok = false;
  free(z);
  free(raw);
  return ok;
}

static void AttrToRgb(uint16 attr, uint8 *r, uint8 *g, uint8 *b) {
  uint8 hi = (uint8)((attr >> 4) & 0xf);
  uint8 lo = (uint8)(attr & 0xf);
  *r = (uint8)(32 + hi * 13);
  *g = (uint8)(32 + lo * 13);
  *b = (uint8)(32 + ((hi ^ lo) * 13));
}

bool FrameDesc_WriteSemanticPng(const char *path, const FrameDesc *desc,
                                int layer_index) {
  if (layer_index < 0 || layer_index >= 2)
    return false;
  const FrameDescTileLayer *tl = &desc->semantics.layer[layer_index];
  if (!tl->present || tl->cols == 0 || tl->rows == 0)
    return false;

  int width = tl->cols * 8;
  int height = tl->rows * 8;
  uint8 *rgb = (uint8 *)malloc((size_t)width * height * 3);
  if (rgb == NULL)
    return false;

  for (int row = 0; row < tl->rows; row++) {
    for (int col = 0; col < tl->cols; col++) {
      int idx = row * kFrameDesc_MaxTileCols + col;
      uint8 r = 16, g = 16, b = 16;
      if (tl->valid[idx])
        AttrToRgb(tl->attr[idx], &r, &g, &b);
      for (int y = 0; y < 8; y++) {
        uint8 *dst = rgb + (((size_t)row * 8 + y) * width + col * 8) * 3;
        for (int x = 0; x < 8; x++) {
          dst[0] = r;
          dst[1] = g;
          dst[2] = b;
          dst += 3;
        }
      }
    }
  }
  bool ok = FrameDesc_WritePngRgb(path, width, height, rgb);
  free(rgb);
  return ok;
}

static uint32 FrameDesc_Fnv1a(const void *data, size_t size) {
  const uint8 *p = (const uint8 *)data;
  uint32 hash = 2166136261u;
  while (size--)
    hash = (hash ^ *p++) * 16777619u;
  return hash;
}

static double JsonFloat(float v) {
  if (v != v || v > 1e30f || v < -1e30f)
    return 0.0;
  return (double)v;
}

static void PrintLayerJson(FILE *f, const FrameDescTileLayer *tl,
                           const char *name, bool last) {
  fprintf(f, "    \"%s\": {\n", name);
  fprintf(f, "      \"present\": %s,\n", tl->present ? "true" : "false");
  fprintf(f, "      \"source\": %u,\n", (unsigned)tl->source);
  fprintf(f, "      \"floor\": %u,\n", (unsigned)tl->floor);
  fprintf(f, "      \"origin_tile_x\": %d,\n", (int)tl->origin_tile_x);
  fprintf(f, "      \"origin_tile_y\": %d,\n", (int)tl->origin_tile_y);
  fprintf(f, "      \"cols\": %u,\n", (unsigned)tl->cols);
  fprintf(f, "      \"rows\": %u,\n", (unsigned)tl->rows);
  fprintf(f, "      \"attr\": [");
  for (int row = 0; row < tl->rows; row++) {
    fprintf(f, "%s\n        [", row ? "," : "");
    for (int col = 0; col < tl->cols; col++) {
      int idx = row * kFrameDesc_MaxTileCols + col;
      fprintf(f, "%s%u", col ? ", " : "", (unsigned)tl->attr[idx]);
    }
    fprintf(f, "]");
  }
  fprintf(f, "\n      ],\n");
  fprintf(f, "      \"raw\": [");
  for (int row = 0; row < tl->rows; row++) {
    fprintf(f, "%s\n        [", row ? "," : "");
    for (int col = 0; col < tl->cols; col++) {
      int idx = row * kFrameDesc_MaxTileCols + col;
      fprintf(f, "%s%u", col ? ", " : "", (unsigned)tl->raw[idx]);
    }
    fprintf(f, "]");
  }
  fprintf(f, "\n      ],\n");
  fprintf(f, "      \"valid\": [");
  for (int row = 0; row < tl->rows; row++) {
    fprintf(f, "%s\n        [", row ? "," : "");
    for (int col = 0; col < tl->cols; col++) {
      int idx = row * kFrameDesc_MaxTileCols + col;
      fprintf(f, "%s%u", col ? ", " : "", (unsigned)tl->valid[idx]);
    }
    fprintf(f, "]");
  }
  fprintf(f, "\n      ]\n");
  fprintf(f, "    }%s\n", last ? "" : ",");
}

bool FrameDesc_WriteJson(const char *path, const FrameDesc *desc) {
  FILE *f = fopen(path, "wb");
  if (f == NULL)
    return false;

  const FrameDescPpuState *p = &desc->ppu;
  fprintf(f, "{\n");
  fprintf(f, "  \"version\": %u,\n", (unsigned)desc->version);
  fprintf(f, "  \"frame\": %u,\n", (unsigned)desc->frame_index);
  fprintf(f, "  \"width\": %u,\n", (unsigned)desc->width);
  fprintf(f, "  \"height\": %u,\n", (unsigned)desc->height);
  fprintf(f, "  \"render_flags\": %u,\n", (unsigned)desc->render_flags);
  fprintf(f, "  \"display\": {\"scale\": %u, \"pane_index\": %u, "
             "\"pane_count\": %u, \"pane_x\": %d, \"pane_y\": %d, "
             "\"output_width\": %u, \"output_height\": %u, "
             "\"extra\": [%u, %u, %u, %u]},\n",
          (unsigned)desc->display.scale, (unsigned)desc->display.pane_index,
          (unsigned)desc->display.pane_count, (int)desc->display.pane_x,
          (int)desc->display.pane_y, (unsigned)desc->display.output_width,
          (unsigned)desc->display.output_height,
          (unsigned)desc->display.extra_left_right,
          (unsigned)desc->display.extra_left_cur,
          (unsigned)desc->display.extra_right_cur,
          (unsigned)desc->display.extra_bottom_cur);
  fprintf(f, "  \"complete\": %s,\n", desc->complete ? "true" : "false");

  fprintf(f, "  \"ppu\": {\n");
  fprintf(f, "    \"mode\": %u,\n", (unsigned)p->mode);
  fprintf(f, "    \"brightness\": %u,\n", (unsigned)p->brightness);
  fprintf(f, "    \"forced_blank\": %s,\n",
          p->forced_blank ? "true" : "false");
  fprintf(f, "    \"screen_enabled\": [%u, %u],\n",
          (unsigned)p->screen_enabled[0], (unsigned)p->screen_enabled[1]);
  fprintf(f, "    \"screen_windowed\": [%u, %u],\n",
          (unsigned)p->screen_windowed[0], (unsigned)p->screen_windowed[1]);
  fprintf(f, "    \"mosaic_enabled\": %u,\n", (unsigned)p->mosaic_enabled);
  fprintf(f, "    \"mosaic_size\": %u,\n", (unsigned)p->mosaic_size);
  fprintf(f, "    \"obj_tile_adr1\": %u,\n", (unsigned)p->obj_tile_adr1);
  fprintf(f, "    \"obj_tile_adr2\": %u,\n", (unsigned)p->obj_tile_adr2);
  fprintf(f, "    \"obj_size\": %u,\n", (unsigned)p->obj_size);
  fprintf(f, "    \"windows\": [%u, %u, %u, %u, %u],\n",
          (unsigned)p->window1left, (unsigned)p->window1right,
          (unsigned)p->window2left, (unsigned)p->window2right,
          (unsigned)p->windowsel);
  fprintf(f, "    \"windowsel\": %u,\n", (unsigned)p->windowsel);
  fprintf(f, "    \"color_math\": {\n");
  fprintf(f, "      \"clip_mode\": %u,\n", (unsigned)p->clip_mode);
  fprintf(f, "      \"prevent_math_mode\": %u,\n",
          (unsigned)p->prevent_math_mode);
  fprintf(f, "      \"add_subscreen\": %s,\n",
          p->add_subscreen ? "true" : "false");
  fprintf(f, "      \"subtract_color\": %s,\n",
          p->subtract_color ? "true" : "false");
  fprintf(f, "      \"half_color\": %s,\n",
          p->half_color ? "true" : "false");
  fprintf(f, "      \"math_enabled\": %u,\n", (unsigned)p->math_enabled);
  fprintf(f, "      \"fixed_color\": [%u, %u, %u]\n",
          (unsigned)p->fixed_color_r, (unsigned)p->fixed_color_g,
          (unsigned)p->fixed_color_b);
  fprintf(f, "    },\n");
  fprintf(f, "    \"extra\": [%u, %u, %u, %u],\n",
          (unsigned)p->extra_left_right, (unsigned)p->extra_left_cur,
          (unsigned)p->extra_right_cur, (unsigned)p->extra_bottom_cur);
  fprintf(f, "    \"mode7_perspective\": [%g, %g],\n",
          JsonFloat(p->mode7_perspective_low),
          JsonFloat(p->mode7_perspective_high));
  fprintf(f, "    \"bg\": [");
  for (int i = 0; i < 4; i++) {
    const FrameDescBg *bg = &p->bg[i];
    fprintf(f, "%s\n      {\"hscroll\": %u, \"vscroll\": %u, "
               "\"tilemap\": %u, \"tiles\": %u, \"wider\": %s, "
               "\"higher\": %s}",
            i ? "," : "", (unsigned)bg->hscroll, (unsigned)bg->vscroll,
            (unsigned)bg->tilemap_adr, (unsigned)bg->tile_adr,
            bg->wider ? "true" : "false", bg->higher ? "true" : "false");
  }
  fprintf(f, "\n    ],\n");
  fprintf(f, "    \"scroll_prev\": [%u, %u],\n",
          (unsigned)p->scroll_prev, (unsigned)p->scroll_prev2);
  fprintf(f, "    \"m7matrix\": [%d, %d, %d, %d, %d, %d, %d, %d],\n",
          (int)p->m7matrix[0], (int)p->m7matrix[1], (int)p->m7matrix[2],
          (int)p->m7matrix[3], (int)p->m7matrix[4], (int)p->m7matrix[5],
          (int)p->m7matrix[6], (int)p->m7matrix[7]);
  fprintf(f, "    \"m7_flags\": {\"large\": %s, \"char_fill\": %s, "
             "\"x_flip\": %s, \"y_flip\": %s, \"ext_bg\": %s},\n",
          p->m7large_field ? "true" : "false",
          p->m7char_fill ? "true" : "false",
          p->m7x_flip ? "true" : "false",
          p->m7y_flip ? "true" : "false",
          p->m7ext_bg ? "true" : "false");
  fprintf(f, "    \"m7start\": [%d, %d]\n",
          (int)p->m7start_x, (int)p->m7start_y);
  fprintf(f, "  },\n");

  fprintf(f, "  \"refs\": {\"vram_words\": %u, \"cgram_words\": %u, "
             "\"oam_words\": %u, \"vram_fnv1a\": %u},\n",
          (unsigned)desc->vram_words, (unsigned)desc->cgram_words,
          (unsigned)desc->oam_words,
          desc->vram ? (unsigned)FrameDesc_Fnv1a(desc->vram,
              desc->vram_words * sizeof(uint16)) : 0u);

  fprintf(f, "  \"cgram\": [");
  for (uint32 i = 0; i < desc->cgram_words; i++)
    fprintf(f, "%s%u", i ? ", " : "", (unsigned)desc->cgram[i]);
  fprintf(f, "],\n");

  fprintf(f, "  \"oam\": [");
  for (uint32 i = 0; i < desc->oam_words; i++)
    fprintf(f, "%s%u", i ? ", " : "", (unsigned)desc->oam[i]);
  fprintf(f, "],\n");

  fprintf(f, "  \"changes\": [");
  for (uint32 i = 0; i < desc->change_count; i++) {
    const FrameDescChange *c = &desc->changes[i];
    fprintf(f, "%s\n    {\"line\": %u, \"reg\": %u, \"value\": %u, "
               "\"source\": %u}",
            i ? "," : "", (unsigned)c->line, (unsigned)c->reg,
            (unsigned)c->value, (unsigned)c->source);
  }
  fprintf(f, "\n  ],\n");

  fprintf(f, "  \"sprites\": [");
  for (uint32 i = 0; i < desc->sprite_count; i++) {
    const FrameDescSprite *s = &desc->sprites[i];
    fprintf(f, "%s\n    {\"oam_index\": %u, \"x_raw\": %u, \"x\": %d, "
               "\"y\": %u, \"tile\": %u, \"palette\": %u, "
               "\"priority\": %u, \"size\": %u, \"flip_x\": %s, "
               "\"flip_y\": %s, \"owner_kind\": %u, \"owner_index\": %u, "
               "\"owner_type\": %u, \"floor\": %u, \"z\": %d}",
            i ? "," : "", (unsigned)s->oam_index, (unsigned)s->x_raw,
            (int)s->x, (unsigned)s->y, (unsigned)s->tile,
            (unsigned)s->palette, (unsigned)s->priority, (unsigned)s->size,
            s->flip_x ? "true" : "false", s->flip_y ? "true" : "false",
            (unsigned)s->owner_kind, (unsigned)s->owner_index,
            (unsigned)s->owner_type, (unsigned)s->floor, (int)s->z);
  }
  fprintf(f, "\n  ],\n");

  const FrameDescSemantics *sem = &desc->semantics;
  fprintf(f, "  \"semantics\": {\n");
  fprintf(f, "    \"indoors\": %s,\n", sem->indoors ? "true" : "false");
  fprintf(f, "    \"mode7\": %s,\n", sem->mode7 ? "true" : "false");
  fprintf(f, "    \"room\": %u,\n", (unsigned)sem->room);
  fprintf(f, "    \"area\": %u,\n", (unsigned)sem->area);
  fprintf(f, "    \"floor\": %u,\n", (unsigned)sem->floor);
  fprintf(f, "    \"link_floor\": %u,\n", (unsigned)sem->link_floor);
  fprintf(f, "    \"link\": {\"x\": %d, \"y\": %d, \"z\": %d},\n",
          (int)sem->link_x, (int)sem->link_y, (int)sem->link_z);
  fprintf(f, "    \"camera\": {\"x\": %d, \"y\": %d},\n",
          (int)sem->camera_x, (int)sem->camera_y);
  PrintLayerJson(f, &sem->layer[0], "bg1", false);
  PrintLayerJson(f, &sem->layer[1], "bg2", true);
  fprintf(f, "  }\n");
  fprintf(f, "}\n");

  bool ok = fclose(f) == 0;
  return ok;
}

// ---------------------------------------------------------------------------
// --frame-desc-dump: visible tile inventory and PNG sheet
//
// The sheet is meant for HD pack authors, so it is written under .agents/shots
// together with the JSON dump (never into the repository). Tiles are deduped
// by content hash + palette + bit depth + collision attribute; the hash is the
// same canonical FNV-1a used by the hd-tiles plugin.

typedef struct DumpTile {
  uint32 hash;
  uint16 attr;
  uint8 palette;
  uint8 palette_base;
  uint8 bpp;
  uint8 kind;       // 0 = background, 1 = sprite
  uint8 layer;      // BG layer, or 3 for sprites
  uint16 screen_x;
  uint16 screen_y;
  uint32 count;
  uint8 pixels[64]; // color indices, row-major, unflipped
} DumpTile;

typedef struct DumpTileList {
  DumpTile *items;
  int count;
  int capacity;
} DumpTileList;

static bool DumpTileList_Add(DumpTileList *list, const DumpTile *tile) {
  for (int i = 0; i < list->count; i++) {
    const DumpTile *it = &list->items[i];
    if (it->hash == tile->hash && it->palette == tile->palette &&
        it->bpp == tile->bpp && it->kind == tile->kind &&
        it->layer == tile->layer && it->attr == tile->attr) {
      list->items[i].count++;
      return true;
    }
  }
  if (list->count == list->capacity) {
    int capacity = list->capacity ? list->capacity * 2 : 256;
    DumpTile *items = (DumpTile *)realloc(list->items,
                                          (size_t)capacity * sizeof(*items));
    if (items == NULL)
      return false;
    list->items = items;
    list->capacity = capacity;
  }
  list->items[list->count++] = *tile;
  return true;
}

static void DumpTileList_Free(DumpTileList *list) {
  free(list->items);
  memset(list, 0, sizeof(*list));
}

static void DumpReadBgTile(const FrameDesc *desc, int layer, uint16 tile,
                           uint8 out[64], uint16 words[16], int *word_count) {
  int bpp = layer < 2 ? 4 : 2;
  int tile_num = tile & 0x3ff;
  int base = desc->ppu.bg[layer].tile_adr + tile_num * (bpp == 4 ? 16 : 8);
  int n = bpp == 4 ? 16 : 8;
  for (int i = 0; i < n; i++)
    words[i] = desc->vram[(base + i) & 0x7fff];
  *word_count = n;
  memset(out, 0, 64);
  for (int row = 0; row < 8; row++) {
    if (bpp == 4) {
      uint16 p0 = words[row];
      uint16 p1 = words[8 + row];
      for (int col = 0; col < 8; col++) {
        out[row * 8 + col] = (uint8)(((p0 >> col) & 1) |
            (((p0 >> (8 + col)) & 1) << 1) |
            (((p1 >> col) & 1) << 2) |
            (((p1 >> (8 + col)) & 1) << 3));
      }
    } else {
      uint16 p0 = words[row];
      for (int col = 0; col < 8; col++)
        out[row * 8 + col] = (uint8)(((p0 >> col) & 1) |
            (((p0 >> (8 + col)) & 1) << 1));
    }
  }
}

static bool DumpAddBgTile(DumpTileList *list, const FrameDesc *desc, int layer,
                          uint16 tile, int screen_x, int screen_y) {
  DumpTile t;
  uint16 words[16];
  int word_count = 0;
  memset(&t, 0, sizeof(t));
  DumpReadBgTile(desc, layer, tile, t.pixels, words, &word_count);
  t.hash = HdTileHashWords(words, word_count);
  t.bpp = layer < 2 ? 4 : 2;
  t.palette = (uint8)((tile & 0x1c00) >> 10);
  if (desc->ppu.mode == 0)
    t.palette = (uint8)(t.palette + layer * 8);
  t.palette_base = (uint8)(t.palette * (t.bpp == 4 ? 16 : 4));
  t.kind = 0;
  t.layer = (uint8)layer;
  t.attr = 0;
  int col = screen_x >> 3;
  int row = screen_y >> 3;
  const FrameDescTileLayer *tl = &desc->semantics.layer[layer];
  if (layer < 2 && tl->present && col >= 0 && row >= 0 &&
      col < tl->cols && row < tl->rows) {
    t.attr = tl->attr[row * kFrameDesc_MaxTileCols + col];
  }
  t.screen_x = (uint16)screen_x;
  t.screen_y = (uint16)screen_y;
  t.count = 1;
  return DumpTileList_Add(list, &t);
}

static void DumpWalkBgLayer(DumpTileList *list, const FrameDesc *desc,
                            int layer) {
  const FrameDescPpuState *p = &desc->ppu;
  if (!(p->screen_enabled[0] & (1 << layer)) &&
      !(p->screen_enabled[1] & (1 << layer)))
    return;
  int cols = (int)(desc->width + 7) / 8;
  int rows = (int)(desc->height + 7) / 8;
  const FrameDescBg *bg = &p->bg[layer];
  for (int row = 0; row < rows; row++) {
    int world_y = row * 8 + bg->vscroll;
    int map_row = (world_y >> 3) & 0x1f;
    int map_adr = bg->tilemap_adr + (map_row << 5);
    if ((world_y & 0x100) && bg->higher)
      map_adr += bg->wider ? 0x800 : 0x400;
    for (int col = 0; col < cols; col++) {
      int world_x = col * 8 + bg->hscroll;
      int map_col = (world_x >> 3) & 0x1f;
      int adr = map_adr + map_col;
      if ((world_x & 0x100) && bg->wider)
        adr += 0x400;
      uint16 tile = desc->vram[adr & 0x7fff];
      if (!DumpAddBgTile(list, desc, layer, tile, col * 8, row * 8))
        return;
    }
  }
}

static bool DumpAddSpriteTile(DumpTileList *list, const FrameDesc *desc,
                              int palette, int tile_num, int obj_adr,
                              bool use_link_chr, int screen_x, int screen_y) {
  DumpTile t;
  uint16 words[16];
  memset(&t, 0, sizeof(t));
  for (int i = 0; i < 16; i++) {
    if (use_link_chr)
      words[i] = desc->link_chr[(tile_num * 16 + i) & 0x3ff];
    else
      words[i] = desc->vram[(obj_adr + tile_num * 16 + i) & 0x7fff];
  }
  t.hash = HdTileHashWords(words, 16);
  memset(t.pixels, 0, sizeof(t.pixels));
  for (int row = 0; row < 8; row++) {
    uint16 p0 = words[row];
    uint16 p1 = words[8 + row];
    for (int col = 0; col < 8; col++) {
      t.pixels[row * 8 + col] = (uint8)(((p0 >> col) & 1) |
          (((p0 >> (8 + col)) & 1) << 1) |
          (((p1 >> col) & 1) << 2) |
          (((p1 >> (8 + col)) & 1) << 3));
    }
  }
  t.bpp = 4;
  t.palette = (uint8)palette;
  t.palette_base = (uint8)(0x80 + palette * 16);
  t.kind = 1;
  t.layer = 3;
  t.attr = 0;
  t.screen_x = (uint16)screen_x;
  t.screen_y = (uint16)screen_y;
  t.count = 1;
  return DumpTileList_Add(list, &t);
}

static void DumpWalkSprites(DumpTileList *list, const FrameDesc *desc) {
  for (uint32 i = 0; i < desc->sprite_count; i++) {
    const FrameDescSprite *s = &desc->sprites[i];
    if (s->oam_index + 1 >= desc->oam_words)
      continue;
    if ((int)s->oam_index * 2 + 1 >= (int)desc->oam_words)
      continue;
    uint16 oam1 = desc->oam[s->oam_index * 2 + 1];
    int size = s->size;
    int palette = (oam1 & 0xe00) >> 9;
    for (int row_block = 0; row_block < size / 8; row_block++) {
      for (int col_block = 0; col_block < size / 8; col_block++) {
        int used_col = (oam1 & 0x4000) ? size - 1 - col_block * 8
                                       : col_block * 8;
        int source_row = (oam1 & 0x8000) ? size / 8 - 1 - row_block
                                         : row_block;
        int used_tile = ((((oam1 & 0xff) >> 4) + source_row) << 4) |
            ((((oam1 & 0xf) + (used_col >> 3)) & 0xf));
        int obj_adr = (oam1 & 0x100) ? desc->ppu.obj_tile_adr2
                                     : desc->ppu.obj_tile_adr1;
        bool use_link_chr = desc->oam_is_player2 != NULL &&
            desc->link_chr != NULL &&
            desc->oam_is_player2[s->oam_index >> 1] != 0 &&
            !(oam1 & 0x100) && used_tile < 0x40;
        int sx = s->x + col_block * 8;
        int sy = s->y + row_block * 8;
        if (sx <= -8 || sx >= (int)desc->width || sy <= -8 ||
            sy >= (int)desc->height + 8)
          continue;
        if (!DumpAddSpriteTile(list, desc, palette, used_tile, obj_adr,
                               use_link_chr, sx, sy))
          return;
      }
    }
  }
}

static uint32 DumpTileColor(const FrameDesc *desc, const DumpTile *tile,
                            int pixel) {
  if (pixel == 0)
    return 0;
  int index = tile->palette_base + pixel;
  uint16 color = desc->cgram[index & 0xff];
  uint32 r = (color & 0x1f);
  uint32 g = (color >> 5) & 0x1f;
  uint32 b = (color >> 10) & 0x1f;
  r = (r << 3) | (r >> 2);
  g = (g << 3) | (g >> 2);
  b = (b << 3) | (b >> 2);
  return (r << 16) | (g << 8) | b;
}

static bool DumpWriteTileSheet(const char *path, const char *base,
                               const FrameDesc *desc, const DumpTileList *list) {
  enum { kSheetScale = 2, kColumns = 16, kTile = 8 };
  if (list->count == 0)
    return false;
  int rows = (list->count + kColumns - 1) / kColumns;
  int cell = kTile * kSheetScale;
  int width = kColumns * cell;
  int height = rows * cell;
  uint8 *rgb = (uint8 *)malloc((size_t)width * height * 3);
  if (rgb == NULL)
    return false;

  for (int n = 0; n < list->count; n++) {
    const DumpTile *tile = &list->items[n];
    int ox = (n % kColumns) * cell;
    int oy = (n / kColumns) * cell;
    for (int y = 0; y < cell; y++) {
      for (int x = 0; x < cell; x++) {
        int px = x / kSheetScale;
        int py = y / kSheetScale;
        int index = tile->pixels[py * kTile + px];
        uint32 color;
        if (index == 0) {
          uint8 c = ((x / 4 + y / 4) & 1) ? 44 : 24;
          color = ((uint32)c << 16) | ((uint32)c << 8) | c;
        } else {
          color = DumpTileColor(desc, tile, index);
        }
        uint8 *dst = rgb + ((size_t)(oy + y) * width + ox + x) * 3;
        dst[0] = (uint8)(color >> 16);
        dst[1] = (uint8)(color >> 8);
        dst[2] = (uint8)color;
      }
    }
  }

  bool ok = FrameDesc_WritePngRgb(path, width, height, rgb);
  if (ok) {
    char json_path[300];
    snprintf(json_path, sizeof(json_path), "%s.json", base);
    FILE *f = fopen(json_path, "wb");
    if (f == NULL) {
      free(rgb);
      return false;
    }
    fprintf(f, "{\n");
    fprintf(f, "  \"frame\": %u,\n", (unsigned)desc->frame_index);
    fprintf(f, "  \"sheet\": \"%s.png\",\n", base);
    fprintf(f, "  \"tile_size\": 8,\n");
    fprintf(f, "  \"sheet_scale\": %d,\n", kSheetScale);
    fprintf(f, "  \"columns\": %d,\n", kColumns);
    fprintf(f, "  \"indoors\": %s,\n",
            desc->semantics.indoors ? "true" : "false");
    fprintf(f, "  \"room\": %u,\n", (unsigned)desc->semantics.room);
    fprintf(f, "  \"area\": %u,\n", (unsigned)desc->semantics.area);
    fprintf(f, "  \"tiles\": [");
    for (int n = 0; n < list->count; n++) {
      const DumpTile *tile = &list->items[n];
      int sx = (n % kColumns) * cell;
      int sy = (n / kColumns) * cell;
      fprintf(f, "%s\n    {\"index\": %d, \"hash\": %u, \"palette\": %u, "
                 "\"bpp\": %u, \"kind\": \"%s\", \"layer\": %u, "
                 "\"attr\": %u, \"screen\": [%u, %u], \"count\": %u, "
                 "\"sheet\": [%d, %d, %d, %d]}",
              n ? "," : "", n, (unsigned)tile->hash,
              (unsigned)tile->palette, (unsigned)tile->bpp,
              tile->kind ? "sprite" : "bg", (unsigned)tile->layer,
              (unsigned)tile->attr, (unsigned)tile->screen_x,
              (unsigned)tile->screen_y, (unsigned)tile->count, sx, sy,
              cell, cell);
    }
    fprintf(f, "\n  ]\n}\n");
    ok = fclose(f) == 0;
    if (ok)
      printf("frame-desc: wrote %s\n", json_path);
    else
      fprintf(stderr, "frame-desc: failed to write %s\n", json_path);
  }
  if (ok)
    printf("frame-desc: wrote %s\n", path);
  free(rgb);
  return ok;
}

static bool DumpFrameTiles(const char *dir, const FrameDesc *desc) {
  DumpTileList list;
  memset(&list, 0, sizeof(list));
  if (desc->ppu.mode == 1 || desc->ppu.mode == 0) {
    for (int layer = 0; layer < 3; layer++)
      DumpWalkBgLayer(&list, desc, layer);
  }
  DumpWalkSprites(&list, desc);

  char base[256];
  snprintf(base, sizeof(base), "%s/frame-desc-%u-tiles", dir,
           (unsigned)desc->frame_index);
  char path[300];
  snprintf(path, sizeof(path), "%s.png", base);
  bool ok = DumpWriteTileSheet(path, base, desc, &list);
  if (!ok && list.count == 0)
    printf("frame-desc: no visible tiles to export for frame %u\n",
           (unsigned)desc->frame_index);
  DumpTileList_Free(&list);
  return ok;
}

static bool EnsureDumpDirectory(const char *path) {
  // path is .agents/shots; create the two levels separately.
  if (FrameDesc_Mkdir(".agents") != 0 && errno != EEXIST)
    return false;
  if (FrameDesc_Mkdir(path) != 0 && errno != EEXIST)
    return false;
  return true;
}

bool FrameDesc_DumpCurrentFrame(long frame, uint32 render_flags, int width,
                                int height) {
  const char *dir = ".agents/shots";
  if (!EnsureDumpDirectory(dir)) {
    fprintf(stderr, "frame-desc: cannot create %s\n", dir);
    return false;
  }

  size_t size = (size_t)width * 4 * (size_t)height;
  uint8 *pixels = (uint8 *)malloc(size);
  if (pixels == NULL)
    return false;
  memset(pixels, 0, size);
  bool ok = FrameDesc_DrawCurrentFrame(render_flags, width, height, true,
                                       pixels, (size_t)width * 4);
  free(pixels);
  if (!ok) {
    fprintf(stderr, "frame-desc: failed to capture frame %ld\n", frame);
    return false;
  }
  FrameDesc *desc = FrameDesc_Active();
  desc->frame_index = (uint32)frame;

  char base[256];
  snprintf(base, sizeof(base), "%s/frame-desc-%ld", dir, frame);
  char path[300];
  snprintf(path, sizeof(path), "%s.json", base);
  ok = FrameDesc_WriteJson(path, desc);
  if (ok)
    printf("frame-desc: wrote %s\n", path);
  else
    fprintf(stderr, "frame-desc: failed to write %s\n", path);

  for (int layer = 0; layer < 2; layer++) {
    if (!desc->semantics.layer[layer].present)
      continue;
    snprintf(path, sizeof(path), "%s-bg%d.png", base, layer + 1);
    if (FrameDesc_WriteSemanticPng(path, desc, layer)) {
      printf("frame-desc: wrote %s\n", path);
    } else {
      fprintf(stderr, "frame-desc: failed to write %s\n", path);
      ok = false;
    }
  }
  // The tile inventory is extra credit for pack authors; a frame with no
  // tiles (for example mode 7) does not make the JSON dump fail.
  DumpFrameTiles(dir, desc);
  return ok;
}
