#pragma once
#include <stdint.h>

// Apply the US PatchRom byte changes to the JP seed ROM by matching the US
// instruction bytes plus surrounding context. Returns the number applied.
int SeedRom_ApplyUSPatches(uint8_t *rom, uint32_t size, int verbose);
int SeedRom_ApplyMap16Stripes(uint8_t *rom, int verbose);
