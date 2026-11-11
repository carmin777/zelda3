#ifndef ZELDA3_MOD_INVERTED_H_
#define ZELDA3_MOD_INVERTED_H_

#include "../types.h"

// Inverted world state from the z3randomizer ("InvertedMode", $B0:8000 byte
// at PC 0x18004A). It only takes effect when a seed mod enabled Z3rRules and
// its z3r_settings has "inverted": 1. Without that combination every helper
// returns the vanilla result, so the original game is unchanged.

// True when a seed mod with Z3rRules requests the inverted world state.
bool ModInverted_Active(void);

// Whether Link needs the Moon Pearl in the world identified by a $40 world
// bit. Normal: the Dark World needs it. Inverted: the Light World needs it.
// The argument can be savegame_is_darkworld or an overworld screen index.
bool ModInverted_PearlRequired(uint8 world);

// Called after a new save is initialized. With a save template from an
// imported seed the start fields are left alone apart from the world bit; a
// settings-only seed (no template) also gets the open-style inverted start.
void ModInverted_OnNewSave(uint8 *sram, size_t size, int slot,
                           bool seed_template);

// Called at the start of the file load module. Returns true when this load
// belongs to the inverted new file. The flag is consumed here so a different
// slot keeps the normal path.
bool ModInverted_BeginNewGameLoad(int slot);

// StartingEntrance row selected by the seed and whether it uses the z3r
// StartingAreaExitTable instead of the normal starting-point position.
bool ModInverted_UsesStartingAreaExit(int point);

// Applies the z3r StartingAreaExitTable row for the current StartingEntrance
// to the cached entrance/exit variables (AllowStartFromSingleEntranceCave at
// $82:D9B9).
void ModInverted_ApplyStartingAreaExit(void);

// Room to load for the inverted new game instead of the normal dark-world
// respawn (the pyramid). Consumed by the caller.
uint16 ModInverted_TakeNewGameRoom(uint16 fallback);

// Applies the imported inverted overworld tile patch for a screen.
void ModInverted_ApplyOverworldPatches(uint16 screen);

// z3r MirrorBonk ($87:A9AC): force a mirror bonk inside the seed's
// rectangles while in the Dark World.
bool ModInverted_MirrorBonk(uint16 x, uint16 y);

// Fixed inverted exits. The house door is on the Dark World screen in
// inverted mode; the normal table still points at the Light World screen.
uint8 ModInverted_ExitScreen(uint16 room, uint8 screen);

#endif  // ZELDA3_MOD_INVERTED_H_
