#ifndef ZELDA3_MOD_LOCAL_COOP_H_
#define ZELDA3_MOD_LOCAL_COOP_H_

#include "../types.h"

// Local split-screen co-op: two complete native game states in one process.
// When [Coop] Local = 0 every function here is inert and the game keeps using
// its single global state exactly as before.
bool LocalCoop_IsEnabled(void);
bool LocalCoop_IsActive(void);
void LocalCoop_Start(void);
void LocalCoop_Stop(void);

// Swaps the RAM image, the PPU/DMA/APU objects, the state recorder and the
// co-op context to the selected instance. RAM is copied; the rest is pointers.
void LocalCoop_Select(int index);

// Combined output size: twice the pane width (horizontal) or height
// (vertical). Both functions return the pane size when local co-op is off.
int LocalCoop_OutputWidth(int pane_width);
int LocalCoop_OutputHeight(int pane_height);
bool LocalCoop_IsVertical(void);

#endif  // ZELDA3_MOD_LOCAL_COOP_H_
