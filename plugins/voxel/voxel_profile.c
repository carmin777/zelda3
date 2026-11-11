#include "voxel_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

// 1.0 = one 8x8 tile block (8 SNES pixels). This table is mirrored by
// default-profile.json; keep them in sync when changing the defaults.
static const float kDefaultHeights[256] = {
  0.00f, 1.50f, 1.50f, 1.50f, 0.25f, 0.00f, 0.00f, 0.00f,
  -0.75f, -0.25f, -0.25f, -0.75f, 0.00f, 0.00f, 0.00f, 0.00f,
  0.50f, 0.50f, 0.50f, 0.50f, 0.00f, 0.00f, 0.00f, 0.00f,
  0.50f, 0.50f, 0.50f, 0.50f, -0.25f, 0.50f, 0.50f, 0.50f,
  -1.00f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f, 1.50f, 1.50f,
  0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
  0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f,
  0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.50f, 0.50f, 0.50f,
  0.25f, 0.00f, 1.00f, 1.50f, 0.00f, 0.00f, 1.50f, 0.00f,
  0.00f, 0.00f, 0.00f, 0.25f, 0.50f, 0.50f, 0.50f, 0.50f,
  1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f,
  1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.00f,
  0.00f, 0.00f, 0.00f, 1.00f, 0.00f, 0.00f, 0.00f, 1.00f,
  0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 0.00f, 0.00f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
  -1.00f, -1.00f, -1.00f, -1.00f, -1.00f, -1.00f, -1.00f, -1.00f,
  -1.00f, -1.00f, -1.00f, -1.00f, -1.00f, -1.00f, 0.00f, 0.00f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
  0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,
  0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,
  0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,
  0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
  1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f, 1.50f,
};

enum {
  kExceptionIndoors = 1 << 0,
  kExceptionRoom = 1 << 1,
  kExceptionArea = 1 << 2,
  kExceptionFloor = 1 << 3,
  kExceptionLayer = 1 << 4,
  kExceptionAttr = 1 << 5,
  kExceptionRaw = 1 << 6,
  kExceptionTile = 1 << 7,
  kExceptionRawRange = 1 << 8,
  kExceptionTileRange = 1 << 9,
};

typedef struct VoxelException {
  uint32_t flags;
  bool indoors;
  int room;
  int area;
  int floor;
  int layer;
  int attr;
  int raw;
  int tile;
  int raw_min, raw_max;
  int tile_min, tile_max;
  float height;
  VoxelArtMode mode;
} VoxelException;

struct VoxelProfile {
  float heights[256];
  VoxelCameraSettings camera;
  VoxelRenderSettings render;
  VoxelLightSettings light;
  VoxelShadowSettings shadows;
  VoxelException *exceptions;
  int exception_count;
  int exception_capacity;
};

static void SetDefaultCamera(VoxelCameraSettings *camera) {
  camera->angle_deg = 54.0f;
  camera->indoor_angle_deg = 45.0f;
  camera->distance_scale = 1.5f;
  camera->height_scale = 1.0f;
  camera->follow_link = true;
  camera->yaw_deg = 0.0f;
  camera->zoom = 1.0f;
}

static void SetDefaultRender(VoxelRenderSettings *render) {
  render->side_shade = 0.86f;
  render->sprite_scale = 1.0f;
  render->floor_step = 0.5f;
  render->skirt = 1.0f;
  render->height_scale = 1.0f;
  render->tilt_shift = 0.0f;
  render->exposure = 1.04f;
  render->stack_height = 2.2f;
  render->mesh_detail = 1;
  render->face_top = 1.0f;
  render->face_front = 0.98f;
  render->face_side = 0.92f;
  render->face_back = 0.85f;
}

static void SetDefaultLight(VoxelLightSettings *light) {
  light->enabled = true;
  light->azimuth_deg = 45.0f;
  light->elevation_deg = 55.0f;
  light->ambient = 0.55f;
  light->diffuse = 0.45f;
}

