#ifndef ZELDA3_MOD_SHARED_WORLD_H_
#define ZELDA3_MOD_SHARED_WORLD_H_

#include "../types.h"

// Model B first slice: one world, two Links.
//
// Player 1 runs through the normal game loop. Player 2 is a second copy of
// the Link-owned RAM regions; after player 1's step the regions are swapped
// into g_ram, Link_Main()/Ancilla_Main()/LinkOam_Main() run for player 2 with
// its own controller, and the result is copied back before NMI. With
// [Coop] SharedWorld = 0 every function here is inert.
bool SharedWorld_IsEnabled(void);
bool SharedWorld_IsActive(void);
void SharedWorld_Start(void);
void SharedWorld_Stop(void);

// Stored before ZeldaRunFrame() by both the interactive and headless loops.
void SharedWorld_SetInput2(uint16 input);
uint16 SharedWorld_GetInput2(void);

// Called by ZeldaRunFrameInternal() before the world step: when both Links
// fit in one screen, the shared camera is moved to their midpoint (clamped to
// the room/area scroll limits). While split, player 1 keeps the vanilla camera.
void SharedWorld_PreFrame(void);

// Called by ZeldaRunFrameInternal() after the one world step and before NMI.
void SharedWorld_FrameStep(void);

// Add player 2's hearts and magic to the HUD tilemap. Called after the world
// step and before NMI; inert unless the shared world is in normal gameplay.
void SharedWorld_UpdateHud(void);

// Mark player 2's copy stale after a save/replay/reset. It is re-seeded from
// player 1 on the next normal gameplay frame.
void SharedWorld_ResetFromCurrent(void);

// Automatic split-screen state. When split, the output is two panes of
// pane_width pixels (the same world state rendered twice with different
// scrolls); when merged it is a single normal view.
bool SharedWorld_IsSplit(void);
int SharedWorld_OutputWidth(int pane_width);

// Render the one shared camera (merged) or both panes (split). pane_width is
// in pixels and includes the render scale. buffer is contiguous for both
// panes: pane 1 starts at pane_width * 4 bytes.
void SharedWorld_DrawFrame(uint8 *buffer, size_t pitch, uint32 render_flags,
                           int pane_width);

// Nearest-Link helpers for sprite AI and damage checks. "Other" is the Link
// that is not currently swapped into g_ram. When the shared world is off the
// other Link is never selected and the live Link keeps its vanilla behavior.
// A dead Link is never selected; if only the other Link is alive it becomes
// the target even when the dead one is closer.
bool SharedWorld_OtherLinkIsTarget(uint16 from_x, uint16 from_y);
uint16 SharedWorld_OtherLinkX(void);
uint16 SharedWorld_OtherLinkY(void);

// Coordinates of the Link that a sprite at (from_x, from_y) should aim at.
// With SharedWorld off this is exactly link_x_coord/link_y_coord, so AI code
// can use it unconditionally.
void SharedWorld_LinkPosFor(uint16 from_x, uint16 from_y, uint16 *x,
                            uint16 *y);

// --dump-state support: swap one player into g_ram and back.
void SharedWorld_DumpSelect(int player);
void SharedWorld_DumpDone(void);

#endif  // ZELDA3_MOD_SHARED_WORLD_H_
