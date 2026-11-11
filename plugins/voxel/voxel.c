// Voxel diorama renderer plugin (R-4/R-5, V6 rewrite).
//
// The scene is built from the whole current room/screen in FrameDesc, not
// only from the pixels inside the SNES viewport. Each 8x8 tile is classified
// from the collision semantics and from the drawing itself:
//
//   flat     ground and water: one textured quad, no box.
//   top      roofs and ledges: the artwork is a surface seen from above, so
//            it lies flat at the measured height and the sides take an edge
//            colour instead of stretching the tile art.
//   upright  walls, trees, fences, signs and facades: the artwork is a
//            surface seen face-on, so the south face reconstructs the 2D
//            drawing at full resolution (the fold-up rule). Transparent
//            props are built per pixel, which keeps a bush a bush and the
//            gap between two fence posts open.
//
// Depth comes from a real perspective camera and the depth buffer; there is
// no y-sorting. Mode 7, force blank, mosaic and special screens fall back to
// the reference renderer. The GL context is the offscreen EGL/OpenGL one in
// voxel_gl.c; without GL the plugin is a pass-through.

#include "../../src/mod/render_plugin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "voxel_gl.h"
#include "voxel_profile.h"

enum {
  kVoxelAtlasCols = 128,
  kVoxelAtlasRows = 128,
  kVoxelAtlasTile = 8,
  kVoxelAtlasWidth = kVoxelAtlasCols * kVoxelAtlasTile,   // 1024
  kVoxelAtlasHeight = kVoxelAtlasRows * kVoxelAtlasTile,  // 1024
  kVoxelAtlasSlots = kVoxelAtlasCols * kVoxelAtlasRows,   // 16384
  kVoxelAtlasHashSize = 8192,
  kVoxelMaxCells = kFrameDesc_MaxRoomTiles,
  kVoxelColorCache = 512,
};

typedef enum VoxelCellMode {
  kVoxelCell_Empty = 0,
  kVoxelCell_Flat = 1,
  kVoxelCell_Top = 2,
  kVoxelCell_Upright = 3,
  kVoxelCell_Stack = 4,
} VoxelCellMode;

typedef struct VoxelCell {
  float base_z;
  float top_z;
  int slot;
  int obj;
  uint32_t color_bottom;
  uint32_t color_top;
  uint32_t color_left;
  uint32_t color_right;
  uint8_t mode;
  uint8_t coverage;
  uint8_t solid;
  uint8_t canopy;
  uint8_t crown;  // tile art explicitly drawn as a tree/hedge crown
  uint8_t pit;
  uint8_t prop;  // small standing object embedded in an opaque ground tile
  uint8_t bevel_distance[64];  // chamfer distance per crown pixel, 10 = 1 px
  uint8_t alpha[64];
} VoxelCell;

typedef struct VoxelAtlasHash {
  uint32_t hash;
  int slot;  // -1 = free
} VoxelAtlasHash;

typedef struct VoxelMesh {
  VoxelGlVertex *verts;
  int count;
  int cap;
} VoxelMesh;

// A connected solid object. Green components use sprite stacking: each pixel
// row becomes a horizontal slice at a height measured from the drawing, so a
// canopy keeps its silhouette instead of turning into one dark box.
typedef struct VoxelObject {
  bool stack;
  bool row_stack;  // wide tree row: wavy top instead of one giant dome
  bool bevel_stack;  // light-world crown: rounded height from the edge
  bool prop;       // small upright prop embedded in the ground tile
  bool emitted;    // prop billboard already emitted this build
  int min_row, max_row;
  int min_col, max_col;
  float bevel_radius;
  float base_z;
} VoxelObject;

typedef struct VoxelLight {
  float x, y, z;
  float ambient;
  float diffuse;
} VoxelLight;

typedef struct VoxelState {
  const ZeldaRenderHost *host;
  VoxelGl *gl;
  VoxelProfile *profile;
  VoxelCameraSettings camera;        // target, changed by the controls
  VoxelCameraSettings camera_render; // eased toward camera every frame
  VoxelCameraSettings default_camera;
  VoxelRenderSettings render;
  VoxelLightSettings light_settings;
  VoxelLightSettings default_light_settings;
  VoxelShadowSettings shadow_settings;
  VoxelShadowSettings default_shadow_settings;
  VoxelLight light;
  int preferred_scale;
  bool gl_failed;
  bool fallback_logged;
  bool atlas_overflow_logged;

  const FrameDesc *pane[kRenderPluginMaxPanes];
  int pane_count;

  uint8_t *atlas;
  int slot_count;
  VoxelAtlasHash atlas_hash[kVoxelAtlasHashSize];
  uint32_t color_key[kVoxelColorCache];
  int color_slot[kVoxelColorCache];
  int color_count;
  VoxelCell *cells;
  int cell_cols;
  int cell_rows;
  int cell_count;
  int cell_object_count;
  int interior_link_row;
  VoxelObject objects[kVoxelMaxCells];

  VoxelMesh mesh_bg;
  VoxelMesh *mesh_world;
  bool cache_valid;
  uint64_t cache_key;
  float cache_origin_x, cache_origin_y;
  uint64_t cache_fill_r, cache_fill_g, cache_fill_b, cache_fill_count;
  uint8_t *cache_atlas;
  size_t cache_atlas_size;
  int cache_slot_count;
  int cache_atlas_rows;
  float world_offset_x, world_offset_y;
  uint64_t cache_hits, cache_misses;
  VoxelMesh mesh_backdrop;
  VoxelMesh mesh_overlay;
  VoxelMesh mesh_sprites;
  VoxelMesh mesh_2d;
  bool overlay_subtract;
  bool overlay_half;
  float overlay_scale;
  bool flat_mode;
  uint64_t fill_r, fill_g, fill_b, fill_count;
  bool stats;
  uint64_t stat_frames;
  uint64_t stat_bg, stat_backdrop, stat_overlay, stat_sprites, stat_2d;
  int stat_max_bg;
} VoxelState;

static VoxelState s_voxel_state;
static float s_stack_amplitude = 3.6f;

static uint32_t BackdropColor(const FrameDesc *desc);

static void HostLog(VoxelState *st, int level, const char *message) {
  if (st->host != NULL && st->host->log != NULL)
    st->host->log(st->host, level, message);
}

static void VoxelLog(void *user, int level, const char *message) {
  VoxelState *st = (VoxelState *)user;
  HostLog(st, level, message);
}

static float ClampFloat(float v, float lo, float hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static float EnvFloat(const char *name, float def) {
  const char *s = getenv(name);
  if (s == NULL || *s == 0)
    return def;
  char *end = NULL;
  float v = strtof(s, &end);
  return end != s ? v : def;
}

static bool EnvBool(const char *name, bool def) {
  const char *s = getenv(name);
  if (s == NULL || *s == 0)
    return def;
  if (strcmp(s, "0") == 0 || strcmp(s, "false") == 0 ||
      strcmp(s, "no") == 0)
    return false;
  if (strcmp(s, "1") == 0 || strcmp(s, "true") == 0 ||
      strcmp(s, "yes") == 0)
    return true;
  return def;
}

static int EnvScale(const char *name, int def) {
  int v = (int)EnvFloat(name, (float)def);
  if (v == 2 || v == 4)
    return v;
  return 1;
}

// ---------------------------------------------------------------------------
// Tile decoding and atlas

static uint32_t BrightnessChannel(int v, int brightness) {
  int expanded = (v << 3) | (v >> 2);
  int scaled = expanded * brightness / 15;
  if (scaled < 0)
    scaled = 0;
  if (scaled > 255)
    scaled = 255;
  return (uint32_t)scaled;
}

static uint32_t RgbFromCgram(const FrameDesc *desc, int index) {
  if (desc->cgram == NULL)
    return 0;
  uint16_t color = desc->cgram[index & 0xff];
  uint32_t r = BrightnessChannel(color & 0x1f, desc->ppu.brightness);
  uint32_t g = BrightnessChannel((color >> 5) & 0x1f, desc->ppu.brightness);
  uint32_t b = BrightnessChannel((color >> 10) & 0x1f, desc->ppu.brightness);
  return (r << 16) | (g << 8) | b;
}

static bool DecodeTilePixels(const FrameDesc *desc, const uint16_t *words,
                             int bpp, int palette_base, bool flip_x,
                             bool flip_y, uint8_t *out, uint8_t *alpha,
                             int *coverage, uint32_t *edge_top,
                             uint32_t *edge_bottom, uint32_t *edge_left,
                             uint32_t *edge_right) {
  int count = 0;
  int top_count = 0, bottom_count = 0, left_count = 0, right_count = 0;
  uint32_t top_r = 0, top_g = 0, top_b = 0;
  uint32_t bottom_r = 0, bottom_g = 0, bottom_b = 0;
  uint32_t left_r = 0, left_g = 0, left_b = 0;
  uint32_t right_r = 0, right_g = 0, right_b = 0;
  int top_row = -1, bottom_row = -1;
  uint8_t mask[64];
  memset(mask, 0, sizeof(mask));

  for (int y = 0; y < 8; y++) {
    int row = flip_y ? 7 - y : y;
    for (int x = 0; x < 8; x++) {
      int col = flip_x ? x : 7 - x;
      int pixel;
      if (bpp == 4) {
        uint16_t p0 = words[row];
        uint16_t p1 = words[8 + row];
        pixel = (int)(((p0 >> col) & 1) |
                      (((p0 >> (8 + col)) & 1) << 1) |
                      (((p1 >> col) & 1) << 2) |
                      (((p1 >> (8 + col)) & 1) << 3));
      } else {
        uint16_t p0 = words[row];
        pixel = (int)(((p0 >> col) & 1) |
                      (((p0 >> (8 + col)) & 1) << 1));
      }
      uint8_t *dst = out + ((size_t)y * 8 + x) * 4;
      if (pixel == 0) {
        dst[0] = 0;
        dst[1] = 0;
        dst[2] = 0;
        dst[3] = 0;
        continue;
      }
      count++;
      mask[y * 8 + x] = 1;
      uint32_t rgb = RgbFromCgram(desc, palette_base + pixel);
      uint32_t r = (rgb >> 16) & 0xff;
      uint32_t g = (rgb >> 8) & 0xff;
      uint32_t b = rgb & 0xff;
      dst[0] = (uint8_t)r;
      dst[1] = (uint8_t)g;
      dst[2] = (uint8_t)b;
      dst[3] = 255;
      if (top_row < 0)
        top_row = y;
      bottom_row = y;
      if (y == top_row) {
        top_count++;
        top_r += r;
        top_g += g;
        top_b += b;
      }
      if (x == 0) {
        left_count++;
        left_r += r;
        left_g += g;
        left_b += b;
      }
      if (x == 7) {
        right_count++;
        right_r += r;
        right_g += g;
        right_b += b;
      }
      if (y == 7) {
        bottom_count++;
        bottom_r += r;
        bottom_g += g;
        bottom_b += b;
      }
    }
  }

  // Top/bottom edges follow the actual silhouette, not row 0/7 which may be
  // completely transparent.
  if (top_row >= 0) {
    top_count = 0;
    top_r = top_g = top_b = 0;
    for (int x = 0; x < 8; x++) {
      if (!mask[top_row * 8 + x])
        continue;
      const uint8_t *p = out + ((size_t)top_row * 8 + x) * 4;
      top_count++;
      top_r += p[0];
      top_g += p[1];
      top_b += p[2];
    }
  }
  if (bottom_row >= 0) {
    bottom_count = 0;
    bottom_r = bottom_g = bottom_b = 0;
    for (int x = 0; x < 8; x++) {
      if (!mask[bottom_row * 8 + x])
        continue;
      const uint8_t *p = out + ((size_t)bottom_row * 8 + x) * 4;
      bottom_count++;
      bottom_r += p[0];
      bottom_g += p[1];
      bottom_b += p[2];
    }
  }

  if (alpha != NULL)
    memcpy(alpha, mask, sizeof(mask));
  if (coverage != NULL)
    *coverage = count;
  if (edge_top != NULL) {
    *edge_top = top_count != 0
        ? ((top_r / top_count) << 16) | ((top_g / top_count) << 8) |
              (top_b / top_count)
        : 0;
  }
  if (edge_bottom != NULL) {
    *edge_bottom = bottom_count != 0
        ? ((bottom_r / bottom_count) << 16) |
              ((bottom_g / bottom_count) << 8) | (bottom_b / bottom_count)
        : 0;
  }
  if (edge_left != NULL) {
    *edge_left = left_count != 0
        ? ((left_r / left_count) << 16) | ((left_g / left_count) << 8) |
              (left_b / left_count)
        : 0;
  }
  if (edge_right != NULL) {
    *edge_right = right_count != 0
        ? ((right_r / right_count) << 16) | ((right_g / right_count) << 8) |
              (right_b / right_count)
        : 0;
  }
  return count != 0;
}

static uint32_t HashTile(const uint8_t *tile) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 8 * 8 * 4; i++) {
    h ^= tile[i];
    h *= 16777619u;
  }
  return h;
}

static int AtlasLookup(VoxelState *st, uint32_t hash, const uint8_t *tile) {
  int idx = (int)(hash & (kVoxelAtlasHashSize - 1));
  for (int probe = 0; probe < kVoxelAtlasHashSize; probe++) {
    VoxelAtlasHash *e = &st->atlas_hash[(idx + probe) &
                                       (kVoxelAtlasHashSize - 1)];
    if (e->slot < 0)
      return -1;
    if (e->hash == hash) {
      int x = (e->slot % kVoxelAtlasCols) * kVoxelAtlasTile;
      int y = (e->slot / kVoxelAtlasCols) * kVoxelAtlasTile;
      const uint8_t *src = st->atlas +
          ((size_t)y * kVoxelAtlasWidth + x) * 4;
      bool same = true;
      for (int row = 0; row < 8 && same; row++)
        same = memcmp(src + (size_t)row * kVoxelAtlasWidth * 4,
                      tile + (size_t)row * 8 * 4, 8 * 4) == 0;
      if (same)
        return e->slot;
    }
  }
  return -1;
}

static void AtlasHashInsert(VoxelState *st, uint32_t hash, int slot) {
  int idx = (int)(hash & (kVoxelAtlasHashSize - 1));
  for (int probe = 0; probe < kVoxelAtlasHashSize; probe++) {
    VoxelAtlasHash *e = &st->atlas_hash[(idx + probe) &
                                       (kVoxelAtlasHashSize - 1)];
    if (e->slot < 0) {
      e->hash = hash;
      e->slot = slot;
      return;
    }
  }
}

static int AtlasAdd(VoxelState *st, const uint8_t *tile) {
  uint32_t hash = HashTile(tile);
  int found = AtlasLookup(st, hash, tile);
  if (found >= 0)
    return found;
  if (st->slot_count >= kVoxelAtlasSlots) {
    if (!st->atlas_overflow_logged) {
      HostLog(st, 1, "voxel: tile atlas is full; some tiles are skipped");
      st->atlas_overflow_logged = true;
    }
    return -1;
  }
  int slot = st->slot_count++;
  int x = (slot % kVoxelAtlasCols) * kVoxelAtlasTile;
  int y = (slot / kVoxelAtlasCols) * kVoxelAtlasTile;
  for (int row = 0; row < 8; row++) {
    memcpy(st->atlas + ((size_t)(y + row) * kVoxelAtlasWidth + x) * 4,
           tile + (size_t)row * 8 * 4, (size_t)8 * 4);
  }
  AtlasHashInsert(st, hash, slot);
  return slot;
}

static int AtlasColor(VoxelState *st, uint32_t rgb) {
  rgb &= 0xffffff;
  for (int i = 0; i < st->color_count; i++) {
    if (st->color_key[i] == rgb)
      return st->color_slot[i];
  }
  uint8_t tile[8 * 8 * 4];
  for (int i = 0; i < 64; i++) {
    tile[i * 4 + 0] = (uint8_t)(rgb >> 16);
    tile[i * 4 + 1] = (uint8_t)(rgb >> 8);
    tile[i * 4 + 2] = (uint8_t)rgb;
    tile[i * 4 + 3] = 255;
  }
  int slot = AtlasAdd(st, tile);
  if (slot < 0)
    return -1;
  if (st->color_count < kVoxelColorCache) {
    st->color_key[st->color_count] = rgb;
    st->color_slot[st->color_count] = slot;
    st->color_count++;
  }
  return slot;
}

static void AtlasSlotUV(int slot, float *u0, float *v0, float *u1,
                        float *v1) {
  int x = (slot % kVoxelAtlasCols) * kVoxelAtlasTile;
  int y = (slot / kVoxelAtlasCols) * kVoxelAtlasTile;
  *u0 = ((float)x + 0.5f) / (float)kVoxelAtlasWidth;
  *v0 = ((float)y + 0.5f) / (float)kVoxelAtlasHeight;
  *u1 = ((float)x + 7.5f) / (float)kVoxelAtlasWidth;
  *v1 = ((float)y + 7.5f) / (float)kVoxelAtlasHeight;
}

static bool FetchBgTile(const FrameDesc *desc, int layer, int screen_x,
                        int screen_y, int hscroll, int vscroll,
                        uint16_t *words, int *bpp, int *palette,
                        bool *flip_x, bool *flip_y, int *tile_num) {
  const FrameDescBg *bg = &desc->ppu.bg[layer];
  int depth = layer < 2 ? 4 : 2;
  int wx = screen_x + hscroll;
  int wy = screen_y + vscroll;
  int map_adr = bg->tilemap_adr +
      ((((wy >> 3) & 0x1f) << 5) | ((wx >> 3) & 0x1f));
  if ((wx & 0x100) && bg->wider)
    map_adr += 0x400;
  if ((wy & 0x100) && bg->higher)
    map_adr += bg->wider ? 0x800 : 0x400;
  uint16_t tile = desc->vram[map_adr & 0x7fff];
  *palette = (tile & 0x1c00) >> 10;
  *flip_x = (tile & 0x4000) != 0;
  *flip_y = (tile & 0x8000) != 0;
  *tile_num = tile & 0x3ff;
  *bpp = depth;
  int base = bg->tile_adr + (*tile_num) * (depth == 4 ? 16 : 8);
  int word_count = depth == 4 ? 16 : 8;
  for (int i = 0; i < word_count; i++)
    words[i] = desc->vram[(base + i) & 0x7fff];
  return true;
}

static bool FetchSpriteTile(const FrameDesc *desc,
                            const FrameDescSprite *s, int sub_x, int sub_y,
                            uint16_t *words, int *palette_base,
                            bool *flip_x, bool *flip_y, int *used_tile) {
  if (s->oam_index >= 128 ||
      (int)s->oam_index * 2 + 1 >= (int)desc->oam_words)
    return false;
  uint16_t oam1 = desc->oam[s->oam_index * 2 + 1];
  int obj_adr = (oam1 & 0x100) ? desc->ppu.obj_tile_adr2
                               : desc->ppu.obj_tile_adr1;
  int size = s->size;
  int src_x = s->flip_x ? size - 8 - sub_x : sub_x;
  int src_y = s->flip_y ? size - 8 - sub_y : sub_y;
  int tile = ((((s->tile >> 4) + (src_y >> 3)) << 4) |
              (((s->tile & 0xf) + (src_x >> 3)) & 0xf));
  bool use_link_chr = desc->oam_is_player2 != NULL &&
      desc->link_chr != NULL &&
      desc->oam_is_player2[s->oam_index >> 1] != 0 &&
      !(oam1 & 0x100) && tile < 0x40;
  for (int i = 0; i < 16; i++) {
    if (use_link_chr)
      words[i] = desc->link_chr[(tile * 16 + i) & 0x3ff];
    else
      words[i] = desc->vram[(obj_adr + tile * 16 + i) & 0x7fff];
  }
  *palette_base = 0x80 + 16 * s->palette;
  *flip_x = s->flip_x;
  *flip_y = s->flip_y;
  *used_tile = tile;
  return true;
}

