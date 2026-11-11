#include "mods.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "../../third_party/cjson/cJSON.h"
#include "../assets.h"
#include "../config.h"
#include "../load_gfx.h"
#include "../util.h"
#include "asset_names.h"
#include "game_tables.h"
#include "lua_script.h"
#include "mod_features.h"
#include "seed_import.h"

bool g_mods_change_content;

// Snapshot of the mods directory for the options menu. Only filled by
// Mods_LoadAll; the menu does not own any of these strings.
enum { kModListCapacity = 128 };
static ModListInfo g_mod_list[kModListCapacity];
static int g_mod_list_count;

int Mods_ListCount(void) {
  return g_mod_list_count;
}

const ModListInfo *Mods_ListGet(int index) {
  if (index < 0 || index >= g_mod_list_count)
    return NULL;
  return &g_mod_list[index];
}

static void ModListReset(void) {
  memset(g_mod_list, 0, sizeof(g_mod_list));
  g_mod_list_count = 0;
}

static bool ModListAdd(const char *id, const char *name, int priority,
                       bool enabled, int manifest_priority,
                       bool manifest_enabled) {
  if (g_mod_list_count >= kModListCapacity)
    return false;
  for (int i = 0; i < g_mod_list_count; i++) {
    if (strcmp(g_mod_list[i].id, id) == 0)
      return false;
  }
  ModListInfo *info = &g_mod_list[g_mod_list_count++];
  snprintf(info->id, sizeof(info->id), "%s", id);
  snprintf(info->name, sizeof(info->name), "%s", name);
  info->priority = priority;
  info->enabled = enabled;
  info->manifest_priority = manifest_priority;
  info->manifest_enabled = manifest_enabled;
  return true;
}

static void ModListMarkLoaded(const char *id) {
  for (int i = 0; i < g_mod_list_count; i++) {
    if (strcmp(g_mod_list[i].id, id) == 0) {
      g_mod_list[i].loaded = true;
      return;
    }
  }
}

// Asset sizes as they were before any mod loaded. A plain override must keep
// these sizes because much of the game treats several assets as fixed tables.
static uint32 g_original_asset_sizes[kNumberOfAssets];
static bool g_original_asset_sizes_set;

typedef struct StagedAsset {
  int asset;
  uint32 size;
  uint8 *data;
} StagedAsset;

typedef struct StagedTable {
  ModTable *table;
  uint8 *data;
} StagedTable;

typedef struct ModTransaction {
  StagedAsset *assets;
  int count;
  int capacity;
  StagedTable *tables;
  int table_count;
  int table_capacity;
  bool changed;
  bool gloves_changed;
  uint8 gloves[4];
  uint32 feature_mask;
  uint32 feature_value;
} ModTransaction;

typedef struct ModEntry {
  char *dir_name;
  char *dir_path;
  cJSON *json;
  const char *id;
  int priority;
  uint32 permissions;
  bool enabled;
  bool valid;
} ModEntry;

typedef struct DirList {
  char **names;
  int count;
  int capacity;
} DirList;

static void SetError(char *err, size_t err_size, const char *fmt, ...) {
  if (err_size == 0)
    return;
  va_list va;
  va_start(va, fmt);
  vsnprintf(err, err_size, fmt, va);
  va_end(va);
}

static bool IsPathSeparator(char c) {
  return c == '/' || c == '\\';
}

static bool IsAbsolutePath(const char *path) {
  if (IsPathSeparator(path[0]))
    return true;
  // Reject drive-letter paths on every platform so manifests stay portable.
  if (isalpha((unsigned char)path[0]) && path[1] == ':')
    return true;
  return false;
}

static char *StrDup(const char *s) {
  size_t length = strlen(s);
  char *r = malloc(length + 1);
  if (!r)
    Die("malloc failed");
  memcpy(r, s, length + 1);
  return r;
}

static char *JoinPath(const char *dir, const char *name) {
  size_t dir_len = strlen(dir);
  while (dir_len > 1 && IsPathSeparator(dir[dir_len - 1]))
    dir_len--;
  bool add_sep = dir_len != 0 && !IsPathSeparator(dir[dir_len - 1]);
  size_t name_len = strlen(name);
  char *r = malloc(dir_len + (add_sep ? 1 : 0) + name_len + 1);
  if (!r)
    Die("malloc failed");
  memcpy(r, dir, dir_len);
  size_t pos = dir_len;
  if (add_sep)
    r[pos++] = '/';
  memcpy(r + pos, name, name_len + 1);
  return r;
}