static void SetDefaultShadows(VoxelShadowSettings *shadows) {
  shadows->enabled = true;
  shadows->strength = 0.15f;
  shadows->length = 1.5f;
  shadows->columns = true;
  shadows->sprites = true;
}

// ALttP houses are a single drawn object: the upper Map16 band is the roof
// seen from above and the lower band is the facade seen face-on. The raw
// values repeat for every placement of the same house style, so a small
// range table covers all of them. The profile file can override these.
static const VoxelException kBuiltinExceptions[] = {
  { kExceptionRawRange, false, 0, 0, 0, 0, 0, 0, 0,
    0x3e8, 0x3e8, 0, 0, 1.0f, kVoxelArt_Top },
  { kExceptionRawRange, false, 0, 0, 0, 0, 0, 0, 0,
    0x3f0, 0x3f0, 0, 0, 1.0f, kVoxelArt_Top },
  { kExceptionRawRange, false, 0, 0, 0, 0, 0, 0, 0,
    0x3e4, 0x3e4, 0, 0, 2.5f, kVoxelArt_Top },
  { kExceptionRawRange, false, 0, 0, 0, 0, 0, 0, 0,
    0x3f3, 0x3f3, 0, 0, 2.5f, kVoxelArt_Top },
  { kExceptionRawRange, false, 0, 0, 0, 0, 0, 0, 0,
    0x3ff, 0x403, 0, 0, 2.5f, kVoxelArt_Top },
  { kExceptionRawRange, false, 0, 0, 0, 0, 0, 0, 0,
    0x407, 0x40b, 0, 0, 2.5f, kVoxelArt_Top },
  { kExceptionRawRange, false, 0, 0, 0, 0, 0, 0, 0,
    0x410, 0x418, 0, 0, 0.0f, kVoxelArt_Flat },
  { kExceptionRawRange, false, 0, 0, 0, 0, 0, 0, 0,
    0x0da5, 0x0da7, 0, 0, 0.0f, kVoxelArt_Flat },
  { kExceptionRawRange, false, 0, 0, 0, 0, 0, 0, 0,
    0x41b, 0x41c, 0, 0, 2.5f, kVoxelArt_Upright },
  // Lost Woods (area 0) paints its dirt patches with Map16 0x36 on top of the
  // raised grass shelf. Keep the patch at the shelf level, flat, instead of
  // carving a four-pixel pit in the lawn.
  { kExceptionArea | kExceptionRawRange | kExceptionIndoors, false,
    0, 0, 0, 0, 0, 0, 0, 0x36, 0x36, 0, 0, 1.5f, kVoxelArt_Flat },
  // Lost Woods (area 0) draws its brown barricades as a single low
  // embankment. The generic cliff height turns them into tall walls that hide
  // the round crowns behind, so keep this area's collision walls one layer.
  { kExceptionArea | kExceptionAttr | kExceptionIndoors, false,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1.0f, kVoxelArt_Auto },
  { kExceptionArea | kExceptionAttr | kExceptionIndoors, false,
    0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 1.0f, kVoxelArt_Auto },
  { kExceptionArea | kExceptionAttr | kExceptionIndoors, false,
    0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 1.0f, kVoxelArt_Auto },
  // The hut's dark doorway/shading band is drawn over a pit-height attribute;
  // stand it up at the facade instead of laying it flat below the roof.
  { kExceptionArea | kExceptionRawRange | kExceptionIndoors, false,
    0, 0, 0, 0, 0, 0, 0, 0x325, 0x325, 0, 0, 0.5f, kVoxelArt_Upright },
  // Eastern Palace grounds (area 41) draw their ledges with the same one-tile
  // fall as Lost Woods. Keep the plateau edge one layer so the cell steps do
  // not read as a sawtooth.
  { kExceptionArea | kExceptionAttr | kExceptionIndoors, false,
    0, 41, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1.0f, kVoxelArt_Auto },
  { kExceptionArea | kExceptionAttr | kExceptionIndoors, false,
    0, 41, 0, 0, 2, 0, 0, 0, 0, 0, 0, 1.0f, kVoxelArt_Auto },
  { kExceptionArea | kExceptionAttr | kExceptionIndoors, false,
    0, 41, 0, 0, 3, 0, 0, 0, 0, 0, 0, 1.0f, kVoxelArt_Auto },
  // The Dark World swamp (area 82) paints its mud and flower ground with the
  // same Map16 ids the overworld uses for cliff tops. Nothing there is a
  // terrace: keep those drawings flat so the grey/violet patches stay ground.
  { kExceptionArea | kExceptionRawRange | kExceptionIndoors, false,
    0, 82, 0, 0, 0, 0, 0, 0x34, 0x36, 0, 0, 0.0f, kVoxelArt_Flat },
  // The Dark World village (area 98) uses Map16 0x34 for its raised village
  // ground with gravestones painted into the tile. At the generic cliff
  // height that whole square becomes a plateau and the posts stay flat; keep
  // this area's square flat and let the embedded-prop split raise only the
  // painted silhouette.
  { kExceptionArea | kExceptionRawRange | kExceptionIndoors, false,
    0, 98, 0, 0, 0, 0, 0, 0x34, 0x34, 0, 0, 0.0f, kVoxelArt_Flat },
  // The Dark World village graveyard paints each tombstone into an opaque
  // ground tile. Only these tiles may split into a ground quad and a small
  // upright billboard; running that split on every tile turned cliff faces
  // and house facades into thin stretched cards.
  { kExceptionArea | kExceptionRawRange | kExceptionIndoors, false,
    0, 98, 0, 0, 0, 0, 0, 0x21b, 0x21b, 0, 0, 1.5f, kVoxelArt_Prop },
  // Overworld Map16 0x34/0x35 is used both for the round forest canopy
  // (low tile numbers) and for the cliff/room wall face (high tile numbers).
  // Stack the canopy, keep the wall face upright.
  { kExceptionRawRange | kExceptionTileRange | kExceptionIndoors, false,
    0, 0, 0, 0, 0, 0, 0, 0x34, 0x35, 0, 159, 1.5f, kVoxelArt_Stack },
  { kExceptionRawRange | kExceptionTileRange | kExceptionIndoors, false,
    0, 0, 0, 0, 0, 0, 0, 0x34, 0x35, 160, 1023, 1.5f,
    kVoxelArt_Upright },
  { kExceptionRawRange | kExceptionIndoors, false, 0, 0, 0, 0, 0, 0, 0,
    0x644, 0x663, 0, 0, 1.5f, kVoxelArt_Stack },
  // Overworld Map16 tree ranges. The same numbers are ordinary dungeon
  // attributes indoors, so keep the canopy rule outside only.
  { kExceptionRawRange | kExceptionIndoors, false, 0, 0, 0, 0, 0, 0, 0,
    0x365, 0x379, 0, 0, 1.5f, kVoxelArt_Stack },
  { kExceptionRawRange | kExceptionIndoors, false, 0, 0, 0, 0, 0, 0, 0,
    0x095, 0x09c, 0, 0, 1.5f, kVoxelArt_Stack },
};

