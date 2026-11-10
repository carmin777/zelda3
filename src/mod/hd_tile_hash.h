#ifndef ZELDA3_MOD_HD_TILE_HASH_H_
#define ZELDA3_MOD_HD_TILE_HASH_H_

// Tile content hash shared by the HD pack plugin and the --frame-desc-dump
// tile-sheet exporter. The hash covers the bytes the PPU would read for the
// tile, in SNES order (low byte first), so it does not depend on the host
// endianness. A 4bpp tile is 16 words (32 bytes); a 2bpp tile is 8 words
// (16 bytes).

#include <stdint.h>
#include <stddef.h>

static inline uint32_t HdTileHashWords(const uint16_t *words, int word_count) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < word_count; i++) {
    hash = (hash ^ (uint8_t)(words[i] & 0xff)) * 16777619u;
    hash = (hash ^ (uint8_t)(words[i] >> 8)) * 16777619u;
  }
  return hash;
}

#endif  // ZELDA3_MOD_HD_TILE_HASH_H_