// ---------------------------------------------------------------------------
// Geometry primitives

static bool MeshReserve(VoxelMesh *mesh, int needed) {
  if (mesh->cap >= needed)
    return true;
  int cap = mesh->cap != 0 ? mesh->cap : 8192;
  while (cap < needed)
    cap *= 2;
  VoxelGlVertex *p = (VoxelGlVertex *)realloc(
      mesh->verts, (size_t)cap * sizeof(*mesh->verts));
  if (p == NULL)
    return false;
  mesh->verts = p;
  mesh->cap = cap;
  return true;
}

static bool AddVertexMesh(VoxelMesh *mesh, float x, float y, float z, float u,
                          float v, float shade, float depth) {
  if (!MeshReserve(mesh, mesh->count + 1))
    return false;
  VoxelGlVertex *out = &mesh->verts[mesh->count++];
  out->x = x;
  out->y = y;
  out->z = z;
  out->u = u;
  out->v = v;
  out->shade = shade;
  out->depth = depth;
  return true;
}

static bool QuadMesh(VoxelMesh *mesh, float ax, float ay, float az, float au,
                     float av, float ad, float bx, float by, float bz, float bu,
                     float bv, float bd, float cx, float cy, float cz, float cu,
                     float cv, float cd, float dx, float dy, float dz, float du,
                     float dv, float dd, float shade) {
  if (!AddVertexMesh(mesh, ax, ay, az, au, av, shade, ad) ||
      !AddVertexMesh(mesh, bx, by, bz, bu, bv, shade, bd) ||
      !AddVertexMesh(mesh, cx, cy, cz, cu, cv, shade, cd) ||
      !AddVertexMesh(mesh, ax, ay, az, au, av, shade, ad) ||
      !AddVertexMesh(mesh, cx, cy, cz, cu, cv, shade, cd) ||
      !AddVertexMesh(mesh, dx, dy, dz, du, dv, shade, dd))
    return false;
  return true;
}

// Vertical quad whose lower vertices are darkened a little. The gradient
// makes a flat cliff face read as a wall instead of a pasted colour plate
// without bringing back the stretched tile art.
static bool SideQuad(VoxelMesh *mesh, float ax, float ay, float az, float au,
                     float av, float ad, float bx, float by, float bz,
                     float bu, float bv, float bd, float cx, float cy,
                     float cz, float cu, float cv, float cd, float dx,
                     float dy, float dz, float du, float dv, float dd,
                     float top_shade, float bottom_shade) {
  if (!AddVertexMesh(mesh, ax, ay, az, au, av, bottom_shade, ad) ||
      !AddVertexMesh(mesh, bx, by, bz, bu, bv, bottom_shade, bd) ||
      !AddVertexMesh(mesh, cx, cy, cz, cu, cv, top_shade, cd) ||
      !AddVertexMesh(mesh, ax, ay, az, au, av, bottom_shade, ad) ||
      !AddVertexMesh(mesh, cx, cy, cz, cu, cv, top_shade, cd) ||
      !AddVertexMesh(mesh, dx, dy, dz, du, dv, top_shade, dd))
    return false;
  return true;
}

// Tall faces repeat the tile art instead of squashing the whole 8x8 into the
// face. Each 8-pixel band keeps the source tile's aspect; the last band is a
// partial wrap. Faces up to one and a half tiles keep the single quad so a
// low step or a small sign still shows the whole drawing.
static bool SideQuadWrapped(VoxelMesh *mesh, float ax, float ay, float az,
                            float au, float av, float ad, float bx, float by,
                            float bz, float bu, float bv, float bd, float cx,
                            float cy, float cz, float cu, float cv, float cd,
                            float dx, float dy, float dz, float du, float dv,
                            float dd, float top_shade, float bottom_shade) {
  float height = cz - az;
  if (height <= 12.5f)
    return SideQuad(mesh, ax, ay, az, au, av, ad, bx, by, bz, bu, bv, bd, cx,
                    cy, cz, cu, cv, cd, dx, dy, dz, du, dv, dd, top_shade,
                    bottom_shade);
  float pos = 0.0f;
  while (pos < height - 0.001f) {
    float phase = fmodf(pos, 8.0f);
    float seg = 8.0f - phase;
    if (seg > height - pos)
      seg = height - pos;
    float z_hi = cz - pos;
    float z_lo = z_hi - seg;
    float t_hi = (z_hi - az) / height;
    float t_lo = (z_lo - az) / height;
    float shade_hi = bottom_shade + (top_shade - bottom_shade) * t_hi;
    float shade_lo = bottom_shade + (top_shade - bottom_shade) * t_lo;
    float v_hi = dv + (phase / 8.0f) * (av - dv);
    float v_lo = dv + ((phase + seg) / 8.0f) * (av - dv);
    if (!AddVertexMesh(mesh, ax, ay, z_lo, au, v_lo, shade_lo, ad) ||
        !AddVertexMesh(mesh, bx, by, z_lo, bu, v_lo, shade_lo, bd) ||
        !AddVertexMesh(mesh, cx, cy, z_hi, cu, v_hi, shade_hi, cd) ||
        !AddVertexMesh(mesh, ax, ay, z_lo, au, v_lo, shade_lo, ad) ||
        !AddVertexMesh(mesh, cx, cy, z_hi, cu, v_hi, shade_hi, cd) ||
        !AddVertexMesh(mesh, dx, dy, z_hi, du, v_hi, shade_hi, dd))
      return false;
    pos += seg;
  }
  return true;
}

// A partial cliff tile can have a one-pixel silhouette even when most of the
// tile is opaque. Build its exposed wall in the same horizontal pixel runs as
// the artwork instead of extending the whole 8x8 tile edge.
static bool SidePixelWrapped(VoxelState *st, const VoxelCell *cell, int x,
                             int y, int side, int along, float base,
                             float top, float top_shade,
                             float bottom_shade) {
  float u0, v0, u1, v1;
  AtlasSlotUV(cell->slot, &u0, &v0, &u1, &v1);
  float du = (u1 - u0) / 8.0f;
  float pu0 = u0 + (float)along * du;
  float pu1 = pu0 + du;
  float yy = (float)y;
  float xx = (float)x;
  switch (side) {
  case 0:  // south
    return SideQuadWrapped(&st->mesh_bg, xx + along, yy + 8.0f, base,
                           pu0, v1, 0.0f, xx + along + 1.0f, yy + 8.0f,
                           base, pu1, v1, 0.0f, xx + along + 1.0f, yy + 8.0f,
                           top, pu1, v0, 0.0f, xx + along, yy + 8.0f, top,
                           pu0, v0, 0.0f, top_shade, bottom_shade);
  case 1:  // north
    return SideQuadWrapped(&st->mesh_bg, xx + along, yy, base, pu0, v1,
                           0.0f, xx + along + 1.0f, yy, base, pu1, v1, 0.0f,
                           xx + along + 1.0f, yy, top, pu1, v0, 0.0f,
                           xx + along, yy, top, pu0, v0, 0.0f, top_shade,
                           bottom_shade);
  case 2:  // west
    return SideQuadWrapped(&st->mesh_bg, xx, yy + along, base, pu0, v1,
                           0.0f, xx, yy + along + 1.0f, base, pu1, v1,
                           0.0f, xx, yy + along + 1.0f, top, pu1, v0, 0.0f,
                           xx, yy + along, top, pu0, v0, 0.0f, top_shade,
                           bottom_shade);
  default:  // east
    return SideQuadWrapped(&st->mesh_bg, xx + 8.0f, yy + along, base, pu0,
                           v1, 0.0f, xx + 8.0f, yy + along + 1.0f, base,
                           pu1, v1, 0.0f, xx + 8.0f, yy + along + 1.0f, top,
                           pu1, v0, 0.0f, xx + 8.0f, yy + along, top, pu0,
                           v0, 0.0f, top_shade, bottom_shade);
  }
}

static float TopBevelHeight(float top, float base, bool south, bool north,
                            bool west, bool east, int px, int py) {
  float ramp = 0.0f;
  if (south) {
    float t = ClampFloat(((float)py - 6.0f) * 0.5f, 0.0f, 1.0f);
    if (t > ramp)
      ramp = t;
  }
  if (north) {
    float t = ClampFloat((2.0f - (float)py) * 0.5f, 0.0f, 1.0f);
    if (t > ramp)
      ramp = t;
  }
  if (west) {
    float t = ClampFloat((2.0f - (float)px) * 0.5f, 0.0f, 1.0f);
    if (t > ramp)
      ramp = t;
  }
  if (east) {
    float t = ClampFloat(((float)px - 6.0f) * 0.5f, 0.0f, 1.0f);
    if (t > ramp)
      ramp = t;
  }
  float depth = fminf(2.0f, (top - base) * 0.25f);
  return top - ramp * depth;
}

static bool SidePixelBeveled(VoxelState *st, const VoxelCell *cell, int x,
                             int y, int side, int along, float base,
                             float top0, float top1, float top_shade,
                             float bottom_shade) {
  float u0, v0, u1, v1;
  AtlasSlotUV(cell->slot, &u0, &v0, &u1, &v1);
  float du = (u1 - u0) / 8.0f;
  float pu0 = u0 + (float)along * du;
  float pu1 = pu0 + du;
  float xx = (float)x;
  float yy = (float)y;
  if (side == 0)
    return SideQuad(&st->mesh_bg, xx + along, yy + 8.0f, base, pu0, v1, 0.0f,
                    xx + along + 1.0f, yy + 8.0f, base, pu1, v1, 0.0f,
                    xx + along + 1.0f, yy + 8.0f, top1, pu1, v0, 0.0f,
                    xx + along, yy + 8.0f, top0, pu0, v0, 0.0f, top_shade,
                    bottom_shade);
  if (side == 1)
    return SideQuad(&st->mesh_bg, xx + along, yy, base, pu0, v1, 0.0f,
                    xx + along + 1.0f, yy, base, pu1, v1, 0.0f,
                    xx + along + 1.0f, yy, top1, pu1, v0, 0.0f,
                    xx + along, yy, top0, pu0, v0, 0.0f, top_shade,
                    bottom_shade);
  if (side == 2)
    return SideQuad(&st->mesh_bg, xx, yy + along, base, pu0, v1, 0.0f, xx,
                    yy + along + 1.0f, base, pu1, v1, 0.0f, xx,
                    yy + along + 1.0f, top1, pu1, v0, 0.0f, xx,
                    yy + along, top0, pu0, v0, 0.0f, top_shade,
                    bottom_shade);
  return SideQuad(&st->mesh_bg, xx + 8.0f, yy + along, base, pu0, v1, 0.0f,
                  xx + 8.0f, yy + along + 1.0f, base, pu1, v1, 0.0f,
                  xx + 8.0f, yy + along + 1.0f, top1, pu1, v0, 0.0f,
                  xx + 8.0f, yy + along, top0, pu0, v0, 0.0f, top_shade,
                  bottom_shade);
}

static uint64_t HashBytes(uint64_t h, const void *data, size_t length) {
  const uint8_t *p = (const uint8_t *)data;
  for (size_t i = 0; i < length; i++) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

// A single-room mesh cache key. The mesh itself is in the frame's screen
// coordinates, so the key covers everything that changes its shape: the room
// map, the tile/palette data and the room window. Sub-tile scroll is not in
// the key; it is applied as a uniform offset on reuse.
static uint64_t RoomCacheKey(const VoxelState *st, const FrameDesc *desc,
                             int layer, int cols, int rows, int extra) {
  const FrameDescRoomLayer *room = &desc->semantics.room_layer[layer];
  uint64_t h = 1469598103934665603ull;
  h = HashBytes(h, &desc->semantics.area, sizeof(desc->semantics.area));
  h = HashBytes(h, &desc->semantics.room, sizeof(desc->semantics.room));
  h = HashBytes(h, &desc->semantics.floor, sizeof(desc->semantics.floor));
  h = HashBytes(h, &desc->semantics.link_floor,
                sizeof(desc->semantics.link_floor));
  h = HashBytes(h, &desc->semantics.indoors,
                sizeof(desc->semantics.indoors));
  h = HashBytes(h, &desc->width, sizeof(desc->width));
  h = HashBytes(h, &desc->height, sizeof(desc->height));
  h = HashBytes(h, &desc->ppu.brightness, sizeof(desc->ppu.brightness));
  h = HashBytes(h, &desc->ppu.screen_enabled[0],
                sizeof(desc->ppu.screen_enabled[0]));
  h = HashBytes(h, &extra, sizeof(extra));
  h = HashBytes(h, &layer, sizeof(layer));
  h = HashBytes(h, &cols, sizeof(cols));
  h = HashBytes(h, &rows, sizeof(rows));
  h = HashBytes(h, &room->base_tile_x, sizeof(room->base_tile_x));
  h = HashBytes(h, &room->base_tile_y, sizeof(room->base_tile_y));
  h = HashBytes(h, &room->screen_origin_tile_x,
                sizeof(room->screen_origin_tile_x));
  h = HashBytes(h, &room->screen_origin_tile_y,
                sizeof(room->screen_origin_tile_y));
  h = HashBytes(h, &desc->ppu.bg[layer].tilemap_adr,
                sizeof(desc->ppu.bg[layer].tilemap_adr));
  h = HashBytes(h, &desc->ppu.bg[layer].tile_adr,
                sizeof(desc->ppu.bg[layer].tile_adr));
  h = HashBytes(h, &st->render.mesh_detail, sizeof(st->render.mesh_detail));
  for (int r = 0; r < rows; r++) {
    int idx = r * kFrameDesc_MaxRoomDim;
    h = HashBytes(h, &room->attr[idx], (size_t)cols);
    h = HashBytes(h, &room->raw[idx], (size_t)cols * sizeof(room->raw[0]));
  }
  if (desc->vram != NULL)
    h = HashBytes(h, desc->vram, 0x8000u * sizeof(desc->vram[0]));
  if (desc->cgram != NULL)
    h = HashBytes(h, desc->cgram, 256u * sizeof(desc->cgram[0]));
  return h;
}

static bool RgbIsFoliage(uint32_t rgb) {
  int r = (int)((rgb >> 16) & 0xff);
  int g = (int)((rgb >> 8) & 0xff);
  int b = (int)(rgb & 0xff);
  return g > 30 && g > r + 8 && g > b + 8;
}

static void RgbToHsv(const uint8_t *p, float *h, float *s, float *v) {
  int r = p[0], g = p[1], b = p[2];
  int max = r > g ? (r > b ? r : b) : (g > b ? g : b);
  int min = r < g ? (r < b ? r : b) : (g < b ? g : b);
  int delta = max - min;
  *v = (float)max / 255.0f;
  *s = max != 0 ? (float)delta / (float)max : 0.0f;
  if (delta == 0) {
    *h = 0.0f;
  } else if (max == r) {
    *h = 60.0f * (float)(g - b) / (float)delta;
  } else if (max == g) {
    *h = 60.0f * (2.0f + (float)(b - r) / (float)delta);
  } else {
    *h = 60.0f * (4.0f + (float)(r - g) / (float)delta);
  }
  if (*h < 0.0f)
    *h += 360.0f;
}

// Split one opaque tile into ground and a small painted prop. The corner
// pixel is a ground sample; ground colours share its hue/value family, so a
// gravestone, post or sign stands out even when it covers most of the tile.
static bool BuildPropMask(const uint8_t *pixels, int seed, uint8_t *mask) {
  const uint8_t *sp = pixels + seed * 4;
  if (sp[3] == 0)
    return false;
  float sh, ss, sv;
  RgbToHsv(sp, &sh, &ss, &sv);
  uint8_t fg[64];
  int fg_count = 0;
  for (int i = 0; i < 64; i++) {
    const uint8_t *p = pixels + i * 4;
    fg[i] = 0;
    if (p[3] == 0)
      continue;
    float h, s, v;
    RgbToHsv(p, &h, &s, &v);
    bool ground;
    if (ss < 0.18f && s < 0.18f) {
      ground = fabsf(v - sv) <= 0.35f;
    } else {
      float dh = fabsf(h - sh);
      if (dh > 180.0f)
        dh = 360.0f - dh;
      ground = ss >= 0.18f && dh <= 26.0f && fabsf(v - sv) <= 0.30f;
    }
    if (!ground) {
      fg[i] = 1;
      fg_count++;
    }
  }
  if (fg_count < 6)
    return false;

  // Keep the largest 8-connected component. A tile-wide shading band is
  // not a prop: its box is too wide or too tall.
  bool seen[64];
  memset(seen, 0, sizeof(seen));
  int best_area = 0, best_min_x = 0, best_max_x = 0;
  int best_min_y = 0, best_max_y = 0;
  uint8_t best_mask[64];
  memset(best_mask, 0, sizeof(best_mask));
  for (int start = 0; start < 64; start++) {
    if (!fg[start] || seen[start])
      continue;
    int queue[64];
    int head = 0, tail = 0;
    queue[tail++] = start;
    seen[start] = true;
    uint8_t comp[64];
    memset(comp, 0, sizeof(comp));
    int area = 0, min_x = 7, max_x = 0, min_y = 7, max_y = 0;
    while (head < tail) {
      int idx = queue[head++];
      int x = idx & 7, y = idx >> 3;
      comp[idx] = 1;
      area++;
      if (x < min_x)
        min_x = x;
      if (x > max_x)
        max_x = x;
      if (y < min_y)
        min_y = y;
      if (y > max_y)
        max_y = y;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (dx == 0 && dy == 0)
            continue;
          int nx = x + dx, ny = y + dy;
          if (nx < 0 || nx > 7 || ny < 0 || ny > 7)
            continue;
          int ni = ny * 8 + nx;
          if (!fg[ni] || seen[ni])
            continue;
          seen[ni] = true;
          queue[tail++] = ni;
        }
      }
    }
    if (area > best_area) {
      best_area = area;
      best_min_x = min_x;
      best_max_x = max_x;
      best_min_y = min_y;
      best_max_y = max_y;
      memcpy(best_mask, comp, sizeof(best_mask));
    }
  }
  int w = best_max_x - best_min_x + 1;
  int h = best_max_y - best_min_y + 1;
  if (best_area < 8 || best_area > 56 || w < 2 || w > 7 || h < 4 || h > 8)
    return false;
  if (best_min_x == 0 && best_max_x == 7 && best_min_y == 0 &&
      best_max_y == 7)
    return false;
  memcpy(mask, best_mask, sizeof(best_mask));
  return true;
}