bool ModResolvePath(const char *mod_dir, const char *rel, char **out,
                    char *err, size_t err_size) {
  if (out)
    *out = NULL;
  if (rel == NULL || rel[0] == 0) {
    SetError(err, err_size, "path must not be empty");
    return false;
  }
  if (IsAbsolutePath(rel)) {
    SetError(err, err_size, "absolute paths are not allowed: '%s'", rel);
    return false;
  }
  const char *p = rel;
  while (*p) {
    while (IsPathSeparator(*p))
      p++;
    const char *start = p;
    while (*p && !IsPathSeparator(*p))
      p++;
    size_t len = (size_t)(p - start);
    if (len == 2 && start[0] == '.' && start[1] == '.') {
      SetError(err, err_size, "path must stay inside the mod directory (contains '..'): '%s'", rel);
      return false;
    }
  }
  if (out)
    *out = JoinPath(mod_dir, rel);
  return true;
}

bool Mods_ParseZspr(const uint8 *file, size_t length, ZsprData *out) {
  if (length < 27 || memcmp(file, "ZSPR", 4) != 0)
    return false;
  uint32 pixel_offs, palette_offs;
  uint16 pixel_length, palette_length;
  memcpy(&pixel_offs, file + 9, 4);
  memcpy(&pixel_length, file + 13, 2);
  memcpy(&palette_offs, file + 15, 4);
  memcpy(&palette_length, file + 19, 2);
  if ((uint64)pixel_offs + pixel_length > length ||
      (uint64)palette_offs + palette_length > length ||
      pixel_length != 0x7000)
    return false;
  if (out) {
    out->pixels = file + pixel_offs;
    out->pixel_length = pixel_length;
    out->palette = file + palette_offs;
    out->palette_length = palette_length;
  }
  return true;
}

static int HexNibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static bool ParseHexBytes(const char *s, uint8 **out, int *out_size, char *err, size_t err_size) {
  size_t capacity = strlen(s) / 2 + 1;
  uint8 *data = malloc(capacity ? capacity : 1);
  if (!data)
    Die("malloc failed");
  int count = 0;
  const char *start = s;
  for (;;) {
    while (isspace((unsigned char)*s))
      s++;
    if (*s == 0)
      break;
    int hi = HexNibble(*s++);
    if (hi < 0 || *s == 0 || (HexNibble(*s) < 0)) {
      free(data);
      SetError(err, err_size, "invalid hex byte string '%s'", start);
      return false;
    }
    int lo = HexNibble(*s++);
    data[count++] = (uint8)(hi * 16 + lo);
  }
  *out = data;
  *out_size = count;
  return true;
}

static void BytesToHex(const uint8 *data, int size, char *out, size_t out_size) {
  size_t pos = 0;
  for (int i = 0; i < size && pos + 4 < out_size; i++) {
    if (i != 0)
      out[pos++] = ' ';
    int n = snprintf(out + pos, out_size - pos, "%02X", data[i]);
    if (n < 0)
      break;
    pos += (size_t)n;
  }
  out[pos < out_size ? pos : out_size - 1] = 0;
}

static StagedAsset *TxFind(ModTransaction *tx, int asset) {
  for (int i = 0; i < tx->count; i++) {
    if (tx->assets[i].asset == asset)
      return &tx->assets[i];
  }
  return NULL;
}

static StagedAsset *TxGetOrCopy(ModTransaction *tx, int asset) {
  StagedAsset *staged = TxFind(tx, asset);
  if (staged)
    return staged;
  if (tx->count == tx->capacity) {
    int new_capacity = tx->capacity ? tx->capacity * 2 : 4;
    StagedAsset *new_assets = realloc(tx->assets, sizeof(*new_assets) * new_capacity);
    if (!new_assets)
      Die("malloc failed");
    tx->assets = new_assets;
    tx->capacity = new_capacity;
  }
  staged = &tx->assets[tx->count++];
  staged->asset = asset;
  staged->size = g_asset_sizes[asset];
  staged->data = malloc(staged->size ? staged->size : 1);
  if (!staged->data)
    Die("malloc failed");
  if (staged->size != 0)
    memcpy(staged->data, g_asset_ptrs[asset], staged->size);
  return staged;
}

static bool TxOverrideFile(ModTransaction *tx, int asset, const char *path, bool allow_resize,
                           char *err, size_t err_size) {
  size_t length = 0;
  uint8 *data = ReadWholeFile(path, &length);
  if (data == NULL) {
    SetError(err, err_size, "unable to read asset override '%s'", path);
    return false;
  }
  if (length > 0xffffffffu) {
    free(data);
    SetError(err, err_size, "asset override '%s' is too large", path);
    return false;
  }
  uint32 expected_size = g_original_asset_sizes_set
      ? g_original_asset_sizes[asset]
      : g_asset_sizes[asset];
  if (!allow_resize && length != expected_size) {
    free(data);
    SetError(err, err_size,
             "asset '%s' override '%s' has %u bytes, expected %u (allow_resize is false)",
             kAssetNames[asset], path, (unsigned)length, expected_size);
    return false;
  }
  StagedAsset *staged = TxFind(tx, asset);
  if (staged) {
    free(staged->data);
    staged->data = data;
    staged->size = (uint32)length;
  } else {
    if (tx->count == tx->capacity) {
      int new_capacity = tx->capacity ? tx->capacity * 2 : 4;
      StagedAsset *new_assets = realloc(tx->assets, sizeof(*new_assets) * new_capacity);
      if (!new_assets)
        Die("malloc failed");
      tx->assets = new_assets;
      tx->capacity = new_capacity;
    }
    staged = &tx->assets[tx->count++];
    staged->asset = asset;
    staged->data = data;
    staged->size = (uint32)length;
  }
  tx->changed = true;
  return true;
}

