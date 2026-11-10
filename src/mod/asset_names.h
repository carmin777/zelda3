#ifndef ZELDA3_MOD_ASSET_NAMES_H_
#define ZELDA3_MOD_ASSET_NAMES_H_

#include "../assets.h"

extern const char *const kAssetNames[kNumberOfAssets];

// Returns the g_asset_ptrs[] index for name, or -1 if it is not an asset.
int FindAssetByName(const char *name);

#endif  // ZELDA3_MOD_ASSET_NAMES_H_
