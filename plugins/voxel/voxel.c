// Voxel renderer plugin (R-4).
//
// Builds a 3D scene from FrameDesc: every visible BG1/BG2 tile becomes a
// textured voxel column whose height comes from the collision attribute, and
// sprites become upright cards positioned with the game's z coordinate. BG3
// (HUD/text) and owner-less sprites are drawn as a 2D overlay on top. Mode 7,
// force blank and special screens fall back to host->render_frame_desc().
//
// The scene is rendered by an offscreen EGL/OpenGL context (voxel_gl.c) and
// read back into the host's ARGB8888 buffer. If EGL/OpenGL is unavailable the
// plugin keeps working as a reference-renderer pass-through.

#include "../../src/mod/render_plugin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "voxel_gl.h"
#include "voxel_profile.h"

enum {
  kVoxelAtlasCols = 64,
  kVoxelAtlasRows = 96,
  kVoxelAtlasTile = 8,
  kVoxelAtlasWidth = kVoxelAtlasCols * kVoxelAtlasTile,   // 512
  kVoxelAtlasHeight = kVoxelAtlasRows * kVoxelAtlasTile,  // 768
  kVoxelAtlasSlots = kVoxelAtlasCols * kVoxelAtlasRows,
};

typedef struct VoxelCell {
  float top_z;
  float base_z;
  int slot;
  bool opaque;
} VoxelCell;

typedef struct VoxelMesh {
  VoxelGlVertex *verts;
  int count;
  int cap;
} VoxelMesh;

typedef struct VoxelLight {
  float x, y, z;
  float ambient;
  float diffuse;
} VoxelLight;

typedef struct VoxelState {
  const ZeldaRenderHost *host;
  VoxelGl *gl;
  VoxelProfile *profile;
  VoxelCameraSettings camera;
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
  VoxelCell *cells;

  VoxelMesh mesh_bg;
  VoxelMesh mesh_sprites;
  VoxelMesh mesh_shadow;
  VoxelMesh mesh_2d;
} VoxelState;

static VoxelState s_voxel_state;

static void VoxelLog(void *user, int level, const char *message) {
  VoxelState *st = (VoxelState *)user;
  if (st->host != NULL && st->host->log != NULL)
    st->host->log(st->host, level, message);
}

static void HostLog(VoxelState *st, int level, const char *message) {
  if (st->host != NULL && st->host->log != NULL)
    st->host->log(st->host, level, message);
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

static void DecodeTilePixels(const uint16_t *words, int bpp, int palette_base,
                             bool flip_x, bool flip_y, const uint16_t *cgram,
                             int brightness, uint8_t *out, bool *opaque) {
  bool any = false;
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
      any = true;
      int index = palette_base + pixel;
      uint16_t color = cgram != NULL ? cgram[index & 0xff] : 0;
      dst[0] = (uint8_t)BrightnessChannel(color & 0x1f, brightness);
      dst[1] = (uint8_t)BrightnessChannel((color >> 5) & 0x1f, brightness);
      dst[2] = (uint8_t)BrightnessChannel((color >> 10) & 0x1f, brightness);
      dst[3] = 255;
    }
  }
  *opaque = any;
}

static int AtlasAdd(VoxelState *st, const uint8_t *tile) {
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
  for (int row = 0; row < kVoxelAtlasTile; row++) {
    memcpy(st->atlas + ((size_t)(y + row) * kVoxelAtlasWidth + x) * 4,
           tile + (size_t)row * kVoxelAtlasTile * 4,
           (size_t)kVoxelAtlasTile * 4);
  }
  return slot;
}

