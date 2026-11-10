#include "ending_credits.h"

bool EndingCredits_UsesRawWords(const uint8 *text, size_t text_size,
                                const uint16 *offs, size_t offs_count) {
  int map_fits = 0;
  int raw_fits = 0;
  for (size_t i = 0; i < offs_count; i++) {
    uint32 offset = offs[i];
    if (offset + 2 > text_size || text[offset] == 0xff)
      continue;
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) {
      if (offs[j] == offs[i]) {
        duplicate = true;
        break;
      }
    }
    if (duplicate)
      continue;
    uint32 next = (uint32)text_size;
    for (size_t j = 0; j < offs_count; j++) {
      uint32 other = offs[j];
      if (other > offset && other < next)
        next = other;
    }
    uint32 count = (text[offset + 1] + 1) >> 1;
    if (offset + 2 + count == next)
      map_fits++;
    if (offset + 2 + 1 + count == next)
      raw_fits++;
  }
  return raw_fits > map_fits;
}

bool EndingEpilogue_UsesRawWords(const uint8 *data, size_t data_size,
                                 const uint16 *offs, size_t offs_count) {
  int map_fits = 0;
  int raw_fits = 0;
  for (size_t i = 0; i < offs_count; i++) {
    uint32 start = offs[i];
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) {
      if (offs[j] == offs[i]) {
        duplicate = true;
        break;
      }
    }
    if (duplicate)
      continue;
    uint32 end = (uint32)data_size;
    for (size_t j = 0; j < offs_count; j++) {
      uint32 other = offs[j];
      if (other > start && other < end)
        end = other;
    }
    if (start >= end)
      continue;
    for (int layout = 0; layout < 2; layout++) {
      uint32 pos = start;
      bool fits = true;
      while (pos < end) {
        if (pos + 4 > data_size) {
          fits = false;
          break;
        }
        uint32 count = ((data[pos + 2] | data[pos + 3] << 8) >> 9 & 0x7f) + 1;
        uint32 step = 4 + count + (layout ? 1 : 0);
        if (pos + step > data_size || pos + step > end) {
          fits = false;
          break;
        }
        pos += step;
      }
      if (fits && pos == end) {
        if (layout)
          raw_fits++;
        else
          map_fits++;
      }
    }
  }
  return raw_fits > map_fits;
}