static void AddBuiltinExceptions(VoxelProfile *profile) {
  int count = (int)(sizeof(kBuiltinExceptions) / sizeof(kBuiltinExceptions[0]));
  profile->exceptions = (VoxelException *)calloc(
      (size_t)count, sizeof(*profile->exceptions));
  if (profile->exceptions == NULL)
    return;
  memcpy(profile->exceptions, kBuiltinExceptions, sizeof(kBuiltinExceptions));
  profile->exception_count = count;
  profile->exception_capacity = count;
}

VoxelProfile *VoxelProfile_CreateDefault(void) {
  VoxelProfile *profile = (VoxelProfile *)calloc(1, sizeof(*profile));
  if (profile == NULL)
    return NULL;
  memcpy(profile->heights, kDefaultHeights, sizeof(profile->heights));
  SetDefaultCamera(&profile->camera);
  SetDefaultRender(&profile->render);
  SetDefaultLight(&profile->light);
  SetDefaultShadows(&profile->shadows);
  AddBuiltinExceptions(profile);
  return profile;
}

void VoxelProfile_Free(VoxelProfile *profile) {
  if (profile == NULL)
    return;
  free(profile->exceptions);
  free(profile);
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

static int JsonInt(const cJSON *o, const char *key, int def) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static double JsonDouble(const cJSON *o, const char *key, double def) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsNumber(v) ? v->valuedouble : def;
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

static VoxelArtMode ParseArtMode(const char *s) {
  if (s == NULL)
    return kVoxelArt_Auto;
  if (strcmp(s, "flat") == 0)
    return kVoxelArt_Flat;
  if (strcmp(s, "top") == 0)
    return kVoxelArt_Top;
  if (strcmp(s, "upright") == 0)
    return kVoxelArt_Upright;
  if (strcmp(s, "stack") == 0)
    return kVoxelArt_Stack;
  if (strcmp(s, "prop") == 0)
    return kVoxelArt_Prop;
  return kVoxelArt_Auto;
}

static int ParseLayer(const cJSON *item) {
  if (cJSON_IsString(item)) {
    if (strcmp(item->valuestring, "bg1") == 0)
      return 0;
    if (strcmp(item->valuestring, "bg2") == 0)
      return 1;
    return -1;
  }
  if (cJSON_IsNumber(item)) {
    int v = (int)item->valuedouble;
    return (v == 0 || v == 1) ? v : -1;
  }
  return -1;
}

static void ParseCamera(VoxelProfile *profile, const cJSON *camera) {
  if (!cJSON_IsObject(camera))
    return;
  profile->camera.angle_deg =
      (float)JsonDouble(camera, "angle", profile->camera.angle_deg);
  profile->camera.indoor_angle_deg =
      (float)JsonDouble(camera, "indoor_angle",
                        profile->camera.indoor_angle_deg);
  profile->camera.distance_scale =
      (float)JsonDouble(camera, "distance", profile->camera.distance_scale);
  profile->camera.height_scale =
      (float)JsonDouble(camera, "height", profile->camera.height_scale);
  profile->camera.follow_link =
      JsonBool(camera, "follow_link", profile->camera.follow_link);
  profile->camera.yaw_deg =
      (float)JsonDouble(camera, "yaw", profile->camera.yaw_deg);
  profile->camera.zoom =
      (float)JsonDouble(camera, "zoom", profile->camera.zoom);
}

static void ParseRender(VoxelProfile *profile, const cJSON *render) {
  if (!cJSON_IsObject(render))
    return;
  profile->render.side_shade =
      (float)JsonDouble(render, "side_shade", profile->render.side_shade);
  profile->render.sprite_scale =
      (float)JsonDouble(render, "sprite_scale", profile->render.sprite_scale);
  profile->render.floor_step =
      (float)JsonDouble(render, "floor_step", profile->render.floor_step);
  profile->render.skirt =
      (float)JsonDouble(render, "skirt", profile->render.skirt);
  profile->render.height_scale =
      (float)JsonDouble(render, "height_scale", profile->render.height_scale);
  profile->render.tilt_shift =
      (float)JsonDouble(render, "tilt_shift", profile->render.tilt_shift);
  profile->render.exposure =
      (float)JsonDouble(render, "exposure", profile->render.exposure);
  profile->render.stack_height = (float)JsonDouble(
      render, "stack_height", profile->render.stack_height);
  profile->render.mesh_detail = (int)JsonDouble(render, "mesh_detail",
                                                profile->render.mesh_detail);
  profile->render.face_top =
      (float)JsonDouble(render, "face_top", profile->render.face_top);
  profile->render.face_front =
      (float)JsonDouble(render, "face_front", profile->render.face_front);
  profile->render.face_side =
      (float)JsonDouble(render, "face_side", profile->render.face_side);
  profile->render.face_back =
      (float)JsonDouble(render, "face_back", profile->render.face_back);
}

static void ParseLight(VoxelProfile *profile, const cJSON *light) {
  if (!cJSON_IsObject(light))
    return;
  profile->light.enabled =
      JsonBool(light, "enabled", profile->light.enabled);
  profile->light.azimuth_deg =
      (float)JsonDouble(light, "azimuth", profile->light.azimuth_deg);
  profile->light.elevation_deg =
      (float)JsonDouble(light, "elevation", profile->light.elevation_deg);
  profile->light.ambient =
      (float)JsonDouble(light, "ambient", profile->light.ambient);
  profile->light.diffuse =
      (float)JsonDouble(light, "diffuse", profile->light.diffuse);
}

static void ParseShadows(VoxelProfile *profile, const cJSON *shadows) {
  if (!cJSON_IsObject(shadows))
    return;
  profile->shadows.enabled =
      JsonBool(shadows, "enabled", profile->shadows.enabled);
  profile->shadows.strength =
      (float)JsonDouble(shadows, "strength", profile->shadows.strength);
  profile->shadows.length =
      (float)JsonDouble(shadows, "length", profile->shadows.length);
  profile->shadows.columns =
      JsonBool(shadows, "columns", profile->shadows.columns);
  profile->shadows.sprites =
      JsonBool(shadows, "sprites", profile->shadows.sprites);
}

static void ParseHeights(VoxelProfile *profile, const cJSON *heights) {
  if (!cJSON_IsObject(heights))
    return;
  const cJSON *item = NULL;
  cJSON_ArrayForEach(item, heights) {
    if (!cJSON_IsNumber(item) || item->string == NULL)
      continue;
    char *end = NULL;
    long index = strtol(item->string, &end, 0);
    if (end == item->string || *end != 0 || index < 0 || index > 255)
      continue;
    profile->heights[index] = (float)item->valuedouble;
  }
}

static bool AppendException(VoxelProfile *profile, const cJSON *item) {
  if (!cJSON_IsObject(item))
    return true;
  const cJSON *height = cJSON_GetObjectItemCaseSensitive(item, "height");
  if (!cJSON_IsNumber(height))
    return true;

  if (profile->exception_count == profile->exception_capacity) {
    int capacity = profile->exception_capacity ? profile->exception_capacity * 2 : 16;
    VoxelException *exceptions = (VoxelException *)realloc(
        profile->exceptions, (size_t)capacity * sizeof(*exceptions));
    if (exceptions == NULL)
      return false;
    profile->exceptions = exceptions;
    profile->exception_capacity = capacity;
  }

  VoxelException *e = &profile->exceptions[profile->exception_count++];
  memset(e, 0, sizeof(*e));
  e->height = (float)height->valuedouble;
  if (JsonHas(item, "indoors")) {
    e->flags |= kExceptionIndoors;
    e->indoors = JsonBool(item, "indoors", false);
  }
  if (JsonHas(item, "room")) {
    e->flags |= kExceptionRoom;
    e->room = JsonInt(item, "room", 0);
  }
  if (JsonHas(item, "area")) {
    e->flags |= kExceptionArea;
    e->area = JsonInt(item, "area", 0);
  }
  if (JsonHas(item, "floor")) {
    e->flags |= kExceptionFloor;
    e->floor = JsonInt(item, "floor", 0);
  }
  if (JsonHas(item, "layer")) {
    int layer = ParseLayer(cJSON_GetObjectItemCaseSensitive(item, "layer"));
    if (layer >= 0) {
      e->flags |= kExceptionLayer;
      e->layer = layer;
    }
  }
  if (JsonHas(item, "attr")) {
    e->flags |= kExceptionAttr;
    e->attr = JsonInt(item, "attr", 0);
  }
  if (JsonHas(item, "raw")) {
    e->flags |= kExceptionRaw;
    e->raw = JsonInt(item, "raw", 0);
  }
  if (JsonHas(item, "tile")) {
    e->flags |= kExceptionTile;
    e->tile = JsonInt(item, "tile", 0);
  }
  if (JsonHas(item, "raw_min") || JsonHas(item, "raw_max")) {
    e->flags |= kExceptionRawRange;
    e->raw_min = JsonInt(item, "raw_min", 0);
    e->raw_max = JsonInt(item, "raw_max", 0xffff);
  }
  if (JsonHas(item, "tile_min") || JsonHas(item, "tile_max")) {
    e->flags |= kExceptionTileRange;
    e->tile_min = JsonInt(item, "tile_min", 0);
    e->tile_max = JsonInt(item, "tile_max", 0x3ff);
  }
  const cJSON *mode = cJSON_GetObjectItemCaseSensitive(item, "mode");
  if (cJSON_IsString(mode))
    e->mode = ParseArtMode(mode->valuestring);
  return true;
}

static void ParseExceptions(VoxelProfile *profile, const cJSON *exceptions) {
  if (!cJSON_IsArray(exceptions))
    return;
  const cJSON *item = NULL;
  cJSON_ArrayForEach(item, exceptions)
    AppendException(profile, item);
}

static const char *JsonString(const cJSON *o, const char *key) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}

