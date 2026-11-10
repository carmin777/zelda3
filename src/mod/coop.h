#ifndef ZELDA3_MOD_COOP_H_
#define ZELDA3_MOD_COOP_H_

#include <stddef.h>
#include <stdint.h>

#include "../types.h"

// Classic ALttPO-compatible co-op. Everything is inert while the [Coop]
// section has Enabled = 0: no socket is created, no memory is written and the
// overlay draws nothing.
void Coop_Start(void);
void Coop_Stop(void);
bool Coop_IsActive(void);
bool Coop_IsLocal(void);

// Local split-screen keeps one context per instance. Select it before running
// that instance's frame, sending and drawing. Network mode always uses 0.
void Coop_SelectInstance(int index);

// Exchanges packets with the server (or the in-memory peer) and applies remote
// progress. Must be called once per simulated frame, after ZeldaRunFrame().
void Coop_FrameStep(void);

// Draws remote players on top of the finished PPU frame. width and height are
// the logical SNES dimensions and scale is the PPU render scale. May queue
// text through ModDraw_AddText(), so call it between ModDraw_BeginFrame() and
// ModDraw_EndFrame().
void Coop_DrawOverlay(uint8 *buffer, size_t pitch, int width, int height,
                      int scale);

// Set when a remote progress update changed the local game state. The
// side-by-side comparison against the reference ROM must then be disabled,
// like it is for mods.
extern bool g_coop_changed_content;

#endif  // ZELDA3_MOD_COOP_H_
