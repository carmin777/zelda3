#ifndef ZELDA3_MOD_MODS_H_
#define ZELDA3_MOD_MODS_H_

#include "../types.h"

typedef struct ZsprData {
  const uint8 *pixels;
  uint32 pixel_length;
  const uint8 *palette;
  uint32 palette_length;
} ZsprData;

// True when at least one enabled mod replaced an asset, applied a patch or
// loaded a Link sprite. The side-by-side comparison must be disabled in that
// case because the C implementation no longer matches the original ROM.
extern bool g_mods_change_content;

// Bits for the optional "permissions" array in mod.json.
enum {
  kModPermission_StateWrite = 1u << 0,
};

// One entry from the mods directory, for the in-game options menu. The
// effective values already include the [Mods] enable/priority overrides from
// zelda3.user.ini; manifest_* keeps what mod.json declared.
typedef struct ModListInfo {
  char id[64];
  char name[96];
  int priority;
  bool enabled;
  bool loaded;
  int manifest_priority;
  bool manifest_enabled;
} ModListInfo;

// Loads every mod below directory (normally "mods"). Failures are reported on
// stderr and only disable the affected mod; this function never dies.
void Mods_LoadAll(const char *directory);

// Snapshot of the mods directory after Mods_LoadAll. The menu only reads it.
int Mods_ListCount(void);
const ModListInfo *Mods_ListGet(int index);

// Resolves a path from a mod manifest relative to mod_dir. Rejects empty
// paths, absolute paths (including drive letters and UNC paths) and any ".."
// component. On success *out is an allocated path that the caller frees.
// Also used by future mod features that need to open files from a mod.
bool ModResolvePath(const char *mod_dir, const char *rel, char **out,
                    char *err, size_t err_size);

// Shared ZSPR header validation used by both the config file loader and mods.
bool Mods_ParseZspr(const uint8 *file, size_t length, ZsprData *out);

#endif  // ZELDA3_MOD_MODS_H_
