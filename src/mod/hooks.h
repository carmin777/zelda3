#ifndef ZELDA3_MOD_HOOKS_H_
#define ZELDA3_MOD_HOOKS_H_

#include "../types.h"

// Value filters. The game computes a value and mods may replace it before the
// game uses it. In the game code use MOD_FILTER(type, value, arg).
typedef enum ModFilterType {
  kModFilter_SwordBeamDamage,          // value = damage, arg = sprite slot
  kModFilter_FairyReviveHealth,        // value = link_hearts_filler, arg = health capacity
  kModFilter_MaxBombs,                 // value = max bombs, arg = upgrade level
  kModFilter_MaxArrows,                // value = max arrows, arg = upgrade level
  kModFilter_ReceiveItemAlternate,     // value = alternate item id, arg = chest item id
  kModFilter_BirdTravelDestination,    // value = destination 0..7, arg = reserved byte
  kModFilter_OverworldTransitionTarget,// value = destination screen, arg = direction
  kModFilter_LinkDamage,               // value = damage applied to health, arg = ModLinkDamageSource
  kModFilter_Count
} ModFilterType;

// Origin of the damage passed as the LinkDamage filter argument. The game
// records it next to every assignment to link_give_damage; pit damage passes
// kModLinkDamage_Pit directly.
typedef enum ModLinkDamageSource {
  kModLinkDamage_Sprite = 0,
  kModLinkDamage_Ancilla,
  kModLinkDamage_Pit,
  kModLinkDamage_Environment,
} ModLinkDamageSource;

// Yes/no decisions. The game asks and mods may answer. In the game code use
// MOD_DECIDE(type, default_value, arg).
typedef enum ModDecisionType {
  kModDecide_PendantDropSpecialCase,   // value = use palace 20 drop, arg = falling item index
  kModDecide_SwordTurnWhileCharging,   // value = allow turning, arg = current facing
  kModDecide_BlockAlreadyPushed,       // value = mark block pushed, arg = block index
  kModDecide_PikitStealShield,         // value = steal shield, arg = sprite slot
  kModDecide_MirrorAllowed,            // value = allow mirror, arg = current world (0 or 0x40)
  kModDecide_SwordBeamSpawn,           // value = create the beam, arg = current facing
  kModDecide_PotBreakSwordLevel,       // value = sword breaks the pot, arg = sword level
  kModDecide_Count
} ModDecisionType;

typedef enum ModHookKind {
  kModHookKind_Filter,
  kModHookKind_Decide,
} ModHookKind;

// One row per hook. This is the only name table: Lua, the trace and the docs
// all use these names. min/max is the safe range for a Lua filter result.
typedef struct ModHookDef {
  const char *name;
  uint8 kind;   // ModHookKind
  uint8 index;  // ModFilterType or ModDecisionType
  int min;
  int max;
} ModHookDef;

typedef int ModFilterHandler(ModFilterType type, int value, int arg, void *user);
typedef bool ModDecisionHandler(ModDecisionType type, bool value, int arg, void *user);

// Returns the definition for name, or NULL. Names are case-sensitive.
const ModHookDef *ModHooks_Find(const char *name);

// Definition for an already validated type, or NULL when type is out of range.
const ModHookDef *ModHooks_FilterDef(ModFilterType type);
const ModHookDef *ModHooks_DecisionDef(ModDecisionType type);

// Subscribers with lower priority run first; equal priorities keep the order in
// which they subscribed (the mod loader loads lower priority / lower id first).
// Subscribing a filter or decision sets g_mods_change_content.
bool ModHooks_SubscribeFilter(ModFilterType type, int priority,
                              ModFilterHandler *handler, void *user);
bool ModHooks_SubscribeDecision(ModDecisionType type, int priority,
                                ModDecisionHandler *handler, void *user);

// Removes every subscription owned by user. Used when a Lua script is disabled.
void ModHooks_UnsubscribeUser(void *user);

// Called by the game code when the mask says at least one mod subscribed.
int ModHooks_Filter(ModFilterType type, int value, int arg);
bool ModHooks_Decide(ModDecisionType type, bool value, int arg);

// Max bomb/arrow helpers. Every read of kMaxBombsForLevel/kMaxArrowsForLevel
// in the game must go through these so one filter covers all of them.
uint8 ModHooks_MaxBombsForLevel(uint8 level);
uint8 ModHooks_MaxArrowsForLevel(uint8 level);

// --mod-trace: print each call whose final result differs from the input.
void ModHooks_EnableTrace(void);

extern uint32 g_mod_filter_mask;  // bit (1u << type) is set when there is a listener
extern uint32 g_mod_decide_mask;  // bit (1u << type) is set when there is a listener

// Origin used by the next LinkDamage filter call in Link_ControlHandler. The
// damage sites write it before assigning link_give_damage. This is a C global,
// not game RAM, and it never changes what the game does without a subscriber.
extern uint8 g_mod_link_damage_source;

#define MOD_FILTER(t, v, a) \
  ((g_mod_filter_mask & (1u << (t))) ? ModHooks_Filter((t), (v), (a)) : (v))
#define MOD_DECIDE(t, d, a) \
  ((g_mod_decide_mask & (1u << (t))) ? ModHooks_Decide((t), (d), (a)) : (d))

#endif  // ZELDA3_MOD_HOOKS_H_
