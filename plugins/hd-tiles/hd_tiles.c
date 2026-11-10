// HD tile replacement renderer (R-3).
//
// The plugin receives one FrameDesc per pane, rebuilds the mode-1 PPU line
// pipeline from that description, and substitutes 8x8 BG/sprite tiles for
// images from an HD pack. The tile key is style/content based:
//
//   hash of the tile bytes in VRAM + palette number (+ optional conditions)
//
// Tiles without a pack entry keep the original CGRAM pixels. The renderer
// keeps the PPU window rules, layer priorities, mosaic, color math and
// brightness, so an empty pack is a pixel-perfect pass-through of the
// reference renderer at 1x.
//
// The scanline code is a focused adaptation of the mode-1 path in snes/ppu.c.
// It is compiled into the plugin only (the engine never links it).

#include "../../src/mod/render_plugin.h"
#include "../../src/mod/hd_tile_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

enum {
  kHdMaxXPixels = kFrameDesc_MaxTileCols * 8,
  kHdMaxRefs = 512,
};

typedef struct HdImage {
  char path[1024];
  uint8_t *rgba;
  int width;
  int height;
} HdImage;

enum {
  kHdCondIndoors = 1,
  kHdCondRoom = 2,
  kHdCondArea = 4,
  kHdCondFloor = 8,
  kHdCondAttr = 16,
  kHdCondLayer = 32,
};

typedef struct HdConditions {
  uint8_t flags;
  bool indoors;
  int room;
  int area;
  int floor;
  int attr;
  int layer;
} HdConditions;

typedef struct HdEntry {
  uint32_t hash;
  int palette;   // -1 = any
  int bpp;       // 0 = any, 2 or 4
  int layer;     // -1 = any, 0..2 BG, 3 sprite
  int image;
  int x, y, w, h;
  int scale;
  HdConditions cond;
} HdEntry;

typedef struct HdState {
  const ZeldaRenderHost *host;
  HdImage *images;
  int image_count;
  int image_capacity;
  HdEntry *entries;
  int entry_count;
  int entry_capacity;
  int preferred_scale;
  bool pack_loaded;
  const FrameDesc *pane[kRenderPluginMaxPanes];
  int pane_count;
} HdState;

typedef struct HdTileRef {
  const HdEntry *entry;
  const HdImage *image;
} HdTileRef;

typedef struct HdPixSrc {
  uint16_t ref;  // index into s_refs, 0xffff = original tile
  uint8_t row;
  uint8_t col;
} HdPixSrc;

typedef struct HdZbuf {
  uint16_t z;
  HdPixSrc src;
} HdZbuf;

typedef struct HdBg {
  uint16_t hscroll;
  uint16_t vscroll;
  uint16_t tilemap_adr;
  uint16_t tile_adr;
  bool wider;
  bool higher;
} HdBg;

typedef struct HdPpu {
  HdState *st;
  const FrameDesc *desc;
  uint8_t mode;
  uint8_t brightness;
  bool forced_blank;
  uint8_t screen_enabled[2];
  uint8_t screen_windowed[2];
  uint8_t mosaic_enabled;
  uint8_t mosaic_size;
  uint16_t obj_tile_adr1;
  uint16_t obj_tile_adr2;
  uint8_t obj_size;
  uint8_t window1left, window1right;
  uint8_t window2left, window2right;
  uint32_t windowsel;
  uint8_t clip_mode;
  uint8_t prevent_math_mode;
  bool add_subscreen;
  bool subtract_color;
  bool half_color;
  uint8_t math_enabled;
  uint8_t fixed_color_r, fixed_color_g, fixed_color_b;
  uint8_t extra_left_right;
  uint8_t extra_left_cur;
  uint8_t extra_right_cur;
  uint8_t extra_bottom_cur;
  HdBg bg[4];
  uint8_t scroll_prev;
  uint8_t scroll_prev2;
  uint8_t brightness_mult[63];
  uint8_t brightness_mult_half[64];
  uint8_t last_brightness;
  bool line_has_sprites;
  bool new_renderer;
} HdPpu;

static HdTileRef s_refs[kHdMaxRefs];
static int s_ref_count;
static HdZbuf s_main[kHdMaxXPixels];
static HdZbuf s_sub[kHdMaxXPixels];
static HdZbuf s_obj[kHdMaxXPixels];

static const uint16_t kSpriteSizes[8][2] = {
  {8, 16}, {8, 32}, {8, 64}, {16, 32},
  {16, 64}, {32, 64}, {16, 32}, {16, 32}
};

enum {
  kWindow1Inversed = 1,
  kWindow1Enabled = 2,
  kWindow2Inversed = 4,
  kWindow2Enabled = 8,
};

#define GET_WINDOW_FLAGS(p, layer) ((p)->windowsel >> ((layer) * 4))
#define SPRITE_PRIO_TO_PRIO(prio, level6) \
  (((prio) * 4 + 2) * 16 + 4 + ((level6) ? 2 : 0))

// ---------------------------------------------------------------------------
// Pack loading

static int JsonInt(const cJSON *o, const char *key, int def) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static const char *JsonString(const cJSON *o, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}

static bool JsonBool(const cJSON *o, const char *key, bool def) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  if (cJSON_IsBool(v))
    return cJSON_IsTrue(v) != 0;
  if (cJSON_IsNumber(v))
    return v->valuedouble != 0;
  return def;
}

static bool JsonHas(const cJSON *o, const char *key) {
  return cJSON_GetObjectItemCaseSensitive(o, key) != NULL;
}

static bool ReadFile(const char *path, char **out) {
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return false;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  long size = ftell(f);
  if (size < 0 || size > 16 * 1024 * 1024) {
    fclose(f);
    return false;
  }
  rewind(f);
  char *text = (char *)malloc((size_t)size + 1);
  if (text == NULL) {
    fclose(f);
    return false;
  }
  if (size != 0 && fread(text, 1, (size_t)size, f) != (size_t)size) {
    free(text);
    fclose(f);
    return false;
  }
  text[size] = 0;
  fclose(f);
  *out = text;
  return true;
}

