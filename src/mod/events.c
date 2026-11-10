#include "events.h"

#include <stdio.h>
#include <string.h>

enum { kMaxModEventSubscribers = 64 };

typedef struct ModEventSubscriber {
  ModEventHandler *handler;
  void *user;
  ModEventType type;
  int id;
} ModEventSubscriber;

static ModEventSubscriber g_subscribers[kMaxModEventSubscribers];
static int g_next_subscriber_id = 1;

uint32 g_mod_event_mask;

static void UpdateEventMask(ModEventType type) {
  for (int i = 0; i < kMaxModEventSubscribers; i++) {
    if (g_subscribers[i].handler && g_subscribers[i].type == type) {
      g_mod_event_mask |= 1u << type;
      return;
    }
  }
  g_mod_event_mask &= ~(1u << type);
}

int ModEvents_Subscribe(ModEventType type, ModEventHandler *h, void *user) {
  if (h == NULL || type < 0 || type >= kModEv_Count)
    return -1;
  for (int i = 0; i < kMaxModEventSubscribers; i++) {
    if (!g_subscribers[i].handler) {
      int id = g_next_subscriber_id++;
      if (g_next_subscriber_id <= 0)
        g_next_subscriber_id = 1;
      g_subscribers[i].handler = h;
      g_subscribers[i].user = user;
      g_subscribers[i].type = type;
      g_subscribers[i].id = id;
      g_mod_event_mask |= 1u << type;
      return id;
    }
  }
  return -1;
}

void ModEvents_Unsubscribe(int id) {
  if (id <= 0)
    return;
  for (int i = 0; i < kMaxModEventSubscribers; i++) {
    if (g_subscribers[i].handler && g_subscribers[i].id == id) {
      ModEventType type = g_subscribers[i].type;
      g_subscribers[i].handler = NULL;
      g_subscribers[i].user = NULL;
      UpdateEventMask(type);
      return;
    }
  }
}

void ModEvents_Emit(ModEventType type, int a, int b, int c, int *filter) {
  if (type < 0 || type >= kModEv_Count)
    return;
  ModEvent ev;
  ev.type = type;
  ev.a = a;
  ev.b = b;
  ev.c = c;
  ev.filter = filter;
  for (int i = 0; i < kMaxModEventSubscribers; i++) {
    ModEventHandler *handler = g_subscribers[i].handler;
    if (handler && g_subscribers[i].type == type) {
      handler(&ev, g_subscribers[i].user);
      // Keep later subscribers in sync when a filter handler replaced the
      // value. Without mods this is always false and changes nothing.
      if (filter != NULL && *filter != ev.a)
        ev.a = *filter;
    }
  }
}

static const char *const kModEventNames[kModEv_Count] = {
  "FrameStart",
  "FrameEnd",
  "ModuleChange",
  "RoomEnter",
  "AreaEnter",
  "WorldChange",
  "ItemReceive",
  "ChestOpen",
  "DialogueOpen",
  "DialogueClose",
  "LinkDamage",
  "LinkDeath",
  "SpriteSpawn",
  "SpriteKilled",
  "BossDefeated",
  "SramSave",
  "SramLoad",
  "FrameDraw",
  "FileLoad",
  "FileCreate",
  "DungeonEnter",
  "DungeonExit",
  "Quit",
  "OverworldTransition",
  "DropSpawned",
  "CameraSplit",
  "PlayerDeath",
  "PlayerRespawn",
  "Player2SpriteDamage",
};

const char *ModEvent_Name(ModEventType type) {
  if (type < 0 || type >= kModEv_Count)
    return NULL;
  return kModEventNames[type];
}

int ModEvent_TypeFromName(const char *name) {
  if (name == NULL)
    return -1;
  for (int type = 0; type < kModEv_Count; type++) {
    if (strcmp(name, kModEventNames[type]) == 0)
      return type;
  }
  return -1;
}

static int g_trace_frame;
static bool g_trace_enabled;

static void ModEventTraceHandler(const ModEvent *ev, void *user) {
  (void)user;
  if (ev->type == kModEv_FrameStart)
    g_trace_frame++;
  // Frame boundaries only advance the counter; printing them would bury the
  // interesting events (one pair per frame).
  if (ev->type == kModEv_FrameStart || ev->type == kModEv_FrameEnd)
    return;
  printf("ev %d %s %d %d %d\n", g_trace_frame, kModEventNames[ev->type],
         ev->a, ev->b, ev->c);
}

void ModEvents_EnableTrace(void) {
  if (g_trace_enabled)
    return;
  g_trace_enabled = true;
  for (int type = 0; type < kModEv_Count; type++)
    ModEvents_Subscribe((ModEventType)type, ModEventTraceHandler, NULL);
}

int64 ModEvents_TraceFrame(void) {
  return g_trace_frame;
}
