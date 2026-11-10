#ifndef ZELDA3_MOD_DRAW_H_
#define ZELDA3_MOD_DRAW_H_

#include <stddef.h>

#include "../types.h"

// Maximum number of zelda.draw.* calls accepted during one FrameDraw event.
enum { kModDrawMaxCommands = 4096 };

// Starts a frame where scripts may queue draw commands. width and height are
// the logical SNES dimensions (including the widescreen sides) and scale is
// the PPU render scale (1 normally, 4 for the 4x4 Mode 7 renderer).
void ModDraw_BeginFrame(uint8 *buffer, size_t pitch, int width, int height,
                        int scale);

// Applies the queued commands to the buffer and finishes the frame.
void ModDraw_EndFrame(void);

// True only between BeginFrame and EndFrame, while FrameDraw handlers run.
bool ModDraw_FrameActive(void);

// Logical screen size in SNES pixels.
int ModDraw_Width(void);
int ModDraw_Height(void);

// Each function returns NULL on success or a static error message. The queue
// is only accepted while ModDraw_FrameActive() is true.
const char *ModDraw_AddRect(int x, int y, int w, int h, uint32 color);
const char *ModDraw_AddText(int x, int y, const char *text, size_t length,
                            uint32 color);

#endif  // ZELDA3_MOD_DRAW_H_