static bool TxOverrideTable(ModTransaction *tx, ModTable *table, const char *path,
                            char *err, size_t err_size) {
  size_t length = 0;
  uint8 *data = ReadWholeFile(path, &length);
  if (data == NULL) {
    SetError(err, err_size, "unable to read table override '%s'", path);
    return false;
  }
  if (length != table->size) {
    free(data);
    SetError(err, err_size,
             "table '%s' override '%s' has %u bytes, expected %u",
             table->name, path, (unsigned)length, (unsigned)table->size);
    return false;
  }
  if (!ModTable_Validate(table, data, err, err_size)) {
    free(data);
    return false;
  }
  for (int i = 0; i < tx->table_count; i++) {
    if (tx->tables[i].table == table) {
      free(tx->tables[i].data);
      tx->tables[i].data = data;
      tx->changed = true;
      return true;
    }
  }
  if (tx->table_count == tx->table_capacity) {
    int new_capacity = tx->table_capacity ? tx->table_capacity * 2 : 4;
    StagedTable *new_tables = realloc(tx->tables, sizeof(*new_tables) * new_capacity);
    if (!new_tables)
      Die("malloc failed");
    tx->tables = new_tables;
    tx->table_capacity = new_capacity;
  }
  tx->tables[tx->table_count].table = table;
  tx->tables[tx->table_count].data = data;
  tx->table_count++;
  tx->changed = true;
  return true;
}

static void TxDestroy(ModTransaction *tx) {
  for (int i = 0; i < tx->count; i++)
    free(tx->assets[i].data);
  free(tx->assets);
  for (int i = 0; i < tx->table_count; i++)
    free(tx->tables[i].data);
  free(tx->tables);
  memset(tx, 0, sizeof(*tx));
}

static void TxCommit(ModTransaction *tx, const char *mod_id, int priority) {
  for (int i = 0; i < tx->count; i++) {
    g_asset_ptrs[tx->assets[i].asset] = tx->assets[i].data;
    g_asset_sizes[tx->assets[i].asset] = tx->assets[i].size;
  }
  free(tx->assets);
  for (int i = 0; i < tx->table_count; i++) {
    memcpy(tx->tables[i].table->ptr, tx->tables[i].data, tx->tables[i].table->size);
    free(tx->tables[i].data);
  }
  free(tx->tables);
  if (tx->changed)
    g_mods_change_content = true;
  if (tx->gloves_changed)
    memcpy(kGlovesColor, tx->gloves, sizeof(tx->gloves));
  if (tx->feature_mask != 0)
    ModFeatures_Apply(mod_id, priority, tx->feature_mask, tx->feature_value);
  memset(tx, 0, sizeof(*tx));
}

