// This file declares extensions to the base game
#ifndef ZELDA3_FEATURES_H_
#define ZELDA3_FEATURES_H_

#include "types.h"

// Special RAM locations that are unused but I use for compat things.
enum {
  kRam_APUI00 = 0x648,
  kRam_CrystalRotateCounter = 0x649,
  kRam_BugsFixed = 0x64a,
  kRam_Features0 = 0x64c,
};

enum {
  // Poly rendered uses correct speed
  kBugFix_PolyRenderer = 1,
  kBugFix_AncillaOverwrites = 1,
  kBugFix_Latest = 1,
};

// Enum values for kRam_Features0
enum {
  kFeatures0_ExtendScreen64 = 1,
  kFeatures0_SwitchLR = 2,
  kFeatures0_TurnWhileDashing = 4,
  kFeatures0_MirrorToDarkworld = 8,
  kFeatures0_CollectItemsWithSword = 16,
  kFeatures0_BreakPotsWithSword = 32,
  kFeatures0_DisableLowHealthBeep = 64,
  kFeatures0_SkipIntroOnKeypress = 128,
  kFeatures0_ShowMaxItemsInYellow = 256,
  kFeatures0_MoreActiveBombs = 512,

  // This is set for visual fixes that don't affect game behavior but will affect ram compare.
  kFeatures0_WidescreenVisualFixes = 1024,

  kFeatures0_CarryMoreRupees = 2048,

  kFeatures0_MiscBugFixes = 4096,

  kFeatures0_CancelBirdTravel = 8192,

  kFeatures0_GameChangingBugFixes = 16384,

  kFeatures0_SwitchLRLimit = 32768,

  kFeatures0_DimFlashes = 65536,

  // 24-slot inventory/menu layout (six columns by four rows, separate
  // bottles and a permanent shovel slot). Changes only HUD state.
  kFeatures0_NewStyleInventory = 131072,

  // Horizontal magic meter and the in-menu play timer introduced by Redux.
  kFeatures0_HorizontalMagicBar = 262144,
  kFeatures0_MenuTimer = 524288,

  // Redux in-game HUD layout: hearts at the top-left, the horizontal magic
  // bar below them, the counters shifted right and the item box at the
  // top-right. Independent from the 24-slot menu.
  kFeatures0_ReduxHud = 1048576,

  // Behavior rules that every ALttPR seed expects from the z3randomizer:
  // fake flippers, bonk recoil, jump-down-ledge and the stat counters.
  kFeatures0_Z3rRules = 2097152,
};

// The named macros for these feature RAM locations live in variables.h.



extern uint32 g_wanted_zelda_features;


#endif  // ZELDA3_FEATURES_H_