static void SplitPath(const char *path, char *dir, size_t dir_size,
                      char *base, size_t base_size) {
  const char *slash = strrchr(path, '/');
#ifdef _WIN32
  const char *bslash = strrchr(path, '\\');
  if (bslash != NULL && (slash == NULL || bslash > slash))
    slash = bslash;
#endif
  if (slash == NULL) {
    snprintf(dir, dir_size, ".");
    snprintf(base, base_size, "%s", path);
  } else {
    size_t n = (size_t)(slash - path);
    if (n >= dir_size)
      n = dir_size - 1;
    memcpy(dir, path, n);
    dir[n] = 0;
    snprintf(base, base_size, "%s", slash + 1);
  }
}

static int HdLoadImage(HdState *st, const char *dir, const char *name) {
  if (name == NULL || name[0] == 0)
    return -1;
  char path[1024];
  if (name[0] == '/' || (name[0] != 0 && name[1] == ':')) {
    snprintf(path, sizeof(path), "%.1023s", name);
  } else {
    snprintf(path, sizeof(path), "%.500s/%.500s", dir, name);
  }
  for (int i = 0; i < st->image_count; i++) {
    if (strcmp(st->images[i].path, path) == 0)
      return i;
  }
  int w = 0, h = 0, channels = 0;
  uint8_t *rgba = stbi_load(path, &w, &h, &channels, 4);
  if (rgba == NULL) {
    char msg[1200];
    snprintf(msg, sizeof(msg), "hd-tiles: cannot load image %.1000s: %.100s",
             path, stbi_failure_reason());
    st->host->log(st->host, 1, msg);
    return -1;
  }
  if (st->image_count == st->image_capacity) {
    int capacity = st->image_capacity ? st->image_capacity * 2 : 8;
    HdImage *images = (HdImage *)realloc(
        st->images, (size_t)capacity * sizeof(*images));
    if (images == NULL) {
      stbi_image_free(rgba);
      return -1;
    }
    st->images = images;
    st->image_capacity = capacity;
  }
  HdImage *img = &st->images[st->image_count];
  memset(img, 0, sizeof(*img));
  snprintf(img->path, sizeof(img->path), "%s", path);
  img->rgba = rgba;
  img->width = w;
  img->height = h;
  char msg[1200];
  snprintf(msg, sizeof(msg), "hd-tiles: loaded %.1000s (%dx%d)", path, w, h);
  st->host->log(st->host, 0, msg);
  return st->image_count++;
}

static bool HdParseConditions(HdConditions *cond, const cJSON *object) {
  memset(cond, 0, sizeof(*cond));
  cond->layer = -1;
  if (object == NULL || !cJSON_IsObject(object))
    return true;
  if (JsonHas(object, "indoors")) {
    cond->flags |= kHdCondIndoors;
    cond->indoors = JsonBool(object, "indoors", false);
  }
  if (JsonHas(object, "room")) {
    cond->flags |= kHdCondRoom;
    cond->room = JsonInt(object, "room", 0);
  }
  if (JsonHas(object, "area")) {
    cond->flags |= kHdCondArea;
    cond->area = JsonInt(object, "area", 0);
  }
  if (JsonHas(object, "floor")) {
    cond->flags |= kHdCondFloor;
    cond->floor = JsonInt(object, "floor", 0);
  }
  if (JsonHas(object, "attr")) {
    cond->flags |= kHdCondAttr;
    cond->attr = JsonInt(object, "attr", 0);
  }
  if (JsonHas(object, "layer")) {
    cond->flags |= kHdCondLayer;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(object, "layer");
    if (cJSON_IsString(v)) {
      if (strcmp(v->valuestring, "bg1") == 0)
        cond->layer = 0;
      else if (strcmp(v->valuestring, "bg2") == 0)
        cond->layer = 1;
      else if (strcmp(v->valuestring, "bg3") == 0)
        cond->layer = 2;
      else if (strcmp(v->valuestring, "sprite") == 0)
        cond->layer = 3;
      else
        cond->layer = -1;
    } else if (cJSON_IsNumber(v)) {
      cond->layer = (int)v->valuedouble;
    }
  }
  return true;
}

static bool HdPushEntry(HdState *st, const HdEntry *entry) {
  if (st->entry_count == st->entry_capacity) {
    int capacity = st->entry_capacity ? st->entry_capacity * 2 : 64;
    HdEntry *entries = (HdEntry *)realloc(
        st->entries, (size_t)capacity * sizeof(*entries));
    if (entries == NULL)
      return false;
    st->entries = entries;
    st->entry_capacity = capacity;
  }
  st->entries[st->entry_count++] = *entry;
  return true;
}