static bool ApplyPatchEntry(ModTransaction *tx, cJSON *patch, const char *display_path,
                            int patch_index, char *err, size_t err_size) {
  if (!cJSON_IsObject(patch)) {
    SetError(err, err_size, "patch '%s': entry %d must be an object", display_path, patch_index);
    return false;
  }
  cJSON *asset = cJSON_GetObjectItemCaseSensitive(patch, "asset");
  if (!cJSON_IsString(asset) || asset->valuestring == NULL || asset->valuestring[0] == 0) {
    SetError(err, err_size, "patch '%s': entry %d is missing a valid 'asset'", display_path, patch_index);
    return false;
  }
  int asset_index = FindAssetByName(asset->valuestring);
  if (asset_index < 0) {
    SetError(err, err_size, "patch '%s': entry %d: unknown asset '%s'", display_path, patch_index,
             asset->valuestring);
    return false;
  }

  cJSON *offset = cJSON_GetObjectItemCaseSensitive(patch, "offset");
  if (!cJSON_IsNumber(offset) || offset->valuedouble < 0 ||
      offset->valuedouble != (double)offset->valueint || offset->valueint < 0) {
    SetError(err, err_size, "patch '%s': entry %d: 'offset' must be a non-negative integer",
             display_path, patch_index);
    return false;
  }

  cJSON *bytes = cJSON_GetObjectItemCaseSensitive(patch, "bytes");
  if (!cJSON_IsString(bytes) || bytes->valuestring == NULL) {
    SetError(err, err_size, "patch '%s': entry %d is missing a valid 'bytes'", display_path, patch_index);
    return false;
  }
  uint8 *patch_data = NULL;
  int patch_size = 0;
  char detail[256];
  if (!ParseHexBytes(bytes->valuestring, &patch_data, &patch_size, detail, sizeof(detail))) {
    SetError(err, err_size, "patch '%s': entry %d: %s", display_path, patch_index, detail);
    return false;
  }

  cJSON *expect = cJSON_GetObjectItemCaseSensitive(patch, "expect");
  uint8 *expect_data = NULL;
  int expect_size = 0;
  if (expect != NULL) {
    if (!cJSON_IsString(expect) || expect->valuestring == NULL) {
      SetError(err, err_size, "patch '%s': entry %d: 'expect' must be a hex byte string",
               display_path, patch_index);
      free(patch_data);
      return false;
    }
    if (!ParseHexBytes(expect->valuestring, &expect_data, &expect_size, detail, sizeof(detail))) {
      SetError(err, err_size, "patch '%s': entry %d: 'expect': %s", display_path, patch_index, detail);
      free(patch_data);
      return false;
    }
    if (expect_size != patch_size) {
      SetError(err, err_size, "patch '%s': entry %d: 'expect' has %d bytes but 'bytes' has %d",
               display_path, patch_index, expect_size, patch_size);
      free(expect_data);
      free(patch_data);
      return false;
    }
  }

  StagedAsset *staged = TxGetOrCopy(tx, asset_index);
  uint32 offset_value = (uint32)offset->valueint;
  if ((uint64)offset_value + (uint64)patch_size > staged->size) {
    SetError(err, err_size,
             "patch '%s': entry %d: asset '%s' offset %u + %d bytes exceeds asset size %u",
             display_path, patch_index, asset->valuestring, offset_value, patch_size, staged->size);
    free(expect_data);
    free(patch_data);
    return false;
  }
  if (expect_data != NULL && memcmp(staged->data + offset_value, expect_data, (size_t)patch_size) != 0) {
    char expected_hex[256], actual_hex[256];
    BytesToHex(expect_data, expect_size, expected_hex, sizeof(expected_hex));
    BytesToHex(staged->data + offset_value, patch_size, actual_hex, sizeof(actual_hex));
    SetError(err, err_size,
             "patch '%s': entry %d: asset '%s' offset %u: expected [%s], found [%s]",
             display_path, patch_index, asset->valuestring, offset_value, expected_hex, actual_hex);
    free(expect_data);
    free(patch_data);
    return false;
  }
  memcpy(staged->data + offset_value, patch_data, (size_t)patch_size);
  free(expect_data);
  free(patch_data);
  tx->changed = true;
  return true;
}

static bool LoadPatchFile(ModTransaction *tx, const char *path, const char *display_path,
                          char *err, size_t err_size) {
  size_t length = 0;
  uint8 *file = ReadWholeFile(path, &length);
  if (file == NULL) {
    SetError(err, err_size, "patch '%s': unable to read file", display_path);
    return false;
  }
  const char *parse_end = NULL;
  cJSON *root = cJSON_ParseWithOpts((const char *)file, &parse_end, 1);
  free(file);
  if (root == NULL) {
    SetError(err, err_size, "patch '%s': invalid JSON", display_path);
    return false;
  }
  if (!cJSON_IsArray(root)) {
    SetError(err, err_size, "patch '%s': top-level value must be an array", display_path);
    cJSON_Delete(root);
    return false;
  }
  bool ok = true;
  int index = 0;
  cJSON *patch = NULL;
  cJSON_ArrayForEach(patch, root) {
    if (!ApplyPatchEntry(tx, patch, display_path, index++, err, err_size)) {
      ok = false;
      break;
    }
  }
  cJSON_Delete(root);
  return ok;
}

static bool ApplyLinkSprite(ModTransaction *tx, const char *path, char *err, size_t err_size) {
  size_t length = 0;
  uint8 *file = ReadWholeFile(path, &length);
  if (file == NULL) {
    SetError(err, err_size, "unable to read Link sprite '%s'", path);
    return false;
  }
  ZsprData zspr;
  if (!Mods_ParseZspr(file, length, &zspr)) {
    free(file);
    SetError(err, err_size, "invalid ZSPR Link sprite '%s'", path);
    return false;
  }

  int graphics_index = FindAssetByName("kLinkGraphics");
  int palette_index = FindAssetByName("kPalette_ArmorAndGloves");
  if (graphics_index < 0 || palette_index < 0)
    Die("Mods: required Link assets are missing");

  StagedAsset *graphics = TxGetOrCopy(tx, graphics_index);
  if (graphics->size != 0x7000) {
    free(file);
    SetError(err, err_size, "Link sprite '%s': kLinkGraphics has size %u, expected 0x7000",
             path, graphics->size);
    return false;
  }
  memcpy(graphics->data, zspr.pixels, 0x7000);

  if (zspr.palette_length >= 120) {
    StagedAsset *palette = TxGetOrCopy(tx, palette_index);
    if (palette->size < 120) {
      free(file);
      SetError(err, err_size, "Link sprite '%s': kPalette_ArmorAndGloves has size %u, need at least 120",
               path, palette->size);
      return false;
    }
    memcpy(palette->data, zspr.palette, 120);
  }
  if (zspr.palette_length >= 124) {
    tx->gloves_changed = true;
    memcpy(tx->gloves, zspr.palette + 120, 4);
  }
  tx->changed = true;
  free(file);
  return true;
}