static bool ExtractEmbeddedProp(const uint8_t *pixels, uint8_t *mask) {
  static const int kSeeds[4] = {7 * 8 + 0, 7 * 8 + 7, 0, 7};
  for (int i = 0; i < 4; i++) {
    if (BuildPropMask(pixels, kSeeds[i], mask))
      return true;
  }
  return false;
}

static VoxelLight MakeLight(const VoxelLightSettings *settings) {
  VoxelLight light;
  float az = settings->azimuth_deg * 3.14159265358979323846f / 180.0f;
  float el = settings->elevation_deg * 3.14159265358979323846f / 180.0f;
  float c = cosf(el);
  light.x = c * cosf(az);
  light.y = c * sinf(az);
  light.z = sinf(el);
  light.ambient = ClampFloat(settings->ambient, 0.0f, 1.5f);
  light.diffuse = ClampFloat(settings->diffuse, 0.0f, 1.5f);
  return light;
}

// ---------------------------------------------------------------------------
// Room grid

static float FloorBaseZ(const FrameDesc *desc,
                        const VoxelRenderSettings *render, int layer) {
  if (desc->semantics.indoors) {
    float step = render->floor_step * 8.0f;
    if (layer == 0)
      return desc->semantics.link_floor ? 0.0f : step;
    return desc->semantics.link_floor ? -step : 0.0f;
  }
  return layer == 0 ? 0.05f * 8.0f : 0.0f;
}

static bool RoomCellScreen(const FrameDesc *desc, int layer,
                           const FrameDescRoomLayer *room, int c, int r,
                           int extra, int *sx, int *sy) {
  int ox = (room->base_tile_x + c) - room->screen_origin_tile_x;
  int oy = (room->base_tile_y + r) - room->screen_origin_tile_y;
  // The screen origin can fall in the middle of a tile; keep the sub-tile
  // scroll so the terrain lines up with the 2D camera instead of snapping to
  // the nearest tile edge.
  int sub_x = (int)desc->ppu.bg[layer].hscroll & 7;
  int sub_y = (int)desc->ppu.bg[layer].vscroll & 7;
  *sx = extra - sub_x + ox * 8;
  *sy = -sub_y + oy * 8;
  return true;
}

static bool DecodeRoomTile(VoxelState *st, const FrameDesc *desc, int layer,
                           const FrameDescRoomLayer *room, int c, int r,
                           int extra, uint8_t *pixels, uint8_t *alpha,
                           int *coverage, uint32_t *edge_top,
                           uint32_t *edge_bottom, uint32_t *edge_left,
                           uint32_t *edge_right, int *tile_num) {
  (void)st;
  int sx, sy;
  RoomCellScreen(desc, layer, room, c, r, extra, &sx, &sy);
  int line = sy;
  if (line < 0)
    line = 0;
  if (line >= (int)desc->height)
    line = (int)desc->height - 1;
  uint16_t hscroll, vscroll;
  FrameDesc_GetBgScrollAtLine(desc, layer, line, &hscroll, &vscroll);
  uint16_t words[16];
  int bpp = 4, palette = 0;
  bool flip_x = false, flip_y = false;
  FetchBgTile(desc, layer, -extra + sx, sy, hscroll, vscroll, words, &bpp,
              &palette, &flip_x, &flip_y, tile_num);
  int palette_base = palette * (bpp == 4 ? 16 : 4);
  return DecodeTilePixels(desc, words, bpp, palette_base, flip_x, flip_y,
                          pixels, alpha, coverage, edge_top, edge_bottom,
                          edge_left, edge_right);
}

static void ClearCells(VoxelState *st, int cols, int rows) {
  st->cell_cols = cols;
  st->cell_rows = rows;
  st->cell_count = 0;
  st->cell_object_count = 0;
  if (cols > kFrameDesc_MaxRoomDim)
    cols = kFrameDesc_MaxRoomDim;
  if (rows > kFrameDesc_MaxRoomDim)
    rows = kFrameDesc_MaxRoomDim;
  memset(st->cells, 0,
         (size_t)kFrameDesc_MaxRoomTiles * sizeof(*st->cells));
  for (int i = 0; i < kFrameDesc_MaxRoomTiles; i++)
    st->cells[i].obj = -1;
}

// Split a solid tall object into a top band (roof/canopy) and a facade band.
// Solid objects are grouped into connected components. Green components
// (canopies, hedges, bushes) use sprite stacking and keep their drawn
// silhouette; the rest split into a top band (roof/canopy) and a facade.
static uint8_t *CrownDistanceSlot(VoxelState *st, int object_id, int x, int y) {
  if (x < 0 || y < 0 || x >= st->cell_cols * 8 || y >= st->cell_rows * 8)
    return NULL;
  int c = x >> 3;
  int r = y >> 3;
  int pixel = (y & 7) * 8 + (x & 7);
  VoxelCell *cell = &st->cells[r * kFrameDesc_MaxRoomDim + c];
  if (cell->obj != object_id || cell->mode != kVoxelCell_Stack ||
      !cell->crown)
    return NULL;
  return &cell->bevel_distance[pixel];
}

static int CrownDistanceAt(VoxelState *st, int object_id, int x, int y) {
  uint8_t *distance = CrownDistanceSlot(st, object_id, x, y);
  return distance != NULL ? *distance : 0;
}

static uint8_t CrownDistancePlus(int distance, int amount) {
  distance += amount;
  return (uint8_t)(distance < 255 ? distance : 255);
}

// Chamfer distance across the connected crown's cell silhouette. Unlike a
// four-way cell flood, the diagonal weights round corners without smoothing
// or changing the drawn pixel mask.
static void BuildCrownDistanceField(VoxelState *st, int object_id,
                                    int min_row, int max_row, int min_col,
                                    int max_col) {
  int x0 = min_col * 8;
  int y0 = min_row * 8;
  int x1 = (max_col + 1) * 8;
  int y1 = (max_row + 1) * 8;
  for (int r = min_row; r <= max_row; r++) {
    for (int c = min_col; c <= max_col; c++) {
      VoxelCell *cell = &st->cells[r * kFrameDesc_MaxRoomDim + c];
      if (cell->obj == object_id && cell->mode == kVoxelCell_Stack &&
          cell->crown)
        memset(cell->bevel_distance, 255, sizeof(cell->bevel_distance));
    }
  }
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      uint8_t *distance = CrownDistanceSlot(st, object_id, x, y);
      if (distance == NULL)
        continue;
      int best = *distance;
      int candidate = CrownDistancePlus(CrownDistanceAt(st, object_id,
                                                        x - 1, y), 10);
      if (candidate < best)
        best = candidate;
      candidate = CrownDistancePlus(CrownDistanceAt(st, object_id, x, y - 1),
                                    10);
      if (candidate < best)
        best = candidate;
      candidate = CrownDistancePlus(CrownDistanceAt(st, object_id, x - 1,
                                                    y - 1), 14);
      if (candidate < best)
        best = candidate;
      candidate = CrownDistancePlus(CrownDistanceAt(st, object_id, x + 1,
                                                    y - 1), 14);
      if (candidate < best)
        best = candidate;
      *distance = (uint8_t)best;
    }
  }
  for (int y = y1 - 1; y >= y0; y--) {
    for (int x = x1 - 1; x >= x0; x--) {
      uint8_t *distance = CrownDistanceSlot(st, object_id, x, y);
      if (distance == NULL)
        continue;
      int best = *distance;
      int candidate = CrownDistancePlus(CrownDistanceAt(st, object_id,
                                                        x + 1, y), 10);
      if (candidate < best)
        best = candidate;
      candidate = CrownDistancePlus(CrownDistanceAt(st, object_id, x, y + 1),
                                    10);
      if (candidate < best)
        best = candidate;
      candidate = CrownDistancePlus(CrownDistanceAt(st, object_id, x + 1,
                                                    y + 1), 14);
      if (candidate < best)
        best = candidate;
      candidate = CrownDistancePlus(CrownDistanceAt(st, object_id, x - 1,
                                                    y + 1), 14);
      if (candidate < best)
        best = candidate;
      *distance = (uint8_t)best;
    }
  }
}

static void AssignObjectBands(VoxelState *st, const FrameDesc *desc) {
  // Lost Woods and Eastern Palace repeat a shaded crown across neighbouring
  // cells, so their whole connected green mass can use the rounded edge
  // height. Other areas keep the previous per-tree cap and bounding-box dome.
  bool light_canopy = !desc->semantics.indoors &&
      (desc->semantics.area == 0 || desc->semantics.area == 41);
  int cols = st->cell_cols;
  int rows = st->cell_rows;
  int stack[kVoxelMaxCells];
  int members[kVoxelMaxCells];
  bool seen[kVoxelMaxCells];
  memset(seen, 0, sizeof(seen));
  int next_object = 0;

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      int start = r * kFrameDesc_MaxRoomDim + c;
      VoxelCell *sc = &st->cells[start];
      if (seen[start] || (!sc->solid && !sc->crown) ||
          (sc->mode != kVoxelCell_Upright && sc->mode != kVoxelCell_Top))
        continue;
      int top = 0, member_count = 0;
      int min_row = r, max_row = r, min_col = c, max_col = c;
      stack[top++] = start;
      seen[start] = true;
      while (top > 0) {
        int idx = stack[--top];
        members[member_count++] = idx;
        int rr = idx / kFrameDesc_MaxRoomDim;
        int cc = idx % kFrameDesc_MaxRoomDim;
        if (rr < min_row)
          min_row = rr;
        if (rr > max_row)
          max_row = rr;
        if (cc < min_col)
          min_col = cc;
        if (cc > max_col)
          max_col = cc;
        static const int kOff[4] = {-1, 1, -kFrameDesc_MaxRoomDim,
                                    kFrameDesc_MaxRoomDim};
        for (int k = 0; k < 4; k++) {
          int ni = idx + kOff[k];
          if (k == 0 && cc == 0)
            continue;
          if (k == 1 && cc == cols - 1)
            continue;
          if (ni < 0 || ni >= rows * kFrameDesc_MaxRoomDim)
            continue;
          if (seen[ni])
            continue;
          VoxelCell *n = &st->cells[ni];
          if ((!n->solid && !n->crown) ||
              (n->mode != kVoxelCell_Upright && n->mode != kVoxelCell_Top))
            continue;
          // Tree canopies are attached to the terrain in the tile map but
          // are separate volumes. Keep them out of the collision object so
          // a row of trees does not become one giant box (or drag a cliff
          // into the canopy stack).
          if (n->canopy != sc->canopy || n->prop != sc->prop)
            continue;
          if (light_canopy && n->crown != sc->crown)
            continue;
          if (sc->canopy && !(light_canopy && sc->crown)) {
            // One dark-world tree is an 8x8-cell canopy block. Neighbouring
            // trees touch in the map, so cap the component box to keep each
            // tree's dome separate instead of flattening a whole grove.
            int nr = ni / kFrameDesc_MaxRoomDim;
            int nc = ni % kFrameDesc_MaxRoomDim;
            int nmin_row = nr < min_row ? nr : min_row;
            int nmax_row = nr > max_row ? nr : max_row;
            int nmin_col = nc < min_col ? nc : min_col;
            int nmax_col = nc > max_col ? nc : max_col;
            if (nmax_row - nmin_row + 1 > 8 ||
                nmax_col - nmin_col + 1 > 8)
              continue;
          }
          seen[ni] = true;
          stack[top++] = ni;
        }
      }

      int object_rows = max_row - min_row + 1;
      int object_cols = max_col - min_col + 1;
      int canopy_count = 0;
      int crown_count = 0;
      int prop_count = 0;
      for (int i = 0; i < member_count; i++) {
        VoxelCell *cell = &st->cells[members[i]];
        if (cell->canopy)
          canopy_count++;
        if (cell->crown)
          crown_count++;
        if (cell->prop)
          prop_count++;
      }

      VoxelObject *object = &st->objects[next_object];
      memset(object, 0, sizeof(*object));
      object->min_row = min_row;
      object->max_row = max_row;
      object->min_col = min_col;
      object->max_col = max_col;
      object->base_z = st->cells[members[0]].base_z;
      object->prop = prop_count != 0;

      if (object->prop) {
        // The painted prop becomes one small billboard; the ground tile
        // stays a flat quad under it.
        object->stack = true;
        for (int i = 0; i < member_count; i++) {
          VoxelCell *cell = &st->cells[members[i]];
          cell->mode = kVoxelCell_Stack;
          cell->obj = next_object;
        }
        next_object++;
        continue;
      }

      // Only cells whose own drawing reads as a tree crown stack. A colour
      // average over the whole component used to turn long cliff bands into
      // stacked columns, one dark seam per cell.
      // A one/two-cell strip around a plateau is a painted border, not a
      // row of trees. Stacking it raised a comb of thin vertical cards, one
      // per drawn pixel column; keep such strips flat at their cell height.
      bool crown_major = crown_count * 2 > member_count;
      bool thin = (object_rows <= 2 && object_cols >= 5) ||
          (object_cols <= 2 && object_rows >= 5);
      if (thin && !(light_canopy && crown_major))
        canopy_count = 0;
      bool foliage = canopy_count * 2 > member_count;
      if (foliage && object_rows >= 1 && object_cols >= 1 &&
          (member_count >= 2 || canopy_count != 0)) {
        object->stack = true;
        object->bevel_stack = light_canopy;
        int radius_cells = object_rows < object_cols ? object_rows : object_cols;
        object->bevel_radius = ClampFloat((float)radius_cells * 4.0f, 4.0f,
                                          16.0f);
        object->row_stack = !object->bevel_stack &&
            object_cols > object_rows * 2;
        for (int i = 0; i < member_count; i++) {
          VoxelCell *cell = &st->cells[members[i]];
          cell->mode = kVoxelCell_Stack;
          cell->obj = next_object;
        }
        if (object->bevel_stack) {
          BuildCrownDistanceField(st, next_object, min_row, max_row, min_col,
                                  max_col);
        }
        next_object++;
        continue;
      }

      // A long wall, cliff or room border is one connected component too,
      // but giving it a single object height and splitting it into a roof
      // band and a facade stretches the border artwork into tall slabs.
      // Only compact props get the band/boost treatment; large structures
      // keep their per-tile height and top-surface art.
      bool compact = object_rows < 9 && object_cols < 9 &&
          member_count < 96;
      if (compact && object_rows >= 2 && member_count >= 2) {
        float current_h = 0.0f;
        for (int i = 0; i < member_count; i++) {
          VoxelCell *cell = &st->cells[members[i]];
          float h = cell->top_z - cell->base_z;
          if (h > current_h)
            current_h = h;
        }
        float boost = 0.5f + 0.35f * (float)object_rows;
        float blocks = current_h / 8.0f;
        if (boost > blocks)
          blocks = boost;
        if (blocks > 4.0f)
          blocks = 4.0f;
        float object_top = 0.0f;
        for (int i = 0; i < member_count; i++) {
          VoxelCell *cell = &st->cells[members[i]];
          cell->top_z = cell->base_z + blocks * 8.0f;
          if (cell->top_z > object_top)
            object_top = cell->top_z;
        }
        int facade_rows = object_rows / 2;
        if (facade_rows < 1)
          facade_rows = 1;
        int roof_limit = max_row - facade_rows;
        for (int i = 0; i < member_count; i++) {
          int idx = members[i];
          int rr = idx / kFrameDesc_MaxRoomDim;
          VoxelCell *cell = &st->cells[idx];
          if (rr <= roof_limit) {
            cell->mode = kVoxelCell_Top;
            cell->top_z = object_top;
          } else {
            cell->mode = kVoxelCell_Upright;
          }
          cell->obj = next_object;
        }
        next_object++;
      } else {
        for (int i = 0; i < member_count; i++)
          st->cells[members[i]].obj = next_object;
        next_object++;
      }
    }
  }
  st->cell_object_count = next_object;
}