static bool HdLoadPack(HdState *st, const char *path) {
  if (path == NULL || path[0] == 0) {
    st->host->log(st->host, 0,
                  "hd-tiles: no pack configured (ZELDA3_HD_PACK is empty)");
    return true;
  }

  char manifest[1024];
  char dir[1024];
  size_t len = strlen(path);
  if (len > 5 && strcmp(path + len - 5, ".json") == 0) {
    snprintf(manifest, sizeof(manifest), "%.1023s", path);
    char base[1024];
    SplitPath(path, dir, sizeof(dir), base, sizeof(base));
  } else {
    snprintf(manifest, sizeof(manifest), "%.1000s/pack.json", path);
    snprintf(dir, sizeof(dir), "%.1023s", path);
  }

  char *text = NULL;
  if (!ReadFile(manifest, &text)) {
    char msg[1200];
    snprintf(msg, sizeof(msg), "hd-tiles: cannot read %.1000s", manifest);
    st->host->log(st->host, 1, msg);
    return true;
  }
  cJSON *root = cJSON_Parse(text);
  free(text);
  if (root == NULL) {
    st->host->log(st->host, 1, "hd-tiles: pack.json is not valid JSON");
    return true;
  }

  const char *format = JsonString(root, "format");
  int version = JsonInt(root, "version", 1);
  if (format != NULL && strcmp(format, "zelda3-hd-pack") != 0) {
    st->host->log(st->host, 1, "hd-tiles: unexpected pack format");
    cJSON_Delete(root);
    return true;
  }
  if (version != 1) {
    char msg[128];
    snprintf(msg, sizeof(msg), "hd-tiles: unsupported pack version %d",
             version);
    st->host->log(st->host, 1, msg);
    cJSON_Delete(root);
    return true;
  }

  int global_scale = JsonInt(root, "scale", 1);
  if (global_scale < 1 || global_scale > 4)
    global_scale = 1;
  const char *global_image = JsonString(root, "image");
  const cJSON *tiles = cJSON_GetObjectItemCaseSensitive(root, "tiles");
  int count = 0;
  if (cJSON_IsArray(tiles)) {
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, tiles) {
      if (!cJSON_IsObject(item))
        continue;
      HdEntry e;
      memset(&e, 0, sizeof(e));
      e.hash = (uint32_t)JsonInt(item, "hash", 0);
      e.palette = JsonInt(item, "palette", -1);
      e.bpp = JsonInt(item, "bpp", 0);
      e.layer = JsonInt(item, "layer", -1);
      e.scale = JsonInt(item, "scale", global_scale);
      if (e.scale < 1 || e.scale > 4)
        e.scale = global_scale;
      const char *image = JsonString(item, "image");
      if (image == NULL)
        image = global_image;
      e.image = HdLoadImage(st, dir, image);
      if (e.image < 0)
        continue;
      e.x = JsonInt(item, "x", 0);
      e.y = JsonInt(item, "y", 0);
      e.w = JsonInt(item, "w", 8 * e.scale);
      e.h = JsonInt(item, "h", 8 * e.scale);
      if (e.w <= 0 || e.h <= 0)
        continue;
      HdParseConditions(&e.cond,
                        cJSON_GetObjectItemCaseSensitive(item, "conditions"));
      if (!HdPushEntry(st, &e))
        break;
      count++;
    }
  }

  st->preferred_scale = 1;
  for (int i = 0; i < st->entry_count; i++) {
    if (st->entries[i].scale > st->preferred_scale)
      st->preferred_scale = st->entries[i].scale;
  }
  st->pack_loaded = true;
  char msg[1200];
  snprintf(msg, sizeof(msg), "hd-tiles: pack %.1000s: %d tile(s), %dx",
           manifest, count, st->preferred_scale);
  st->host->log(st->host, 0, msg);
  cJSON_Delete(root);
  return true;
}

// ---------------------------------------------------------------------------
// PPU state and pack lookup

static void HdEnsureBrightness(HdPpu *p) {
  if (p->brightness == p->last_brightness)
    return;
  p->last_brightness = p->brightness;
  for (int i = 0; i < 32; i++) {
    int v = ((i << 3) | (i >> 2)) * (int)p->brightness / 15;
    p->brightness_mult[i] = (uint8_t)v;
  }
  for (int i = 0; i < 31; i++)
    p->brightness_mult[32 + i] = p->brightness_mult[31];
  for (int i = 0; i < 32; i++) {
    p->brightness_mult_half[i * 2] = p->brightness_mult[i];
    p->brightness_mult_half[i * 2 + 1] = p->brightness_mult[i];
  }
}

static void HdInitPpu(HdPpu *p, HdState *st, const FrameDesc *desc) {
  const FrameDescPpuState *s = &desc->ppu;
  memset(p, 0, sizeof(*p));
  p->st = st;
  p->desc = desc;
  p->mode = s->mode;
  p->brightness = s->brightness;
  p->forced_blank = s->forced_blank;
  p->screen_enabled[0] = s->screen_enabled[0];
  p->screen_enabled[1] = s->screen_enabled[1];
  p->screen_windowed[0] = s->screen_windowed[0];
  p->screen_windowed[1] = s->screen_windowed[1];
  p->mosaic_enabled = s->mosaic_enabled;
  p->mosaic_size = s->mosaic_size;
  p->obj_tile_adr1 = s->obj_tile_adr1;
  p->obj_tile_adr2 = s->obj_tile_adr2;
  p->obj_size = s->obj_size;
  p->window1left = s->window1left;
  p->window1right = s->window1right;
  p->window2left = s->window2left;
  p->window2right = s->window2right;
  p->windowsel = s->windowsel;
  p->clip_mode = s->clip_mode;
  p->prevent_math_mode = s->prevent_math_mode;
  p->add_subscreen = s->add_subscreen;
  p->subtract_color = s->subtract_color;
  p->half_color = s->half_color;
  p->math_enabled = s->math_enabled;
  p->fixed_color_r = s->fixed_color_r;
  p->fixed_color_g = s->fixed_color_g;
  p->fixed_color_b = s->fixed_color_b;
  p->extra_left_right = s->extra_left_right;
  p->extra_left_cur = s->extra_left_cur;
  p->extra_right_cur = s->extra_right_cur;
  p->extra_bottom_cur = s->extra_bottom_cur;
  p->scroll_prev = s->scroll_prev;
  p->scroll_prev2 = s->scroll_prev2;
  p->new_renderer = (desc->render_flags & 1u) != 0;
  for (int i = 0; i < 4; i++) {
    p->bg[i].hscroll = s->bg[i].hscroll;
    p->bg[i].vscroll = s->bg[i].vscroll;
    p->bg[i].tilemap_adr = s->bg[i].tilemap_adr;
    p->bg[i].tile_adr = s->bg[i].tile_adr;
    p->bg[i].wider = s->bg[i].wider;
    p->bg[i].higher = s->bg[i].higher;
  }
  p->last_brightness = 0xff;
  HdEnsureBrightness(p);
}