static void FreeModEntry(ModEntry *entry) {
  free(entry->dir_name);
  free(entry->dir_path);
  cJSON_Delete(entry->json);
  memset(entry, 0, sizeof(*entry));
}

static bool ValidateStringArray(cJSON *value, const char *field, char *err, size_t err_size) {
  if (!cJSON_IsArray(value)) {
    SetError(err, err_size, "'%s' must be an array of strings", field);
    return false;
  }
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, value) {
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
      SetError(err, err_size, "'%s' must contain only strings", field);
      return false;
    }
  }
  return true;
}

static bool LoadManifest(const char *dir_path, const char *dir_name, ModEntry *entry,
                         char *err, size_t err_size) {
  memset(entry, 0, sizeof(*entry));
  entry->valid = true;
  entry->dir_name = StrDup(dir_name);
  entry->dir_path = StrDup(dir_path);

  char *manifest_path = JoinPath(dir_path, "mod.json");
  size_t length = 0;
  uint8 *data = ReadWholeFile(manifest_path, &length);
  free(manifest_path);
  if (data == NULL) {
    SetError(err, err_size, "missing mod.json");
    return false;
  }
  const char *parse_end = NULL;
  entry->json = cJSON_ParseWithOpts((const char *)data, &parse_end, 1);
  free(data);
  if (entry->json == NULL) {
    SetError(err, err_size, "mod.json is not valid JSON");
    return false;
  }
  if (!cJSON_IsObject(entry->json)) {
    SetError(err, err_size, "mod.json must contain a JSON object");
    return false;
  }

  cJSON *id = cJSON_GetObjectItemCaseSensitive(entry->json, "id");
  if (!cJSON_IsString(id) || id->valuestring == NULL || id->valuestring[0] == 0) {
    SetError(err, err_size, "missing or invalid required field 'id'");
    return false;
  }
  if (strchr(id->valuestring, '/') != NULL || strchr(id->valuestring, '\\') != NULL) {
    SetError(err, err_size, "'id' must not contain path separators");
    return false;
  }
  entry->id = id->valuestring;

  cJSON *api = cJSON_GetObjectItemCaseSensitive(entry->json, "api");
  if (!cJSON_IsNumber(api) || api->valuedouble != 1.0 || api->valueint != 1) {
    SetError(err, err_size, "missing or unsupported 'api' (this loader supports api 1)");
    return false;
  }

  cJSON *enabled = cJSON_GetObjectItemCaseSensitive(entry->json, "enabled");
  if (enabled != NULL && !cJSON_IsBool(enabled)) {
    SetError(err, err_size, "'enabled' must be a boolean");
    return false;
  }
  entry->enabled = enabled == NULL || cJSON_IsTrue(enabled);

  cJSON *priority = cJSON_GetObjectItemCaseSensitive(entry->json, "priority");
  if (priority != NULL) {
    if (!cJSON_IsNumber(priority) || priority->valuedouble != (double)priority->valueint) {
      SetError(err, err_size, "'priority' must be an integer");
      return false;
    }
    entry->priority = priority->valueint;
  }

  cJSON *depends = cJSON_GetObjectItemCaseSensitive(entry->json, "depends");
  if (depends != NULL && !ValidateStringArray(depends, "depends", err, err_size))
    return false;

  cJSON *permissions = cJSON_GetObjectItemCaseSensitive(entry->json, "permissions");
  if (permissions != NULL) {
    if (!ValidateStringArray(permissions, "permissions", err, err_size))
      return false;
    cJSON *permission = NULL;
    cJSON_ArrayForEach(permission, permissions) {
      if (strcmp(permission->valuestring, "state_write") == 0) {
        entry->permissions |= kModPermission_StateWrite;
      } else {
        SetError(err, err_size, "unknown permission '%s' (supported: state_write)",
                 permission->valuestring);
        return false;
      }
    }
  }

  cJSON *link_sprite = cJSON_GetObjectItemCaseSensitive(entry->json, "link_sprite");
  if (link_sprite != NULL &&
      (!cJSON_IsString(link_sprite) || link_sprite->valuestring == NULL || link_sprite->valuestring[0] == 0)) {
    SetError(err, err_size, "'link_sprite' must be a non-empty path string");
    return false;
  }

  cJSON *script = cJSON_GetObjectItemCaseSensitive(entry->json, "script");
  if (script != NULL &&
      (!cJSON_IsString(script) || script->valuestring == NULL || script->valuestring[0] == 0)) {
    SetError(err, err_size, "'script' must be a non-empty path string");
    return false;
  }

  cJSON *seed_data = cJSON_GetObjectItemCaseSensitive(entry->json, "seed_data");
  if (seed_data != NULL &&
      (!cJSON_IsString(seed_data) || seed_data->valuestring == NULL || seed_data->valuestring[0] == 0)) {
    SetError(err, err_size, "'seed_data' must be a non-empty path string");
    return false;
  }

  cJSON *overrides = cJSON_GetObjectItemCaseSensitive(entry->json, "asset_overrides");
  if (overrides != NULL) {
    if (!cJSON_IsObject(overrides)) {
      SetError(err, err_size, "'asset_overrides' must be an object");
      return false;
    }
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, overrides) {
      if (cJSON_IsString(item)) {
        if (item->valuestring == NULL || item->valuestring[0] == 0) {
          SetError(err, err_size, "'asset_overrides.%s' must be a non-empty path string", item->string);
          return false;
        }
      } else if (cJSON_IsObject(item)) {
        cJSON *file = cJSON_GetObjectItemCaseSensitive(item, "file");
        if (!cJSON_IsString(file) || file->valuestring == NULL || file->valuestring[0] == 0) {
          SetError(err, err_size, "'asset_overrides.%s.file' must be a non-empty path string", item->string);
          return false;
        }
        cJSON *allow_resize = cJSON_GetObjectItemCaseSensitive(item, "allow_resize");
        if (allow_resize != NULL && !cJSON_IsBool(allow_resize)) {
          SetError(err, err_size, "'asset_overrides.%s.allow_resize' must be a boolean", item->string);
          return false;
        }
      } else {
        SetError(err, err_size,
                 "'asset_overrides.%s' must be a path string or an object with 'file' and optional 'allow_resize'",
                 item->string);
        return false;
      }
    }
  }

  cJSON *tables = cJSON_GetObjectItemCaseSensitive(entry->json, "table_overrides");
  if (tables != NULL) {
    if (!cJSON_IsObject(tables)) {
      SetError(err, err_size, "'table_overrides' must be an object");
      return false;
    }
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, tables) {
      if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == 0) {
        SetError(err, err_size, "'table_overrides.%s' must be a non-empty path string", item->string);
        return false;
      }
    }
  }

  cJSON *features = cJSON_GetObjectItemCaseSensitive(entry->json, "features");
  if (features != NULL) {
    if (!cJSON_IsObject(features)) {
      SetError(err, err_size, "'features' must be an object");
      return false;
    }
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, features) {
      uint32 mask = 0;
      if (!ModFeatures_Find(item->string, &mask)) {
        char names[512];
        ModFeatures_FormatNames(names, sizeof(names));
        SetError(err, err_size, "unknown feature '%s' (supported: %s)",
                 item->string, names);
        return false;
      }
      if (!cJSON_IsBool(item)) {
        SetError(err, err_size, "'features.%s' must be a boolean", item->string);
        return false;
      }
    }
  }

  cJSON *patches = cJSON_GetObjectItemCaseSensitive(entry->json, "patches");
  if (patches != NULL && !ValidateStringArray(patches, "patches", err, err_size))
    return false;

  return true;
}