static void AtlasSlotUV(int slot, float *u0, float *v0, float *u1, float *v1) {
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

static bool FetchSpriteTile(const FrameDesc *desc, const FrameDescSprite *s,
                            int sub_x, int sub_y, uint16_t *words,
                            int *palette_base, bool *flip_x, bool *flip_y,
                            int *used_tile) {
  if (s->oam_index >= 128)
    return false;
  uint16_t oam1 = desc->oam[s->oam_index + 1];
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
// Geometry

static bool MeshReserve(VoxelMesh *mesh, int needed) {
  if (mesh->cap >= needed)
    return true;
  int cap = mesh->cap != 0 ? mesh->cap : 4096;
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

static float DepthOf(float y, float height, float bias) {
  return y + bias * height;
}

static float ClampShade(float v) {
  return ClampFloat(v, 0.05f, 1.6f);
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

static float FaceShade(const VoxelLight *light, bool enabled, float nx,
                       float ny, float nz, float fallback) {
  if (!enabled)
    return fallback;
  float d = nx * light->x + ny * light->y + nz * light->z;
  if (d < 0.0f)
    d = 0.0f;
  return ClampShade(light->ambient + light->diffuse * d);
}

static bool BuildBgLayer(VoxelState *st, const FrameDesc *desc, int layer,
                         int cols, int rows, int extra, int margin_left,
                         int content_right) {
  VoxelCell *cells = st->cells;
  // The cell array uses kFrameDesc_MaxTileCols as its row stride, like the
  // semantic layer arrays; clear all of it so neighbor reads are defined.
  memset(cells, 0, (size_t)kFrameDesc_MaxTiles * sizeof(*cells));
  const FrameDescTileLayer *tl = &desc->semantics.layer[layer];
  const FrameDescTileLayer *fallback = NULL;
  bool has_sem = tl->present;
  if (!has_sem && layer == 0 && desc->semantics.layer[1].present)
    fallback = &desc->semantics.layer[1];

  uint8_t tile_pixels[8 * 8 * 4];
  for (int row = 0; row < rows; row++) {
    uint16_t hscroll, vscroll;
    FrameDesc_GetBgScrollAtLine(desc, layer, row * 8, &hscroll, &vscroll);
    for (int col = 0; col < cols; col++) {
      int idx = row * kFrameDesc_MaxTileCols + col;
      int pane_x = col * 8;
      int pane_y = row * 8;
      if (pane_x + 8 <= margin_left || pane_x >= content_right)
        continue;
      if (has_sem && !tl->valid[idx])
        continue;
      if (!has_sem && fallback != NULL && !fallback->valid[idx])
        continue;

      int screen_x = -extra + pane_x;
      uint16_t words[16];
      int bpp = 4, palette = 0, tile_num = 0;
      bool flip_x = false, flip_y = false;
      FetchBgTile(desc, layer, screen_x, pane_y, hscroll, vscroll,
                  words, &bpp, &palette, &flip_x, &flip_y, &tile_num);
      int palette_base = palette * (bpp == 4 ? 16 : 4);
      bool opaque = false;
      DecodeTilePixels(words, bpp, palette_base, flip_x, flip_y, desc->cgram,
                       desc->ppu.brightness, tile_pixels, &opaque);
      if (!opaque)
        continue;
      int slot = AtlasAdd(st, tile_pixels);
      if (slot < 0)
        continue;

      int attr = 0;
      int raw = 0;
      if (has_sem) {
        attr = tl->attr[idx];
        raw = tl->raw[idx];
      } else if (fallback != NULL) {
        attr = fallback->attr[idx];
        raw = fallback->raw[idx];
      }
      float height = VoxelProfile_Height(st->profile, desc, layer, attr, raw,
                                         tile_num);
      height *= st->render.height_scale;
      float base_z = 0.0f;
      if (desc->semantics.indoors) {
        float step = st->render.floor_step * 8.0f;
        if (layer == 0)
          base_z = desc->semantics.link_floor ? 0.0f : step;
        else
          base_z = desc->semantics.link_floor ? -step : 0.0f;
      } else if (layer == 0) {
        base_z = 0.05f * 8.0f;
      }
      VoxelCell *cell = &cells[idx];
      cell->top_z = base_z + height * 8.0f;
      cell->base_z = base_z;
      cell->slot = slot;
      cell->opaque = true;
    }
  }

  float height_f = (float)desc->height;
  float bias = layer == 0 ? 0.002f : 0.0f;
  float side_shade = st->render.side_shade;
  float top_shade = FaceShade(&st->light, st->light_settings.enabled,
                              0.0f, 0.0f, 1.0f, 1.0f);
  float left_shade = FaceShade(&st->light, st->light_settings.enabled,
                               -1.0f, 0.0f, 0.0f, side_shade * 0.85f);
  float right_shade = FaceShade(&st->light, st->light_settings.enabled,
                                1.0f, 0.0f, 0.0f, side_shade * 0.85f);
  float back_shade = FaceShade(&st->light, st->light_settings.enabled,
                               0.0f, -1.0f, 0.0f, side_shade);
  float front_shade = FaceShade(&st->light, st->light_settings.enabled,
                                0.0f, 1.0f, 0.0f, side_shade);
  float skirt = st->render.skirt * 8.0f;
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      int idx = row * kFrameDesc_MaxTileCols + col;
      VoxelCell *cell = &cells[idx];
      if (!cell->opaque)
        continue;
      int x = col * 8;
      int y = row * 8;
      float top = cell->top_z;
      float bottom = fminf(cell->base_z, top) - skirt;
      float u0, v0, u1, v1;
      AtlasSlotUV(cell->slot, &u0, &v0, &u1, &v1);
      float d0 = DepthOf((float)y, height_f, bias);
      float d1 = DepthOf((float)(y + 8), height_f, bias);

      if (!QuadMesh(&st->mesh_bg, (float)x, (float)y, top, u0, v0, d0,
                    (float)(x + 8), (float)y, top, u1, v0, d0,
                    (float)(x + 8), (float)(y + 8), top, u1, v1, d1,
                    (float)x, (float)(y + 8), top, u0, v1, d1, top_shade))
        return false;

      bool draw_left = col == 0;
      if (!draw_left) {
        VoxelCell *n = &cells[idx - 1];
        draw_left = !n->opaque || n->top_z < top;
      }
      if (draw_left) {
        if (!QuadMesh(&st->mesh_bg, (float)x, (float)y, bottom, u0, v1, d0,
                      (float)x, (float)(y + 8), bottom, u1, v1, d1,
                      (float)x, (float)(y + 8), top, u1, v0, d1,
                      (float)x, (float)y, top, u0, v0, d0, left_shade))
          return false;
      }

      bool draw_right = col == cols - 1;
      if (!draw_right) {
        VoxelCell *n = &cells[idx + 1];
        draw_right = !n->opaque || n->top_z < top;
      }
      if (draw_right) {
        if (!QuadMesh(&st->mesh_bg, (float)(x + 8), (float)y, bottom, u1, v1,
                      d0, (float)(x + 8), (float)(y + 8), bottom, u0, v1, d1,
                      (float)(x + 8), (float)(y + 8), top, u0, v0, d1,
                      (float)(x + 8), (float)y, top, u1, v0, d0, right_shade))
          return false;
      }

      bool draw_back = row == 0;
      if (!draw_back) {
        VoxelCell *n = &cells[idx - kFrameDesc_MaxTileCols];
        draw_back = !n->opaque || n->top_z < top;
      }
      if (draw_back) {
        if (!QuadMesh(&st->mesh_bg, (float)x, (float)y, bottom, u0, v1, d0,
                      (float)(x + 8), (float)y, bottom, u1, v1, d0,
                      (float)(x + 8), (float)y, top, u1, v0, d0,
                      (float)x, (float)y, top, u0, v0, d0, back_shade))
          return false;
      }

      bool draw_front = row == rows - 1;
      if (!draw_front) {
        VoxelCell *n = &cells[idx + kFrameDesc_MaxTileCols];
        draw_front = !n->opaque || n->top_z < top;
      }
      if (draw_front) {
        if (!QuadMesh(&st->mesh_bg, (float)x, (float)(y + 8), bottom, u0, v1,
                      d1, (float)(x + 8), (float)(y + 8), bottom, u1, v1, d1,
                      (float)(x + 8), (float)(y + 8), top, u1, v0, d1,
                      (float)x, (float)(y + 8), top, u0, v0, d1, front_shade))
          return false;
      }
    }
  }

  if (st->shadow_settings.enabled && st->shadow_settings.columns) {
    float length = ClampFloat(st->shadow_settings.length, 0.0f, 4.0f);
    float lz = fmaxf(st->light.z, 0.15f);
    for (int row = 0; row < rows; row++) {
      for (int col = 0; col < cols; col++) {
        int idx = row * kFrameDesc_MaxTileCols + col;
        VoxelCell *cell = &cells[idx];
        if (!cell->opaque)
          continue;
        float h = cell->top_z - cell->base_z;
        if (h <= 0.5f)
          continue;
        int tc = col + (int)lroundf(-h * st->light.x / lz * length / 8.0f);
        int tr = row + (int)lroundf(-h * st->light.y / lz * length / 8.0f);
        if (tc < 0 || tc >= cols || tr < 0 || tr >= rows)
          continue;
        VoxelCell *target = &cells[tr * kFrameDesc_MaxTileCols + tc];
        if (!target->opaque || target->top_z > cell->base_z + 0.5f)
          continue;
        int sx = tc * 8;
        int sy = tr * 8;
        float z = target->top_z + 0.25f;
        float sd0 = DepthOf((float)sy, height_f, -0.001f);
        float sd1 = DepthOf((float)(sy + 8), height_f, -0.001f);
        if (!QuadMesh(&st->mesh_shadow, (float)sx, (float)sy, z, 0.0f, 0.0f,
                      sd0, (float)(sx + 8), (float)sy, z, 0.0f, 0.0f, sd0,
                      (float)(sx + 8), (float)(sy + 8), z, 0.0f, 0.0f, sd1,
                      (float)sx, (float)(sy + 8), z, 0.0f, 0.0f, sd1, 1.0f))
          return false;
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
    for (int col = 0; col < cols; col++) {
      int pane_x = col * 8;
      int pane_y = row * 8;
      if (pane_x + 8 <= margin_left || pane_x >= content_right)
        continue;
      int screen_x = -extra + pane_x;
      uint16_t words[16];
      int bpp = 2, palette = 0, tile_num = 0;
      bool flip_x = false, flip_y = false;
      FetchBgTile(desc, 2, screen_x, pane_y, hscroll, vscroll, words, &bpp,
                  &palette, &flip_x, &flip_y, &tile_num);
      int palette_base = palette * (bpp == 4 ? 16 : 4);
      bool opaque = false;
      DecodeTilePixels(words, bpp, palette_base, flip_x, flip_y, desc->cgram,
                       desc->ppu.brightness, tile_pixels, &opaque);
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
      bool opaque = false;
      DecodeTilePixels(words, 4, palette_base, flip_x, flip_y, desc->cgram,
                       desc->ppu.brightness, tile_pixels, &opaque);
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
  float tilt = camera->tilt > 0.05f ? camera->tilt : 0.05f;
  float zfactor = camera->zscale / tilt;
  float ground_y = (float)(s->y + s->size) + z * zfactor;
  float depth = DepthOf(ground_y, camera->height, 0.004f);
  float z_step = st->render.sprite_scale / zfactor;
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
      bool opaque = false;
      DecodeTilePixels(words, 4, palette_base, flip_x, flip_y, desc->cgram,
                       desc->ppu.brightness, tile_pixels, &opaque);
      if (!opaque)
        continue;
      int slot = AtlasAdd(st, tile_pixels);
      if (slot < 0)
        continue;
      float u0, v0, u1, v1;
      AtlasSlotUV(slot, &u0, &v0, &u1, &v1);
      float x0 = (float)(pane_x + sub_x);
      float x1 = x0 + 8.0f;
      float z0 = z + (float)(s->size - sub_y - 8) * z_step;
      float z1 = z + (float)(s->size - sub_y) * z_step;
      float shade = FaceShade(&st->light, st->light_settings.enabled,
                              0.0f, -1.0f, 0.0f, 1.0f);
      if (!QuadMesh(&st->mesh_sprites, x0, ground_y, z0, u0, v1, depth,
                    x1, ground_y, z0, u1, v1, depth,
                    x1, ground_y, z1, u1, v0, depth,
                    x0, ground_y, z1, u0, v0, depth, shade))
        return false;
    }
  }
  return true;
}

static bool BuildSpriteShadow(VoxelState *st, const FrameDescSprite *s,
                              int extra, int margin_left, int content_right,
                              const VoxelGlCamera *camera) {
  if (!st->shadow_settings.enabled || !st->shadow_settings.sprites)
    return true;
  int pane_x = s->x + extra;
  if (pane_x + s->size <= margin_left || pane_x >= content_right)
    return true;
  float z = s->z == -1 ? 0.0f : (float)s->z;
  float tilt = camera->tilt > 0.05f ? camera->tilt : 0.05f;
  float zfactor = camera->zscale / tilt;
  float ground_y = (float)(s->y + s->size) + z * zfactor;
  float x0 = (float)pane_x + 1.0f;
  float x1 = (float)(pane_x + s->size) - 1.0f;
  float y0 = ground_y - 2.0f;
  float y1 = ground_y + 5.0f;
  float zz = 0.25f;
  float d0 = DepthOf(y0, camera->height, -0.002f);
  float d1 = DepthOf(y1, camera->height, -0.002f);
  return QuadMesh(&st->mesh_shadow, x0, y0, zz, 0.0f, 0.0f, d0,
                  x1, y0, zz, 0.0f, 0.0f, d0,
                  x1, y1, zz, 0.0f, 0.0f, d1,
                  x0, y1, zz, 0.0f, 0.0f, d1, 1.0f);
}

static bool BuildSprites(VoxelState *st, const FrameDesc *desc, int extra,
                         int margin_left, int content_right,
                         const VoxelGlCamera *camera) {
  for (uint32_t i = 0; i < desc->sprite_count; i++) {
    const FrameDescSprite *s = &desc->sprites[i];
    if (s->size != 8 && s->size != 16)
      continue;
    if (s->owner_kind == kFrameDescOwner_None) {
      if (!BuildSpriteCard2D(st, desc, s, extra, margin_left, content_right))
        return false;
    } else {
      if (!BuildSpriteCard3D(st, desc, s, extra, camera))
        return false;
      if (!BuildSpriteShadow(st, s, extra, margin_left, content_right, camera))
        return false;
    }
  }
  return true;
}

static const FrameDescSprite *FindLinkSprite(const FrameDesc *desc) {
  const FrameDescSprite *first = NULL;
  for (uint32_t i = 0; i < desc->sprite_count; i++) {
    const FrameDescSprite *s = &desc->sprites[i];
    if (s->owner_kind != kFrameDescOwner_Link)
      continue;
    if (s->size == 16)
      return s;
    if (first == NULL)
      first = s;
  }
  return first;
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
  if (st->camera.follow_link) {
    const FrameDescSprite *link = FindLinkSprite(desc);
    if (link != NULL) {
      float link_z = link->z == -1 ? 0.0f : (float)link->z;
      float lx = (float)link->x + extra + (float)link->size * 0.5f;
      // Follow the ground position, not the jump: OAM y already moved up by z.
      float ly = (float)link->y + link_z + (float)link->size * 0.5f;
      float max_dx = width * 0.18f;
      float max_dy = height * 0.18f;
      camera.center_x += ClampFloat(lx - camera.center_x, -max_dx, max_dx);
      camera.center_y += ClampFloat(ly - camera.center_y, -max_dy, max_dy);
    }
  }
  float angle = ClampFloat(st->camera.angle_deg, 5.0f, 89.0f);
  camera.tilt = sinf(angle * 3.14159265358979323846f / 180.0f);
  camera.persp = ClampFloat(0.35f / ClampFloat(st->camera.distance_scale,
                                               0.2f, 10.0f),
                            0.0f, 0.8f);
  camera.zscale = ClampFloat(st->camera.height_scale, 0.2f, 4.0f);
  camera.yaw = ClampFloat(st->camera.yaw_deg, -60.0f, 60.0f) *
               3.14159265358979323846f / 180.0f;
  camera.zoom = ClampFloat(st->camera.zoom, 0.5f, 2.5f);
  return camera;
}

static bool BuildPane(VoxelState *st, const FrameDesc *desc) {
  st->slot_count = 0;
  st->mesh_bg.count = 0;
  st->mesh_sprites.count = 0;
  st->mesh_shadow.count = 0;
  st->mesh_2d.count = 0;

  int extra = (int)desc->display.extra_left_right;
  int left_cur = (int)desc->display.extra_left_cur;
  int right_cur = (int)desc->display.extra_right_cur;
  int margin_left = extra - left_cur;
  int content_right = margin_left + 256 + left_cur + right_cur;
  int cols = (int)((desc->width + 7) / 8);
  int rows = (int)((desc->height + 7) / 8);
  if (cols > kFrameDesc_MaxTileCols)
    cols = kFrameDesc_MaxTileCols;
  if (rows > kFrameDesc_MaxTileRows)
    rows = kFrameDesc_MaxTileRows;

  VoxelGlCamera camera = MakeCamera(st, desc);

  for (int layer = 0; layer < 2; layer++) {
    if (!(desc->ppu.screen_enabled[0] & (1 << layer)))
      continue;
    if (!BuildBgLayer(st, desc, layer, cols, rows, extra, margin_left,
                      content_right))
      return false;
  }

  if (!BuildBg3(st, desc, cols, rows, extra, margin_left, content_right))
    return false;
  if (!BuildSprites(st, desc, extra, margin_left, content_right, &camera))
    return false;
  return true;
}

// ---------------------------------------------------------------------------
// Interactive controls

static void VoxelLogStatus(VoxelState *st, const char *what) {
  char message[160];
  snprintf(message, sizeof(message), "voxel: %s (yaw %.0f, tilt %.0f, zoom %.2f, shadows %s)", what,
           st->camera.yaw_deg, st->camera.angle_deg, st->camera.zoom,
           st->shadow_settings.enabled ? "on" : "off");
  HostLog(st, 0, message);
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
  case kRenderCommand_CameraReset:
    st->camera = st->default_camera;
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
  // Title/file-select/attract screens have no room/area and no Link position;
  // they are UI, not a world to voxelize.
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
  st->render = *VoxelProfile_Render(st->profile);
  st->light_settings = *VoxelProfile_Light(st->profile);
  st->shadow_settings = *VoxelProfile_Shadows(st->profile);
  st->camera.angle_deg =
      EnvFloat("ZELDA3_VOXEL_CAMERA_ANGLE", st->camera.angle_deg);
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
  st->render.sprite_scale =
      EnvFloat("ZELDA3_VOXEL_SPRITE_SCALE", st->render.sprite_scale);
  st->render.height_scale =
      EnvFloat("ZELDA3_VOXEL_HEIGHT_SCALE", st->render.height_scale);
  st->light_settings.enabled =
      EnvBool("ZELDA3_VOXEL_LIGHTING", st->light_settings.enabled);
  st->shadow_settings.enabled =
      EnvBool("ZELDA3_VOXEL_SHADOWS", st->shadow_settings.enabled);
  st->shadow_settings.strength = EnvFloat("ZELDA3_VOXEL_SHADOW_STRENGTH",
                                          st->shadow_settings.strength);
  st->shadow_settings.length = EnvFloat("ZELDA3_VOXEL_SHADOW_LENGTH",
                                        st->shadow_settings.length);
  st->default_camera = st->camera;
  st->default_light_settings = st->light_settings;
  st->default_shadow_settings = st->shadow_settings;
  st->light = MakeLight(&st->light_settings);

  st->atlas = (uint8_t *)malloc((size_t)kVoxelAtlasWidth *
                                (size_t)kVoxelAtlasHeight * 4);
  st->cells = (VoxelCell *)calloc(kFrameDesc_MaxTiles, sizeof(*st->cells));
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
          VoxelGlCamera camera = MakeCamera(st, desc);
          float shadow_strength = st->shadow_settings.enabled
              ? st->shadow_settings.strength : 0.0f;
          if (!VoxelGl_Draw3D(st->gl, st->mesh_bg.verts, st->mesh_bg.count,
                              &camera)) {
            HostLog(st, 2, "voxel: 3D draw failed");
            gl_ok = false;
          } else if (!VoxelGl_DrawShadows(st->gl, st->mesh_shadow.verts,
                                          st->mesh_shadow.count, &camera,
                                          shadow_strength)) {
            HostLog(st, 2, "voxel: shadow draw failed");
            gl_ok = false;
          } else if (!VoxelGl_Draw3D(st->gl, st->mesh_sprites.verts,
                                     st->mesh_sprites.count, &camera)) {
            HostLog(st, 2, "voxel: sprite draw failed");
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
  VoxelGl_Destroy(st->gl);
  VoxelProfile_Free(st->profile);
  free(st->atlas);
  free(st->cells);
  free(st->mesh_bg.verts);
  free(st->mesh_sprites.verts);
  free(st->mesh_shadow.verts);
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
  "Collision-height voxel renderer with an EGL/OpenGL offscreen pass",
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