static void HdWrite(HdPpu *p, uint16_t reg, uint8_t val) {
  switch (reg) {
    case 0x2100:
      p->brightness = val & 0x0f;
      p->forced_blank = (val & 0x80) != 0;
      break;
    case 0x2105:
      p->mode = val & 0x07;
      break;
    case 0x2106:
      p->mosaic_size = (uint8_t)((val >> 4) + 1);
      p->mosaic_enabled = p->mosaic_size > 1 ? val : 0;
      break;
    case 0x2107:
    case 0x2108:
    case 0x2109:
    case 0x210a: {
      int layer = reg - 0x2107;
      p->bg[layer].wider = (val & 1) != 0;
      p->bg[layer].higher = (val & 2) != 0;
      p->bg[layer].tilemap_adr = (uint16_t)((val & 0xfc) << 8);
      break;
    }
    case 0x210b:
      p->bg[0].tile_adr = (uint16_t)((val & 0x0f) << 12);
      p->bg[1].tile_adr = (uint16_t)((val & 0xf0) << 8);
      break;
    case 0x210c:
      p->bg[2].tile_adr = (uint16_t)((val & 0x0f) << 12);
      p->bg[3].tile_adr = (uint16_t)((val & 0xf0) << 8);
      break;
    case 0x210d:
    case 0x210f:
    case 0x2111:
    case 0x2113: {
      int layer = (reg - 0x210d) / 2;
      p->bg[layer].hscroll = (uint16_t)(((val << 8) |
          (p->scroll_prev & 0xf8) | (p->scroll_prev2 & 0x07)) & 0x3ff);
      p->scroll_prev = val;
      p->scroll_prev2 = val;
      break;
    }
    case 0x210e:
    case 0x2110:
    case 0x2112:
    case 0x2114: {
      int layer = (reg - 0x210e) / 2;
      p->bg[layer].vscroll = (uint16_t)(((val << 8) | p->scroll_prev) & 0x3ff);
      p->scroll_prev = val;
      break;
    }
    case 0x2123:
      p->windowsel = (p->windowsel & ~0xffu) | val;
      break;
    case 0x2124:
      p->windowsel = (p->windowsel & ~0xff00u) | ((uint32_t)val << 8);
      break;
    case 0x2125:
      p->windowsel = (p->windowsel & ~0xff0000u) | ((uint32_t)val << 16);
      break;
    case 0x2126: p->window1left = val; break;
    case 0x2127: p->window1right = val; break;
    case 0x2128: p->window2left = val; break;
    case 0x2129: p->window2right = val; break;
    case 0x212c: p->screen_enabled[0] = val; break;
    case 0x212d: p->screen_enabled[1] = val; break;
    case 0x212e: p->screen_windowed[0] = val; break;
    case 0x212f: p->screen_windowed[1] = val; break;
    case 0x2130:
      p->add_subscreen = (val & 0x02) != 0;
      p->prevent_math_mode = (uint8_t)((val & 0x30) >> 4);
      p->clip_mode = (uint8_t)((val & 0xc0) >> 6);
      break;
    case 0x2131:
      p->subtract_color = (val & 0x80) != 0;
      p->half_color = (val & 0x40) != 0;
      p->math_enabled = (uint8_t)(val & 0x3f);
      break;
    case 0x2132:
      if (val & 0x80) p->fixed_color_b = val & 0x1f;
      if (val & 0x40) p->fixed_color_g = val & 0x1f;
      if (val & 0x20) p->fixed_color_r = val & 0x1f;
      break;
    default:
      break;
  }
}

static bool HdGetWindowState(const HdPpu *p, int layer, int x) {
  uint32_t winflags = GET_WINDOW_FLAGS(p, layer);
  if (!(winflags & kWindow1Enabled) && !(winflags & kWindow2Enabled))
    return false;
  if ((winflags & kWindow1Enabled) && !(winflags & kWindow2Enabled)) {
    bool test = x >= p->window1left && x <= p->window1right;
    return (winflags & kWindow1Inversed) ? !test : test;
  }
  if (!(winflags & kWindow1Enabled) && (winflags & kWindow2Enabled)) {
    bool test = x >= p->window2left && x <= p->window2right;
    return (winflags & kWindow2Inversed) ? !test : test;
  }
  bool test1 = x >= p->window1left && x <= p->window1right;
  bool test2 = x >= p->window2left && x <= p->window2right;
  if (winflags & kWindow1Inversed) test1 = !test1;
  if (winflags & kWindow2Inversed) test2 = !test2;
  return test1 || test2;
}

static bool HdConditionMatches(const HdEntry *e, const FrameDesc *desc,
                               int layer, int attr) {
  const HdConditions *c = &e->cond;
  if (e->layer >= 0 && e->layer != layer)
    return false;
  if ((c->flags & kHdCondIndoors) && desc->semantics.indoors != c->indoors)
    return false;
  if ((c->flags & kHdCondRoom) && desc->semantics.room != c->room)
    return false;
  if ((c->flags & kHdCondArea) && desc->semantics.area != c->area)
    return false;
  if ((c->flags & kHdCondFloor) && desc->semantics.floor != c->floor)
    return false;
  if (c->flags & kHdCondAttr) {
    if (attr < 0 || attr != c->attr)
      return false;
  }
  if ((c->flags & kHdCondLayer) && c->layer >= 0 && c->layer != layer)
    return false;
  return true;
}

static const HdEntry *HdFindEntry(const HdState *st, uint32_t hash,
                                  int palette, int bpp, int layer, int attr,
                                  const FrameDesc *desc) {
  for (int i = 0; i < st->entry_count; i++) {
    const HdEntry *e = &st->entries[i];
    if (e->hash != hash)
      continue;
    if (e->palette >= 0 && e->palette != palette)
      continue;
    if (e->bpp != 0 && e->bpp != bpp)
      continue;
    if (!HdConditionMatches(e, desc, layer, attr))
      continue;
    return e;
  }
  return NULL;
}

static uint16_t HdMakeRef(HdPpu *p, uint32_t hash, int palette, int bpp,
                          int layer, int attr) {
  const HdEntry *e = HdFindEntry(p->st, hash, palette, bpp, layer, attr,
                                 p->desc);
  if (e == NULL || s_ref_count >= kHdMaxRefs)
    return 0xffff;
  s_refs[s_ref_count].entry = e;
  s_refs[s_ref_count].image = &p->st->images[e->image];
  return (uint16_t)s_ref_count++;
}

static int HdAttrAt(const HdPpu *p, int layer, int screen_x, int screen_y) {
  if (layer < 0 || layer > 1)
    return -1;
  const FrameDescTileLayer *tl = &p->desc->semantics.layer[layer];
  if (!tl->present)
    return -1;
  int col = (screen_x + p->extra_left_right) >> 3;
  int row = screen_y >> 3;
  if (col < 0 || row < 0 || col >= tl->cols || row >= tl->rows)
    return -1;
  return tl->attr[row * kFrameDesc_MaxTileCols + col];
}

