#include "inverted.h"

#include "../variables.h"
#include "seed_import.h"

// SRAM offsets copied from z3r/sram.asm (the same layout as the US save).
enum {
  kSramProgressIndicator = 0x3C5,
  kSramProgressFlags = 0x3C6,
  kSramStartingEntrance = 0x3C8,
  kSramCurrentWorld = 0x3CA,
};

// Link's House interior and its vanilla overworld exit. In inverted mode the
// house is on the Dark World side, so the exit screen keeps the same low six
// bits with the $40 bit set.
enum {
  kRoom_LinksHouse = 0x104,
  kScreen_LinksHouseLightWorld = 0x2C,
  kScreen_LinksHouseDarkWorld = 0x6C,
};

static bool g_new_file;
static int g_new_file_slot;
static uint16 g_new_game_room;
static bool g_start_points_applied;

static uint16 ReadWord(const uint8 *p) {
  return (uint16)(p[0] | p[1] << 8);
}

bool ModInverted_Active(void) {
  const ModSeedZ3rSettings *s = ModSeedImport_Z3rSettings();
  bool active = ModSeedImport_Z3rRulesActive() && s != NULL && s->inverted != 0;
  if (active && !g_start_points_applied) {
    ModSeedImport_ApplyStartingPoints();
    ModSeedImport_ApplyPalettes();
    ModSeedImport_ApplySignText();
    g_start_points_applied = true;
  }
  return active;
}

bool ModInverted_PearlRequired(uint8 world) {
  bool dark = (world & 0x40) != 0;
  // Inactive: keep the original "screen >= $40 is the Dark World" test.
  return ModInverted_Active() ? !dark : world >= 0x40;
}

void ModInverted_OnNewSave(uint8 *sram, size_t size, int slot,
                           bool seed_template) {
  if (!ModInverted_Active() || sram == NULL || size < 0x500)
    return;

  if (!seed_template) {
    // A settings-only proof has no SRAM template. Start at Link's House with
    // the open-mode progress so the bed/uncle intro is skipped, matching the
    // inverted world state (there is no escape sequence in inverted mode).
    sram[kSramCurrentWorld] = 0x40;
    if (sram[kSramProgressIndicator] < 2)
      sram[kSramProgressIndicator] = 2;
    sram[kSramProgressFlags] = 0x14;
    sram[kSramStartingEntrance] = 0;
  }
  // With a seed template the saved CurrentWorld is kept as the ROM supplied
  // it; Module05 sets the runtime $7EF3CA to $40 for the inverted start.
  g_new_file = true;
  g_new_file_slot = slot;
  g_new_game_room = 0;
}

bool ModInverted_BeginNewGameLoad(int slot) {
  if (!g_new_file || slot != g_new_file_slot)
    return false;
  g_new_file = false;
  if (!ModInverted_Active())
    return false;
  // StartingEntrance 0 is the house path used by the settings-only proof;
  // the seed's own template normally selects another row (Sanctuary, 1).
  if (which_starting_point == 0)
    g_new_game_room = kRoom_LinksHouse;
  return true;
}

bool ModInverted_UsesStartingAreaExit(int point) {
  const ModSeedZ3rSettings *s = ModSeedImport_Z3rSettings();
  return ModInverted_Active() && s != NULL && point >= 0 && point < 7 &&
         s->starting_area_exit_offset[point] != 0;
}

void ModInverted_ApplyStartingAreaExit(void) {
  const ModSeedZ3rSettings *s = ModSeedImport_Z3rSettings();
  int point = which_starting_point;
  if (!ModInverted_Active() || s == NULL || point < 0 || point >= 7)
    return;
  int offset = s->starting_area_exit_offset[point];
  if (offset == 0 || offset > 4)
    return;
  const uint8 *row = &s->starting_area_exit_table[(offset - 1) * 20];
  uint8 area = row[2];
  // The z3r routine does not rewrite RoomIndex; the current room comes from
  // the seed's starting-point table. Only the cached exit side is replaced.
  // The z3r table stores the overworld side of the cave in both EN_OWSCR
  // and EN_OWSCR2, then rewrites the cached Link/camera values below.
  overworld_area_index_exit = area;
  overworld_screen_index_exit = area;
  TM_copy_exit = 0x16;
  map16_load_src_off_exit = ReadWord(row + 3);
  BG2VOFS_copy2_exit = ReadWord(row + 5);
  BG2HOFS_copy2_exit = ReadWord(row + 7);
  link_y_coord_exit = ReadWord(row + 9) + 0x10;
  link_x_coord_exit = ReadWord(row + 0xB);
  camera_y_coord_scroll_low_exit = ReadWord(row + 0xD);
  camera_x_coord_scroll_low_exit = ReadWord(row + 0xF);
  int8 scroll_x = (int8)row[0x11];
  int8 scroll_y = (int8)row[0x12];
  overworld_unk1_exit = (uint16)scroll_x;
  overworld_unk1_neg_exit = (uint16)-scroll_x;
  overworld_unk3_exit = (uint16)scroll_y;
  overworld_unk3_neg_exit = (uint16)-scroll_y;
}

uint16 ModInverted_TakeNewGameRoom(uint16 fallback) {
  if (g_new_game_room == 0)
    return fallback;
  uint16 room = g_new_game_room;
  g_new_game_room = 0;
  return room;
}

uint8 ModInverted_ExitScreen(uint16 room, uint8 screen) {
  if (ModInverted_Active() && room == kRoom_LinksHouse &&
      screen == kScreen_LinksHouseLightWorld)
    return kScreen_LinksHouseDarkWorld;
  return screen;
}

void ModInverted_ApplyOverworldPatches(uint16 screen) {
  if (ModInverted_Active())
    ModSeedImport_ApplyOverworldPatches(screen);
}

bool ModInverted_MirrorBonk(uint16 x, uint16 y) {
  return ModInverted_Active() && (overworld_screen_index & 0x40) != 0 &&
         ModSeedImport_MirrorBonk(x, y);
}
