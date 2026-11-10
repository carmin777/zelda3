#include "game_tables.h"

#include <stdio.h>
#include <string.h>

#include "game_over_text.h"

// The .def file is the single source of truth shared with tools/modkit.py.
// Declare each table with its bounds so sizeof() works in the registry below.
#define MOD_TABLE(name, words, addr) extern uint16 name[words];
#define MOD_TABLE_BYTES(name, bytes, addr) extern uint8 name[bytes];
#define MOD_TABLE_DERIVED(name, bytes) extern uint8 name[bytes];
#include "game_tables.def"
#undef MOD_TABLE
#undef MOD_TABLE_BYTES
#undef MOD_TABLE_DERIVED

#define MOD_TABLE(name, words, addr) { #name, name, sizeof(name), addr },
#define MOD_TABLE_BYTES(name, bytes, addr) { #name, name, sizeof(name), addr },
#define MOD_TABLE_DERIVED(name, bytes) { #name, name, sizeof(name), 0 },
ModTable g_modTables[] = {
#include "game_tables.def"
};
#undef MOD_TABLE
#undef MOD_TABLE_BYTES
#undef MOD_TABLE_DERIVED

const int g_modTableCount = (int)(sizeof(g_modTables) / sizeof(g_modTables[0]));

ModTable *ModTable_Find(const char *name) {
  for (int i = 0; i < g_modTableCount; i++) {
    if (strcmp(g_modTables[i].name, name) == 0)
      return &g_modTables[i];
  }
  return NULL;
}

bool ModTable_Validate(ModTable *table, const uint8 *data, char *err, size_t err_size) {
  if (strcmp(table->name, "kGameOverTextLetterCount") == 0) {
    if (data[0] < kGameOverTextMinLetters || data[0] > kGameOverTextMaxLetters) {
      snprintf(err, err_size,
               "table '%s' value %u is outside %d..%d letters",
               table->name, (unsigned)data[0],
               kGameOverTextMinLetters, kGameOverTextMaxLetters);
      return false;
    }
  }
  return true;
}