// ---------------------------------------------------------------------------
// Background and sprite scanline drawing

static void HdDrawBackground(HdPpu *p, int line, bool sub, int layer,
                             uint16_t zhi, uint16_t zlo) {
  if (!(p->screen_enabled[sub] & (1 << layer)))
    return;
  const HdBg *bg = &p->bg[layer];
  const uint16_t *vram = p->desc->vram;
  HdZbuf *dst = sub ? s_sub : s_main;
  int bpp = layer < 2 ? 4 : 2;
  int left = 0, right = 256;
  if (p->new_renderer) {
    left = layer == 2 ? 0 : -(int)p->extra_left_cur;
    right = layer == 2 ? 256 : 256 + p->extra_right_cur;
  }
  bool mosaic = (p->mosaic_enabled & (1 << layer)) != 0 && p->mosaic_size > 1;
  int size = p->mosaic_size;

  int last_map_adr = -1;
  int last_palette = -1;
  int last_attr = -1;
  uint16_t last_ref = 0xffff;

  for (int x = left; x < right; x++) {
    if ((p->screen_windowed[sub] & (1 << layer)) &&
        HdGetWindowState(p, layer, x))
      continue;
    int sx = x, sy = line;
    if (mosaic) {
      sx = x - (x % size);
      // The old per-pixel renderer aligns the visible row (line - 1) to the
      // mosaic block; the z-buffer renderer aligns the internal line. Both
      // are used by the game depending on NewRenderer.
      sy = p->new_renderer ? line - (line % size)
                           : line - ((line - 1) % size);
    }
    int wx = sx + bg->hscroll;
    int wy = sy + bg->vscroll;
    int map_adr = bg->tilemap_adr +
        ((((wy >> 3) & 0x1f) << 5) | ((wx >> 3) & 0x1f));
    if ((wx & 0x100) && bg->wider)
      map_adr += 0x400;
    if ((wy & 0x100) && bg->higher)
      map_adr += bg->wider ? 0x800 : 0x400;
    uint16_t tile = vram[map_adr & 0x7fff];
    int priority = (tile & 0x2000) != 0;
    int palette = (tile & 0x1c00) >> 10;
    if (p->mode == 0)
      palette += 8 * layer;
    int row = (tile & 0x8000) ? 7 - (wy & 7) : (wy & 7);
    int col = (tile & 0x4000) ? (wx & 7) : 7 - (wx & 7);
    int tile_num = tile & 0x3ff;
    int base = bg->tile_adr + tile_num * (bpp == 4 ? 16 : 8);
    int pixel;
    if (bpp == 4) {
      uint16_t p0 = vram[(base + row) & 0x7fff];
      uint16_t p1 = vram[(base + 8 + row) & 0x7fff];
      pixel = (int)(((p0 >> col) & 1) | (((p0 >> (8 + col)) & 1) << 1) |
                    (((p1 >> col) & 1) << 2) |
                    (((p1 >> (8 + col)) & 1) << 3));
    } else {
      uint16_t p0 = vram[(base + row) & 0x7fff];
      pixel = (int)(((p0 >> col) & 1) | (((p0 >> (8 + col)) & 1) << 1));
    }
    if (pixel == 0)
      continue;

    uint16_t z = priority ? zhi : zlo;
    z = (uint16_t)(z + ((tile & 0x1c00) >> (bpp == 4 ? 6 : 8)));
    int di = x + kPpuExtraLeftRight;
    if (z <= dst[di].z)
      continue;
    dst[di].z = (uint16_t)(z + pixel);

    int attr = HdAttrAt(p, layer, sx, line - 1);
    if (map_adr != last_map_adr || palette != last_palette ||
        attr != last_attr) {
      uint16_t words[16];
      int word_count = bpp == 4 ? 16 : 8;
      for (int i = 0; i < word_count; i++)
        words[i] = vram[(base + i) & 0x7fff];
      uint32_t hash = HdTileHashWords(words, word_count);
      last_ref = HdMakeRef(p, hash, palette, bpp, layer, attr);
      last_palette = palette;
      last_attr = attr;
      last_map_adr = map_adr;
    }
    dst[di].src.ref = last_ref;
    dst[di].src.row = (uint8_t)row;
    dst[di].src.col = (uint8_t)col;
  }
}

static uint32_t HdSpriteHash(const HdPpu *p, int obj_adr, int used_tile,
                             bool use_link_chr) {
  uint16_t words[16];
  for (int i = 0; i < 16; i++) {
    if (use_link_chr)
      words[i] = p->desc->link_chr[(used_tile * 16 + i) & 0x3ff];
    else
      words[i] = p->desc->vram[(obj_adr + used_tile * 16 + i) & 0x7fff];
  }
  return HdTileHashWords(words, 16);
}