static bool LoadOneMod(const ModEntry *entry, char *err, size_t err_size) {
  ModTransaction tx;
  memset(&tx, 0, sizeof(tx));

  cJSON *overrides = cJSON_GetObjectItemCaseSensitive(entry->json, "asset_overrides");
  if (overrides != NULL) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, overrides) {
      int asset = FindAssetByName(item->string);
      if (asset < 0) {
        SetError(err, err_size, "unknown asset '%s'", item->string);
        TxDestroy(&tx);
        return false;
      }
      const char *file;
      bool allow_resize = false;
      if (cJSON_IsString(item)) {
        file = item->valuestring;
      } else {
        cJSON *file_item = cJSON_GetObjectItemCaseSensitive(item, "file");
        file = file_item->valuestring;
        cJSON *allow_resize_item = cJSON_GetObjectItemCaseSensitive(item, "allow_resize");
        allow_resize = allow_resize_item != NULL && cJSON_IsTrue(allow_resize_item);
      }
      char detail[256];
      char *path = NULL;
      if (!ModResolvePath(entry->dir_path, file, &path, detail, sizeof(detail))) {
        SetError(err, err_size, "asset_overrides['%s']: %s", item->string, detail);
        TxDestroy(&tx);
        return false;
      }
      bool ok = TxOverrideFile(&tx, asset, path, allow_resize, err, err_size);
      free(path);
      if (!ok) {
        TxDestroy(&tx);
        return false;
      }
    }
  }

  cJSON *tables = cJSON_GetObjectItemCaseSensitive(entry->json, "table_overrides");
  if (tables != NULL) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, tables) {
      ModTable *table = ModTable_Find(item->string);
      if (table == NULL) {
        SetError(err, err_size, "unknown table '%s'", item->string);
        TxDestroy(&tx);
        return false;
      }
      char detail[256];
      char *path = NULL;
      if (!ModResolvePath(entry->dir_path, item->valuestring, &path, detail, sizeof(detail))) {
        SetError(err, err_size, "table_overrides['%s']: %s", item->string, detail);
        TxDestroy(&tx);
        return false;
      }
      bool ok = TxOverrideTable(&tx, table, path, err, err_size);
      free(path);
      if (!ok) {
        TxDestroy(&tx);
        return false;
      }
    }
  }

  cJSON *patches = cJSON_GetObjectItemCaseSensitive(entry->json, "patches");
  if (patches != NULL) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, patches) {
      char detail[256];
      char *path = NULL;
      if (!ModResolvePath(entry->dir_path, item->valuestring, &path, detail, sizeof(detail))) {
        SetError(err, err_size, "patch '%s': %s", item->valuestring, detail);
        TxDestroy(&tx);
        return false;
      }
      bool ok = LoadPatchFile(&tx, path, item->valuestring, err, err_size);
      free(path);
      if (!ok) {
        TxDestroy(&tx);
        return false;
      }
    }
  }

  cJSON *link_sprite = cJSON_GetObjectItemCaseSensitive(entry->json, "link_sprite");
  if (link_sprite != NULL) {
    char detail[256];
    char *path = NULL;
    if (!ModResolvePath(entry->dir_path, link_sprite->valuestring, &path, detail, sizeof(detail))) {
      SetError(err, err_size, "link_sprite '%s': %s", link_sprite->valuestring, detail);
      TxDestroy(&tx);
      return false;
    }
    bool ok = ApplyLinkSprite(&tx, path, err, err_size);
    free(path);
    if (!ok) {
      TxDestroy(&tx);
      return false;
    }
  }

  cJSON *seed_data = cJSON_GetObjectItemCaseSensitive(entry->json, "seed_data");
  bool seed_loaded = false;
  if (seed_data != NULL) {
    char detail[256];
    char *path = NULL;
    if (!ModResolvePath(entry->dir_path, seed_data->valuestring, &path, detail, sizeof(detail))) {
      SetError(err, err_size, "seed_data '%s': %s", seed_data->valuestring, detail);
      TxDestroy(&tx);
      return false;
    }
    bool ok = ModSeedImport_LoadFile(path, entry->id, err, err_size);
    free(path);
    if (!ok) {
      TxDestroy(&tx);
      return false;
    }
    seed_loaded = true;
    g_mods_change_content = true;
  }

  cJSON *script = cJSON_GetObjectItemCaseSensitive(entry->json, "script");
  if (script != NULL) {
    char detail[256];
    char *path = NULL;
    if (!ModResolvePath(entry->dir_path, script->valuestring, &path, detail, sizeof(detail))) {
      SetError(err, err_size, "script '%s': %s", script->valuestring, detail);
      if (seed_loaded)
        ModSeedImport_Reset();
      TxDestroy(&tx);
      return false;
    }
    bool ok = LuaScript_Load(entry->id, entry->dir_path, script->valuestring, path,
                             entry->permissions, entry->priority, err, err_size);
    free(path);
    if (!ok) {
      if (seed_loaded)
        ModSeedImport_Reset();
      TxDestroy(&tx);
      return false;
    }
  }

  cJSON *features = cJSON_GetObjectItemCaseSensitive(entry->json, "features");
  if (features != NULL) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, features) {
      uint32 mask = 0;
      ModFeatures_Find(item->string, &mask);
      tx.feature_mask |= mask;
      if (cJSON_IsTrue(item))
        tx.feature_value |= mask;
      else
        tx.feature_value &= ~mask;
    }
  }

  TxCommit(&tx, entry->id, entry->priority);
  return true;
}

