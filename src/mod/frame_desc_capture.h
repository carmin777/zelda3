#ifndef ZELDA3_MOD_FRAME_DESC_CAPTURE_H_
#define ZELDA3_MOD_FRAME_DESC_CAPTURE_H_

// Engine-internal capture hooks. Plugins see only render_plugin.h and
// frame_desc.h; this header is the one place that mentions the live Ppu.

#include <stdint.h>
#include <stdbool.h>

#include "frame_desc.h"

struct Ppu;

// Called by ZeldaDrawPpuFrame. BeginCapture is called after the per-frame
// side-space setup and before the line loop; EndCapture after the loop.
void FrameDesc_BeginCapture(FrameDesc *desc, struct Ppu *ppu,
                            uint32 render_flags, int width, int height);
void FrameDesc_RecordRegister(FrameDesc *desc, int line, uint16_t reg,
                              uint8_t value, uint8_t source);
void FrameDesc_EndCapture(FrameDesc *desc);

#endif  // ZELDA3_MOD_FRAME_DESC_CAPTURE_H_