static char *JoinProfilePath(const char *base, const char *rel) {
  if (rel == NULL || *rel == 0)
    return NULL;
  if (rel[0] == '/' || rel[0] == '\\' ||
      (rel[0] != 0 && rel[1] == ':')) {
    size_t len = strlen(rel);
    char *out = (char *)malloc(len + 1);
    if (out != NULL)
      memcpy(out, rel, len + 1);
    return out;
  }
  const char *slash = strrchr(base, '/');
  const char *backslash = strrchr(base, '\\');
  if (backslash != NULL && (slash == NULL || backslash > slash))
    slash = backslash;
  size_t dir_len = slash != NULL ? (size_t)(slash - base + 1) : 0;
  size_t rel_len = strlen(rel);
  char *out = (char *)malloc(dir_len + rel_len + 1);
  if (out == NULL)
    return NULL;
  if (dir_len != 0)
    memcpy(out, base, dir_len);
  memcpy(out + dir_len, rel, rel_len + 1);
  return out;
}

static VoxelProfile *LoadInternal(const char *path, int depth) {
  if (path == NULL || depth > 8)
    return NULL;
  char *text = NULL;
  if (!ReadFile(path, &text))
    return NULL;
  cJSON *root = cJSON_Parse(text);
  free(text);
  if (root == NULL)
    return NULL;

  VoxelProfile *profile = VoxelProfile_CreateDefault();
  if (profile == NULL) {
    cJSON_Delete(root);
    return NULL;
  }

  // "extends" makes a per-hack profile a small diff on top of another one.
  const char *extends = JsonString(root, "extends");
  if (extends != NULL) {
    char *base_path = JoinProfilePath(path, extends);
    VoxelProfile *base = LoadInternal(base_path, depth + 1);
    // A hack profile lives outside the tree; if the path relative to the
    // profile file does not exist, also try it as written (relative to the
    // launch directory) so "plugins/voxel/default-profile.json" works.
    if (base == NULL && (base_path == NULL ||
                         strcmp(base_path, extends) != 0))
      base = LoadInternal(extends, depth + 1);
    free(base_path);
    if (base != NULL) {
      memcpy(profile->heights, base->heights, sizeof(profile->heights));
      profile->camera = base->camera;
      profile->render = base->render;
      profile->light = base->light;
      profile->shadows = base->shadows;
      free(profile->exceptions);
      profile->exceptions = base->exceptions;
      profile->exception_count = base->exception_count;
      profile->exception_capacity = base->exception_capacity;
      base->exceptions = NULL;
      base->exception_count = 0;
      base->exception_capacity = 0;
      VoxelProfile_Free(base);
    }
  }

  ParseCamera(profile, cJSON_GetObjectItemCaseSensitive(root, "camera"));
  ParseRender(profile, cJSON_GetObjectItemCaseSensitive(root, "render"));
  ParseLight(profile, cJSON_GetObjectItemCaseSensitive(root, "lighting"));
  ParseShadows(profile, cJSON_GetObjectItemCaseSensitive(root, "shadows"));
  ParseHeights(profile, cJSON_GetObjectItemCaseSensitive(root, "heights"));
  ParseExceptions(profile, cJSON_GetObjectItemCaseSensitive(root, "exceptions"));
  cJSON_Delete(root);
  return profile;
}

