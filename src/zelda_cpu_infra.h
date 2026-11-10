#ifndef ZELDA3_ZELDA_CPU_INFRA_H_
#define ZELDA3_ZELDA_CPU_INFRA_H_
#include "types.h"

struct Snes;
extern struct Snes *g_snes;

extern uint8 g_emulated_ram[0x20000];

uint8 *GetPtr(uint32 addr);

void RunEmulatedFuncSilent(uint32 pc, uint16 a, uint16 x, uint16 y, bool mf, bool xf, int b, int whatflags);
void RunEmulatedFunc(uint32 pc, uint16 a, uint16 x, uint16 y, bool mf, bool xf, int b, int whatflags);

bool EmuInitialize(uint8 *data, size_t size);
// Oracle mode for a complete ALttPR seed ROM (JP 1.0 + z3randomizer + seed
// data). Only the patches that still match the seed ROM are applied; the
// native C game is told that it is running in seed mode.
bool EmuInitializeSeed(uint8 *data, size_t size);
bool EmuSeedRomActive(void);
// Raw, already patched seed ROM bytes (PC offsets). NULL when the oracle is
// not in seed mode. Used only by the seed-mode compatibility code.
const uint8 *EmuSeedRomData(void);
// Run a JP 1.0 helper from the seed ROM against the native C RAM. The
// emulator state is saved/restored around the call by RunEmulatedFuncSilent.
void EmuRunSeedRoutine(uint32 pc);
// ALTTPR ROM version stored by z3randomizer in the ROM header ($7FE0) and
// copied to SRAM $701FFC at boot.
uint32 EmuSeedRomVersion(void);
// Historical exploration helper for the seed oracle. It is kept for
// command-line compatibility; the JP 1.0 divergences found so far are
// implemented in C, so this masks nothing today. Never enabled by default.
void EmuSetSeedIgnoreKnownJp(bool enabled);

#endif  // ZELDA3_ZELDA_CPU_INFRA_H_
