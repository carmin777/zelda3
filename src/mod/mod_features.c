#include "mod_features.h"

#include <stdio.h>
#include <string.h>

#include "../config.h"
#include "../features.h"
#include "../util.h"

// Same numbers as GetIniSection() in src/config.c.
enum {
  kIniSection_Graphics = 1,
  kIniSection_Features = 4,
};

typedef struct ModFeatureDef {
  const char *name;
  uint32 mask;
  uint8 ini_section;
} ModFeatureDef;

// Single source for the names accepted by both zelda3.ini and mod.json.
// Video/audio/keys/paths are deliberately absent: they are not gameplay or
// bug-fix bits of features0. ExtendScreen64/WidescreenVisualFixes are set
// indirectly by ExtendedAspectRatio, which is also not a feature here.
static const ModFeatureDef kModFeatureDefs[] = {
  {"ItemSwitchLR", kFeatures0_SwitchLR, kIniSection_Features},
  {"ItemSwitchLRLimit", kFeatures0_SwitchLRLimit, kIniSection_Features},
  {"TurnWhileDashing", kFeatures0_TurnWhileDashing, kIniSection_Features},
  {"MirrorToDarkworld", kFeatures0_MirrorToDarkworld, kIniSection_Features},
  {"CollectItemsWithSword", kFeatures0_CollectItemsWithSword, kIniSection_Features},
  {"BreakPotsWithSword", kFeatures0_BreakPotsWithSword, kIniSection_Features},
  {"DisableLowHealthBeep", kFeatures0_DisableLowHealthBeep, kIniSection_Features},
  {"SkipIntroOnKeypress", kFeatures0_SkipIntroOnKeypress, kIniSection_Features},
  {"ShowMaxItemsInYellow", kFeatures0_ShowMaxItemsInYellow, kIniSection_Features},
  {"MoreActiveBombs", kFeatures0_MoreActiveBombs, kIniSection_Features},
  {"CarryMoreRupees", kFeatures0_CarryMoreRupees, kIniSection_Features},
  {"MiscBugFixes", kFeatures0_MiscBugFixes, kIniSection_Features},
  {"GameChangingBugFixes", kFeatures0_GameChangingBugFixes, kIniSection_Features},
  {"CancelBirdTravel", kFeatures0_CancelBirdTravel, kIniSection_Features},
  {"NewStyleInventory", kFeatures0_NewStyleInventory, kIniSection_Features},
  {"HorizontalMagicBar", kFeatures0_HorizontalMagicBar, kIniSection_Features},
  {"MenuTimer", kFeatures0_MenuTimer, kIniSection_Features},
  {"ReduxHud", kFeatures0_ReduxHud, kIniSection_Features},
  {"Z3rRules", kFeatures0_Z3rRules, kIniSection_Features},
  {"DimFlashes", kFeatures0_DimFlashes, kIniSection_Graphics},
};

static uint32 g_override_mask;
static uint32 g_override_value;

// Borrowed pointers into the mod manifests, only used while the loader is
// running. ModFeatures_EndLoad() clears them before the manifests are freed.
static const char *g_owner[32];
static int g_owner_priority[32];

bool ModFeatures_Find(const char *name, uint32 *mask) {
  for (int i = 0; i < countof(kModFeatureDefs); i++) {
    if (StringEqualsNoCase(name, kModFeatureDefs[i].name)) {
      if (mask)
        *mask = kModFeatureDefs[i].mask;
      return true;
    }
  }
  return false;
}

bool ModFeatures_ParseIni(int section, const char *key, const char *value,
                          uint32 *features0) {
  for (int i = 0; i < countof(kModFeatureDefs); i++) {
    const ModFeatureDef *feature = &kModFeatureDefs[i];
    if (section != feature->ini_section ||
        !StringEqualsNoCase(key, feature->name))
      continue;
    bool enabled;
    if (!ParseBool(value, &enabled))
      return false;
    *features0 = (*features0 & ~feature->mask) | (enabled ? feature->mask : 0);
    return true;
  }
  return false;
}

void ModFeatures_FormatNames(char *out, size_t out_size) {
  if (out_size == 0)
    return;
  size_t pos = 0;
  out[0] = 0;
  for (int i = 0; i < countof(kModFeatureDefs); i++) {
    int n = snprintf(out + pos, out_size - pos, "%s%s",
                     i == 0 ? "" : ", ", kModFeatureDefs[i].name);
    if (n < 0 || (size_t)n >= out_size - pos)
      break;
    pos += (size_t)n;
  }
}

static int BitIndex(uint32 mask) {
  int bit = 0;
  while ((mask & 1u) == 0) {
    mask >>= 1;
    bit++;
  }
  return bit;
}

void ModFeatures_Apply(const char *mod_id, int priority, uint32 mask,
                       uint32 value) {
  for (int i = 0; i < countof(kModFeatureDefs); i++) {
    const ModFeatureDef *feature = &kModFeatureDefs[i];
    if ((mask & feature->mask) == 0)
      continue;
    bool old_value = (g_config.features0 & feature->mask) != 0;
    bool new_value = (value & feature->mask) != 0;
    int bit = BitIndex(feature->mask);
    if (g_owner[bit] != NULL && old_value != new_value) {
      fprintf(stderr,
              "mods: feature conflict '%s': '%s' (priority %d) overrides "
              "'%s' (priority %d): %s -> %s\n",
              feature->name, mod_id, priority, g_owner[bit],
              g_owner_priority[bit], old_value ? "on" : "off",
              new_value ? "on" : "off");
    }
    if (old_value != new_value) {
      g_config.features0 = (g_config.features0 & ~feature->mask) |
                           (new_value ? feature->mask : 0);
      fprintf(stderr, "mods: feature '%s' = %s ('%s', priority %d)\n",
              feature->name, new_value ? "on" : "off", mod_id, priority);
    }
    g_owner[bit] = mod_id;
    g_owner_priority[bit] = priority;
    g_override_mask |= feature->mask;
    g_override_value = (g_override_value & ~feature->mask) |
                       (new_value ? feature->mask : 0);
  }
}

uint32 ModFeatures_OverrideMask(void) {
  return g_override_mask;
}

uint32 ModFeatures_OverrideValue(void) {
  return g_override_value;
}

void ModFeatures_EndLoad(void) {
  memset(g_owner, 0, sizeof(g_owner));
  memset(g_owner_priority, 0, sizeof(g_owner_priority));
}
