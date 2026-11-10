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
  float height;
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
  camera->angle_deg = 58.0f;
  camera->distance_scale = 1.0f;
  camera->height_scale = 1.0f;
  camera->follow_link = true;
  camera->yaw_deg = 0.0f;
  camera->zoom = 1.0f;
}

static void SetDefaultRender(VoxelRenderSettings *render) {
  render->side_shade = 0.72f;
  render->sprite_scale = 1.0f;
  render->floor_step = 0.5f;
  render->skirt = 1.0f;
  render->height_scale = 1.0f;
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
  shadows->strength = 0.32f;
  shadows->length = 1.5f;
  shadows->columns = true;
  shadows->sprites = true;
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

const VoxelCameraSettings *VoxelProfile_Camera(const VoxelProfile *profile) {
  static const VoxelCameraSettings kDefault = {
      58.0f, 1.0f, 1.0f, true, 0.0f, 1.0f};
  return profile != NULL ? &profile->camera : &kDefault;
}

const VoxelRenderSettings *VoxelProfile_Render(const VoxelProfile *profile) {
  static const VoxelRenderSettings kDefault = {
      0.72f, 1.0f, 0.5f, 1.0f, 1.0f};
  return profile != NULL ? &profile->render : &kDefault;
}

const VoxelLightSettings *VoxelProfile_Light(const VoxelProfile *profile) {
  static const VoxelLightSettings kDefault = {
      true, 45.0f, 55.0f, 0.55f, 0.45f};
  return profile != NULL ? &profile->light : &kDefault;
}

const VoxelShadowSettings *VoxelProfile_Shadows(const VoxelProfile *profile) {
  static const VoxelShadowSettings kDefault = {
      true, 0.32f, 1.5f, true, true};
  return profile != NULL ? &profile->shadows : &kDefault;
}
