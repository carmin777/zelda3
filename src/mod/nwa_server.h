#ifndef ZELDA3_MOD_NWA_SERVER_H_
#define ZELDA3_MOD_NWA_SERVER_H_

#include <stdbool.h>
#include <stdint.h>

// EmuNWA (Emulator Network Access) server used by the SNI bridge, EmoTracker,
// PopTracker and the Archipelago client. When disabled, Start/Poll/Stop do not
// create or touch any socket.
void NwaServer_Start(bool enabled, uint16_t port, bool allow_write,
                     const char *rom_path);
void NwaServer_Stop(void);

// Point the server at the SNES instance used by --emu-rom.  While set, WRAM,
// SRAM, CARTROM, VRAM, OAM and CGRAM come from that cartridge instead of the
// native game state.  Pass NULL to return to the native domains.
struct Snes;
void NwaServer_SetEmulatorSnes(struct Snes *snes);

// Services connections. Call once per frame from the main thread, after the
// current frame has finished and before the next one starts.
void NwaServer_Poll(void);

// True while a client asked to pause emulation. The main loops use this to
// stop running frames but keep servicing the server.
bool NwaServer_IsPaused(void);
bool NwaServer_IsRunning(void);

// Set when a bCORE_WRITE changes guest memory. The side-by-side comparison
// against the reference ROM must be disabled from then on.
extern bool g_nwa_changed_content;

#endif  // ZELDA3_MOD_NWA_SERVER_H_