static float StackTopZ(const VoxelObject *object, const VoxelCell *cell,
                       int local_px,
                       int local_py, int global_px, int global_py) {
  float blocks;
  if (object->bevel_stack) {
    // A pixel-space chamfer field lets diagonal edges round naturally while
    // preserving the mask and avoiding one square height band per tile.
    float dist = (float)cell->bevel_distance[local_py * 8 + local_px] - 5.0f;
    float radius = object->bevel_radius;
    float t = dist / (radius * 10.0f - 5.0f);
    t = ClampFloat(t, 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    blocks = 0.20f + t * (s_stack_amplitude - 0.20f);
  } else if (object->row_stack) {
    int bottom_py = object->max_row * 8 + 7;
    int phase = (global_px - object->min_col * 8) & 15;
    float bump = 1.0f - fabsf((float)phase - 7.5f) / 7.5f;
    blocks = 0.25f + (float)(bottom_py - global_py) * 0.05f +
        0.35f * bump;
  } else {
    float cx = (float)(object->min_col + object->max_col + 1) * 4.0f;
    float cy = (float)(object->min_row + object->max_row + 1) * 4.0f;
    float hw = (float)(object->max_col - object->min_col + 1) * 4.0f;
    float hh = (float)(object->max_row - object->min_row + 1) * 4.0f;
    float dx = ((float)global_px + 0.5f - cx) / (hw > 1.0f ? hw : 1.0f);
    float dy = ((float)global_py + 0.5f - cy) / (hh > 1.0f ? hh : 1.0f);
    float d2 = dx * dx + dy * dy;
    float dome = d2 < 1.0f ? sqrtf(1.0f - d2) : 0.0f;
    blocks = 0.25f + s_stack_amplitude * dome;
  }
  if (blocks < 0.20f)
    blocks = 0.20f;
  if (blocks > s_stack_amplitude + 0.4f)
    blocks = s_stack_amplitude + 0.4f;
  return object->base_z + blocks * 8.0f;
}

static VoxelCell *StackNeighbor(VoxelState *st, const VoxelCell *cell,
                                int r, int c, int px, int py) {
  int nc = c + px;
  int nr = r + py;
  if (nc < 0 || nc >= st->cell_cols || nr < 0 || nr >= st->cell_rows)
    return NULL;
  VoxelCell *n = &st->cells[nr * kFrameDesc_MaxRoomDim + nc];
  if (n->mode != kVoxelCell_Stack || n->obj != cell->obj)
    return NULL;
  return n;
}

// Emit one stacked column block. The size is the preset's mesh detail: at
// size 1 this is exactly one art pixel, at 8 it is one whole tile.
static bool EmitStackColumn(VoxelState *st, const VoxelObject *object,
                            const VoxelCell *cell, int r, int c,
                            int x, int y, int px, int py, int size,
                            int sx, int sy, float top_shade,
                            float front_shade, float side_shade) {
  float u0, v0, u1, v1;
  AtlasSlotUV(cell->slot, &u0, &v0, &u1, &v1);
  float top = StackTopZ(object, cell, sx, sy, x + sx, r * 8 + sy);
  float pu = u0 + ((float)sx + 0.5f) / 8.0f * (u1 - u0);
  float pv = v0 + ((float)sy + 0.5f) / 8.0f * (v1 - v0);
  float x0 = (float)(x + px);
  float y0 = (float)(y + py);
  float x1 = x0 + (float)size;
  float y1 = y0 + (float)size;
  if (!QuadMesh(&st->mesh_bg, x0, y0, top, pu, pv, 0.0f,
                x1, y0, top, pu, pv, 0.0f, x1, y1, top, pu, pv, 0.0f,
                x0, y1, top, pu, pv, 0.0f, top_shade))
    return false;

  bool have_south = false;
  float south_z = object->base_z;
  int ny = py + size;
  const VoxelCell *south_cell = NULL;
  int south_px = px + size / 2;
  int south_py = ny;
  if (ny < 8) {
    south_cell = cell;
    for (int k = 0; k < size; k++)
      if (cell->alpha[ny * 8 + px + k])
        have_south = true;
  } else {
    VoxelCell *n = StackNeighbor(st, cell, r, c, 0, 1);
    if (n != NULL) {
      south_cell = n;
      south_py = 0;
      for (int k = 0; k < size; k++)
        if (n->alpha[px + k])
          have_south = true;
    }
  }
  if (have_south && south_cell != NULL)
    south_z = StackTopZ(object, south_cell, south_px, south_py,
                        x + px + size / 2, r * 8 + ny);
  if (south_z < top - 0.01f) {
    // The face that closes the canopy silhouette is darker than the steps
    // inside it, so two touching trees keep a visible outline instead of
    // melting into one golden mass.
    float shade = have_south ? front_shade : front_shade * 0.78f;
    if (!QuadMesh(&st->mesh_bg, x0, y1, south_z, pu, pv, 0.0f,
                  x1, y1, south_z, pu, pv, 0.0f, x1, y1, top, pu, pv, 0.0f,
                  x0, y1, top, pu, pv, 0.0f, shade))
      return false;
  }

  bool have_west = false, have_east = false;
  const VoxelCell *west_cell = NULL, *east_cell = NULL;
  int west_px = px - 1, west_py = py + size / 2;
  int east_px = px + size, east_py = py + size / 2;
  if (px > 0) {
    west_cell = cell;
    for (int k = 0; k < size; k++)
      if (cell->alpha[(py + k) * 8 + px - 1])
        have_west = true;
  } else {
    VoxelCell *n = StackNeighbor(st, cell, r, c, -1, 0);
    if (n != NULL) {
      west_cell = n;
      west_px = 7;
      for (int k = 0; k < size; k++)
        if (n->alpha[(py + k) * 8 + 7])
          have_west = true;
    }
  }
  if (px + size < 8) {
    east_cell = cell;
    for (int k = 0; k < size; k++)
      if (cell->alpha[(py + k) * 8 + px + size])
        have_east = true;
  } else {
    VoxelCell *n = StackNeighbor(st, cell, r, c, 1, 0);
    if (n != NULL) {
      east_cell = n;
      east_px = 0;
      for (int k = 0; k < size; k++)
        if (n->alpha[(py + k) * 8])
          have_east = true;
    }
  }
  float west_z = object->base_z;
  float east_z = object->base_z;
  if (have_west && west_cell != NULL)
    west_z = StackTopZ(object, west_cell, west_px, west_py, x + px - 1,
                       r * 8 + py + size / 2);
  if (have_east && east_cell != NULL)
    east_z = StackTopZ(object, east_cell, east_px, east_py, x + px + size,
                       r * 8 + py + size / 2);
  if (!have_west || west_z < top - 0.01f) {
    float shade = have_west ? side_shade : side_shade * 0.78f;
    if (!QuadMesh(&st->mesh_bg, x0, y0, west_z, pu, pv, 0.0f,
                  x0, y1, west_z, pu, pv, 0.0f, x0, y1, top, pu, pv, 0.0f,
                  x0, y0, top, pu, pv, 0.0f, shade))
      return false;
  }
  if (!have_east || east_z < top - 0.01f) {
    float shade = have_east ? side_shade : side_shade * 0.78f;
    if (!QuadMesh(&st->mesh_bg, x1, y0, east_z, pu, pv, 0.0f,
                  x1, y1, east_z, pu, pv, 0.0f, x1, y1, top, pu, pv, 0.0f,
                  x1, y0, top, pu, pv, 0.0f, shade))
      return false;
  }
  return true;
}

// Sprite stacking for foliage: every opaque art pixel becomes a one-pixel
// column. The top face carries the pixel's art; south/west/east steps use the
// same colour. The height follows the drawn row, so the silhouette and the
// round tops survive instead of collapsing into a dark cuboid.
static bool EmitStackCell(VoxelState *st, const VoxelCell *cell, int r,
                          int c, int x, int y, float top_shade,
                          float front_shade, float side_shade) {
  if (cell->obj < 0 || cell->obj >= st->cell_object_count)
    return true;
  const VoxelObject *object = &st->objects[cell->obj];
  if (!object->stack)
    return true;
  int step = st->render.mesh_detail;
  if (step < 1 || step > 8)
    step = 1;
  for (int py = 0; py < 8; py += step) {
    for (int px = 0; px < 8; px += step) {
      int sx = -1, sy = -1;
      for (int by = 0; by < step && sy < 0; by++) {
        for (int bx = 0; bx < step; bx++) {
          if (cell->alpha[(py + by) * 8 + px + bx]) {
            sx = px + bx;
            sy = py + by;
            break;
          }
        }
      }
      if (sx < 0)
        continue;
      if (!EmitStackColumn(st, object, cell, r, c, x, y, px, py, step,
                           sx, sy, top_shade, front_shade, side_shade))
        return false;
    }
  }
  return true;
}

// A prop embedded in an opaque ground tile becomes one small camera-facing
// billboard, like the dungeon brazier cut-outs. The mask pixels from every
// cell of the object are combined here, so a post split across two 8x8 tiles
// keeps one continuous silhouette. A fixed vertical scale approximates the
// default tilt, so the prop keeps its drawn height.
static bool EmitPropBillboard(VoxelState *st, const FrameDesc *desc,
                              const FrameDescRoomLayer *room, int layer,
                              int extra, int object_index, float shade) {
  VoxelObject *object = &st->objects[object_index];
  int max_py = -0x40000000;
  float ground_z = object->base_z;
  bool any = false;
  for (int r = object->min_row; r <= object->max_row; r++) {
    for (int c = object->min_col; c <= object->max_col; c++) {
      VoxelCell *cell = &st->cells[r * kFrameDesc_MaxRoomDim + c];
      if (cell->obj != object_index || !cell->prop ||
          cell->mode != kVoxelCell_Stack)
        continue;
      int sx, sy;
      RoomCellScreen(desc, layer, room, c, r, extra, &sx, &sy);
      if (cell->top_z > ground_z)
        ground_z = cell->top_z;
      for (int i = 0; i < 64; i++) {
        if (!cell->alpha[i])
          continue;
        int gy = sy + (i >> 3);
        if (gy > max_py)
          max_py = gy;
        any = true;
      }
    }
  }
  if (!any)
    return true;
  int px, py;
  RoomCellScreen(desc, layer, room, object->min_col, object->max_row, extra,
                 &px, &py);
  float plane_y = (float)(py + 8);
  // One art pixel of height projects to one screen pixel at the default
  // tilt. A fixed factor keeps the cached room mesh independent of the
  // easing camera; the difference at the other presets is small.
  const float z_unit = 1.7f;
  for (int r = object->min_row; r <= object->max_row; r++) {
    for (int c = object->min_col; c <= object->max_col; c++) {
      VoxelCell *cell = &st->cells[r * kFrameDesc_MaxRoomDim + c];
      if (cell->obj != object_index || !cell->prop ||
          cell->mode != kVoxelCell_Stack)
        continue;
      int sx, sy;
      RoomCellScreen(desc, layer, room, c, r, extra, &sx, &sy);
      float u0, v0, u1, v1;
      AtlasSlotUV(cell->slot, &u0, &v0, &u1, &v1);
      for (int i = 0; i < 64; i++) {
        if (!cell->alpha[i])
          continue;
        int sub_x = i & 7, sub_y = i >> 3;
        float pu = u0 + ((float)sub_x + 0.5f) / 8.0f * (u1 - u0);
        float pv = v0 + ((float)sub_y + 0.5f) / 8.0f * (v1 - v0);
        float z0 = ground_z + (float)(max_py - (sy + sub_y)) * z_unit;
        float z1 = z0 + z_unit;
        float x0 = (float)(sx + sub_x);
        if (!QuadMesh(&st->mesh_bg, x0, plane_y, z0, pu, pv, 0.0f,
                      x0 + 1.0f, plane_y, z0, pu, pv, 0.0f,
                      x0 + 1.0f, plane_y, z1, pu, pv, 0.0f,
                      x0, plane_y, z1, pu, pv, 0.0f, shade))
          return false;
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Mesh emission

static bool BuildRoomLayer(VoxelState *st, const FrameDesc *desc, int layer,
                           int extra) {
  const FrameDescRoomLayer *room = &desc->semantics.room_layer[layer];
  if (!room->present || room->cols == 0 || room->rows == 0)
    return true;
  int cols = room->cols > kFrameDesc_MaxRoomDim ? kFrameDesc_MaxRoomDim
                                                 : room->cols;
  int rows = room->rows > kFrameDesc_MaxRoomDim ? kFrameDesc_MaxRoomDim
                                                 : room->rows;
  ClearCells(st, cols, rows);
  if (getenv("ZELDA3_VOXEL_DEBUG_ROOM"))
    fprintf(stderr, "voxel-room layer %d base %d,%d origin %d,%d cols %d rows %d\n",
            layer, room->base_tile_x, room->base_tile_y,
            room->screen_origin_tile_x, room->screen_origin_tile_y, cols,
            rows);
  if (desc->semantics.indoors) {
    int mask = rows > 0 ? rows - 1 : 63;
    int link_tile = (int)desc->semantics.link_y >> 3;
    st->interior_link_row = (link_tile - room->base_tile_y) & mask;
  }

  float base_z = FloorBaseZ(desc, &st->render, layer);
  uint8_t pixels[8 * 8 * 4];
  uint8_t alpha[64];
  const int cull_margin = 256;
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      int sx, sy;
      RoomCellScreen(desc, layer, room, c, r, extra, &sx, &sy);
      if (sx < -cull_margin || sx >= (int)desc->width + cull_margin ||
          sy < -cull_margin || sy >= (int)desc->height + cull_margin)
        continue;
      int idx = r * kFrameDesc_MaxRoomDim + c;
      int attr = room->attr[idx];
      int raw = room->raw[idx];
      int tile_num = 0;
      int coverage = 0;
      uint32_t edge_top = 0, edge_bottom = 0, edge_left = 0, edge_right = 0;
      if (!DecodeRoomTile(st, desc, layer, room, c, r, extra, pixels,
                          alpha, &coverage, &edge_top, &edge_bottom,
                          &edge_left, &edge_right, &tile_num))
        continue;
      if (desc->semantics.indoors) {
        for (int i = 0; i < 64; i++) {
          const uint8_t *p = pixels + i * 4;
          if (p[3] == 0)
            continue;
          st->fill_r += p[0];
          st->fill_g += p[1];
          st->fill_b += p[2];
          st->fill_count++;
        }
      }

      // Tile behavior 0x20 and 0xb0..0xbd are pits (TileBehavior_Pit). They
      // must read as a recessed dark opening, never as ordinary floor.
      bool pit = attr == 0x20 || (attr >= 0xb0 && attr <= 0xbd);
      float height = VoxelProfile_Height(st->profile, desc, layer, attr, raw,
                                         tile_num);
      if (pit)
        height = -1.0f;
      height *= st->render.height_scale;
      VoxelArtMode art = VoxelProfile_Art(st->profile, desc, layer, attr,
                                          raw, tile_num);
      VoxelCell *cell = &st->cells[idx];
      cell->base_z = base_z;
      cell->top_z = base_z + height * 8.0f;
      bool opaque = coverage != 0;
      int slot;
      if (!opaque && pit) {
        // The pit tile can be fully transparent in some rooms; give it a
        // dark bottom so the room fill cannot show through the hole.
        slot = AtlasColor(st, 0x0a0a12);
        coverage = 64;
        memset(alpha, 0, sizeof(alpha));
        opaque = true;
      } else {
        if (!opaque)
          continue;
        slot = AtlasAdd(st, pixels);
      }
      if (slot < 0)
        continue;
      cell->slot = slot;
      cell->coverage = (uint8_t)coverage;
      cell->solid = coverage >= 48;
      memcpy(cell->alpha, alpha, sizeof(cell->alpha));
      cell->color_top = edge_top;
      cell->color_bottom = edge_bottom;
      cell->color_left = edge_left;
      cell->color_right = edge_right;
      uint8_t prop_mask[64];
      bool embedded_prop = false;
      if (!pit && !desc->semantics.indoors && height >= 0.9f &&
          art == kVoxelArt_Prop && coverage >= 56)
        embedded_prop = ExtractEmbeddedProp(pixels, prop_mask);
      cell->prop = embedded_prop ? 1 : 0;
      // A green, tall outdoor cell is a tree/hedge crown even when its
      // Map16 id is shared with a cliff or ground tile in another area.
      // Marking it here (instead of averaging the whole connected wall)
      // keeps each crown a separate stacked volume and stops the big
      // square blocks the colour fallback left behind.
      bool green_cell = false;
      bool foliage_art = false;
      bool stack_art = art == kVoxelArt_Stack && !embedded_prop;
      bool want_green = !desc->semantics.indoors &&
          art != kVoxelArt_Flat && height >= 0.9f && cell->solid;
      bool want_crown = !desc->semantics.indoors && stack_art;
      if (want_green || want_crown) {
        uint64_t pr = 0, pg = 0, pb = 0;
        int pn = 0;
        for (int pi = 0; pi < 64; pi++) {
          const uint8_t *p = pixels + pi * 4;
          if (p[3] == 0)
            continue;
          pr += p[0];
          pg += p[1];
          pb += p[2];
          pn++;
        }
        if (pn != 0) {
          uint32_t avg = (uint32_t)(pr / (uint64_t)pn) << 16 |
                         (uint32_t)(pg / (uint64_t)pn) << 8 |
                         (uint32_t)(pb / (uint64_t)pn);
          foliage_art = RgbIsFoliage(avg);
          if (want_green)
            green_cell = foliage_art;
        }
      }
      // Only a drawn green crown gets the light-world rounded top. Stack art
      // also covers stone props and autumn trees; those keep the per-tree
      // dome or the plain band split.
      cell->crown = (stack_art && foliage_art) ? 1 : 0;
      cell->canopy =
          (art == kVoxelArt_Stack || green_cell) && !embedded_prop ? 1 : 0;
      cell->pit = pit ? 1 : 0;
      if (pit) {
        cell->mode = kVoxelCell_Flat;
      } else if (embedded_prop) {
        cell->mode = kVoxelCell_Top;
        memcpy(cell->alpha, prop_mask, sizeof(cell->alpha));
      } else if (art == kVoxelArt_Stack) {
        cell->mode = kVoxelCell_Top;
      } else if (art == kVoxelArt_Flat) {
        cell->mode = kVoxelCell_Flat;
      } else if (art == kVoxelArt_Top) {
        cell->mode = kVoxelCell_Top;
      } else if (green_cell) {
        cell->mode = kVoxelCell_Top;
      } else if (art == kVoxelArt_Upright) {
        cell->mode = kVoxelCell_Upright;
      } else if (desc->semantics.indoors && height >= 0.9f && cell->solid) {
        // Dungeon wall art is drawn as seen from above, like the rest of
        // the 2D frame. Keeping those tiles as top surfaces preserves the
        // bright wall colour; treating them as vertical façades shows the
        // dark shaded half of the drawing and the room turns muddy.
        cell->mode = kVoxelCell_Top;
      } else if (height <= 0.05f && height >= -0.05f) {
        cell->mode = kVoxelCell_Flat;
      } else if (height < 0.0f) {
        cell->mode = kVoxelCell_Flat;
      } else if (height < 0.9f) {
        cell->mode = cell->solid ? kVoxelCell_Top : kVoxelCell_Upright;
      } else {
        cell->mode = kVoxelCell_Upright;
      }
      st->cell_count++;
    }
  }

  AssignObjectBands(st, desc);
  if (getenv("ZELDA3_VOXEL_DEBUG_CELLS") && desc->semantics.indoors) {
    int lc = (room->origin_tile_x + 25) & 63;
    int lr = (room->origin_tile_y + 15) & 63;
    int li = lr * kFrameDesc_MaxRoomDim + lc;
    VoxelCell *lcell = &st->cells[li];
    fprintf(stderr, "voxel-dbg room %d layer %d origin %d,%d rows %d linkrow %d cell %d,%d attr %u mode %u cov %u top %.2f base %.2f\n",
            desc->semantics.room, layer, room->origin_tile_x,
            room->origin_tile_y, rows, st->interior_link_row, lc, lr,
            room->attr[li], lcell->mode, lcell->coverage, lcell->top_z,
            lcell->base_z);
  }

  // Stylised fixed shading: the top face keeps the 2D colour exactly, the
  // south face is just slightly darker, and the sides step down gently. A
  // physical N.L term made ground and walls far darker than the source art.
  float top_shade = st->render.face_top * st->render.exposure;
  float front_shade = (st->light_settings.enabled ? st->render.face_front
                                                   : st->render.face_top) *
      st->render.exposure;
  float side_shade = (st->light_settings.enabled ? st->render.face_side
                                                 : st->render.side_shade) *
      st->render.exposure;
  float back_shade = (st->light_settings.enabled ? st->render.face_back
                                                 : st->render.side_shade) *
      st->render.exposure;
  float left_shade = side_shade;
  float right_shade = side_shade;

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      int idx = r * kFrameDesc_MaxRoomDim + c;
      VoxelCell *cell = &st->cells[idx];
      if (cell->mode == kVoxelCell_Empty)
        continue;
      if (st->interior_link_row >= 0 &&
          (cell->mode == kVoxelCell_Upright || cell->mode == kVoxelCell_Top) &&
          cell->solid) {
        int nrows = st->cell_rows;
        int dist = (r - st->interior_link_row + nrows) & (nrows - 1);
        if (dist > 2 && dist < nrows / 2) {
          // Hide the far room-border wall that would block the diorama,
          // but keep small foreground props (braziers, chests, statues)
          // that happen to sit south of Link.
          bool cull = true;
          int obj = cell->obj;
          if (obj >= 0 && obj < st->cell_object_count) {
            const VoxelObject *o = &st->objects[obj];
            if (o->max_row - o->min_row < 6 &&
                o->max_col - o->min_col < 6)
              cull = false;
          }
          if (cull)
            continue;
        }
      }
      int x = 0, y = 0;
      RoomCellScreen(desc, layer, room, c, r, extra, &x, &y);
      float u0, v0, u1, v1;
      AtlasSlotUV(cell->slot, &u0, &v0, &u1, &v1);
      float top = cell->top_z;
      float base = cell->base_z;
      bool has_above_floor = top > base + 0.05f;

      VoxelCell *south = r + 1 < rows
          ? &st->cells[(r + 1) * kFrameDesc_MaxRoomDim + c] : NULL;
      VoxelCell *north = r > 0
          ? &st->cells[(r - 1) * kFrameDesc_MaxRoomDim + c] : NULL;
      VoxelCell *west = c > 0
          ? &st->cells[r * kFrameDesc_MaxRoomDim + c - 1] : NULL;
      VoxelCell *east = c + 1 < cols
          ? &st->cells[r * kFrameDesc_MaxRoomDim + c + 1] : NULL;

      if (cell->mode == kVoxelCell_Flat) {
        float floor_shade = cell->pit ? top_shade * 0.55f : top_shade;
        if (!QuadMesh(&st->mesh_bg, (float)x, (float)y, top, u0, v0, 0.0f,
                      (float)(x + 8), (float)y, top, u1, v0, 0.0f,
                      (float)(x + 8), (float)(y + 8), top, u1, v1, 0.0f,
                      (float)x, (float)(y + 8), top, u0, v1, 0.0f,
                      floor_shade))
          return false;
        if (cell->pit && top < base - 0.1f) {
          // Close the sides of the hole down to its dark bottom. Without
          // these walls the tilted camera sees the room fill through the
          // gap between the floor edge and the recessed pit.
          int wall_slot = AtlasColor(st, 0x14141c);
          if (wall_slot >= 0) {
            float cu0, cv0, cu1, cv1;
            AtlasSlotUV(wall_slot, &cu0, &cv0, &cu1, &cv1);
            float wall_shade = side_shade * 0.45f;
            if ((south == NULL || !south->pit) &&
                !QuadMesh(&st->mesh_bg, (float)x, (float)(y + 8), base,
                          cu0, cv1, 0.0f, (float)(x + 8), (float)(y + 8),
                          base, cu1, cv1, 0.0f, (float)(x + 8),
                          (float)(y + 8), top, cu1, cv0, 0.0f, (float)x,
                          (float)(y + 8), top, cu0, cv0, 0.0f, wall_shade))
              return false;
            if ((north == NULL || !north->pit) &&
                !QuadMesh(&st->mesh_bg, (float)x, (float)y, base, cu0, cv1,
                          0.0f, (float)(x + 8), (float)y, base, cu1, cv1,
                          0.0f, (float)(x + 8), (float)y, top, cu1, cv0,
                          0.0f, (float)x, (float)y, top, cu0, cv0, 0.0f,
                          wall_shade))
              return false;
            if ((west == NULL || !west->pit) &&
                !QuadMesh(&st->mesh_bg, (float)x, (float)y, base, cu0, cv1,
                          0.0f, (float)x, (float)(y + 8), base, cu1, cv1,
                          0.0f, (float)x, (float)(y + 8), top, cu1, cv0,
                          0.0f, (float)x, (float)y, top, cu0, cv0, 0.0f,
                          wall_shade))
              return false;
            if ((east == NULL || !east->pit) &&
                !QuadMesh(&st->mesh_bg, (float)(x + 8), (float)y, base, cu0,
                          cv1, 0.0f, (float)(x + 8), (float)(y + 8), base,
                          cu1, cv1, 0.0f, (float)(x + 8), (float)(y + 8),
                          top, cu1, cv0, 0.0f, (float)(x + 8), (float)y, top,
                          cu0, cv0, 0.0f, wall_shade))
              return false;
          }
        }
        continue;
      }

      if (cell->mode == kVoxelCell_Stack) {
        if (cell->prop) {
          // Ground under the painted prop. The billboard covers the prop
          // pixels, so the tile can keep its original artwork at its own
          // level; drawing it at the floor base would sink a raised bed.
          if (!QuadMesh(&st->mesh_bg, (float)x, (float)y, top, u0, v0,
                        0.0f, (float)(x + 8), (float)y, top, u1, v0,
                        0.0f, (float)(x + 8), (float)(y + 8), top, u1, v1,
                        0.0f, (float)x, (float)(y + 8), top, u0, v1, 0.0f,
                        top_shade))
            return false;
          VoxelObject *object = &st->objects[cell->obj];
          if (!object->emitted) {
            object->emitted = true;
            if (!EmitPropBillboard(st, desc, room, layer, extra, cell->obj,
                                   front_shade))
              return false;
          }
        } else if (!EmitStackCell(st, cell, r, c, x, y, top_shade,
                                   front_shade, side_shade)) {
          return false;
        }
        continue;
      }

      if (cell->mode == kVoxelCell_Top) {
        bool pixel_edge_sides = !desc->semantics.indoors &&
            (desc->semantics.area == 0 || desc->semantics.area == 41) &&
            cell->coverage < 64;
        bool wide_landform = false;
        if (cell->obj >= 0 && cell->obj < st->cell_object_count) {
          const VoxelObject *object = &st->objects[cell->obj];
          wide_landform = object->max_row - object->min_row + 1 >= 9 ||
              object->max_col - object->min_col + 1 >= 9;
        }
        bool need_south = south == NULL ||
            south->mode == kVoxelCell_Empty || south->top_z < top - 0.1f;
        bool need_north = north == NULL ||
            north->mode == kVoxelCell_Empty || north->top_z < top - 0.1f;
        bool need_west = west == NULL ||
            west->mode == kVoxelCell_Empty || west->top_z < top - 0.1f;
        bool need_east = east == NULL ||
            east->mode == kVoxelCell_Empty || east->top_z < top - 0.1f;
        bool terrain_bevel = !desc->semantics.indoors &&
            (desc->semantics.area == 0 || desc->semantics.area == 41) &&
            wide_landform && has_above_floor && top - base >= 8.0f &&
            cell->coverage >= 56;
        if (cell->coverage < 56 || pixel_edge_sides || terrain_bevel) {
          // Cut-out roofs/ledges keep their silhouette: one top square per
          // opaque pixel (or per mesh-detail block in lower presets). In the
          // two light-world terrain areas, exposed cliff walls follow that
          // same pixel mask instead of filling every partial tile edge. Long
          // raised landforms also get a two-pixel chamfer at exposed tops.
          int step = terrain_bevel ? 1 : st->render.mesh_detail;
          if (step < 1 || step > 8)
            step = 1;
          for (int py = 0; py < 8; py += step) {
            for (int px = 0; px < 8; px += step) {
              int sx = -1, sy = -1;
              for (int by = 0; by < step && sy < 0; by++) {
                for (int bx = 0; bx < step; bx++) {
                  if (cell->alpha[(py + by) * 8 + px + bx]) {
                    sx = px + bx;
                    sy = py + by;
                    break;
                  }
                }
              }
              if (sx < 0)
                continue;
              float pu = u0 + ((float)sx + 0.5f) / 8.0f * (u1 - u0);
              float pv = v0 + ((float)sy + 0.5f) / 8.0f * (v1 - v0);
              float z00 = top, z10 = top, z11 = top, z01 = top;
              if (terrain_bevel) {
                z00 = TopBevelHeight(top, base, need_south, need_north,
                                     need_west, need_east, px, py);
                z10 = TopBevelHeight(top, base, need_south, need_north,
                                     need_west, need_east, px + step, py);
                z11 = TopBevelHeight(top, base, need_south, need_north,
                                     need_west, need_east, px + step,
                                     py + step);
                z01 = TopBevelHeight(top, base, need_south, need_north,
                                     need_west, need_east, px, py + step);
              }
              if (!QuadMesh(&st->mesh_bg, (float)(x + px), (float)(y + py),
                            z00, pu, pv, 0.0f,
                            (float)(x + px + step), (float)(y + py), z10, pu,
                            pv, 0.0f, (float)(x + px + step),
                            (float)(y + py + step), z11, pu, pv, 0.0f,
                            (float)(x + px), (float)(y + py + step), z01, pu,
                            pv, 0.0f, top_shade))
                return false;
            }
          }
          if ((pixel_edge_sides || terrain_bevel) && has_above_floor) {
            for (int i = 0; i < 8; i++) {
              if (need_south && cell->alpha[56 + i]) {
                if (terrain_bevel) {
                  float z0 = TopBevelHeight(top, base, need_south,
                      need_north, need_west, need_east, i, 8);
                  float z1 = TopBevelHeight(top, base, need_south,
                      need_north, need_west, need_east, i + 1, 8);
                  if (!SidePixelBeveled(st, cell, x, y, 0, i, base, z0, z1,
                                        front_shade, front_shade * 0.78f))
                    return false;
                } else if (!SidePixelWrapped(st, cell, x, y, 0, i, base, top,
                                            front_shade,
                                            front_shade * 0.78f)) {
                  return false;
                }
              }
              if (need_north && cell->alpha[i]) {
                if (terrain_bevel) {
                  float z0 = TopBevelHeight(top, base, need_south,
                      need_north, need_west, need_east, i, 0);
                  float z1 = TopBevelHeight(top, base, need_south,
                      need_north, need_west, need_east, i + 1, 0);
                  if (!SidePixelBeveled(st, cell, x, y, 1, i, base, z0, z1,
                                        back_shade, back_shade * 0.78f))
                    return false;
                } else if (!SidePixelWrapped(st, cell, x, y, 1, i, base, top,
                                            back_shade,
                                            back_shade * 0.78f)) {
                  return false;
                }
              }
              if (need_west && cell->alpha[i * 8]) {
                if (terrain_bevel) {
                  float z0 = TopBevelHeight(top, base, need_south,
                      need_north, need_west, need_east, 0, i);
                  float z1 = TopBevelHeight(top, base, need_south,
                      need_north, need_west, need_east, 0, i + 1);
                  if (!SidePixelBeveled(st, cell, x, y, 2, i, base, z0, z1,
                                        left_shade, left_shade * 0.78f))
                    return false;
                } else if (!SidePixelWrapped(st, cell, x, y, 2, i, base, top,
                                            left_shade,
                                            left_shade * 0.78f)) {
                  return false;
                }
              }
              if (need_east && cell->alpha[i * 8 + 7]) {
                if (terrain_bevel) {
                  float z0 = TopBevelHeight(top, base, need_south,
                      need_north, need_west, need_east, 8, i);
                  float z1 = TopBevelHeight(top, base, need_south,
                      need_north, need_west, need_east, 8, i + 1);
                  if (!SidePixelBeveled(st, cell, x, y, 3, i, base, z0, z1,
                                        right_shade,
                                        right_shade * 0.78f))
                    return false;
                } else if (!SidePixelWrapped(st, cell, x, y, 3, i, base, top,
                                            right_shade,
                                            right_shade * 0.78f)) {
                  return false;
                }
              }
            }
          }
          continue;
        }
        if (!QuadMesh(&st->mesh_bg, (float)x, (float)y, top, u0, v0, 0.0f,
                      (float)(x + 8), (float)y, top, u1, v0, 0.0f,
                      (float)(x + 8), (float)(y + 8), top, u1, v1, 0.0f,
                      (float)x, (float)(y + 8), top, u0, v1, 0.0f, top_shade))
          return false;
        if (has_above_floor) {
          bool need_south = south == NULL || south->mode == kVoxelCell_Empty ||
              south->top_z < top - 0.1f;
          bool need_north = north == NULL || north->mode == kVoxelCell_Empty ||
              north->top_z < top - 0.1f;
          bool need_west = west == NULL || west->mode == kVoxelCell_Empty ||
              west->top_z < top - 0.1f;
          bool need_east = east == NULL || east->mode == kVoxelCell_Empty ||
              east->top_z < top - 0.1f;
          if (need_south) {
            int slot = cell->slot;
            if (slot >= 0) {
              float cu0, cv0, cu1, cv1;
              AtlasSlotUV(slot, &cu0, &cv0, &cu1, &cv1);
              if (!SideQuadWrapped(&st->mesh_bg, (float)x, (float)(y + 8), base,
                            cu0, cv1, 0.0f, (float)(x + 8), (float)(y + 8),
                            base, cu1, cv1, 0.0f, (float)(x + 8),
                            (float)(y + 8), top, cu1, cv0, 0.0f, (float)x,
                            (float)(y + 8), top, cu0, cv0, 0.0f, front_shade,
                            front_shade * 0.78f))
                return false;
            }
          }
          if (need_north) {
            int slot = cell->slot;
            if (slot >= 0) {
              float cu0, cv0, cu1, cv1;
              AtlasSlotUV(slot, &cu0, &cv0, &cu1, &cv1);
              if (!SideQuadWrapped(&st->mesh_bg, (float)x, (float)y, base, cu0, cv1,
                            0.0f, (float)(x + 8), (float)y, base, cu1, cv1,
                            0.0f, (float)(x + 8), (float)y, top, cu1, cv0,
                            0.0f, (float)x, (float)y, top, cu0, cv0, 0.0f,
                            back_shade, back_shade * 0.78f))
                return false;
            }
          }
          if (need_west) {
            int slot = cell->slot;
            if (slot >= 0) {
              float cu0, cv0, cu1, cv1;
              AtlasSlotUV(slot, &cu0, &cv0, &cu1, &cv1);
              if (!SideQuadWrapped(&st->mesh_bg, (float)x, (float)y, base, cu0, cv1,
                            0.0f, (float)x, (float)(y + 8), base, cu1, cv1,
                            0.0f, (float)x, (float)(y + 8), top, cu1, cv0,
                            0.0f, (float)x, (float)y, top, cu0, cv0, 0.0f,
                            left_shade, left_shade * 0.78f))
                return false;
            }
          }
          if (need_east) {
            int slot = cell->slot;
            if (slot >= 0) {
              float cu0, cv0, cu1, cv1;
              AtlasSlotUV(slot, &cu0, &cv0, &cu1, &cv1);
              if (!SideQuadWrapped(&st->mesh_bg, (float)(x + 8), (float)y, base,
                            cu0, cv1, 0.0f, (float)(x + 8), (float)(y + 8),
                            base, cu1, cv1, 0.0f, (float)(x + 8),
                            (float)(y + 8), top, cu1, cv0, 0.0f,
                            (float)(x + 8), (float)y, top, cu0, cv0, 0.0f,
                            right_shade, right_shade * 0.78f))
                return false;
            }
          }
        }
        continue;
      }

      // Upright: the south face reconstructs the drawing. Solid tiles are
      // boxes; cut-out props are extruded per pixel so their silhouette and
      // holes survive.
      if (cell->coverage < 56) {
        int step = st->render.mesh_detail;
        if (step < 1 || step > 8)
          step = 1;
        for (int py = 0; py < 8; py += step) {
          for (int px = 0; px < 8; px += step) {
            int sx = -1, sy = -1;
            for (int by = 0; by < step && sy < 0; by++) {
              for (int bx = 0; bx < step; bx++) {
                if (cell->alpha[(py + by) * 8 + px + bx]) {
                  sx = px + bx;
                  sy = py + by;
                  break;
                }
              }
            }
            if (sx < 0)
              continue;
            float pu = u0 + ((float)sx + 0.5f) / 8.0f * (u1 - u0);
            float pv = v0 + ((float)sy + 0.5f) / 8.0f * (v1 - v0);
            float x0 = (float)(x + px);
            float y0 = (float)(y + py + step);
            if (!SideQuadWrapped(&st->mesh_bg, x0, y0, base, pu, v1, 0.0f,
                          x0 + (float)step, y0, base, pu, v1, 0.0f,
                          x0 + (float)step, y0, top, pu, v0, 0.0f, x0, y0,
                          top, pu, v0, 0.0f, front_shade,
                          front_shade * 0.85f))
              return false;
            if (!QuadMesh(&st->mesh_bg, x0, y0 - (float)step, top, pu, pv,
                          0.0f, x0 + (float)step, y0 - (float)step, top, pu,
                          pv, 0.0f, x0 + (float)step, y0, top, pu, pv, 0.0f,
                          x0, y0, top, pu, pv, 0.0f, top_shade))
              return false;
          }
        }
        continue;
      }

      // Solid box: the whole tile art wraps the faces. The top always keeps
      // the 2D drawing; the vertical faces darken it but never replace it
      // with a mean colour, so cliffs keep their slope stripes and flower
      // beds keep their flowers.
      if (!SideQuadWrapped(&st->mesh_bg, (float)x, (float)(y + 8), base, u0, v1,
                    0.0f, (float)(x + 8), (float)(y + 8), base, u1, v1, 0.0f,
                    (float)(x + 8), (float)(y + 8), top, u1, v0, 0.0f,
                    (float)x, (float)(y + 8), top, u0, v0, 0.0f, front_shade,
                    front_shade * 0.92f))
        return false;

      int top_slot = cell->slot;
      if (top_slot >= 0) {
        float cu0, cv0, cu1, cv1;
        AtlasSlotUV(top_slot, &cu0, &cv0, &cu1, &cv1);
        if (!QuadMesh(&st->mesh_bg, (float)x, (float)y, top, cu0, cv0, 0.0f,
                      (float)(x + 8), (float)y, top, cu1, cv0, 0.0f,
                      (float)(x + 8), (float)(y + 8), top, cu1, cv1, 0.0f,
                      (float)x, (float)(y + 8), top, cu0, cv1, 0.0f, top_shade))
          return false;
      }

      bool need_north = north == NULL || north->mode == kVoxelCell_Empty ||
          north->top_z < top - 0.1f;
      bool need_west = west == NULL || west->mode == kVoxelCell_Empty ||
          west->top_z < top - 0.1f;
      bool need_east = east == NULL || east->mode == kVoxelCell_Empty ||
          east->top_z < top - 0.1f;
      if (need_north) {
        int slot = cell->slot;
        if (slot >= 0) {
          float cu0, cv0, cu1, cv1;
          AtlasSlotUV(slot, &cu0, &cv0, &cu1, &cv1);
          if (!SideQuadWrapped(&st->mesh_bg, (float)x, (float)y, base, cu0, cv1,
                        0.0f, (float)(x + 8), (float)y, base, cu1, cv1, 0.0f,
                        (float)(x + 8), (float)y, top, cu1, cv0, 0.0f,
                        (float)x, (float)y, top, cu0, cv0, 0.0f, back_shade,
                        back_shade * 0.78f))
            return false;
        }
      }
      if (need_west) {
        int slot = cell->slot;
        if (slot >= 0) {
          float cu0, cv0, cu1, cv1;
          AtlasSlotUV(slot, &cu0, &cv0, &cu1, &cv1);
          if (!SideQuadWrapped(&st->mesh_bg, (float)x, (float)y, base, cu0, cv1,
                        0.0f, (float)x, (float)(y + 8), base, cu1, cv1, 0.0f,
                        (float)x, (float)(y + 8), top, cu1, cv0, 0.0f,
                        (float)x, (float)y, top, cu0, cv0, 0.0f, left_shade,
                        left_shade * 0.78f))
            return false;
        }
      }
      if (need_east) {
        int slot = cell->slot;
        if (slot >= 0) {
          float cu0, cv0, cu1, cv1;
          AtlasSlotUV(slot, &cu0, &cv0, &cu1, &cv1);
          if (!SideQuadWrapped(&st->mesh_bg, (float)(x + 8), (float)y, base, cu0,
                        cv1, 0.0f, (float)(x + 8), (float)(y + 8), base, cu1,
                        cv1, 0.0f, (float)(x + 8), (float)(y + 8), top, cu1,
                        cv0, 0.0f, (float)(x + 8), (float)y, top, cu0, cv0,
                        0.0f, right_shade, right_shade * 0.78f))
            return false;
        }
      }
    }
  }
  return true;
}

static bool BuildBg3(VoxelState *st, const FrameDesc *desc, int cols, int rows,
                     int extra, int margin_left, int content_right) {
  if (!(desc->ppu.screen_enabled[0] & 4))
    return true;
  uint8_t tile_pixels[8 * 8 * 4];
  for (int row = 0; row < rows; row++) {
    uint16_t hscroll, vscroll;
    FrameDesc_GetBgScrollAtLine(desc, 2, row * 8, &hscroll, &vscroll);
    // The PPU renders output scanline y from source line y + 1 (the first
    // line of the frame is latched one line late), so the layer origin is
    // one pixel above the raw scroll remainder. Keep the same convention as
    // the reference frame or a flat voxel view drifts by one pixel.
    int scroll_y = (int)vscroll + 1;
    for (int col = 0; col < cols; col++) {
      int pane_x = extra - ((int)hscroll & 7) + col * 8;
      int pane_y = row * 8 - (scroll_y & 7);
      if (pane_x + 8 <= margin_left || pane_x >= content_right)
        continue;
      int screen_x = col * 8;
      int screen_y = row * 8 + 1;
      uint16_t words[16];
      int bpp = 2, palette = 0, tile_num = 0;
      bool flip_x = false, flip_y = false;
      FetchBgTile(desc, 2, screen_x, screen_y, hscroll, vscroll, words, &bpp,
                  &palette, &flip_x, &flip_y, &tile_num);
      int palette_base = palette * (bpp == 4 ? 16 : 4);
      int coverage = 0;
      bool opaque = DecodeTilePixels(desc, words, bpp, palette_base, flip_x,
                                     flip_y, tile_pixels, NULL, &coverage,
                                     NULL, NULL, NULL, NULL);
      if (!opaque)
        continue;
      int slot = AtlasAdd(st, tile_pixels);
      if (slot < 0)
        continue;
      float u0, v0, u1, v1;
      AtlasSlotUV(slot, &u0, &v0, &u1, &v1);
      if (!QuadMesh(&st->mesh_2d, (float)pane_x, (float)pane_y, 0.0f, u0, v0,
                    0.0f, (float)(pane_x + 8), (float)pane_y, 0.0f, u1, v0,
                    0.0f, (float)(pane_x + 8), (float)(pane_y + 8), 0.0f, u1,
                    v1, 0.0f, (float)pane_x, (float)(pane_y + 8), 0.0f, u0, v1,
                    0.0f, 1.0f))
        return false;
    }
  }
  return true;
}

// The visible BG2 screen is painted once as a distant panel behind the 3D
// scene. In the overworld that panel carries the sunset/sky and the parallax
// band that sits above the walkable room, so the tilted camera shows a back
// wall instead of the clear colour. It uses only the current screen.
static bool AddFogPanel(VoxelState *st, const FrameDesc *desc) {
  // Base layer for the whole pane: a dark sky/fog gradient. It is hidden by
  // world geometry where the area/room reaches, and replaces the flat clear
  // colour at the 16:9 edges or beyond the loaded area.
  uint32_t base = BackdropColor(desc);
  int br = (int)((base >> 16) & 0xff);
  int bg = (int)((base >> 8) & 0xff);
  int bb = (int)(base & 0xff);
  bool room_fill = desc->semantics.indoors && st->fill_count != 0;
  if (room_fill) {
    // A dungeon room is surrounded by its own wall/floor art in the 2D
    // frame. Fill the empty part of the diorama with the room's average
    // colour instead of a near-black sky; that is what the SNES picture
    // shows around the room, and it keeps the tilt from reading as a dark
    // hole.
    br = (int)(st->fill_r / st->fill_count);
    bg = (int)(st->fill_g / st->fill_count);
    bb = (int)(st->fill_b / st->fill_count);
  }
  uint8_t tile[8 * 8 * 4];
  for (int y = 0; y < 8; y++) {
    float t = (float)y / 7.0f;
    int r, g, bl;
    if (room_fill) {
      float shade = 1.0f - 0.06f * t;
      r = (int)(br * shade);
      g = (int)(bg * shade);
      bl = (int)(bb * shade);
    } else {
      r = (int)(br * 0.18f + 8.0f * (1.0f - t));
      g = (int)(bg * 0.18f + 10.0f * (1.0f - t));
      bl = (int)(bb * 0.18f + 14.0f * (1.0f - t));
    }
    for (int x = 0; x < 8; x++) {
      uint8_t *p = tile + (y * 8 + x) * 4;
      p[0] = (uint8_t)ClampFloat((float)r, 0.0f, 255.0f);
      p[1] = (uint8_t)ClampFloat((float)g, 0.0f, 255.0f);
      p[2] = (uint8_t)ClampFloat((float)bl, 0.0f, 255.0f);
      p[3] = 255;
    }
  }
  int slot = AtlasAdd(st, tile);
  if (slot < 0)
    return true;
  float u0, v0, u1, v1;
  AtlasSlotUV(slot, &u0, &v0, &u1, &v1);
  return QuadMesh(&st->mesh_backdrop, 0.0f, 0.0f, 0.0f, u0, v0, 0.0f,
                  (float)desc->width, 0.0f, 0.0f, u1, v0, 0.0f,
                  (float)desc->width, (float)desc->height, 0.0f, u1, v1,
                  0.0f, 0.0f, (float)desc->height, 0.0f, u0, v1, 0.0f, 1.0f);
}

// At 0 degrees the frame is the SNES picture: the 256x224 content is centred
// and everything outside it is black, exactly like the reference renderer.
// Painting the dark 3D sky there would leave grey bars around an otherwise
// perfect flat frame.
static bool BuildFlatBackdrop(VoxelState *st, const FrameDesc *desc) {
  int slot = AtlasColor(st, 0x000000);
  if (slot < 0)
    return true;
  float u0, v0, u1, v1;
  AtlasSlotUV(slot, &u0, &v0, &u1, &v1);
  return QuadMesh(&st->mesh_backdrop, 0.0f, 0.0f, 0.0f, u0, v0, 0.0f,
                  (float)desc->width, 0.0f, 0.0f, u1, v0, 0.0f,
                  (float)desc->width, (float)desc->height, 0.0f, u1, v1,
                  0.0f, 0.0f, (float)desc->height, 0.0f, u0, v1, 0.0f, 1.0f);
}

static bool BuildBackdrop(VoxelState *st, const FrameDesc *desc, int cols,
                          int rows, int extra, int margin_left,
                          int content_right) {
  if (desc->semantics.indoors)
    return true;
  if (!(desc->ppu.screen_enabled[0] & 2) ||
      !desc->semantics.room_layer[1].present)
    return true;
  const int layer = 1;
  uint8_t tile_pixels[8 * 8 * 4];
  for (int row = 0; row < rows; row++) {
    uint16_t hscroll, vscroll;
    FrameDesc_GetBgScrollAtLine(desc, layer, row * 8, &hscroll, &vscroll);
    int scroll_y = (int)vscroll + 1;
    for (int col = 0; col < cols; col++) {
      int pane_x = extra - ((int)hscroll & 7) + col * 8;
      int pane_y = row * 8 - (scroll_y & 7);
      if (pane_x + 8 <= margin_left || pane_x >= content_right)
        continue;
      uint16_t words[16];
      int bpp = 4, palette = 0, tile_num = 0;
      bool flip_x = false, flip_y = false;
      FetchBgTile(desc, layer, col * 8, row * 8 + 1, hscroll, vscroll,
                  words, &bpp, &palette, &flip_x, &flip_y, &tile_num);
      int palette_base = palette * (bpp == 4 ? 16 : 4);
      int coverage = 0;
      bool opaque = DecodeTilePixels(desc, words, bpp, palette_base, flip_x,
                                     flip_y, tile_pixels, NULL, &coverage,
                                     NULL, NULL, NULL, NULL);
      if (!opaque)
        continue;
      int slot = AtlasAdd(st, tile_pixels);
      if (slot < 0)
        continue;
      float u0, v0, u1, v1;
      AtlasSlotUV(slot, &u0, &v0, &u1, &v1);
      if (!QuadMesh(&st->mesh_backdrop, (float)pane_x, (float)pane_y, 0.0f,
                    u0, v0, 0.0f, (float)(pane_x + 8), (float)pane_y, 0.0f,
                    u1, v0, 0.0f, (float)(pane_x + 8),
                    (float)(pane_y + 8), 0.0f, u1, v1, 0.0f, (float)pane_x,
                    (float)(pane_y + 8), 0.0f, u0, v1, 0.0f, 1.0f))
        return false;
    }
  }
  return true;
}


// Flat camera (0 degrees): paint the visible BG layers as plain 2D panels.
// A top-down voxel has to read exactly like the SNES frame; the 3D height
// geometry is meaningless from straight above.
static bool BuildFlatPanel(VoxelState *st, const FrameDesc *desc, int cols,
                           int rows, int extra, int margin_left,
                           int content_right, int content_height) {
  uint8_t tile_pixels[8 * 8 * 4];
  for (int pass = 0; pass < 2; pass++) {
    int layer = pass == 0 ? 1 : 0;
    if (!(desc->ppu.screen_enabled[0] & (1 << layer)))
      continue;
    for (int row = 0; row < rows; row++) {
      uint16_t hscroll, vscroll;
      FrameDesc_GetBgScrollAtLine(desc, layer, row * 8, &hscroll, &vscroll);
      int scroll_y = (int)vscroll + 1;
      for (int col = 0; col < cols; col++) {
        int pane_x = extra - ((int)hscroll & 7) + col * 8;
        int pane_y = row * 8 - (scroll_y & 7);
        if (pane_x + 8 <= margin_left || pane_x >= content_right)
          continue;
        if (pane_y >= content_height)
          continue;
        int clip_h = 8;
        if (pane_y + clip_h > content_height)
          clip_h = content_height - pane_y;
        if (clip_h <= 0)
          continue;
        uint16_t words[16];
        int bpp = 4, palette = 0, tile_num = 0;
        bool flip_x = false, flip_y = false;
        FetchBgTile(desc, layer, col * 8, row * 8 + 1, hscroll, vscroll,
                    words, &bpp, &palette, &flip_x, &flip_y, &tile_num);
        int palette_base = palette * (bpp == 4 ? 16 : 4);
        int coverage = 0;
        bool opaque = DecodeTilePixels(desc, words, bpp, palette_base, flip_x,
                                       flip_y, tile_pixels, NULL, &coverage,
                                       NULL, NULL, NULL, NULL);
        if (!opaque)
          continue;
        int slot = AtlasAdd(st, tile_pixels);
        if (slot < 0)
          continue;
        float u0, v0, u1, v1;
        AtlasSlotUV(slot, &u0, &v0, &u1, &v1);
        float vclip = v0 + (v1 - v0) * (float)clip_h / 8.0f;
        float bottom = (float)(pane_y + clip_h);
        if (!QuadMesh(&st->mesh_backdrop, (float)pane_x, (float)pane_y, 0.0f,
                      u0, v0, 0.0f, (float)(pane_x + 8), (float)pane_y, 0.0f,
                      u1, v0, 0.0f, (float)(pane_x + 8),
                      bottom, 0.0f, u1, vclip, 0.0f, (float)pane_x,
                      bottom, 0.0f, u0, vclip, 0.0f, 1.0f))
          return false;
      }
    }
  }
  return true;
}

// BG1 on the subscreen is the weather/fog/sunset layer. In the 2D frame it is
// added (or subtracted) by colour math over the main screen; draw it as a
// translucent screen panel after the 3D pass so the same blue fog, rain and
// sunset survive.
static bool BuildSubscreenOverlay(VoxelState *st, const FrameDesc *desc,
                                  int cols, int rows, int extra,
                                  int margin_left, int content_right) {
  (void)cols;
  (void)margin_left;
  (void)content_right;
  if (desc->semantics.indoors ||
      !(desc->ppu.screen_enabled[1] & 1) ||
      (desc->ppu.screen_enabled[0] & 1) ||
      !desc->ppu.add_subscreen)
    return true;
  st->overlay_scale = 1.0f;
  st->overlay_half = desc->ppu.half_color;
  st->overlay_subtract = desc->ppu.subtract_color;
  uint8_t tile_pixels[8 * 8 * 4];
  // The subscreen layer is a weather/fog/cloud sheet over the whole world.
  // It used to be clamped to the 256-pixel SNES window, which left an opaque
  // rectangle in the middle of a widescreen frame. Tile the same repeating
  // BG1 map out to both edges instead (and one row above/below), keeping the
  // scroll anchor so the sheet moves with the world.
  int side_cells = (int)((desc->width + 7) / 8) + 2;
  int row_first = -1;
  int row_last = rows;
  for (int row = row_first; row <= row_last; row++) {
    uint16_t hscroll, vscroll;
    FrameDesc_GetBgScrollAtLine(desc, 0, row * 8, &hscroll, &vscroll);
    int scroll_y = (int)vscroll + 1;
    int pane_y = row * 8 - (scroll_y & 7);
    if (pane_y + 8 <= 0 || pane_y >= (int)desc->height)
      continue;
    for (int col = -side_cells; col < side_cells; col++) {
      int pane_x = extra - ((int)hscroll & 7) + col * 8;
      if (pane_x + 8 <= 0 || pane_x >= (int)desc->width)
        continue;
      int sample_x = col * 8;
      int sample_y = row * 8 + 1;
      if (desc->ppu.bg[0].wider || desc->ppu.bg[0].higher) {
        sample_x %= 512;
        if (sample_x < 0)
          sample_x += 512;
        sample_y %= 512;
        if (sample_y < 0)
          sample_y += 512;
      } else {
        sample_x %= 256;
        if (sample_x < 0)
          sample_x += 256;
        sample_y %= 256;
        if (sample_y < 0)
          sample_y += 256;
      }
      uint16_t words[16];
      int bpp = 4, palette = 0, tile_num = 0;
      bool flip_x = false, flip_y = false;
      FetchBgTile(desc, 0, sample_x, sample_y, hscroll, vscroll, words,
                  &bpp, &palette, &flip_x, &flip_y, &tile_num);
      int palette_base = palette * (bpp == 4 ? 16 : 4);
      int coverage = 0;
      bool opaque = DecodeTilePixels(desc, words, bpp, palette_base, flip_x,
                                     flip_y, tile_pixels, NULL, &coverage,
                                     NULL, NULL, NULL, NULL);
      if (!opaque)
        continue;
      int slot = AtlasAdd(st, tile_pixels);
      if (slot < 0)
        continue;
      float u0, v0, u1, v1;
      AtlasSlotUV(slot, &u0, &v0, &u1, &v1);
      if (!QuadMesh(&st->mesh_overlay, (float)pane_x, (float)pane_y, 0.0f,
                    u0, v0, 0.0f, (float)(pane_x + 8), (float)pane_y, 0.0f,
                    u1, v0, 0.0f, (float)(pane_x + 8),
                    (float)(pane_y + 8), 0.0f, u1, v1, 0.0f, (float)pane_x,
                    (float)(pane_y + 8), 0.0f, u0, v1, 0.0f,
                    st->overlay_scale))
        return false;
    }
  }
  return true;
}

static bool BuildSpriteCard2D(VoxelState *st, const FrameDesc *desc,
                              const FrameDescSprite *s, int extra,
                              int margin_left, int content_right) {
  int pane_x = s->x + extra;
  int pane_y = s->y;
  if (pane_x + s->size <= margin_left || pane_x >= content_right)
    return true;
  uint8_t tile_pixels[8 * 8 * 4];
  for (int sub_y = 0; sub_y < s->size; sub_y += 8) {
    for (int sub_x = 0; sub_x < s->size; sub_x += 8) {
      uint16_t words[16];
      int palette_base = 0;
      bool flip_x = false, flip_y = false;
      int used_tile = 0;
      if (!FetchSpriteTile(desc, s, sub_x, sub_y, words, &palette_base,
                           &flip_x, &flip_y, &used_tile))
        continue;
      int coverage = 0;
      bool opaque = DecodeTilePixels(desc, words, 4, palette_base, flip_x,
                                     flip_y, tile_pixels, NULL, &coverage,
                                     NULL, NULL, NULL, NULL);
      if (!opaque)
        continue;
      int slot = AtlasAdd(st, tile_pixels);
      if (slot < 0)
        continue;
      float u0, v0, u1, v1;
      AtlasSlotUV(slot, &u0, &v0, &u1, &v1);
      float x = (float)(pane_x + sub_x);
      float y = (float)(pane_y + sub_y);
      if (!QuadMesh(&st->mesh_2d, x, y, 0.0f, u0, v0, 0.0f, x + 8.0f, y, 0.0f,
                    u1, v0, 0.0f, x + 8.0f, y + 8.0f, 0.0f, u1, v1, 0.0f, x,
                    y + 8.0f, 0.0f, u0, v1, 0.0f, 1.0f))
        return false;
    }
  }
  return true;
}

static bool BuildSpriteCard3D(VoxelState *st, const FrameDesc *desc,
                              const FrameDescSprite *s, int extra,
                              const VoxelGlCamera *camera) {
  int pane_x = s->x + extra;
  float z = s->z == -1 ? 0.0f : (float)s->z;
  float ground_y = (float)(s->y + s->size) + z;
  float right_x = cosf(camera->yaw);
  float right_y = -sinf(camera->yaw);
  float forward_x = sinf(camera->yaw);
  float forward_y = -cosf(camera->yaw);
  float cos_e = camera->cos_elev > 0.2f ? camera->cos_elev : 0.2f;
  float z_step = st->render.sprite_scale / cos_e;
  float lean = (float)s->size * 0.14f;
  float cx = (float)pane_x + (float)s->size * 0.5f;
  float cy = ground_y;
  uint8_t tile_pixels[8 * 8 * 4];
  for (int sub_y = 0; sub_y < s->size; sub_y += 8) {
    for (int sub_x = 0; sub_x < s->size; sub_x += 8) {
      uint16_t words[16];
      int palette_base = 0;
      bool flip_x = false, flip_y = false;
      int used_tile = 0;
      if (!FetchSpriteTile(desc, s, sub_x, sub_y, words, &palette_base,
                           &flip_x, &flip_y, &used_tile))
        continue;
      int coverage = 0;
      bool opaque = DecodeTilePixels(desc, words, 4, palette_base, flip_x,
                                     flip_y, tile_pixels, NULL, &coverage,
                                     NULL, NULL, NULL, NULL);
      if (!opaque)
        continue;
      int slot = AtlasAdd(st, tile_pixels);
      if (slot < 0)
        continue;
      float u0, v0, u1, v1;
      AtlasSlotUV(slot, &u0, &v0, &u1, &v1);
      float z0 = z + (float)(s->size - sub_y - 8) * z_step;
      float z1 = z + (float)(s->size - sub_y) * z_step;
      // Sprite cards carry the game's own palette. Keep them at full colour
      // so Link stays green and the soldier blue; the world light only tints
      // the geometry around them.
      float shade = 1.0f;
      // The card faces the camera and leans back slightly. The offset grows
      // with height, so the whole card is one plane instead of a stack of
      // independently tilted 8x8 bands.
      float total = (float)(s->size * z_step) + 0.001f;
      float hang0 = lean * (z0 - z) / total;
      float hang1 = lean * (z1 - z) / total;
      float botx0 = cx - (float)s->size * 0.5f * right_x +
          (float)(sub_x) * right_x + forward_x * hang0;
      float boty0 = cy - (float)s->size * 0.5f * right_y +
          (float)(sub_x) * right_y + forward_y * hang0;
      float botx1 = botx0 + 8.0f * right_x;
      float boty1 = boty0 + 8.0f * right_y;
      float topx0 = cx - (float)s->size * 0.5f * right_x +
          (float)(sub_x) * right_x + forward_x * hang1;
      float topy0 = cy - (float)s->size * 0.5f * right_y +
          (float)(sub_x) * right_y + forward_y * hang1;
      float topx1 = topx0 + 8.0f * right_x;
      float topy1 = topy0 + 8.0f * right_y;
      if (!QuadMesh(&st->mesh_sprites, botx0, boty0, z0, u0, v1, 0.004f,
                    botx1, boty1, z0, u1, v1, 0.004f, topx1, topy1, z1, u1,
                    v0, 0.004f, topx0, topy0, z1, u0, v0, 0.004f, shade))
        return false;
    }
  }
  return true;
}

static bool BuildSprites(VoxelState *st, const FrameDesc *desc, int extra,
                         int margin_left, int content_right,
                         const VoxelGlCamera *camera) {
  for (uint32_t i = 0; i < desc->sprite_count; i++) {
    const FrameDescSprite *s = &desc->sprites[i];
    if (s->size != 8 && s->size != 16)
      continue;
    if (st->flat_mode || s->owner_kind == kFrameDescOwner_None) {
      if (!BuildSpriteCard2D(st, desc, s, extra, margin_left, content_right))
        return false;
    } else {
      if (!BuildSpriteCard3D(st, desc, s, extra, camera))
        return false;
    }
  }
  return true;
}

static const FrameDescSprite *FindLinkSprite(const FrameDesc *desc) {
  const FrameDescSprite *best16 = NULL;
  const FrameDescSprite *first = NULL;
  for (uint32_t i = 0; i < desc->sprite_count; i++) {
    const FrameDescSprite *s = &desc->sprites[i];
    if (s->owner_kind != kFrameDescOwner_Link)
      continue;
    if (first == NULL)
      first = s;
    if (s->size == 16 &&
        (best16 == NULL || s->y + s->size > best16->y + best16->size))
      best16 = s;
  }
  return best16 != NULL ? best16 : first;
}

static void ProjectWorldPoint(const VoxelGlCamera *camera, float x, float y,
                              float z, float *out_x, float *out_y) {
  float dx = x - camera->center_x;
  float dy = y - camera->center_y;
  float c = cosf(camera->yaw);
  float s = sinf(camera->yaw);
  float rx = dx * c - dy * s;
  float ry = dx * s + dy * c;
  float zz = z * camera->zscale;
  float view_z = camera->distance - ry * camera->cos_elev -
      zz * camera->sin_elev;
  if (view_z < 1.0f)
    view_z = 1.0f;
  float up = -ry * camera->sin_elev + zz * camera->cos_elev;
  float scale = camera->zoom * camera->distance / view_z;
  float nx = rx * scale / (camera->width * 0.5f);
  float ny = up * scale / (camera->height * 0.5f);
  *out_x = (nx + 1.0f) * camera->width * 0.5f;
  *out_y = (1.0f - ny) * camera->height * 0.5f;
}

static VoxelGlCamera MakeCamera(VoxelState *st, const FrameDesc *desc) {
  VoxelGlCamera camera;
  float width = (float)desc->width;
  float height = (float)desc->height;
  float extra = (float)desc->display.extra_left_right;
  float left_cur = (float)desc->display.extra_left_cur;
  float right_cur = (float)desc->display.extra_right_cur;
  float margin_left = extra - left_cur;
  float content_width = 256.0f + left_cur + right_cur;
  camera.width = width;
  camera.height = height;
  camera.center_x = margin_left + content_width * 0.5f;
  camera.center_y = height * 0.5f;
  camera.yaw = ClampFloat(st->camera_render.yaw_deg, -60.0f, 60.0f) *
      3.14159265358979323846f / 180.0f;
  camera.zoom = ClampFloat(st->camera_render.zoom, 0.5f, 2.5f);
  if (desc->semantics.indoors && st->camera_render.angle_deg < 80.0f)
    camera.zoom *= 1.55f;
  camera.distance = ClampFloat(st->camera_render.distance_scale, 0.25f, 4.0f) *
      620.0f;
  camera.zscale = ClampFloat(st->camera_render.height_scale, 0.2f, 4.0f);
  float elevation = ClampFloat(st->camera_render.angle_deg, 5.0f, 90.0f);
  if (desc->semantics.indoors && elevation < 80.0f &&
      elevation > st->camera_render.indoor_angle_deg)
    elevation = ClampFloat(st->camera_render.indoor_angle_deg, 5.0f, 90.0f);
  float er = elevation * 3.14159265358979323846f / 180.0f;
  camera.cos_elev = cosf(er);
  camera.sin_elev = sinf(er);

  // Anchor the camera so Link's feet land where the OAM put them in the 2D
  // frame. The old "nudge toward Link" moved the whole scene away from the
  // game camera; solving the inverse projection keeps the room and Link
  // aligned at every tilt.
  if (st->camera_render.follow_link) {
    const FrameDescSprite *link = FindLinkSprite(desc);
    if (link != NULL) {
      float link_z = link->z == -1 ? 0.0f : (float)link->z;
      float point_x = (float)link->x + extra + (float)link->size * 0.5f;
      float point_y = (float)link->y + (float)link->size + link_z;
      float sx = (float)link->x + extra + (float)link->size * 0.5f;
      float sy = (float)link->y + (float)link->size;
      // Solve the inverse projection directly. The old six-step nudge
      // diverged once the indoor zoom or a longer lens pushed the effective
      // scale above ~1, leaving Link tens of pixels off centre.
      float zz = link_z * camera.zscale;
      float k = camera.zoom * camera.distance;
      float vc = camera.distance - zz * camera.sin_elev;
      float a = sx - camera.width * 0.5f;
      float b = camera.height * 0.5f - sy;
      float denom = camera.sin_elev * k - b * camera.cos_elev;
      float dx = 0.0f, dy = 0.0f;
      if (fabsf(denom) > 0.0001f && fabsf(k) > 0.0001f) {
        dy = (zz * camera.cos_elev * k - b * vc) / denom;
        dx = a * (vc - dy * camera.cos_elev) / k;
        // The solution above is for yaw 0. Rotate the offset back by the
        // camera yaw so the projected point stays anchored while turning.
        float c = cosf(camera.yaw);
        float s = sinf(camera.yaw);
        float wx = dx * c + dy * s;
        float wy = -dx * s + dy * c;
        dx = wx;
        dy = wy;
      }
      camera.center_x = point_x - dx;
      camera.center_y = point_y - dy;
    }
  }
  return camera;
}

static void LogBackground(const FrameDesc *desc) {
  const char *path = getenv("ZELDA3_VOXEL_LINK_LOG");
  if (path == NULL || *path == 0)
    return;
  uint32_t rgb = BackdropColor(desc);
  FILE *f = fopen(path, "a");
  if (f == NULL)
    return;
  fprintf(f, "bg %u %u %u\n", (unsigned)((rgb >> 16) & 0xff),
          (unsigned)((rgb >> 8) & 0xff), (unsigned)(rgb & 0xff));
  fclose(f);
}

static void LogLinkProjection(const FrameDesc *desc,
                              const VoxelGlCamera *camera) {
  const char *path = getenv("ZELDA3_VOXEL_LINK_LOG");
  if (path == NULL || *path == 0)
    return;
  const FrameDescSprite *best = NULL;
  for (uint32_t i = 0; i < desc->sprite_count; i++) {
    const FrameDescSprite *s = &desc->sprites[i];
    if (s->owner_kind != kFrameDescOwner_Link || s->size != 16)
      continue;
    if (best == NULL || s->y + s->size > best->y + best->size)
      best = s;
  }
  if (best == NULL)
    return;
  float extra = (float)desc->display.extra_left_right;
  float z = best->z == -1 ? 0.0f : (float)best->z;
  float x = (float)best->x + extra + (float)best->size * 0.5f;
  float y = (float)best->y + (float)best->size + z;
  float sx, sy;
  ProjectWorldPoint(camera, x, y, z, &sx, &sy);
  FILE *f = fopen(path, "a");
  if (f == NULL)
    return;
  fprintf(f, "%u %.3f %.3f\n", desc->frame_index, sx, sy);
  fclose(f);
}

static bool BuildPane(VoxelState *st, const FrameDesc *desc) {
  st->slot_count = 0;
  st->color_count = 0;
  for (int i = 0; i < kVoxelAtlasHashSize; i++)
    st->atlas_hash[i].slot = -1;
  st->mesh_backdrop.count = 0;
  st->mesh_overlay.count = 0;
  st->overlay_scale = 0.0f;
  st->overlay_subtract = false;
  st->overlay_half = false;
  st->mesh_sprites.count = 0;
  st->mesh_2d.count = 0;
  st->fill_r = st->fill_g = st->fill_b = 0;
  st->fill_count = 0;

  int extra = (int)desc->display.extra_left_right;
  int left_cur = (int)desc->display.extra_left_cur;
  int right_cur = (int)desc->display.extra_right_cur;
  int margin_left = extra - left_cur;
  int content_right = margin_left + 256 + left_cur + right_cur;
  int cols = 256 / 8;
  int rows = (int)((desc->height + 7) / 8);
  if (rows > kFrameDesc_MaxTileRows)
    rows = kFrameDesc_MaxTileRows;

  VoxelGlCamera camera = MakeCamera(st, desc);
  if (getenv("ZELDA3_VOXEL_DEBUG_CELLS") && desc->semantics.indoors) {
    float fx, fy;
    ProjectWorldPoint(&camera, 204.0f, 124.0f, 0.0f, &fx, &fy);
    fprintf(stderr, "voxel-dbg cam center %.1f,%.1f proj floor %.1f,%.1f\n",
            camera.center_x, camera.center_y, fx, fy);
  }
  st->flat_mode = st->camera_render.angle_deg >= 80.0f;
  if (getenv("ZELDA3_VOXEL_DEBUG_CELLS"))
    fprintf(stderr, "voxel-dbg flat %d angle %.1f height %u extra_bottom %u\n",
            st->flat_mode, st->camera_render.angle_deg, desc->height,
            (unsigned)desc->ppu.extra_bottom_cur);
  st->interior_link_row = -1;
  int content_height = 224 + (int)desc->ppu.extra_bottom_cur;
  if (content_height > (int)desc->height)
    content_height = (int)desc->height;
  if (st->flat_mode) {
    if (!BuildFlatBackdrop(st, desc))
      return false;
  }

  if (!st->flat_mode) {
    int room_layer = -1;
    int room_layers = 0;
    for (int layer = 0; layer < 2; layer++) {
      if (!(desc->ppu.screen_enabled[0] & (1 << layer)))
        continue;
      room_layer = layer;
      room_layers++;
    }
    const FrameDescRoomLayer *room = room_layer >= 0
        ? &desc->semantics.room_layer[room_layer] : NULL;
    if (room_layers == 1 && room != NULL && room->present &&
        room->cols != 0 && room->rows != 0 &&
        !EnvBool("ZELDA3_VOXEL_NO_CACHE", false)) {
      int room_cols = room->cols > kFrameDesc_MaxRoomDim
          ? kFrameDesc_MaxRoomDim : room->cols;
      int room_rows = room->rows > kFrameDesc_MaxRoomDim
          ? kFrameDesc_MaxRoomDim : room->rows;
      uint64_t key = RoomCacheKey(st, desc, room_layer, room_cols,
                                  room_rows, extra);
      int sub_x = (int)desc->ppu.bg[room_layer].hscroll & 7;
      int sub_y = (int)desc->ppu.bg[room_layer].vscroll & 7;
      float origin_x = (float)(extra - sub_x +
          (room->base_tile_x - room->screen_origin_tile_x) * 8);
      float origin_y = (float)(-sub_y +
          (room->base_tile_y - room->screen_origin_tile_y) * 8);
      if (st->cache_valid && key == st->cache_key) {
        if (st->cache_atlas_rows > 0 && st->cache_atlas != NULL) {
          size_t bytes = (size_t)st->cache_atlas_rows *
              (size_t)kVoxelAtlasWidth * (size_t)kVoxelAtlasTile * 4u;
          memcpy(st->atlas, st->cache_atlas, bytes);
        }
        st->slot_count = st->cache_slot_count;
        st->color_count = 0;
        st->mesh_world = &st->mesh_bg;
        st->world_offset_x = st->cache_origin_x - origin_x;
        st->world_offset_y = st->cache_origin_y - origin_y;
        if (getenv("ZELDA3_VOXEL_DEBUG_CACHE"))
          fprintf(stderr, "voxel-cache hit key %llx origin %.1f,%.1f off %.1f,%.1f count %d\n",
                  (unsigned long long)key, origin_x, origin_y,
                  st->world_offset_x, st->world_offset_y,
                  st->mesh_bg.count);
        st->fill_r = st->cache_fill_r;
        st->fill_g = st->cache_fill_g;
        st->fill_b = st->cache_fill_b;
        st->fill_count = st->cache_fill_count;
        st->cache_hits++;
      } else {
        st->mesh_bg.count = 0;
        if (!BuildRoomLayer(st, desc, room_layer, extra))
          return false;
        if (getenv("ZELDA3_VOXEL_DEBUG_CACHE"))
          fprintf(stderr, "voxel-cache miss key %llx origin %.1f,%.1f count %d\n",
                  (unsigned long long)key, origin_x, origin_y,
                  st->mesh_bg.count);
        st->mesh_world = &st->mesh_bg;
        st->world_offset_x = 0.0f;
        st->world_offset_y = 0.0f;
        int atlas_rows = (st->slot_count + kVoxelAtlasCols - 1) /
            kVoxelAtlasCols;
        size_t atlas_bytes = (size_t)atlas_rows * (size_t)kVoxelAtlasWidth *
            (size_t)kVoxelAtlasTile * 4u;
        if (atlas_bytes > st->cache_atlas_size) {
          uint8_t *p = (uint8_t *)realloc(st->cache_atlas, atlas_bytes);
          if (p != NULL) {
            st->cache_atlas = p;
            st->cache_atlas_size = atlas_bytes;
          } else {
            atlas_rows = 0;
            atlas_bytes = 0;
          }
        }
        if (atlas_bytes != 0)
          memcpy(st->cache_atlas, st->atlas, atlas_bytes);
        st->cache_atlas_rows = atlas_rows;
        st->cache_slot_count = st->slot_count;
        st->cache_key = key;
        st->cache_origin_x = origin_x;
        st->cache_origin_y = origin_y;
        st->cache_fill_r = st->fill_r;
        st->cache_fill_g = st->fill_g;
        st->cache_fill_b = st->fill_b;
        st->cache_fill_count = st->fill_count;
        st->cache_valid = true;
        st->cache_misses++;
      }
    } else {
      st->mesh_bg.count = 0;
      st->mesh_world = &st->mesh_bg;
      st->world_offset_x = 0.0f;
      st->world_offset_y = 0.0f;
      st->cache_valid = false;
      for (int layer = 0; layer < 2; layer++) {
        if (!(desc->ppu.screen_enabled[0] & (1 << layer)))
          continue;
        if (!BuildRoomLayer(st, desc, layer, extra))
          return false;
      }
    }
    if (!AddFogPanel(st, desc))
      return false;
  } else {
    st->mesh_bg.count = 0;
    st->mesh_world = &st->mesh_bg;
    st->world_offset_x = 0.0f;
    st->world_offset_y = 0.0f;
    st->cache_valid = false;
  }

  if (st->flat_mode) {
    // A flat view is the 2D frame: the PPU only paints the 224-line picture
    // and leaves the 16-pixel widescreen bottom strip black. Drawing those
    // extra rows would make the top-down voxel differ from the source.
    int flat_rows = content_height / 8 + 1;
    if (!BuildFlatPanel(st, desc, cols, flat_rows, extra, margin_left,
                        content_right, content_height))
      return false;
  } else if (!BuildBackdrop(st, desc, cols, rows, extra, margin_left,
                            content_right)) {
    return false;
  }
  if (!BuildSubscreenOverlay(st, desc, cols, rows, extra, margin_left,
                             content_right))
    return false;
  if (!BuildBg3(st, desc, cols, rows, extra, margin_left, content_right))
    return false;
  if (!BuildSprites(st, desc, extra, margin_left, content_right, &camera))
    return false;
  return true;
}

// ---------------------------------------------------------------------------
// Interactive controls

static void VoxelLogStatus(VoxelState *st, const char *what) {
  char message[192];
  snprintf(message, sizeof(message),
           "voxel: %s (yaw %.0f, elevation %.0f, tilt %.0f, zoom %.2f, "
           "shadows %s)",
           what, st->camera.yaw_deg, st->camera.angle_deg,
           90.0f - st->camera.angle_deg, st->camera.zoom,
           st->shadow_settings.enabled ? "on" : "off");
  HostLog(st, 0, message);
}

static void SetCameraLevel(VoxelState *st, float tilt_deg) {
  st->camera.angle_deg = ClampFloat(90.0f - tilt_deg, 10.0f, 89.0f);
}

static bool VoxelCommand(ZeldaRenderPlugin *plugin, int command) {
  VoxelState *st = (VoxelState *)plugin->state;
  switch (command) {
  case kRenderCommand_CameraRotateLeft:
    st->camera.yaw_deg = ClampFloat(st->camera.yaw_deg - 5.0f, -60.0f, 60.0f);
    VoxelLogStatus(st, "camera turned left");
    return true;
  case kRenderCommand_CameraRotateRight:
    st->camera.yaw_deg = ClampFloat(st->camera.yaw_deg + 5.0f, -60.0f, 60.0f);
    VoxelLogStatus(st, "camera turned right");
    return true;
  case kRenderCommand_CameraTiltUp:
    st->camera.angle_deg = ClampFloat(st->camera.angle_deg + 5.0f, 10.0f, 89.0f);
    VoxelLogStatus(st, "camera tilted up");
    return true;
  case kRenderCommand_CameraTiltDown:
    st->camera.angle_deg = ClampFloat(st->camera.angle_deg - 5.0f, 10.0f, 89.0f);
    VoxelLogStatus(st, "camera tilted down");
    return true;
  case kRenderCommand_CameraZoomIn:
    st->camera.zoom = ClampFloat(st->camera.zoom * 1.1f, 0.5f, 2.5f);
    VoxelLogStatus(st, "camera zoomed in");
    return true;
  case kRenderCommand_CameraZoomOut:
    st->camera.zoom = ClampFloat(st->camera.zoom / 1.1f, 0.5f, 2.5f);
    VoxelLogStatus(st, "camera zoomed out");
    return true;
  case kRenderCommand_CameraLevelLow:
    SetCameraLevel(st, 15.0f);
    VoxelLogStatus(st, "camera level 15");
    return true;
  case kRenderCommand_CameraLevelMid:
    SetCameraLevel(st, 35.0f);
    VoxelLogStatus(st, "camera level 35");
    return true;
  case kRenderCommand_CameraLevelHigh:
    SetCameraLevel(st, 50.0f);
    VoxelLogStatus(st, "camera level 50");
    return true;
  case kRenderCommand_CameraReset:
    st->camera = st->default_camera;
    st->camera_render = st->default_camera;
    st->light_settings = st->default_light_settings;
    st->shadow_settings = st->default_shadow_settings;
    st->light = MakeLight(&st->light_settings);
    VoxelLogStatus(st, "camera reset");
    return true;
  case kRenderCommand_ToggleShadows:
    st->shadow_settings.enabled = !st->shadow_settings.enabled;
    VoxelLogStatus(st, st->shadow_settings.enabled ? "shadows on"
                                                   : "shadows off");
    return true;
  case kRenderCommand_ToggleLighting:
    st->light_settings.enabled = !st->light_settings.enabled;
    st->light = MakeLight(&st->light_settings);
    VoxelLogStatus(st, st->light_settings.enabled ? "lighting on"
                                                  : "lighting off");
    return true;
  default:
    return false;
  }
}

// ---------------------------------------------------------------------------
// Plugin lifecycle

static bool PaneNeedsFallback(const FrameDesc *desc) {
  if (desc == NULL || !desc->complete || desc->version != kFrameDescVersion)
    return true;
  if (desc->ppu.mode != 1 || desc->ppu.forced_blank)
    return true;
  if (desc->semantics.mode7)
    return true;
  if (desc->vram == NULL || desc->cgram == NULL)
    return true;
  if (desc->ppu.mosaic_enabled != 0)
    return true;
  if (desc->semantics.room == 0 && desc->semantics.area == 0 &&
      desc->semantics.link_x == 0 && desc->semantics.link_y == 0)
    return true;
  return false;
}

static uint32_t BackdropColor(const FrameDesc *desc) {
  if (desc == NULL || desc->cgram == NULL)
    return 0;
  int brightness = desc->ppu.brightness;
  uint16_t color = desc->cgram[0];
  uint32_t r = BrightnessChannel(color & 0x1f, brightness);
  uint32_t g = BrightnessChannel((color >> 5) & 0x1f, brightness);
  uint32_t b = BrightnessChannel((color >> 10) & 0x1f, brightness);
  return (r << 16) | (g << 8) | b;
}

static bool RenderReference(VoxelState *st, const FrameDesc *desc,
                            const ZeldaRenderTarget *target) {
  if (st->host == NULL || st->host->render_frame_desc == NULL)
    return false;
  size_t x = (size_t)desc->display.pane_x * (size_t)target->scale;
  size_t y = (size_t)desc->display.pane_y * (size_t)target->scale;
  uint8_t *pixels = target->pixels + y * target->pitch + x * 4;
  return st->host->render_frame_desc(st->host, desc, pixels, target->pitch);
}

static void EaseCamera(VoxelState *st) {
  const float k = 0.22f;
  VoxelCameraSettings *r = &st->camera_render;
  const VoxelCameraSettings *t = &st->camera;
  r->angle_deg += (t->angle_deg - r->angle_deg) * k;
  r->yaw_deg += (t->yaw_deg - r->yaw_deg) * k;
  r->zoom += (t->zoom - r->zoom) * k;
  r->distance_scale += (t->distance_scale - r->distance_scale) * k;
  r->height_scale += (t->height_scale - r->height_scale) * k;
  if (fabsf(t->angle_deg - r->angle_deg) < 0.02f)
    r->angle_deg = t->angle_deg;
  if (fabsf(t->yaw_deg - r->yaw_deg) < 0.02f)
    r->yaw_deg = t->yaw_deg;
  if (fabsf(t->zoom - r->zoom) < 0.001f)
    r->zoom = t->zoom;
  if (fabsf(t->distance_scale - r->distance_scale) < 0.001f)
    r->distance_scale = t->distance_scale;
  if (fabsf(t->height_scale - r->height_scale) < 0.001f)
    r->height_scale = t->height_scale;
}

static bool VoxelInit(ZeldaRenderPlugin *plugin, const ZeldaRenderHost *host,
                      uint32_t version) {
  VoxelState *st = (VoxelState *)plugin->state;
  memset(st, 0, sizeof(*st));
  if (version != kRenderPluginApiVersion || host == NULL ||
      host->api_version != kRenderPluginApiVersion ||
      host->frame_desc_version != kFrameDescVersion ||
      host->render_frame_desc == NULL || host->log == NULL)
    return false;
  st->host = host;
  st->preferred_scale = EnvScale("ZELDA3_VOXEL_SCALE", 1);
  st->stats = EnvBool("ZELDA3_VOXEL_STATS", false);

  const char *profile_path = getenv("ZELDA3_VOXEL_PROFILE");
  if (profile_path != NULL && *profile_path != 0) {
    st->profile = VoxelProfile_Load(profile_path);
    if (st->profile == NULL) {
      HostLog(st, 1, "voxel: cannot load profile; using built-in defaults");
      st->profile = VoxelProfile_CreateDefault();
    }
  } else {
    st->profile = VoxelProfile_Load("plugins/voxel/default-profile.json");
    if (st->profile == NULL)
      st->profile = VoxelProfile_CreateDefault();
  }
  if (st->profile == NULL) {
    HostLog(st, 2, "voxel: out of memory loading the profile");
    return false;
  }
  st->camera = *VoxelProfile_Camera(st->profile);
  st->camera_render = st->camera;
  st->render = *VoxelProfile_Render(st->profile);
  st->light_settings = *VoxelProfile_Light(st->profile);
  st->shadow_settings = *VoxelProfile_Shadows(st->profile);
  st->camera.angle_deg =
      EnvFloat("ZELDA3_VOXEL_CAMERA_ANGLE", st->camera.angle_deg);
  st->camera.indoor_angle_deg = EnvFloat("ZELDA3_VOXEL_CAMERA_INDOOR_ANGLE",
                                         st->camera.indoor_angle_deg);
  st->camera.distance_scale =
      EnvFloat("ZELDA3_VOXEL_CAMERA_DISTANCE", st->camera.distance_scale);
  st->camera.height_scale =
      EnvFloat("ZELDA3_VOXEL_CAMERA_HEIGHT", st->camera.height_scale);
  st->camera.follow_link =
      EnvBool("ZELDA3_VOXEL_CAMERA_FOLLOW", st->camera.follow_link);
  st->camera.yaw_deg =
      EnvFloat("ZELDA3_VOXEL_CAMERA_YAW", st->camera.yaw_deg);
  st->camera.zoom = EnvFloat("ZELDA3_VOXEL_CAMERA_ZOOM", st->camera.zoom);
  st->render.side_shade =
      EnvFloat("ZELDA3_VOXEL_SIDE_SHADE", st->render.side_shade);
  st->render.face_top =
      EnvFloat("ZELDA3_VOXEL_FACE_TOP", st->render.face_top);
  st->render.face_front =
      EnvFloat("ZELDA3_VOXEL_FACE_FRONT", st->render.face_front);
  st->render.face_side =
      EnvFloat("ZELDA3_VOXEL_FACE_SIDE", st->render.face_side);
  st->render.face_back =
      EnvFloat("ZELDA3_VOXEL_FACE_BACK", st->render.face_back);
  st->render.sprite_scale =
      EnvFloat("ZELDA3_VOXEL_SPRITE_SCALE", st->render.sprite_scale);
  st->render.height_scale =
      EnvFloat("ZELDA3_VOXEL_HEIGHT_SCALE", st->render.height_scale);
  st->render.tilt_shift =
      EnvFloat("ZELDA3_VOXEL_TILT_SHIFT", st->render.tilt_shift);
  st->render.exposure =
      EnvFloat("ZELDA3_VOXEL_EXPOSURE", st->render.exposure);
  st->render.stack_height =
      EnvFloat("ZELDA3_VOXEL_STACK_HEIGHT", st->render.stack_height);
  if (st->render.stack_height < 0.4f)
    st->render.stack_height = 0.4f;
  if (st->render.stack_height > 6.0f)
    st->render.stack_height = 6.0f;
  s_stack_amplitude = st->render.stack_height;
  st->render.mesh_detail = (int)EnvFloat("ZELDA3_VOXEL_MESH_DETAIL",
                                         (float)st->render.mesh_detail);
  if (st->render.mesh_detail != 1 && st->render.mesh_detail != 2 &&
      st->render.mesh_detail != 4 && st->render.mesh_detail != 8)
    st->render.mesh_detail = 1;
  st->light_settings.enabled =
      EnvBool("ZELDA3_VOXEL_LIGHTING", st->light_settings.enabled);
  st->shadow_settings.enabled =
      EnvBool("ZELDA3_VOXEL_SHADOWS", st->shadow_settings.enabled);
  st->shadow_settings.strength = EnvFloat("ZELDA3_VOXEL_SHADOW_STRENGTH",
                                          st->shadow_settings.strength);
  st->shadow_settings.length = EnvFloat("ZELDA3_VOXEL_SHADOW_LENGTH",
                                        st->shadow_settings.length);
  st->default_camera = st->camera;
  st->camera_render = st->camera;
  st->default_light_settings = st->light_settings;
  st->default_shadow_settings = st->shadow_settings;
  st->light = MakeLight(&st->light_settings);

  st->atlas = (uint8_t *)malloc((size_t)kVoxelAtlasWidth *
                                (size_t)kVoxelAtlasHeight * 4);
  st->cells = (VoxelCell *)calloc(kFrameDesc_MaxRoomTiles, sizeof(*st->cells));
  for (int i = 0; i < kVoxelAtlasHashSize; i++)
    st->atlas_hash[i].slot = -1;
  if (EnvBool("ZELDA3_VOXEL_NO_GL", false)) {
    st->gl = NULL;
  } else {
    st->gl = VoxelGl_Create(VoxelLog, st);
  }
  if (st->gl == NULL) {
    st->gl_failed = true;
    HostLog(st, 1,
            "voxel: OpenGL/EGL unavailable; using the reference renderer");
  }
  if (st->atlas == NULL || st->cells == NULL) {
    st->gl_failed = true;
    HostLog(st, 2, "voxel: out of memory for the scene buffers");
  }
  return true;
}

static void VoxelBeginFrame(ZeldaRenderPlugin *plugin,
                            const ZeldaRenderFrameInfo *frame) {
  VoxelState *st = (VoxelState *)plugin->state;
  st->pane_count = frame != NULL ? (int)frame->pane_count : 0;
  if (st->pane_count < 0)
    st->pane_count = 0;
  if (st->pane_count > kRenderPluginMaxPanes)
    st->pane_count = kRenderPluginMaxPanes;
  memset(st->pane, 0, sizeof(st->pane));
}

static void VoxelFrameDescription(ZeldaRenderPlugin *plugin,
                                  const FrameDesc *desc) {
  VoxelState *st = (VoxelState *)plugin->state;
  if (desc == NULL || desc->version != kFrameDescVersion)
    return;
  if (desc->display.pane_index >= kRenderPluginMaxPanes)
    return;
  st->pane[desc->display.pane_index] = desc;
}

static bool VoxelPresent(ZeldaRenderPlugin *plugin,
                         const ZeldaRenderTarget *target) {
  VoxelState *st = (VoxelState *)plugin->state;
  if (target == NULL || target->pixels == NULL || st->pane_count <= 0)
    return false;
  EaseCamera(st);

  bool fallback[kRenderPluginMaxPanes];
  int gl_panes = 0;
  int first_gl = -1;
  for (int i = 0; i < st->pane_count; i++) {
    fallback[i] = PaneNeedsFallback(st->pane[i]);
    if (!fallback[i]) {
      if (first_gl < 0)
        first_gl = i;
      gl_panes++;
    }
  }

  bool gl_ok = false;
  if (gl_panes > 0 && !st->gl_failed && st->gl != NULL &&
      st->atlas != NULL && st->cells != NULL) {
    uint32_t clear = BackdropColor(st->pane[first_gl]);
    bool began = VoxelGl_BeginFrame(st->gl, target->width, target->height,
                                    clear);
    if (!began)
      HostLog(st, 2, "voxel: GL begin frame failed");
    if (began) {
      gl_ok = true;
      for (int i = 0; i < st->pane_count && gl_ok; i++) {
        if (fallback[i])
          continue;
        const FrameDesc *desc = st->pane[i];
        int scale = target->scale;
        VoxelGl_SetViewport(st->gl,
                            desc->display.pane_x * scale,
                            desc->display.pane_y * scale,
                            (int)desc->width * scale,
                            (int)desc->height * scale,
                            (int)desc->width, (int)desc->height);
        if (!BuildPane(st, desc)) {
          HostLog(st, 2, "voxel: scene build failed");
          gl_ok = false;
        } else if (!VoxelGl_UploadAtlas(st->gl, st->atlas, kVoxelAtlasWidth,
                                        kVoxelAtlasHeight)) {
          HostLog(st, 2, "voxel: atlas upload failed");
          gl_ok = false;
        }
        if (gl_ok) {
          if (st->stats) {
            st->stat_frames++;
            st->stat_bg += (uint64_t)st->mesh_world->count;
            st->stat_backdrop += (uint64_t)st->mesh_backdrop.count;
            st->stat_overlay += (uint64_t)st->mesh_overlay.count;
            st->stat_sprites += (uint64_t)st->mesh_sprites.count;
            st->stat_2d += (uint64_t)st->mesh_2d.count;
            if (st->mesh_world->count > st->stat_max_bg)
              st->stat_max_bg = st->mesh_world->count;
          }
          VoxelGlCamera camera = MakeCamera(st, desc);
          LogBackground(desc);
          LogLinkProjection(desc, &camera);
          float shadow_strength = st->shadow_settings.enabled
              ? st->shadow_settings.strength : 0.0f;
          // Rooms are lit by their own fixture; a directional sun shadow
          // just turns most of the floor black.
          if (desc->semantics.indoors)
            shadow_strength = 0.0f;
          if (!VoxelGl_DrawBackdrop(st->gl, st->mesh_backdrop.verts,
                                    st->mesh_backdrop.count)) {
            HostLog(st, 2, "voxel: backdrop draw failed");
            gl_ok = false;
          } else if (!VoxelGl_RenderShadowMap(
                  st->gl, st->mesh_world->verts, st->mesh_world->count,
                  st->mesh_sprites.verts, st->mesh_sprites.count, &camera,
                  st->light.x, st->light.y, st->light.z, shadow_strength,
                  st->world_offset_x, st->world_offset_y)) {
            HostLog(st, 2, "voxel: shadow map failed");
            gl_ok = false;
          } else if (!VoxelGl_Draw3D(st->gl, st->mesh_world->verts,
                                     st->mesh_world->count, &camera,
                                     st->world_offset_x,
                                     st->world_offset_y)) {
            HostLog(st, 2, "voxel: 3D draw failed");
            gl_ok = false;
          } else if (!VoxelGl_Draw3D(st->gl, st->mesh_sprites.verts,
                                     st->mesh_sprites.count, &camera, 0.0f,
                                     0.0f)) {
            HostLog(st, 2, "voxel: sprite draw failed");
            gl_ok = false;
          } else if (!VoxelGl_CompositeWorld(st->gl, st->render.tilt_shift)) {
            HostLog(st, 2, "voxel: world composite failed");
            gl_ok = false;
          } else if (!VoxelGl_DrawOverlay(st->gl, st->mesh_overlay.verts,
                                          st->mesh_overlay.count,
                                          st->overlay_subtract,
                                          st->overlay_half)) {
            HostLog(st, 2, "voxel: overlay draw failed");
            gl_ok = false;
          } else if (!VoxelGl_Draw2D(st->gl, st->mesh_2d.verts,
                                     st->mesh_2d.count)) {
            HostLog(st, 2, "voxel: 2D draw failed");
            gl_ok = false;
          }
        }
      }
      bool ended = VoxelGl_EndFrame(st->gl, target->pixels, target->pitch);
      if (!ended)
        HostLog(st, 2, "voxel: GL end frame failed");
      gl_ok = gl_ok && ended;
    }
    if (!gl_ok) {
      st->gl_failed = true;
      if (!st->fallback_logged) {
        HostLog(st, 1,
                "voxel: OpenGL frame failed; using the reference renderer");
        st->fallback_logged = true;
      }
    }
  }

  if (!gl_ok) {
    for (int i = 0; i < st->pane_count; i++) {
      if (st->pane[i] != NULL && !RenderReference(st, st->pane[i], target))
        return false;
    }
    return true;
  }

  for (int i = 0; i < st->pane_count; i++) {
    if (fallback[i] && st->pane[i] != NULL) {
      if (!RenderReference(st, st->pane[i], target))
        return false;
    }
  }
  return true;
}

static void VoxelShutdown(ZeldaRenderPlugin *plugin) {
  VoxelState *st = (VoxelState *)plugin->state;
  if (st->stats && st->stat_frames != 0) {
    char message[512];
    snprintf(message, sizeof(message),
             "voxel-stats detail=%d frames=%llu bg_avg=%llu bg_max=%d "
             "backdrop_avg=%llu overlay_avg=%llu sprites_avg=%llu 2d_avg=%llu "
             "cache_hits=%llu cache_misses=%llu",
             st->render.mesh_detail, (unsigned long long)st->stat_frames,
             (unsigned long long)(st->stat_bg / st->stat_frames),
             st->stat_max_bg,
             (unsigned long long)(st->stat_backdrop / st->stat_frames),
             (unsigned long long)(st->stat_overlay / st->stat_frames),
             (unsigned long long)(st->stat_sprites / st->stat_frames),
             (unsigned long long)(st->stat_2d / st->stat_frames),
             (unsigned long long)st->cache_hits,
             (unsigned long long)st->cache_misses);
    HostLog(st, 0, message);
  }
  VoxelGl_Destroy(st->gl);
  VoxelProfile_Free(st->profile);
  free(st->atlas);
  free(st->cache_atlas);
  free(st->cells);
  free(st->mesh_bg.verts);
  free(st->mesh_backdrop.verts);
  free(st->mesh_overlay.verts);
  free(st->mesh_sprites.verts);
  free(st->mesh_2d.verts);
  memset(st, 0, sizeof(*st));
}

static int32_t VoxelPreferredScale(ZeldaRenderPlugin *plugin) {
  VoxelState *st = (VoxelState *)plugin->state;
  return st->preferred_scale;
}

static ZeldaRenderPlugin s_voxel_plugin = {
  kRenderPluginApiVersion,
  kFrameDescVersion,
  "voxel",
  "Room-aware voxel diorama with folded tile art and a perspective camera",
  &s_voxel_state,
  VoxelInit,
  VoxelBeginFrame,
  VoxelFrameDescription,
  VoxelPresent,
  VoxelShutdown,
  VoxelPreferredScale,
  VoxelCommand,
};

ZELDA_RENDER_PLUGIN_EXPORT ZeldaRenderPlugin *zelda3_render_plugin_get(
    uint32_t api_version) {
  if (api_version != kRenderPluginApiVersion)
    return NULL;
  return &s_voxel_plugin;
}
