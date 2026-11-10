#include "sprite_override.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../variables.h"
#include "events.h"
#include "mods.h"
#include "sprite_names.h"

enum { kMaxSpriteSubscribers = 8 };

typedef struct SpriteOverrideSubscriber {
  ModSpriteTickHandler *handler;
  void *user;
  int priority;
} SpriteOverrideSubscriber;

typedef struct SpriteOverrideCall {
  int type;
  int slot;
  ModSpriteOriginalFunc *original;
  bool active;
  struct SpriteOverrideCall *prev;
} SpriteOverrideCall;

static SpriteOverrideSubscriber g_subs[256][kMaxSpriteSubscribers];
static bool g_trace;
static uint16 g_trace_seen[256];
static SpriteOverrideCall *g_current_call;

uint8 g_mod_sprite_override_mask[256];

static void TraceSample(int type, int slot) {
  if (!g_trace)
    return;
  uint16 bit = (uint16)(1u << slot);
  long long frame = ModEvents_TraceFrame();
  bool first = (g_trace_seen[type] & bit) == 0;
  if (!first && (frame & 31) != 0)
    return;
  g_trace_seen[type] |= bit;
  const char *name = ModSpriteOverride_TypeName(type);
  printf("override %lld Sprite %s %d x %d y %d\n", frame,
         name != NULL ? name : "?", slot,
         sprite_x_lo[slot] | sprite_x_hi[slot] << 8,
         sprite_y_lo[slot] | sprite_y_hi[slot] << 8);
}

bool ModSpriteOverride_Subscribe(int type, int priority,
                                 ModSpriteTickHandler *handler, void *user) {
  if (type < 0 || type >= kModSpriteTypeCount || handler == NULL)
    return false;
  int pos = 0;
  while (pos < kMaxSpriteSubscribers &&
         g_subs[type][pos].handler != NULL &&
         g_subs[type][pos].priority <= priority)
    pos++;
  if (pos == kMaxSpriteSubscribers)
    return false;
  for (int i = kMaxSpriteSubscribers - 1; i > pos; i--)
    g_subs[type][i] = g_subs[type][i - 1];
  g_subs[type][pos].handler = handler;
  g_subs[type][pos].user = user;
  g_subs[type][pos].priority = priority;
  g_mod_sprite_override_mask[type] = 1;
  g_trace_seen[type] = 0;
  // Replacing a sprite routine changes what the game decides, so the
  // side-by-side comparison with the original ROM no longer applies.
  g_mods_change_content = true;
  return true;
}

void ModSpriteOverride_UnsubscribeUser(void *user) {
  if (user == NULL)
    return;
  for (int type = 0; type < kModSpriteTypeCount; type++) {
    bool any = false;
    for (int i = 0; i < kMaxSpriteSubscribers; i++) {
      if (g_subs[type][i].handler != NULL && g_subs[type][i].user == user) {
        for (int j = i; j < kMaxSpriteSubscribers - 1; j++)
          g_subs[type][j] = g_subs[type][j + 1];
        g_subs[type][kMaxSpriteSubscribers - 1].handler = NULL;
        g_subs[type][kMaxSpriteSubscribers - 1].user = NULL;
        g_subs[type][kMaxSpriteSubscribers - 1].priority = 0;
        i--;
      } else if (g_subs[type][i].handler != NULL) {
        any = true;
      }
    }
    if (!any) {
      g_mod_sprite_override_mask[type] = 0;
      g_trace_seen[type] = 0;
    }
  }
}

void ModSpriteOverride_Run(int type, int slot, ModSpriteOriginalFunc *original) {
  SpriteOverrideSubscriber *sub = NULL;
  if (type >= 0 && type < kModSpriteTypeCount) {
    for (int i = kMaxSpriteSubscribers - 1; i >= 0; i--) {
      if (g_subs[type][i].handler != NULL) {
        sub = &g_subs[type][i];
        break;
      }
    }
  }

  if (sub == NULL) {
    original(slot);
    return;
  }

  TraceSample(type, slot);

  SpriteOverrideCall call;
  call.type = type;
  call.slot = slot;
  call.original = original;
  call.active = true;
  call.prev = g_current_call;
  g_current_call = &call;

  bool handled = sub->handler(type, slot, sub->user);

  call.active = false;
  g_current_call = call.prev;

  // On error the Lua handler is already disabled. Fall back to the game
  // routine in the same frame, exactly as if there were no override.
  if (!handled)
    original(slot);
}

void *ModSpriteOverride_CurrentCall(void) {
  return g_current_call;
}

const char *ModSpriteOverride_CallOriginal(void *token, int slot) {
  SpriteOverrideCall *call = token;
  if (call == NULL || !call->active)
    return "original() is only valid inside the override tick";
  if (slot < 0 || slot >= 16)
    return "sprite slot outside 0..15";
  if (slot != call->slot)
    return "original() must use the slot passed to the override tick";
  call->original(slot);
  return NULL;
}

const char *ModSpriteOverride_TypeName(int type) {
  if (type < 0 || type >= kModSpriteTypeCount)
    return NULL;
  return kModSpriteNames[type];
}

static bool NameHasSuffix(const char *name, const char *suffix) {
  size_t name_len = strlen(name);
  size_t suffix_len = strlen(suffix);
  return name_len > suffix_len && name[name_len - suffix_len - 1] == '-' &&
         strcmp(name + name_len - suffix_len, suffix) == 0;
}

bool ModSpriteOverride_ParseType(const char *text, int *type) {
  if (text == NULL || type == NULL)
    return false;
  while (isspace((unsigned char)*text))
    text++;
  const char *end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1]))
    end--;
  if (end == text)
    return false;

  // A full table name wins over a hex number, so "00-Raven" is not parsed as
  // the number 0 followed by garbage.
  size_t len = (size_t)(end - text);
  for (int i = 0; i < kModSpriteTypeCount; i++) {
    if (strlen(kModSpriteNames[i]) == len &&
        strncmp(kModSpriteNames[i], text, len) == 0) {
      *type = i;
      return true;
    }
  }

  // Unique suffix ("Knight" for "4B-Knight") is accepted when unambiguous.
  int suffix_match = -1;
  for (int i = 0; i < kModSpriteTypeCount; i++) {
    if (!NameHasSuffix(kModSpriteNames[i], text))
      continue;
    if (suffix_match >= 0) {
      suffix_match = -1;
      break;
    }
    suffix_match = i;
  }
  if (suffix_match >= 0) {
    *type = suffix_match;
    return true;
  }

  const char *number = text;
  if (number < end && (number[0] == '$' ||
                       (number + 1 < end && number[0] == '0' &&
                        (number[1] == 'x' || number[1] == 'X'))))
    number += number[0] == '$' ? 1 : 2;
  if (number == end)
    return false;
  char *parsed_end;
  char buffer[32];
  if ((size_t)(end - number) >= sizeof(buffer))
    return false;
  memcpy(buffer, number, (size_t)(end - number));
  buffer[end - number] = 0;
  unsigned long value = strtoul(buffer, &parsed_end, 16);
  if (*parsed_end != 0 || value >= kModSpriteTypeCount)
    return false;
  *type = (int)value;
  return true;
}

void ModSpriteOverride_EnableTrace(void) {
  g_trace = true;
}