static bool HdEvaluateSprites(HdPpu *p, int line) {
  const FrameDesc *desc = p->desc;
  const uint16_t *oam = desc->oam;
  int index = 0, index_end = index;
  int sprites_left = 32 + 1, tiles_left = 34 + 1;
  uint8_t sprite_sizes[2] = {
    kSpriteSizes[p->obj_size][0], kSpriteSizes[p->obj_size][1]
  };
  int extra = p->extra_left_right;
  if (desc->render_flags & 0x8u)  // kPpuRenderFlags_NoSpriteLimits
    sprites_left = tiles_left = 1024;
  int tiles_left_org = tiles_left;

  do {
    int yy = oam[index] >> 8;
    if (yy == 0xf0)
      continue;
    int row = (line - yy) & 0xff;
    int high_oam = oam[0x100 + (index >> 4)] >> (index & 15);
    int sprite_size = sprite_sizes[(high_oam >> 1) & 1];
    if (row >= sprite_size)
      continue;
    int x = (oam[index] & 0xff) + (high_oam & 1) * 256;
    x -= (x >= 256 + extra) * 512;
    if (x <= -(sprite_size + extra))
      continue;
    if (--sprites_left == 0)
      break;

    int oam1 = oam[index + 1];
    int obj_adr = (oam1 & 0x100) ? p->obj_tile_adr2 : p->obj_tile_adr1;
    if (oam1 & 0x8000)
      row = sprite_size - 1 - row;
    int palette_base = 0x80 + 16 * ((oam1 & 0xe00) >> 9);
    int prio = SPRITE_PRIO_TO_PRIO((oam1 & 0x3000) >> 12,
                                   (oam1 & 0x800) == 0);
    uint16_t z = (uint16_t)(palette_base + (prio << 8));

    for (int col = 0; col < sprite_size; col += 8) {
      if (!(col + x > -8 - extra && col + x < 256 + extra))
        continue;
      if (--tiles_left == 0)
        return true;
      int used_col = (oam1 & 0x4000) ? sprite_size - 1 - col : col;
      int used_tile = ((((oam1 & 0xff) >> 4) + (row >> 3)) << 4) |
          ((((oam1 & 0xf) + (used_col >> 3)) & 0xf));
      bool use_link_chr = desc->oam_is_player2 != NULL &&
          desc->link_chr != NULL &&
          desc->oam_is_player2[index >> 1] != 0 &&
          !(oam1 & 0x100) && used_tile < 0x40;
      uint32_t hash = HdSpriteHash(p, obj_adr, used_tile, use_link_chr);
      uint16_t ref = HdMakeRef(p, hash, (oam1 & 0xe00) >> 9, 4, 3, -1);

      int px_left = -(col + x + extra);
      if (px_left < 0)
        px_left = 0;
      int px_right = 256 + extra - (col + x);
      if (px_right > 8)
        px_right = 8;
      HdZbuf *dst = s_obj + col + x + px_left + kPpuExtraLeftRight;
      for (int px = px_left; px < px_right; px++, dst++) {
        int shift = (oam1 & 0x4000) ? px : 7 - px;
        uint32_t plane;
        if (use_link_chr)
          plane = desc->link_chr[(used_tile * 16 + (row & 7)) & 0x3ff] |
              ((uint32_t)desc->link_chr[(used_tile * 16 + 8 + (row & 7)) &
                                        0x3ff] << 16);
        else
          plane = desc->vram[(obj_adr + used_tile * 16 + (row & 7)) & 0x7fff] |
              ((uint32_t)desc->vram[(obj_adr + used_tile * 16 + 8 +
                                    (row & 7)) & 0x7fff] << 16);
        uint32_t bits = plane >> shift;
        int pixel = (int)((bits & 1) | ((bits >> 7) & 2) |
                          ((bits >> 14) & 4) | ((bits >> 21) & 8));
        if (pixel != 0 && (dst[0].z & 0xff) == 0) {
          dst[0].z = (uint16_t)(z + pixel);
          dst[0].src.ref = ref;
          dst[0].src.row = (uint8_t)(row & 7);
          dst[0].src.col = (uint8_t)shift;
        }
      }
    }
  } while ((index = (index + 2) & 0xff) != index_end);
  return tiles_left != tiles_left_org;
}

static void HdCopySprites(HdPpu *p, int sub) {
  if (!(p->screen_enabled[sub] & 0x10))
    return;
  // The old renderer always clips the world to 0..255 and leaves the extra
  // side columns black; the new renderer draws into extraLeftCur/RightCur.
  int left = p->new_renderer ? -(int)p->extra_left_cur : 0;
  int right = p->new_renderer ? 256 + p->extra_right_cur : 256;
  HdZbuf *dst = sub ? s_sub : s_main;
  for (int x = left; x < right; x++) {
    if ((p->screen_windowed[sub] & 0x10) &&
        HdGetWindowState(p, 4, x))
      continue;
    int i = x + kPpuExtraLeftRight;
    dst[i] = s_obj[i];
  }
}

static void HdClearBackdrop(HdZbuf *buf) {
  for (int i = 0; i < kHdMaxXPixels; i++) {
    buf[i].z = 0x0500;
    buf[i].src.ref = 0xffff;
    buf[i].src.row = 0;
    buf[i].src.col = 0;
  }
}

// ---------------------------------------------------------------------------
// Colour math and output

static bool HdSampleRef(const HdTileRef *ref, int row, int col, int sx, int sy,
                        int scale, uint8_t rgba[4]) {
  if (ref == NULL || ref->entry == NULL || ref->image == NULL)
    return false;
  const HdEntry *e = ref->entry;
  const HdImage *img = ref->image;
  int u = (col * scale + sx) * e->w / (8 * scale);
  int v = (row * scale + sy) * e->h / (8 * scale);
  if (u < 0) u = 0;
  if (v < 0) v = 0;
  if (u >= e->w) u = e->w - 1;
  if (v >= e->h) v = e->h - 1;
  int px = e->x + u;
  int py = e->y + v;
  if (px < 0 || py < 0 || px >= img->width || py >= img->height)
    return false;
  const uint8_t *p = img->rgba + ((size_t)py * img->width + px) * 4;
  rgba[0] = p[0];
  rgba[1] = p[1];
  rgba[2] = p[2];
  rgba[3] = p[3];
  return true;
}

static void HdResolveColor(const HdPpu *p, const HdZbuf *zbuf, int sx, int sy,
                           int scale, int *r, int *g, int *b) {
  uint8_t idx = (uint8_t)(zbuf->z & 0xff);
  uint8_t rgba[4];
  if (idx != 0 && zbuf->src.ref != 0xffff &&
      HdSampleRef(&s_refs[zbuf->src.ref], zbuf->src.row, zbuf->src.col,
                  sx, sy, scale, rgba) && rgba[3] >= 128) {
    *r = (rgba[0] * 31 + 127) / 255;
    *g = (rgba[1] * 31 + 127) / 255;
    *b = (rgba[2] * 31 + 127) / 255;
    return;
  }
  uint16_t color = p->desc->cgram[idx];
  *r = color & 0x1f;
  *g = (color >> 5) & 0x1f;
  *b = (color >> 10) & 0x1f;
}

