#ifndef ZELDA3_MOD_EVENTS_H_
#define ZELDA3_MOD_EVENTS_H_

#include "../types.h"

typedef enum ModEventType {
  kModEv_FrameStart,
  kModEv_FrameEnd,
  kModEv_ModuleChange,      // a = new module, b = old module
  kModEv_RoomEnter,         // a = dungeon_room_index, b = previous room
  kModEv_AreaEnter,         // a = overworld_area_index/screen, b = previous
  kModEv_WorldChange,       // a = 1 if Dark World
  kModEv_ItemReceive,       // a = item id, b = chest_position, c = item_receipt_method; FILTER
  kModEv_ChestOpen,         // a = dungeon_room_index, b = chest_position
  kModEv_DialogueOpen,      // a = dialogue_message_index
  kModEv_DialogueClose,     // a = dialogue_message_index
  kModEv_LinkDamage,        // a = damage, b = health before
  kModEv_LinkDeath,
  kModEv_SpriteSpawn,       // a = slot, b = type
  kModEv_SpriteKilled,      // a = slot, b = type
  kModEv_BossDefeated,      // a = cur_palace_index_x2
  kModEv_SramSave,
  kModEv_SramLoad,
  kModEv_FrameDraw,         // after the PPU frame; scripts may call zelda.draw
  kModEv_FileLoad,          // a = 0-based save slot; Module05_LoadFile (828136)
  kModEv_FileCreate,        // a = 0-based save slot; new file written (8cdb3e)
  kModEv_DungeonEnter,      // a = cur_palace_index_x2, b = which_entrance (82d8b3)
  kModEv_DungeonExit,       // a = palace being left; LoadOverworldFromDungeon (82e4a3)
  kModEv_Quit,              // a = 1 if the game saved; Death_Func15 quit branch (89f50f)
  kModEv_OverworldTransition, // a = new area, b = direction bits (82a9c4)
  kModEv_DropSpawned,       // a = item type, b = sprite slot (86f9d1, 868264)
  kModEv_CameraSplit,       // a = 1 if split, 0 if merged; b/c = |dx|/|dy|
  kModEv_PlayerDeath,       // a = player (1 or 2)
  kModEv_PlayerRespawn,     // a = player (1 or 2), b = health after respawn
  kModEv_Player2SpriteDamage, // a = slot, b = type, c = damage applied
  kModEv_SeedItem,          // a = item id, b = target dungeon (seed mod only)
  kModEv_GtOpen,            // a = crystal count, b = target, c = mode
  kModEv_SeedSetting,       // a = setting id, b = value (seed mod only)
  kModEv_GanonCheck,        // a = blocked (1/0), b = crystal count, c = mode
  kModEv_DialogueText,      // a = message index, b = FNV-1a of the string, c = size
  kModEv_Count
} ModEventType;

typedef struct ModEvent {
  ModEventType type;
  int a, b, c;
  int *filter;   // non-NULL only in FILTER events: handlers may replace the value
} ModEvent;

typedef void ModEventHandler(const ModEvent *ev, void *user);

int  ModEvents_Subscribe(ModEventType type, ModEventHandler *h, void *user);
void ModEvents_Unsubscribe(int id);
void ModEvents_Emit(ModEventType type, int a, int b, int c, int *filter);

// Name used by scripts and traces (for example "RoomEnter"). Returns NULL for
// an invalid type. ModEvent_TypeFromName returns -1 for an unknown name.
const char *ModEvent_Name(ModEventType type);
int ModEvent_TypeFromName(const char *name);

// Test-only subscriber used by --mod-trace. It prints every event and never
// changes game state.
void ModEvents_EnableTrace(void);

// Frame counter used by the --mod-trace output. It only advances while the
// trace subscriber is active, so it stays zero in normal runs.
int64 ModEvents_TraceFrame(void);

extern uint64 g_mod_event_mask;  // bit (1ull << type) is set when there is a listener

#define MOD_EMIT(t, a, b, c) \
  do { if (g_mod_event_mask & (1ull << (t))) ModEvents_Emit((t), (a), (b), (c), NULL); } while (0)

#endif  // ZELDA3_MOD_EVENTS_H_