VoxelProfile *VoxelProfile_Load(const char *path) {
  return LoadInternal(path, 0);
}

static bool ExceptionMatches(const VoxelException *e, const FrameDesc *desc,
                             int layer, int attr, int raw, int tile) {
  if ((e->flags & kExceptionIndoors) &&
      desc->semantics.indoors != e->indoors)
    return false;
  if ((e->flags & kExceptionRoom) && desc->semantics.room != e->room)
    return false;
  if ((e->flags & kExceptionArea) && desc->semantics.area != e->area)
    return false;
  if ((e->flags & kExceptionFloor) && desc->semantics.floor != e->floor)
    return false;
  if ((e->flags & kExceptionLayer) && layer != e->layer)
    return false;
  if ((e->flags & kExceptionAttr) && attr != e->attr)
    return false;
  if ((e->flags & kExceptionRaw) && raw != e->raw)
    return false;
  if ((e->flags & kExceptionTile) && tile != e->tile)
    return false;
  if ((e->flags & kExceptionRawRange) &&
      (raw < e->raw_min || raw > e->raw_max))
    return false;
  if ((e->flags & kExceptionTileRange) &&
      (tile < e->tile_min || tile > e->tile_max))
    return false;
  return true;
}

float VoxelProfile_Height(const VoxelProfile *profile, const FrameDesc *desc,
                          int layer, int attr, int raw, int tile) {
  if (profile != NULL) {
    for (int i = 0; i < profile->exception_count; i++) {
      if (ExceptionMatches(&profile->exceptions[i], desc, layer, attr, raw,
                           tile))
        return profile->exceptions[i].height;
    }
    if (attr >= 0 && attr < 256)
      return profile->heights[attr];
  }
  if (attr >= 0 && attr < 256)
    return kDefaultHeights[attr];
  return 0.0f;
}