static uint32_t HdComposePixel(const HdPpu *p, int x, int sx, int sy,
                               int scale) {
  int i = x + kPpuExtraLeftRight;
  const HdZbuf *main_px = &s_main[i];
  const HdZbuf *sub_px = &s_sub[i];
  uint16_t z = main_px->z;
  int main_layer = (z >> 8) & 0xf;
  // The old renderer reports sprites with palette indexes below 0xc0 as layer
  // 6, which has no color-math bit; the z-buffer renderer always uses layer 4.
  if (!p->new_renderer && main_layer == 4 && (z & 0xff) < 0xc0)
    main_layer = 6;
  int r, g, b;
  HdResolveColor(p, main_px, sx, sy, scale, &r, &g, &b);

  bool cwin = HdGetWindowState(p, 5, x);
  bool clip = p->clip_mode == 3 ||
      (p->clip_mode == 2 && cwin) ||
      (p->clip_mode == 1 && !cwin);
  if (clip) {
    r = g = b = 0;
  }
  bool prevent = p->prevent_math_mode == 3 ||
      (p->prevent_math_mode == 2 && cwin) ||
      (p->prevent_math_mode == 1 && !cwin);
  uint8_t math = prevent ? 0 : p->math_enabled;
  bool rendered_subscreen = p->prevent_math_mode != 3 && p->add_subscreen &&
      p->math_enabled != 0 && p->screen_enabled[1] != 0;
  uint32_t fixed = (uint32_t)p->fixed_color_r |
      ((uint32_t)p->fixed_color_g << 5) |
      ((uint32_t)p->fixed_color_b << 10);

  if (math == 0 || (fixed == 0 && !p->half_color && !rendered_subscreen)) {
    return (uint32_t)p->brightness_mult[b] |
        ((uint32_t)p->brightness_mult[g] << 8) |
        ((uint32_t)p->brightness_mult[r] << 16);
  }

  const uint8_t *color_map = p->brightness_mult;
  if (math & (1 << main_layer)) {
    int r2, g2, b2;
    if (p->add_subscreen) {
      if ((sub_px->z & 0xff) != 0) {
        HdResolveColor(p, sub_px, sx, sy, scale, &r2, &g2, &b2);
        color_map = p->half_color ? p->brightness_mult_half
                                  : p->brightness_mult;
      } else {
        r2 = p->fixed_color_r;
        g2 = p->fixed_color_g;
        b2 = p->fixed_color_b;
      }
    } else {
      r2 = p->fixed_color_r;
      g2 = p->fixed_color_g;
      b2 = p->fixed_color_b;
      color_map = p->half_color ? p->brightness_mult_half
                                : p->brightness_mult;
    }
    if (p->subtract_color) {
      r = r >= r2 ? r - r2 : 0;
      g = g >= g2 ? g - g2 : 0;
      b = b >= b2 ? b - b2 : 0;
    } else {
      r += r2;
      g += g2;
      b += b2;
    }
  }
  return (uint32_t)color_map[b] | ((uint32_t)color_map[g] << 8) |
      ((uint32_t)color_map[r] << 16);
}

static void HdEmitBlock(uint8_t *dst, size_t pitch, int scale, uint32_t rgb) {
  uint8_t b = (uint8_t)rgb;
  uint8_t g = (uint8_t)(rgb >> 8);
  uint8_t r = (uint8_t)(rgb >> 16);
  for (int yy = 0; yy < scale; yy++) {
    uint8_t *row = dst + (size_t)yy * pitch;
    for (int xx = 0; xx < scale; xx++) {
      row[xx * 4 + 0] = b;
      row[xx * 4 + 1] = g;
      row[xx * 4 + 2] = r;
      row[xx * 4 + 3] = 0;
    }
  }
}

static void HdDrawLine(HdPpu *p, int line, uint8_t *dst, size_t pitch,
                       int scale, int width) {
  HdClearBackdrop(s_main);
  HdClearBackdrop(s_sub);
  HdClearBackdrop(s_obj);
  s_ref_count = 0;

  if (p->forced_blank || line >= 225 + p->extra_bottom_cur) {
    for (int x = 0; x < width; x++)
      HdEmitBlock(dst + (size_t)x * scale * 4, pitch, scale, 0);
    return;
  }

  p->line_has_sprites = HdEvaluateSprites(p, line - 1);
  if (p->line_has_sprites)
    HdCopySprites(p, 0);
  HdDrawBackground(p, line, false, 0, 0xc000, 0x8000);
  HdDrawBackground(p, line, false, 1, 0xb100, 0x7100);
  HdDrawBackground(p, line, false, 2, 0xf200, 0x1200);

  if (p->prevent_math_mode != 3 && p->add_subscreen && p->math_enabled != 0 &&
      p->screen_enabled[1] != 0) {
    if (p->line_has_sprites)
      HdCopySprites(p, 1);
    HdDrawBackground(p, line, true, 0, 0xc000, 0x8000);
    HdDrawBackground(p, line, true, 1, 0xb100, 0x7100);
    HdDrawBackground(p, line, true, 2, 0xf200, 0x1200);
  }

  int left = p->new_renderer ? -(int)p->extra_left_cur : 0;
  int right = p->new_renderer ? 256 + p->extra_right_cur : 256;
  for (int x = 0; x < width; x++) {
    int logical_x = x - p->extra_left_right;
    uint8_t *block = dst + (size_t)x * scale * 4;
    if (logical_x < left || logical_x >= right) {
      HdEmitBlock(block, pitch, scale, 0);
      continue;
    }
    int i = logical_x + kPpuExtraLeftRight;
    bool may_use_hd = s_main[i].src.ref != 0xffff;
    if (p->add_subscreen && p->math_enabled != 0 &&
        s_sub[i].src.ref != 0xffff)
      may_use_hd = true;
    if (scale == 1 || !may_use_hd) {
      HdEmitBlock(block, pitch, scale,
                  HdComposePixel(p, logical_x, 0, 0, scale));
      continue;
    }
    for (int sy = 0; sy < scale; sy++) {
      uint8_t *row = block + (size_t)sy * pitch;
      for (int sx = 0; sx < scale; sx++) {
        uint32_t rgb = HdComposePixel(p, logical_x, sx, sy, scale);
        row[sx * 4 + 0] = (uint8_t)rgb;
        row[sx * 4 + 1] = (uint8_t)(rgb >> 8);
        row[sx * 4 + 2] = (uint8_t)(rgb >> 16);
        row[sx * 4 + 3] = 0;
      }
    }
  }
}

