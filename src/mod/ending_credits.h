#ifndef ZELDA3_MOD_ENDING_CREDITS_H_
#define ZELDA3_MOD_ENDING_CREDITS_H_

#include "../types.h"

// The ending credits text has two entry layouts:
//
//   map_index (US): scroll, count, then (count + 1) / 2 bytes, each an index
//                   into kEnding_MapData.
//   raw_words (PT): scroll, count, one high byte, then (count + 1) / 2 low
//                   bytes; each tilemap word is (high << 8) | low.
//
// Both layouts end a non-empty entry exactly where the next distinct offset
// starts. The detector counts exact fits for the two layouts; a byte-value
// heuristic is not reliable because translated low bytes can be below 160.
bool EndingCredits_UsesRawWords(const uint8 *text, size_t text_size,
                                const uint16 *offs, size_t offs_count);

// The epilogue scene text has two entry layouts:
//
//   map_index (US): 4-byte VRAM header, then one index byte per tilemap word.
//   raw_words (PT): 4-byte VRAM header, then one high byte followed by one
//                   low byte per tilemap word; each word is (high << 8) | low.
//
// Every entry is a sequence of those blocks and ends exactly where the next
// distinct offset starts. Count exact fits for both layouts instead of
// guessing from byte values.
bool EndingEpilogue_UsesRawWords(const uint8 *data, size_t data_size,
                                 const uint16 *offs, size_t offs_count);

#endif  // ZELDA3_MOD_ENDING_CREDITS_H_
