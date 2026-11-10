#ifndef ZELDA3_MOD_GAME_TABLES_H_
#define ZELDA3_MOD_GAME_TABLES_H_

#include "../types.h"

// A fixed-size game table that a mod may replace byte-for-byte. `rom_addr` is
// the SNES address used by the US ROM; tools/modkit.py reads the translated
// ROM at that address when importing tables. Derived tables have rom_addr 0:
// modkit builds them by decoding the translated ROM instead.
typedef struct ModTable {
  const char *name;
  void *ptr;
  uint32 size;
  uint32 rom_addr;
} ModTable;

extern ModTable g_modTables[];
extern const int g_modTableCount;

// Returns the table with this manifest name, or NULL if it is unknown.
ModTable *ModTable_Find(const char *name);

// Checks table-specific constraints before publishing an override. Returns
// false and fills `err` when the bytes are invalid for this table.
bool ModTable_Validate(ModTable *table, const uint8 *data, char *err, size_t err_size);

#endif  // ZELDA3_MOD_GAME_TABLES_H_