static bool HdRenderPane(HdState *st, const FrameDesc *desc, uint8_t *pixels,
                         size_t pitch, int scale) {
  if (desc == NULL || desc->version != kFrameDescVersion || !desc->complete ||
      desc->vram == NULL || desc->cgram == NULL)
    return false;
  int width = (int)desc->width;
  int height = (int)desc->height;
  if (width <= 0 || width > kHdMaxXPixels || height <= 0 || height > 240)
    return false;

  if (desc->ppu.mode != 1 || desc->ppu.forced_blank) {
    int ref_scale = (int)desc->display.scale;
    if (ref_scale < 1)
      ref_scale = 1;
    if (ref_scale == scale)
      return st->host->render_frame_desc(st->host, desc, pixels, pitch);
    size_t tmp_pitch = (size_t)width * ref_scale * 4;
    uint8_t *tmp = (uint8_t *)malloc(tmp_pitch * (size_t)height * ref_scale);
    if (tmp == NULL)
      return false;
    bool ok = st->host->render_frame_desc(st->host, desc, tmp, tmp_pitch);
    if (ok) {
      for (int y = 0; y < height * scale; y++) {
        uint8_t *row = pixels + (size_t)y * pitch;
        const uint8_t *src = tmp + (size_t)(y / scale) * tmp_pitch;
        for (int x = 0; x < width * scale; x++) {
          const uint8_t *s = src + (size_t)(x / scale) * 4;
          row[x * 4 + 0] = s[0];
          row[x * 4 + 1] = s[1];
          row[x * 4 + 2] = s[2];
          row[x * 4 + 3] = 0;
        }
      }
    }
    free(tmp);
    return ok;
  }

  HdPpu p;
  HdInitPpu(&p, st, desc);
  uint32_t change = 0;
  while (change < desc->change_count && desc->changes[change].line == 0) {
    uint16_t reg = desc->changes[change].reg;
    if (reg >= 0x2100 && reg <= 0x213f)
      HdWrite(&p, reg, desc->changes[change].value);
    change++;
  }
  for (int line = 1; line <= height; line++) {
    while (change < desc->change_count &&
           desc->changes[change].line == (uint16_t)line) {
      uint16_t reg = desc->changes[change].reg;
      if (reg >= 0x2100 && reg <= 0x213f)
        HdWrite(&p, reg, desc->changes[change].value);
      change++;
    }
    HdEnsureBrightness(&p);
    uint8_t *row = pixels + (size_t)(line - 1) * scale * pitch;
    HdDrawLine(&p, line, row, pitch, scale, width);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Plugin entry points

static bool HdInit(ZeldaRenderPlugin *plugin, const ZeldaRenderHost *host,
                   uint32_t version) {
  HdState *st = (HdState *)plugin->state;
  if (version != kRenderPluginApiVersion || host == NULL ||
      host->api_version != kRenderPluginApiVersion ||
      host->frame_desc_version != kFrameDescVersion ||
      host->render_frame_desc == NULL)
    return false;
  memset(st, 0, sizeof(*st));
  st->host = host;
  st->preferred_scale = 1;
  HdLoadPack(st, getenv("ZELDA3_HD_PACK"));
  return true;
}

static void HdBeginFrame(ZeldaRenderPlugin *plugin,
                         const ZeldaRenderFrameInfo *frame) {
  HdState *st = (HdState *)plugin->state;
  st->pane_count = frame != NULL ? (int)frame->pane_count : 0;
  memset(st->pane, 0, sizeof(st->pane));
}

static void HdFrameDescription(ZeldaRenderPlugin *plugin,
                               const FrameDesc *desc) {
  HdState *st = (HdState *)plugin->state;
  if (desc == NULL || desc->version != kFrameDescVersion ||
      desc->display.pane_index >= kRenderPluginMaxPanes)
    return;
  st->pane[desc->display.pane_index] = desc;
}

static bool HdPresent(ZeldaRenderPlugin *plugin,
                      const ZeldaRenderTarget *target) {
  HdState *st = (HdState *)plugin->state;
  if (target == NULL || target->pixels == NULL)
    return false;
  for (int i = 0; i < st->pane_count; i++) {
    const FrameDesc *desc = st->pane[i];
    if (desc == NULL)
      return false;
    size_t x = (size_t)desc->display.pane_x * (size_t)target->scale;
    size_t y = (size_t)desc->display.pane_y * (size_t)target->scale;
    uint8_t *pixels = target->pixels + y * target->pitch + x * 4;
    if (!HdRenderPane(st, desc, pixels, target->pitch, target->scale))
      return false;
  }
  return true;
}

static int32_t HdPreferredScale(ZeldaRenderPlugin *plugin) {
  HdState *st = (HdState *)plugin->state;
  return st->preferred_scale;
}

static void HdShutdown(ZeldaRenderPlugin *plugin) {
  HdState *st = (HdState *)plugin->state;
  for (int i = 0; i < st->image_count; i++)
    stbi_image_free(st->images[i].rgba);
  free(st->images);
  free(st->entries);
  memset(st, 0, sizeof(*st));
}

static HdState s_hd_state;

ZeldaRenderPlugin g_render_plugin_hd = {
  kRenderPluginApiVersion,
  kFrameDescVersion,
  "hd-tiles",
  "HD tile replacement renderer for FrameDesc",
  &s_hd_state,
  HdInit,
  HdBeginFrame,
  HdFrameDescription,
  HdPresent,
  HdShutdown,
  HdPreferredScale,
  NULL,
};

ZELDA_RENDER_PLUGIN_EXPORT ZeldaRenderPlugin *zelda3_render_plugin_get(
    uint32_t api_version) {
  if (api_version != kRenderPluginApiVersion)
    return NULL;
  return &g_render_plugin_hd;
}
