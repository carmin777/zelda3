#ifndef ZELDA3_MOD_SPRITE_OVERRIDE_H_
#define ZELDA3_MOD_SPRITE_OVERRIDE_H_

#include "../types.h"

// One tick handler replaces the game routine for one sprite type. Returning
// false means the handler failed (the caller then runs the original routine).
typedef bool ModSpriteTickHandler(int type, int slot, void *user);

// The game routine for the type, called by SpriteActive_Main. Kept as a
// separate typedef so this module does not depend on zelda_rtl.h.
typedef void ModSpriteOriginalFunc(int slot);

// Bit n is 1 when at least one mod overrides sprite type n. SpriteActive_Main
// checks this array before doing anything else, so no mod means no extra call.
extern uint8 g_mod_sprite_override_mask[256];

// Registers a replacement for type (0..242). Lower priorities run first, but
// only the last subscriber (highest priority, then load order) is active; the
// previous one becomes active again if the mod is disabled. Returns false when
// the type is invalid or the per-type subscriber list is full.
bool ModSpriteOverride_Subscribe(int type, int priority,
                                 ModSpriteTickHandler *handler, void *user);
void ModSpriteOverride_UnsubscribeUser(void *user);

// Runs the active override for type, or the original routine when the handler
// fails. Called only when g_mod_sprite_override_mask[type] is set.
void ModSpriteOverride_Run(int type, int slot, ModSpriteOriginalFunc *original);

// Parses a type from "0x4B", "$4B", "4B", "4B-Knight" or a unique suffix such
// as "Knight". Returns false for an unknown type.
bool ModSpriteOverride_ParseType(const char *text, int *type);
const char *ModSpriteOverride_TypeName(int type);

// Context for original() called from Lua. The token belongs to one tick call
// and stops being valid when the tick returns.
void *ModSpriteOverride_CurrentCall(void);
const char *ModSpriteOverride_CallOriginal(void *call, int slot);

// --mod-trace: print the first call for each slot and samples every 32 frames.
void ModSpriteOverride_EnableTrace(void);

#endif  // ZELDA3_MOD_SPRITE_OVERRIDE_H_
