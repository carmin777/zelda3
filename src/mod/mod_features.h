#ifndef ZELDA3_MOD_MOD_FEATURES_H_
#define ZELDA3_MOD_MOD_FEATURES_H_

#include "../types.h"

// Looks up a name from [Features]/[Graphics] and returns its kFeatures0_* mask.
bool ModFeatures_Find(const char *name, uint32 *mask);

// Handles a key from the ini parser. Only the section where the key is
// accepted matches, so "DimFlashes" keeps working only under [Graphics].
bool ModFeatures_ParseIni(int section, const char *key, const char *value,
                          uint32 *features0);

// Writes the comma-separated list of accepted names (for error messages).
void ModFeatures_FormatNames(char *out, size_t out_size);

// Applies the feature values of one successfully loaded mod on top of the
// current config. Ownership and conflicts are logged to stderr. mod_id and
// priority must stay valid until the mod loader has finished.
void ModFeatures_Apply(const char *mod_id, int priority, uint32 mask,
                       uint32 value);

// Overrides successfully applied by active mods. A zero mask means no mod
// changed any feature. Used to keep mod values above replay snapshots.
uint32 ModFeatures_OverrideMask(void);
uint32 ModFeatures_OverrideValue(void);

// Drops the borrowed mod names kept for conflict messages. Called when the
// loader finishes; the mask/value stay valid for the rest of the run.
void ModFeatures_EndLoad(void);

#endif  // ZELDA3_MOD_MOD_FEATURES_H_