static bool PathIsDirectory(const char *path) {
#ifdef _WIN32
  DWORD attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void DirListAdd(DirList *list, const char *name) {
  if (list->count == list->capacity) {
    int new_capacity = list->capacity ? list->capacity * 2 : 8;
    char **new_names = realloc(list->names, sizeof(*new_names) * new_capacity);
    if (!new_names)
      Die("malloc failed");
    list->names = new_names;
    list->capacity = new_capacity;
  }
  list->names[list->count++] = StrDup(name);
}

static void DirListFree(DirList *list) {
  for (int i = 0; i < list->count; i++)
    free(list->names[i]);
  free(list->names);
  memset(list, 0, sizeof(*list));
}

static bool ListModDirectories(const char *root, DirList *list) {
#ifdef _WIN32
  char *pattern = JoinPath(root, "*");
  WIN32_FIND_DATAA find_data;
  HANDLE handle = FindFirstFileA(pattern, &find_data);
  free(pattern);
  if (handle == INVALID_HANDLE_VALUE)
    return false;
  do {
    if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
      continue;
    if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0)
      continue;
    DirListAdd(list, find_data.cFileName);
  } while (FindNextFileA(handle, &find_data));
  FindClose(handle);
  return true;
#else
  DIR *dir = opendir(root);
  if (dir == NULL)
    return false;
  struct dirent *item;
  while ((item = readdir(dir)) != NULL) {
    if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
      continue;
    char *path = JoinPath(root, item->d_name);
    bool is_dir = PathIsDirectory(path);
    free(path);
    if (is_dir)
      DirListAdd(list, item->d_name);
  }
  closedir(dir);
  return true;
#endif
}