VoxelArtMode VoxelProfile_Art(const VoxelProfile *profile,
                              const FrameDesc *desc, int layer, int attr,
                              int raw, int tile) {
  if (profile != NULL) {
    for (int i = 0; i < profile->exception_count; i++) {
      const VoxelException *e = &profile->exceptions[i];
      if (e->mode != kVoxelArt_Auto &&
          ExceptionMatches(e, desc, layer, attr, raw, tile))
        return e->mode;
    }
  }
  return kVoxelArt_Auto;
}

const VoxelCameraSettings *VoxelProfile_Camera(const VoxelProfile *profile) {
  static const VoxelCameraSettings kDefault = {
      60.0f, 45.0f, 1.5f, 1.0f, true, 0.0f, 1.0f};
  return profile != NULL ? &profile->camera : &kDefault;
}

const VoxelRenderSettings *VoxelProfile_Render(const VoxelProfile *profile) {
  static const VoxelRenderSettings kDefault = {
      0.86f, 1.0f, 0.5f, 1.0f, 1.0f, 0.0f, 1.02f, 2.2f, 1, 1.0f, 0.98f,
      0.92f, 0.85f};
  return profile != NULL ? &profile->render : &kDefault;
}

const VoxelLightSettings *VoxelProfile_Light(const VoxelProfile *profile) {
  static const VoxelLightSettings kDefault = {
      true, 45.0f, 55.0f, 0.55f, 0.45f};
  return profile != NULL ? &profile->light : &kDefault;
}

const VoxelShadowSettings *VoxelProfile_Shadows(const VoxelProfile *profile) {
  static const VoxelShadowSettings kDefault = {
      true, 0.15f, 1.5f, true, true};
  return profile != NULL ? &profile->shadows : &kDefault;
}
