#include "hooks.h"

#include <stdio.h>
#include <string.h>

#include "../variables.h"
#include "events.h"
#include "mods.h"

enum { kMaxHookSubscribers = 16 };

typedef struct ModFilterSubscriber {
  ModFilterHandler *handler;
  void *user;
  int priority;
} ModFilterSubscriber;

typedef struct ModDecisionSubscriber {
  ModDecisionHandler *handler;
  void *user;
  int priority;
} ModDecisionSubscriber;

static ModFilterSubscriber g_filter_subs[kModFilter_Count][kMaxHookSubscribers];
static ModDecisionSubscriber g_decide_subs[kModDecide_Count][kMaxHookSubscribers];

uint32 g_mod_filter_mask;
uint32 g_mod_decide_mask;
uint8 g_mod_link_damage_source = kModLinkDamage_Sprite;

static bool g_hook_trace;

// Order matters for the index lookup below: all filters first, in enum order,
// then all decisions, also in enum order.
static const ModHookDef kModHooks[] = {
  { "SwordBeamDamage", kModHookKind_Filter, kModFilter_SwordBeamDamage, 0, 255 },
  { "FairyReviveHealth", kModHookKind_Filter, kModFilter_FairyReviveHealth, 0, 255 },
  { "MaxBombs", kModHookKind_Filter, kModFilter_MaxBombs, 0, 99 },
  { "MaxArrows", kModHookKind_Filter, kModFilter_MaxArrows, 0, 99 },
  { "ReceiveItemAlternate", kModHookKind_Filter, kModFilter_ReceiveItemAlternate, 0, 255 },
  { "BirdTravelDestination", kModHookKind_Filter, kModFilter_BirdTravelDestination, 0, 7 },
  { "OverworldTransitionTarget", kModHookKind_Filter, kModFilter_OverworldTransitionTarget, 0, 255 },
  { "LinkDamage", kModHookKind_Filter, kModFilter_LinkDamage, 0, 255 },
  { "PendantDropSpecialCase", kModHookKind_Decide, kModDecide_PendantDropSpecialCase, 0, 1 },
  { "SwordTurnWhileCharging", kModHookKind_Decide, kModDecide_SwordTurnWhileCharging, 0, 1 },
  { "BlockAlreadyPushed", kModHookKind_Decide, kModDecide_BlockAlreadyPushed, 0, 1 },
  { "PikitStealShield", kModHookKind_Decide, kModDecide_PikitStealShield, 0, 1 },
  { "MirrorAllowed", kModHookKind_Decide, kModDecide_MirrorAllowed, 0, 1 },
  { "SwordBeamSpawn", kModHookKind_Decide, kModDecide_SwordBeamSpawn, 0, 1 },
  { "PotBreakSwordLevel", kModHookKind_Decide, kModDecide_PotBreakSwordLevel, 0, 1 },
};

_Static_assert(arraysize(kModHooks) == kModFilter_Count + kModDecide_Count,
               "hook table must match the enum counts");

static const ModHookDef *HookDefForFilter(ModFilterType type) {
  if (type < 0 || type >= kModFilter_Count)
    return NULL;
  return &kModHooks[type];
}

static const ModHookDef *HookDefForDecision(ModDecisionType type) {
  if (type < 0 || type >= kModDecide_Count)
    return NULL;
  return &kModHooks[kModFilter_Count + type];
}

const ModHookDef *ModHooks_FilterDef(ModFilterType type) {
  return HookDefForFilter(type);
}

const ModHookDef *ModHooks_DecisionDef(ModDecisionType type) {
  return HookDefForDecision(type);
}

const ModHookDef *ModHooks_Find(const char *name) {
  if (name == NULL)
    return NULL;
  for (size_t i = 0; i < arraysize(kModHooks); i++) {
    if (strcmp(name, kModHooks[i].name) == 0)
      return &kModHooks[i];
  }
  return NULL;
}

static int InsertFilterSubscriber(ModFilterType type, int priority,
                                  ModFilterHandler *handler, void *user) {
  if (type < 0 || type >= kModFilter_Count || handler == NULL)
    return 0;
  int pos = 0;
  while (pos < kMaxHookSubscribers &&
         g_filter_subs[type][pos].handler != NULL &&
         g_filter_subs[type][pos].priority <= priority)
    pos++;
  if (pos == kMaxHookSubscribers)
    return 0;
  for (int i = kMaxHookSubscribers - 1; i > pos; i--)
    g_filter_subs[type][i] = g_filter_subs[type][i - 1];
  g_filter_subs[type][pos].handler = handler;
  g_filter_subs[type][pos].user = user;
  g_filter_subs[type][pos].priority = priority;
  g_mod_filter_mask |= 1u << type;
  return 1;
}

static int InsertDecisionSubscriber(ModDecisionType type, int priority,
                                    ModDecisionHandler *handler, void *user) {
  if (type < 0 || type >= kModDecide_Count || handler == NULL)
    return 0;
  int pos = 0;
  while (pos < kMaxHookSubscribers &&
         g_decide_subs[type][pos].handler != NULL &&
         g_decide_subs[type][pos].priority <= priority)
    pos++;
  if (pos == kMaxHookSubscribers)
    return 0;
  for (int i = kMaxHookSubscribers - 1; i > pos; i--)
    g_decide_subs[type][i] = g_decide_subs[type][i - 1];
  g_decide_subs[type][pos].handler = handler;
  g_decide_subs[type][pos].user = user;
  g_decide_subs[type][pos].priority = priority;
  g_mod_decide_mask |= 1u << type;
  return 1;
}