static int CompareDirNames(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int CompareModEntries(const void *a, const void *b) {
  const ModEntry *ma = a, *mb = b;
  if (ma->valid != mb->valid)
    return ma->valid ? -1 : 1;
  if (ma->priority != mb->priority)
    return ma->priority < mb->priority ? -1 : 1;
  return strcmp(ma->id, mb->id);
}

static bool ModListContains(const ModEntry *mods, int count, const char *id) {
  for (int i = 0; i < count; i++) {
    if (mods[i].valid && strcmp(mods[i].id, id) == 0)
      return true;
  }
  return false;
}

void Mods_LoadAll(const char *directory) {
  ModListReset();
  if (!g_original_asset_sizes_set) {
    memcpy(g_original_asset_sizes, g_asset_sizes, sizeof(g_original_asset_sizes));
    g_original_asset_sizes_set = true;
  }
  if (directory == NULL || directory[0] == 0)
    return;
  DirList dirs;
  memset(&dirs, 0, sizeof(dirs));
  if (!ListModDirectories(directory, &dirs))
    return;
  qsort(dirs.names, dirs.count, sizeof(dirs.names[0]), CompareDirNames);

  ModEntry *mods = malloc(sizeof(*mods) * (dirs.count ? dirs.count : 1));
  if (!mods)
    Die("malloc failed");
  memset(mods, 0, sizeof(*mods) * (dirs.count ? dirs.count : 1));
  int count = 0;

  for (int i = 0; i < dirs.count; i++) {
    char err[512];
    char *path = JoinPath(directory, dirs.names[i]);
    ModEntry *entry = &mods[count];
    if (!LoadManifest(path, dirs.names[i], entry, err, sizeof(err))) {
      const char *label = entry->id != NULL ? entry->id : dirs.names[i];
      fprintf(stderr, "mods: refused '%s': %s\n", label, err);
      FreeModEntry(entry);
      free(path);
      continue;
    }
    free(path);
    int manifest_priority = entry->priority;
    bool manifest_enabled = entry->enabled;
    if (Config_IsModDisableOverride(entry->id))
      entry->enabled = false;
    else if (Config_IsModEnableOverride(entry->id))
      entry->enabled = true;
    int override_priority;
    if (Config_GetModPriorityOverride(entry->id, &override_priority))
      entry->priority = override_priority;
    cJSON *name = cJSON_GetObjectItemCaseSensitive(entry->json, "name");
    const char *display_name =
        cJSON_IsString(name) && name->valuestring != NULL &&
                name->valuestring[0] != 0
            ? name->valuestring
            : entry->id;
    ModListAdd(entry->id, display_name, entry->priority, entry->enabled,
               manifest_priority, manifest_enabled);
    if (!entry->enabled) {
      fprintf(stderr, "mods: skipped '%s' (disabled)\n", entry->id);
      FreeModEntry(entry);
      continue;
    }
    count++;
  }

  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count; j++) {
      if (mods[i].valid && mods[j].valid && strcmp(mods[i].id, mods[j].id) == 0) {
        mods[j].valid = false;
        fprintf(stderr, "mods: refused '%s': duplicate id '%s'\n", mods[j].id, mods[j].id);
      }
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (int i = 0; i < count; i++) {
      if (!mods[i].valid)
        continue;
      cJSON *depends = cJSON_GetObjectItemCaseSensitive(mods[i].json, "depends");
      if (depends == NULL)
        continue;
      cJSON *dependency = NULL;
      cJSON_ArrayForEach(dependency, depends) {
        if (!ModListContains(mods, count, dependency->valuestring)) {
          mods[i].valid = false;
          fprintf(stderr, "mods: refused '%s': missing dependency '%s'\n",
                  mods[i].id, dependency->valuestring);
          changed = true;
          break;
        }
      }
    }
  }

  qsort(mods, count, sizeof(mods[0]), CompareModEntries);

  for (int i = 0; i < count; i++) {
    if (!mods[i].valid)
      continue;
    char err[512];
    if (LoadOneMod(&mods[i], err, sizeof(err))) {
      ModListMarkLoaded(mods[i].id);
      fprintf(stderr, "mods: loaded '%s' (priority %d)\n", mods[i].id, mods[i].priority);
    } else {
      fprintf(stderr, "mods: refused '%s': %s\n", mods[i].id, err);
      mods[i].valid = false;
    }
  }

  ModFeatures_EndLoad();
  for (int i = 0; i < count; i++)
    FreeModEntry(&mods[i]);
  free(mods);
  DirListFree(&dirs);
}