bool ModHooks_SubscribeFilter(ModFilterType type, int priority,
                              ModFilterHandler *handler, void *user) {
  if (!InsertFilterSubscriber(type, priority, handler, user))
    return false;
  // Any mod that can change a game decision breaks the byte-for-byte
  // comparison with the original ROM, even before the hook fires.
  g_mods_change_content = true;
  return true;
}

bool ModHooks_SubscribeDecision(ModDecisionType type, int priority,
                                ModDecisionHandler *handler, void *user) {
  if (!InsertDecisionSubscriber(type, priority, handler, user))
    return false;
  g_mods_change_content = true;
  return true;
}

void ModHooks_UnsubscribeUser(void *user) {
  if (user == NULL)
    return;
  for (int type = 0; type < kModFilter_Count; type++) {
    bool any = false;
    for (int i = 0; i < kMaxHookSubscribers; i++) {
      if (g_filter_subs[type][i].handler != NULL &&
          g_filter_subs[type][i].user == user) {
        for (int j = i; j < kMaxHookSubscribers - 1; j++)
          g_filter_subs[type][j] = g_filter_subs[type][j + 1];
        g_filter_subs[type][kMaxHookSubscribers - 1].handler = NULL;
        g_filter_subs[type][kMaxHookSubscribers - 1].user = NULL;
        g_filter_subs[type][kMaxHookSubscribers - 1].priority = 0;
        i--;
      } else if (g_filter_subs[type][i].handler != NULL) {
        any = true;
      }
    }
    if (!any)
      g_mod_filter_mask &= ~(1u << type);
  }

  for (int type = 0; type < kModDecide_Count; type++) {
    bool any = false;
    for (int i = 0; i < kMaxHookSubscribers; i++) {
      if (g_decide_subs[type][i].handler != NULL &&
          g_decide_subs[type][i].user == user) {
        for (int j = i; j < kMaxHookSubscribers - 1; j++)
          g_decide_subs[type][j] = g_decide_subs[type][j + 1];
        g_decide_subs[type][kMaxHookSubscribers - 1].handler = NULL;
        g_decide_subs[type][kMaxHookSubscribers - 1].user = NULL;
        g_decide_subs[type][kMaxHookSubscribers - 1].priority = 0;
        i--;
      } else if (g_decide_subs[type][i].handler != NULL) {
        any = true;
      }
    }
    if (!any)
      g_mod_decide_mask &= ~(1u << type);
  }
}

int ModHooks_Filter(ModFilterType type, int value, int arg) {
  if (type < 0 || type >= kModFilter_Count)
    return value;
  int original = value;
  ModFilterHandler *handlers[kMaxHookSubscribers];
  void *users[kMaxHookSubscribers];
  int count = 0;
  for (int i = 0; i < kMaxHookSubscribers; i++) {
    if (g_filter_subs[type][i].handler != NULL) {
      handlers[count] = g_filter_subs[type][i].handler;
      users[count] = g_filter_subs[type][i].user;
      count++;
    }
  }
  for (int i = 0; i < count; i++)
    value = handlers[i](type, value, arg, users[i]);

  if (g_hook_trace && value != original) {
    const ModHookDef *def = HookDefForFilter(type);
    printf("hook %lld Filter %s %d -> %d arg %d\n",
           (long long)ModEvents_TraceFrame(), def != NULL ? def->name : "?",
           original, value, arg);
  }
  return value;
}

bool ModHooks_Decide(ModDecisionType type, bool value, int arg) {
  if (type < 0 || type >= kModDecide_Count)
    return value;
  bool original = value;
  ModDecisionHandler *handlers[kMaxHookSubscribers];
  void *users[kMaxHookSubscribers];
  int count = 0;
  for (int i = 0; i < kMaxHookSubscribers; i++) {
    if (g_decide_subs[type][i].handler != NULL) {
      handlers[count] = g_decide_subs[type][i].handler;
      users[count] = g_decide_subs[type][i].user;
      count++;
    }
  }
  for (int i = 0; i < count; i++)
    value = handlers[i](type, value, arg, users[i]);

  if (g_hook_trace && value != original) {
    const ModHookDef *def = HookDefForDecision(type);
    printf("hook %lld Decide %s %d -> %d arg %d\n",
           (long long)ModEvents_TraceFrame(), def != NULL ? def->name : "?",
           original ? 1 : 0, value ? 1 : 0, arg);
  }
  return value;
}

uint8 ModHooks_MaxBombsForLevel(uint8 level) {
  return (uint8)MOD_FILTER(kModFilter_MaxBombs, kMaxBombsForLevel[level], level);
}

uint8 ModHooks_MaxArrowsForLevel(uint8 level) {
  return (uint8)MOD_FILTER(kModFilter_MaxArrows, kMaxArrowsForLevel[level], level);
}

void ModHooks_EnableTrace(void) {
  g_hook_trace = true;
}
