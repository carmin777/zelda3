#include "../zelda_rtl.h"
#include "../variables.h"
#include "../load_gfx.h"
#include "../zelda_cpu_infra.h"
#include "../nmi.h"
#include "../sprite.h"
#include "../messaging.h"
#include "seed_import.h"
#include "seed_oracle.h"

// JP 1.0 stores the z3randomizer HUD palette at $B3:8000 (PC 0x198000).
static const uint32 kSeedJpHudPalettePc = 0x198000;

static uint32 SeedOracle_BgPc(const uint8 *rom, int gfx);
static uint8 SeedRomRead8(const uint8 *rom, uint32 addr);
static uint16 SeedRomRead16(const uint8 *rom, uint32 addr);

// $A4:96F4 SetFileSelectPalette (z3randomizer hook at $8C:CDA5):
//   GameMode != 4: copy GFX_HUD_Palette into PaletteBuffer and bump NMICGRAM;
//   GameMode == 4: load the vanilla HUD palette for the naming screen.
// Both paths then fall through to Palette_SelectScreen ($9B:EF96), which the
// C engine implements as Palette_LoadForFileSelect.
void SeedOracle_FileSelectPalette(void) {
  const uint8 *rom = EmuSeedRomData();
  if (!rom) {
    Palette_LoadForFileSelect();
    return;
  }
  if (main_module_index == 4) {
    hud_palette = 1;
    Palette_Load_HUD();
  } else {
    // The ROM loop copies 33 words: LDX #$40, then DEX #2 / BPL.
    memcpy(main_palette_buffer, rom + kSeedJpHudPalettePc, 0x42);
    flag_update_cgram_in_nmi++;
  }
  Palette_LoadForFileSelect();
  if (main_module_index == 1) {
    // JP $9B:F00F copies eight words (the US path only seven) from $D226 and
    // $D244 into both palette buffers at $C4D0/$C4F0 and $C6D0/$C6F0.
    for (int i = 0; i < 8; i++) {
      uint16 v = SeedRomRead16(rom, 0x9BD226 + i * 2);
      aux_palette_buffer[0xE8 + i] = main_palette_buffer[0xE8 + i] = v;
      v = SeedRomRead16(rom, 0x9BD244 + i * 2);
      aux_palette_buffer[0xF8 + i] = main_palette_buffer[0xF8 + i] = v;
    }
  }
}

// $00:90EB NMI upload table: $937A/$9383/$938C hold the low/high/bank
// pointer for each $14 value.  Return the ROM pointer for that entry.
const uint8 *SeedOracle_JpNmiStripePointer(int idx) {
  const uint8 *rom = EmuSeedRomData();
  uint32 src = ((uint32)SeedRomRead8(rom, 0x938C + idx) << 16) |
               ((uint32)SeedRomRead8(rom, 0x9383 + idx) << 8) |
               SeedRomRead8(rom, 0x937A + idx);
  // $00:1002 (index 1) is the WRAM NMI table built by $8C:DC8A, not ROM.
  if (src == 0x001002)
    return g_ram + 0x1002;
  return rom + (((src >> 16) << 15 | (src & 0x7fff)) & 0x1fffff);
}

// $A4:9936 (reached from $A4:99DB): the name-entry background records for
// VRAM $62B1+.  The C fill leaves this area at $0188; the JP table restores
// the text tiles at $62B1/$62D1/...
void SeedOracle_JpNameEntryUpload(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  HandleStripes14(rom + 0x1218DE);
}

// $8C:DC8A: build the two-record NMI 1 table at $1002 for the character
// just written.  X is the byte offset in the name buffer.
static void JpName_DrawSelected(int xoff) {
  const uint8 *rom = EmuSeedRomData();
  uint16 tile = SeedRomRead16(rom, 0xA498BA + selectfile_var4 * 2);
  uint16 a = swap16(0x6000 | tile);
  *(uint16 *)(g_ram + 0x1002) = a;
  *(uint16 *)(g_ram + 0x1008) = swap16((0x6000 | tile) + 0x20);
  *(uint16 *)(g_ram + 0x1004) = 0x0100;
  *(uint16 *)(g_ram + 0x100A) = 0x0100;
  uint16 chr = (xoff < 8) ? *(uint16 *)(g_zenv.sram + 0x500 + xoff)
                          : *(uint16 *)(g_zenv.sram + 0x3D9 + xoff);
  chr |= 0x1C00;
  *(uint16 *)(g_ram + 0x1006) = chr;
  *(uint16 *)(g_ram + 0x100C) = chr + 0x10;
  g_ram[0x100E] = 0xFF;
  nmi_load_bg_from_vram = 1;
}

// $A4:98A9: clamp the name cursor to 0..11.
static void JpName_WrapCursor(void) {
  if ((int8)selectfile_var4 < 0)
    selectfile_var4 = 11;
  else if (selectfile_var4 >= 12)
    selectfile_var4 = 0;
}

// $8C:DB3E..$8C:DBDB: finalize the name and return to the file select.
static void JpName_Finalize(void) {
  const uint8 *rom = EmuSeedRomData();
  for (int i = 0; i < 0x3D8; i++)
    g_zenv.sram[i] = SeedRomRead8(rom, 0xB0B000 + i);
  for (int i = 0; i < 0x10D; i++)
    g_zenv.sram[0x3E3 + i] = SeedRomRead8(rom, 0xB0B3E3 + i);
  if (SeedRomRead8(rom, 0xB0B3C5) & 0x80) {
    g_zenv.sram[0x3C5] = 0;
    g_zenv.sram[0x282] = 0;
  }
  *(uint16 *)(g_zenv.sram + 0x3E1) = 0x55AA;
  // $A4:B198 uses ADC with the carry chain; emulate it exactly.
  uint32 sum = 0, carry = 0;
  for (int i = 0; i <= 0x4FC; i += 2) {
    uint32 t = sum + *(uint16 *)(g_zenv.sram + i) + carry;
    carry = t >> 16;
    sum = t & 0xFFFF;
  }
  for (int i = 0; i <= 0xFFE; i += 2) {
    uint32 t = sum + *(uint16 *)(g_zenv.sram + 0x500 + i) + carry;
    carry = t >> 16;
    sum = t & 0xFFFF;
  }
  *(uint16 *)(g_zenv.sram + 0x4FE) = 0x5A5A - (uint16)sum;
  // MVN $70,$70 copies $700000..$7014FF to $704000..$7054FF.
  for (int i = 0; i < 0x1500; i++)
    g_zenv.sram[0x4000 + i] = g_zenv.sram[i];
  subsubmodule_index = 0;
  selectfile_R16 = 0;
  main_module_index = 1;
  submodule_index = 1;
  irq_flag = 0xFF;
  sound_effect_1 = 0x2C;
}

// $8C:DB1A: confirm the name; a blank name just returns.
static void JpName_Confirm(void) {
  int base = selectfile_R16 * 0x500;
  for (int off = 0; off < 8; off += 2) {
    if (*(uint16 *)(g_zenv.sram + 0x3D9 + base + off) != 0x018C) {
      JpName_Finalize();
      return;
    }
  }
}

// $A4:9823: the seed's Module04 state 1.  If $B0:C041 is set, copy the
// forced 12-word name and confirm it; otherwise use the normal setup.
bool SeedOracle_JpMaybeForceFileName(void) {
  if (!EmuSeedRomActive())
    return false;
  const uint8 *rom = EmuSeedRomData();
  if (SeedRomRead8(rom, 0xB0C041) == 0)
    return false;
  for (int x = 0; x <= 0x16; x += 2) {
    uint16 v = SeedRomRead16(rom, 0xB0B3D9 + x);
    *(uint16 *)(g_zenv.sram + 0x500 + x) = v;
    if (x < 8)
      *(uint16 *)(g_zenv.sram + 0x3D9 + x) = v;
  }
  JpName_Confirm();
  return true;
}

// $8C:DAB0: type the character under the cursor, or handle the $CA/$CB/$CD
// keys, then advance the cursor.
static void JpName_Type(void) {
  const uint8 *rom = EmuSeedRomData();
  sound_effect_1 = 0x2B;
  uint16 row_off = SeedRomRead16(rom, 0x8CD97B + selectfile_var5 * 2);
  uint8 tile = SeedRomRead8(rom, 0x8CD7DF + ((row_off + selectfile_var3) & 0x1FF));
  if (tile == 0xCA) {
    selectfile_var4--;
    JpName_WrapCursor();
    return;
  }
  if (tile == 0xCB) {
    selectfile_var4++;
    JpName_WrapCursor();
    return;
  }
  if (tile == 0xCD) {
    JpName_Confirm();
    return;
  }
  uint16 chr = ((tile & 0xFFF0) << 1) | (tile & 0x0F);
  int xoff = selectfile_R16 * 0x500 + selectfile_var4 * 2;
  *(uint16 *)(g_zenv.sram + 0x500 + xoff) = chr;
  if (xoff < 8)
    *(uint16 *)(g_zenv.sram + 0x3D9 + xoff) = chr;
  JpName_DrawSelected(xoff);
  selectfile_var4++;
  JpName_WrapCursor();
}

// $8C:D9C7: JP name-entry loop.
void SeedOracle_JpNameFile(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  for (int i = 0; i < 26; i++)
    SetOamPlain(&oam_buf[i], 0x18 + i * 8, selectfile_var7, 0x2E, 0x3C, 0);
  uint8 heart_x = SeedRomRead8(rom, 0xA498D2 + selectfile_var4);
  SetOamPlain(&oam_buf[26], heart_x, 0x14, 0x29, 0x0C, 0);

  if (selectfile_var9 || selectfile_var11)
    return;
  if (filtered_joypad_H & 0x10) {
    JpName_Confirm();
    return;
  }
  if ((filtered_joypad_H & 0xC0) || (filtered_joypad_L & 0xC0))
    JpName_Type();
}

// $A4:8AF0: copy a 2x2 tile block from $A4:src to g_ram+dest.
// $A4:8AF0: copy a 2x2 tile block from $A4:src to g_ram+dest.
static void FsCopy4(int dest, int src, int invert) {
  const uint8 *rom = EmuSeedRomData();
  for (int i = 0; i < 4; i++) {
    int d = dest + (i == 0 ? 0 : i == 1 ? 2 : i == 2 ? 0x40 : 0x42);
    uint16 v = SeedRomRead16(rom, 0xA40000 | (src + i * 2));
    if (invert)
      v = (v & 0xE3FF) | 0x1000;
    *(uint16 *)(g_ram + d) = v;
  }
}

static void FsWrite(int dest, uint16 v) {
  *(uint16 *)(g_ram + dest) = v;
}

static void FsSel4(int dest, int src, uint16 v) {
  FsCopy4(dest, src, (v & 0xFF) == 0);
}

// $A4:8B45: value 0..7 selects a block; 0 is the inverted variant.
static void FsSel8(int dest, uint16 v) {
  static const uint16 kTab[8] = {
    0x92E0, 0x92E0, 0x92E8, 0x92F0, 0x92F8, 0x9300, 0x9308, 0x9310
  };
  int idx = v & 0xFF;
  if (idx > 7)
    idx = 7;
  FsCopy4(dest, kTab[idx], idx == 0);
}

// $A4:B08B: A is a 0..255 index; returns the two tile bytes in X/Y.
static void FsB08B(uint8 a, uint8 *x, uint8 *y) {
  const uint8 *rom = EmuSeedRomData();
  uint16 v = SeedRomRead16(rom, 0xA4C91D + a * 2);
  *y = (uint8)(((v >> 4) & 0x0F) | 0x90);
  *x = (uint8)((v & 0x0F) | 0x90);
}

// $A4:C884: BCD digits of A into $7F5003..$7F5007.
static void FsC884(uint16 a) {
  int v = a;
  if (v >= 1000)
    v %= 1000;
  int hi = v / 100;
  int mid = (v / 10) % 10;
  int lo = v % 10;
  g_ram[0x15003] = 0;
  g_ram[0x15004] = 0;
  g_ram[0x15005] = (uint8)hi;
  g_ram[0x15006] = (uint8)mid;
  g_ram[0x15007] = (uint8)lo;
}

// $A4:B0A9: BCD helper for $7003xx values; leaves digits in $04..$07.
static void FsB0CD(uint16 a) {
  FsC884(a);
  *(uint16 *)(g_ram + 4) = *(uint16 *)(g_ram + 0x15004) | 0x9090;
  *(uint16 *)(g_ram + 6) = *(uint16 *)(g_ram + 0x15006) | 0x9090;
}

static uint16 FsSram8(int off) { return g_zenv.sram[off]; }
static uint16 FsSram16(int off) { return *(uint16 *)(g_zenv.sram + off); }

// $8C:9318: static name/quest/seed icons in the file card.
static void FsCard9318(void) {
  FsWrite(0x1018, 0x1ADB);
  FsWrite(0x101A, 0x1ADC);
  FsB0CD(FsSram16(0x362));
  FsWrite(0x1056, (g_ram[4] & 0xFF) + 0x1E50);
  FsWrite(0x1058, (g_ram[5] & 0xFF) + 0x1E50);
  FsWrite(0x105A, (g_ram[6] & 0xFF) + 0x1E50);
  FsWrite(0x105C, (g_ram[7] & 0xFF) + 0x1E50);
  FsWrite(0x1020, 0x0ECB);
  FsWrite(0x1022, 0x0ECC);
  uint8 x, y;
  FsB08B(FsSram8(0x343) & 0xFF, &x, &y);
  FsWrite(0x1060, y + 0x1E50);
  FsWrite(0x1062, x + 0x1E50);
  if (FsSram8(0x38E) & 0x40) {
    FsWrite(0x1026, 0x06D9);
    FsWrite(0x1028, 0x06DA);
  } else {
    FsWrite(0x1026, 0x02C9);
    FsWrite(0x1028, 0x02CA);
  }
  FsB08B(FsSram8(0x377) & 0xFF, &x, &y);
  FsWrite(0x1066, y + 0x1E50);
  FsWrite(0x1068, x + 0x1E50);
}

// $A4:8BFE: dynamic body of the z3r DrawPlayerFile card.
static void FsCardBody(void) {
  // Name: 8 words from $700508; rows $118E/$11CE and $124E/$128E.
  for (int i = 0; i < 8; i++) {
    uint16 v = FsSram16(0x508 + i * 2) | 0x1C00;
    if (i < 4) {
      FsWrite(0x118E + i * 2, v);
      FsWrite(0x11CE + i * 2, v + 0x10);
    } else {
      FsWrite(0x124E + (i - 4) * 2, v);
      FsWrite(0x128E + (i - 4) * 2, v + 0x10);
    }
  }
  FsCard9318();

  // Equipment rows selected by the save fields.
  if (FsSram8(0x38E) & 0x40) {
    if (FsSram8(0x340) & 0xFF)
      FsCopy4(0x10DC, 0x9180, 0);
    else
      FsCopy4(0x10DC, 0x9190, 0);
  } else {
    FsCopy4(0x10DC, 0x9178, (FsSram8(0x340) & 0xFF) == 0);
  }
  if ((FsSram8(0x38C) & 0xC0) == 0xC0)
    FsCopy4(0x10E0, 0x91A8, 0);
  else if (FsSram8(0x38C) & 0x40)
    FsCopy4(0x10E0, 0x91A0, 0);
  else
    FsCopy4(0x10E0, 0x9198, (FsSram8(0x38C) & 0x80) == 0);
  FsSel4(0x10E4, 0x91B0, FsSram16(0x342));
  FsCopy4(0x10EC, 0x9288, (FsSram8(0x38C) & 0x10) == 0);
  FsCopy4(0x10E8, 0x9280, (FsSram8(0x38C) & 0x28) == 0);
  FsCopy4(0x11E4, 0x9208, (FsSram8(0x38C) & 0x03) == 0);
  FsCopy4(0x125C, 0x9200, (FsSram8(0x38C) & 0x04) == 0);

  FsSel4(0x115C, 0x91C0, FsSram16(0x345));
  FsSel4(0x1160, 0x91C8, FsSram16(0x346));
  FsSel4(0x1164, 0x91D0, FsSram16(0x347));
  FsSel4(0x1168, 0x91D8, FsSram16(0x348));
  FsSel4(0x116C, 0x91E0, FsSram16(0x349));
  FsSel4(0x11DC, 0x91E8, FsSram16(0x34A));
  FsSel4(0x11E0, 0x91F0, FsSram16(0x34B));
  FsSel4(0x11E8, 0x91F8, FsSram16(0x34D));
  FsSel4(0x11EC, 0x9210, FsSram16(0x34E));
  FsSel4(0x1260, 0x9218, FsSram16(0x350));
  FsSel4(0x1264, 0x9220, FsSram16(0x351));
  FsSel4(0x1268, 0x9228, FsSram16(0x352));
  FsSel4(0x126C, 0x9230, FsSram16(0x353));

  FsSel8(0x10F2, FsSram8(0x35C));
  FsSel8(0x1172, FsSram8(0x35D));
  FsSel8(0x11F2, FsSram8(0x35E));
  FsSel8(0x1272, FsSram8(0x35F));

  int v = FsSram8(0x359) & 0xFF;
  FsCopy4(0x10F8, v == 0 ? 0x9290 : v == 1 ? 0x9290 : v == 2 ? 0x9298 : v == 3 ? 0x92A0 : v == 4 ? 0x92A8 : 0x9290, v == 0 || v > 4);
  v = FsSram8(0x35A) & 0xFF;
  FsCopy4(0x1178, v == 0 ? 0x92B0 : v == 1 ? 0x92B0 : v == 2 ? 0x92B8 : 0x92C0, v == 0);
  v = FsSram8(0x35B) & 0xFF;
  FsCopy4(0x11F8, v == 0 ? 0x92C8 : v == 1 ? 0x92D0 : 0x92D8, 0);

  // $70036B / $B0:F020: sword/equipment tile block at $1278.
  {
    uint8 idx = SeedRomRead8(EmuSeedRomData(), 0xB0F020);
    uint16 bonus = SeedRomRead16(EmuSeedRomData(), 0xB0F02A + idx * 2);
    uint16 q[4];
    int e = FsSram8(0x36B) & 0xFF;
    if (e == 0) { q[0] = 0x02C0; q[1] = 0x42C0; q[2] = 0x02D0; q[3] = 0x42D0; }
    else if (e == 1) { q[0] = 0x02C1; q[1] = 0x42C0; q[2] = 0x02D0; q[3] = 0x42D0; }
    else if (e == 2) { q[0] = 0x02C1; q[1] = 0x42C0; q[2] = 0x02D1; q[3] = 0x42D0; }
    else { q[0] = 0x02C1; q[1] = 0x42C1; q[2] = 0x02D1; q[3] = 0x42D0; }
    FsWrite(0x1278, q[0] | bonus);
    FsWrite(0x127A, q[1] | bonus);
    FsWrite(0x12B8, q[2] | bonus);
    FsWrite(0x12BA, q[3] | bonus);
  }

  FsC884(FsSram8(0x470) & 0xFF);
  FsWrite(0x12F8, (g_ram[0x15006] & 0xFF) | 0x1EE0);
  FsWrite(0x12FA, (g_ram[0x15007] & 0xFF) | 0x1EE0);

  FsSel4(0x10FC, 0x9240, FsSram16(0x355));
  v = FsSram8(0x354) & 0xFF;
  FsCopy4(0x117C, v == 0 ? 0x9270 : v == 1 ? 0x9270 : 0x9278, v == 0);
  FsSel4(0x11FC, 0x9238, FsSram16(0x356));
  FsSel4(0x127C, 0x9248, FsSram16(0x357));

  FsCopy4(0x131C, (FsSram16(0x374) & 0x04) ? 0x9258 : 0x9250, 0);
  FsCopy4(0x1320, (FsSram16(0x374) & 0x02) ? 0x9260 : 0x9250, 0);
  FsCopy4(0x1324, (FsSram16(0x374) & 0x01) ? 0x9268 : 0x9250, 0);

  uint16 bits = FsSram16(0x37A);
  uint16 b0 = (bits & 0x02) ? 0x0ED7 : 0x12C7;
  uint16 b1 = (bits & 0x10) ? 0x0ED7 : 0x12C7;
  uint16 b2 = (bits & 0x40) ? 0x0ED7 : 0x12C7;
  uint16 b3 = (bits & 0x20) ? 0x0ED7 : 0x12C7;
  uint16 b4 = (bits & 0x04) ? 0x06D7 : 0x12C7;
  uint16 b5 = (bits & 0x01) ? 0x06D7 : 0x12C7;
  uint16 b6 = (bits & 0x08) ? 0x0ED7 : 0x12C7;
  FsWrite(0x1368, b0); FsWrite(0x136A, b0 + 1);
  FsWrite(0x132A, b1); FsWrite(0x132C, b1 + 1);
  FsWrite(0x136C, b2); FsWrite(0x136E, b2 + 1);
  FsWrite(0x132E, b3); FsWrite(0x1330, b3 + 1);
  FsWrite(0x1370, b4); FsWrite(0x1372, b4 + 1);
  FsWrite(0x1332, b5); FsWrite(0x1334, b5 + 1);
  FsWrite(0x1374, b6); FsWrite(0x1376, b6 + 1);
}

// $A4:8B8D: frame-parity wrapper and headers for the DrawPlayerFile card.
static void FsCard(void) {
  if (frame_counter & 1) {
    g_ram[0x710] = 0;
    FsWrite(0x1002, 0xFFFF);
    return;
  }
  FsCardBody();
  g_ram[0x710]++;
  FsWrite(0x1002, 0x0161);
  FsWrite(0x1402, swap16(0x6000));
  FsWrite(0x1408, swap16(0x601F));
  FsWrite(0x1404, swap16(0xC039));
  FsWrite(0x140A, swap16(0xC039));
  FsWrite(0x1406, 0x0188);
  FsWrite(0x140C, 0x4188);
  FsWrite(0x140E, swap16(0x6063));
  FsWrite(0x1410, swap16(0x4001));
  FsWrite(0x1412, SeedRomRead8(EmuSeedRomData(), 0xB0817F) ? 0x26FE : 0x0188);
  FsWrite(0x1414, 0xFFFF);
  *(uint8 *)(g_ram + 2) = 4;
}

// $8C:D518: saved name + hearts of one file into the card table.
static void FsD518(int k) {
  const uint8 *rom = EmuSeedRomData();
  int slot = k * 0x500;
  int y = SeedRomRead8(rom, 0x8CD50C + k * 2) |
          SeedRomRead8(rom, 0x8CD50D + k * 2) << 8;
  for (int i = 0; i < 4; i++) {
    uint16 v = (*(uint16 *)(g_zenv.sram + 0x3D9 + slot + i * 2)) | 0x1C00;
    FsWrite(0x1002 + y + i * 2, v);
    FsWrite(0x1042 + y + i * 2, v + 0x10);
  }
  int hy = SeedRomRead8(rom, 0x8CD512 + k * 2) |
           SeedRomRead8(rom, 0x8CD513 + k * 2) << 8;
  int hearts = g_zenv.sram[0x36C + slot] >> 3;
  uint8 ti = SeedRomRead8(rom, 0xB0F020);
  uint16 tile = SeedRomRead16(rom, 0xB0F02A + ti * 2) | 0x0200;
  for (int i = 0; i < hearts; i++) {
    int row = i / 10;
    int col = i % 10;
    FsWrite(0x1002 + hy + row * 0x40 + col * 2, tile);
  }
}

// $8C:D594: OAM card for one file.
static void FsD594(int k) {
  const uint8 *rom = EmuSeedRomData();
  int slot = k * 0x500;
  link_dma_graphics_index = 0x116 * 2;
  uint8 *o = g_ram + 0x800;
  uint8 ybase = SeedRomRead8(rom, 0x8CD57E + k);
  uint8 x = SeedRomRead8(rom, 0x8CD581 + k);
  o[x + 0] = 0x34;
  o[x + 4] = 0x34;
  o[x + 1] = ybase + 0xFB;
  o[x + 5] = ybase + 0xFB + 8;
  o[x + 3] = o[x + 7] = SeedRomRead8(rom, 0x8CD58B + k);
  g_ram[0xA20 + x / 4] = 0;
  g_ram[0xA20 + (x + 4) / 4] = 0;
  uint8 sword = g_zenv.sram[0x359 + slot];
  if (sword == 0) {
    o[x + 1] = o[x + 5] = 0xF0;
    sword = 0;
  } else {
    sword--;
  }
  uint8 schar = SeedRomRead8(rom, 0x8CD584 + k);
  o[x + 2] = schar;
  o[x + 6] = schar + 0x10;
  o[x + 8] = 0x23;
  o[x + 9] = ybase + 0x0A;
  o[x + 11] = SeedRomRead8(rom, 0x8CD58E + k);
  uint8 shield = g_zenv.sram[0x35A + slot];
  if (shield == 0) {
    o[x + 9] = 0xF0;
  } else {
    shield--;
  }
  o[x + 10] = SeedRomRead8(rom, 0x8CD588 + k);
  g_ram[0xA20 + (x + 8) / 4] = 2;
  o[x + 12] = 0x28;
  o[x + 16] = 0x28;
  o[x + 14] = 0;
  o[x + 18] = 2;
  uint8 fl = SeedRomRead8(rom, 0x8CD591 + k);
  o[x + 15] = fl;
  o[x + 19] = fl | 0x40;
  o[x + 13] = ybase;
  o[x + 17] = ybase + 8;
  g_ram[0xA20 + (x + 12) / 4] = 2;
  g_ram[0xA20 + (x + 16) / 4] = 2;
  (void)sword;
  (void)shield;
}

// $8C:D6C0: death-counter digits for one file.
static void FsD6C0(int k) {
  const uint8 *rom = EmuSeedRomData();
  int slot = k * 0x500;
  uint16 v = *(uint16 *)(g_zenv.sram + 0x401 + slot);
  if (v == 0xFFFF)
    return;
  int d0, d1, d2;
  if (v >= 1000) {
    d0 = d1 = d2 = 9;
  } else {
    d0 = v % 10;
    d1 = (v / 10) % 10;
    d2 = v / 100;
  }
  uint8 *o = g_ram + 0x800;
  int y = SeedRomRead8(rom, 0x8CD6B7 + k);
  uint8 ox = SeedRomRead8(rom, 0x8CD6BA + k);
  uint8 oy = SeedRomRead8(rom, 0x8CD6BD + k);
  int digits[3] = { d0, d1, d2 };
  int start = (d2 != 0) ? 2 : (d1 != 0) ? 1 : 0;
  for (int i = 0; i <= start; i++, y += 4, ox -= 4) {
    o[y + 0] = ox;
    o[y + 1] = oy;
    o[y + 2] = SeedRomRead8(rom, 0x8CD6AD + digits[i]);
    o[y + 3] = 0x3C;
    g_ram[0xA20 + y / 4] = 0;
  }
}

void SeedOracle_JpFileSelectDrawFile(int k) {
  if (!EmuSeedRomActive())
    return;
  if (k == 0)
    FsCard();
  FsD518(k);
  FsD594(k);
  FsD6C0(k);
}


// $8E:EE86: expand the JP dialogue selected by $1CF0 into the 16-bit word
// stream at $7F:1200.  The source is $1C:8000, the LoROM mirror of
// $9C:8000, plus the offset from the $7F:71C0 table built by $9C:F37A.
// $1CD9 tracks the output byte position and $1CDD the source byte position;
// the $FB terminator is stored, but neither position is advanced past it.
void SeedOracle_JpLoadMessageBuffer(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  uint16 off = *(uint16 *)(g_ram + 0x171C0 + dialogue_message_index * 2);
  uint32 src = 0x9C0000 + off;
  uint8 *dst = g_ram + 0x11200;
  static const uint8 kOneByteGlyphs[5] = {0x73, 0x74, 0x75, 0x76, 0x7E};
  int x = 0, y = 0;

  *(uint16 *)dst = 0x7F7F;
  *(uint16 *)(g_ram + 0x1CD9) = 0;
  *(uint16 *)(g_ram + 0x1CDD) = 0;
  for (;;) {
    uint8 c = SeedRomRead8(rom, src + y);
    if (c < 0xF6 || c == 0xFF) {
      // Plain character: the ROM swaps the byte into the high half.
      *(uint16 *)(dst + x) = (uint16)c << 8;
      y++;
      x += 2;
    } else if (c == 0xFD) {
      *(uint16 *)(dst + x) = SeedRomRead16(rom, src + y) & 0xFF01;
      y += 2;
      x += 2;
    } else if (c <= 0xFA) {
      // $F6..$FA: one-byte glyph selected by the table at $8E:EF3D.
      dst[x++] = kOneByteGlyphs[c - 0xF6];
      y++;
    } else if (c == 0xFB) {
      *(uint16 *)(dst + x) = 0x7F7F;
      return;
    } else if (c == 0xFC) {
      *(uint16 *)(dst + x) =
          (SeedRomRead16(rom, src + y) & 0xFF00) | 0x007A;
      y += 2;
      x += 2;
    } else {
      // $FE: two-byte character.  $8E:EF7D dispatches through the table at
      // $8E:EF85 using the second byte minus $67.
      y++;
      uint8 c2 = SeedRomRead8(rom, src + y);
      switch ((uint8)(c2 - 0x67)) {
      case 3: {  // $8E:EFE9: four rows from the glyph in SRAM $70:03D9.
        int sram_off = SeedRomRead16(rom, 0x80848C);
        int kept = 4;
        for (int i = 0; i < 4; i++) {
          uint16 w = *(uint16 *)(g_zenv.sram + 0x3D9 + sram_off + i * 2);
          uint16 v = swap16((uint16)(((w >> 1) & 0xFFF0) | (w & 0x000F)));
          if (v == 0xCC00)
            v = 0xFF00;
          *(uint16 *)(dst + x + i * 2) = v;
        }
        // Drop trailing blank rows; the trimmed words stay in memory.
        while (kept > 0 && *(uint16 *)(dst + x + (kept - 1) * 2) == 0xFF00)
          kept--;
        x += kept * 2;
        y++;
        break;
      }
      case 4:  // $8E:F09D: next byte becomes $1CD4 (render state).
        text_render_state = SeedRomRead8(rom, src + y + 1);
        y += 2;
        break;
      case 5: {  // $8E:F0AF: kana from the nibble table at $1CF2.
        uint8 param = SeedRomRead8(rom, src + y + 1);
        uint8 t = g_ram[0x1CF2 + (param >> 1)];
        uint8 nibble = (param & 1) ? (t >> 4) : (t & 0x0F);
        *(uint16 *)(dst + x) = (uint16)(0xA0 | nibble) << 8;
        x += 2;
        y += 2;
        break;
      }
      case 6:  // $8E:F0E0: window position from $8E:FD3E.
        text_msgbox_topleft =
            SeedRomRead16(rom, 0x8EFD3E + SeedRomRead8(rom, src + y + 1) * 2);
        y += 2;
        break;
      case 0: case 1: case 2:
      case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
      case 20: case 21: case 22: case 23: case 24:
        // $8E:EFB7: one output byte.
        dst[x++] = c2;
        y++;
        break;
      default:
        // $8E:EFD0 (indices 7 and 16..19): one output word.
        *(uint16 *)(dst + x) = SeedRomRead16(rom, src + y);
        x += 2;
        y += 2;
        break;
      }
    }
    *(uint16 *)(g_ram + 0x1CD9) = x;
    *(uint16 *)(g_ram + 0x1CDD) = y;
  }
}

// z3r hook $A4:D0ED, called by the JP text init at $8E:EE5D: clear A12 and
// decompress the two $67/$68 packs through the JP pointer tables at
// $80:D033/$D112/$D1F1 into $7F:4000 and $7F:4800.  Vanilla JP calls
// $00:D84E directly; the hook only adds the A12 clear.
void SeedOracle_JpLoadMessageFont(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  A12 = 0;
  for (int i = 0; i < 2; i++) {
    uint8 y = 0x67 + i;
    uint32 src = ((uint32)SeedRomRead8(rom, 0x80D033 + y) << 16) |
                 ((uint32)SeedRomRead8(rom, 0x80D112 + y) << 8) |
                 SeedRomRead8(rom, 0x80D1F1 + y);
    uint32 pc = (((src >> 16) << 15) | (src & 0x7fff)) & 0x1fffff;
    Decompress(i ? decomp_buffer_3 : decomp_buffer, rom + pc);
  }
}

// z3r $A0:B0FC: count screen transitions in the SRAM stats area.
// StatsLocked ($7E:F443) suppresses the counter; the value is capped by
// refusing to store 999.
void SeedOracle_JpScreenTransition(void) {
  if (!(EmuSeedRomActive() || ModSeedImport_Z3rRulesActive()) ||
      g_ram[0xF443] != 0)
    return;
  uint16 v = (uint16)(g_ram[0xF43C] | g_ram[0xF43D] << 8);
  v++;
  if (v == 999)
    return;
  g_ram[0xF43C] = v & 0xFF;
  g_ram[0xF43D] = v >> 8;
}

// z3r $A0:B2DA/$A0:B2B9: add the damage amount to the 16-bit DamageCounter
// ($7E:F46A) unless StatsLocked ($7E:F443) is set. The SNES clamps overflow
// to $FFFF.
void SeedOracle_JpAddDamageCounter(uint8 damage) {
  if (!(EmuSeedRomActive() || ModSeedImport_Z3rRulesActive()) ||
      g_ram[0xF443] != 0)
    return;
  uint16 v = (uint16)(g_ram[0xF46A] | g_ram[0xF46B] << 8);
  v += damage;
  if (v < damage)
    v = 0xFFFF;
  g_ram[0xF46A] = v & 0xFF;
  g_ram[0xF46B] = v >> 8;
}

// z3r HUD tilemap at $A1:86B0 (165 words), copied by the patched
// RebuildHUD at $8D:FA96 (LDX #$86B0, MVN $A1,$7E).
uint16 SeedOracle_JpHudTile(int idx) {
  if (!EmuSeedRomActive())
    return 0x207F;
  return SeedRomRead16(EmuSeedRomData(), 0xA186B0 + idx * 2);
}

// $8C:D173: copy the 0x7D-byte file-select background record table from
// $8C:E398 to $7E:1002 and set the upload offset.
void SeedOracle_JpFileSelectMainGfx(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  for (int i = 0; i <= 0x7D; i++)
    g_ram[0x1002 + i] = SeedRomRead8(rom, 0x8CE398 + i);
  vram_upload_offset = 0x7C;
}

// $A4:93C6: fill the file-select stripe buffer with $0188, then overwrite
// the 16 row headers.  The optional $B0:8087 == 2 block is not used here.
void SeedOracle_JpFileSelectNameStripes(void) {
  if (!EmuSeedRomActive())
    return;
  uint16 *p = (uint16 *)(g_ram + 0x1000);
  // The asm loop stores $0188 from $1400 down to $1002; $1000 stays 0.
  for (int i = 1; i <= 0x200; i++)
    p[i] = 0x0188;
  for (int r = 0; r < 16; r++) {
    p[r * 0x20 + 1] = (uint16)(0x0161 + (r & 7) * 0x2000 + (r >> 3));
    p[r * 0x20 + 2] = 0x3B00;
  }
  p[0x201] = 0x00FF;
}

// $A4:9B21: build the 0x80-byte file-select attribute row at $7E:C900 and
// DMA it to VRAM $6060.  The base table is at $A4:9CBD; five entries are
// replaced from the table at $A4:9BBD using $B0:8215.
void SeedOracle_JpFileSelectTilemap(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  for (int i = 0; i < 0x80; i++)
    g_ram[0xC900 + i] = SeedRomRead8(rom, 0xA49CBD + i);
  for (int y = 0; y < 5; y++) {
    uint8 v = SeedRomRead8(rom, 0xB08215 + y) & 0x1F;
    uint16 off = (uint16)(v * 8);
    uint16 x = (uint16)(y * 6);
    *(uint16 *)(g_ram + 0xC918 + x) = SeedRomRead16(rom, 0xA49BBD + off);
    *(uint16 *)(g_ram + 0xC91A + x) = SeedRomRead16(rom, 0xA49BBF + off);
    *(uint16 *)(g_ram + 0xC958 + x) = SeedRomRead16(rom, 0xA49BC1 + off);
    *(uint16 *)(g_ram + 0xC95A + x) = SeedRomRead16(rom, 0xA49BC3 + off);
  }
  memcpy(&g_zenv.vram[0x6060], g_ram + 0xC900, 0x80);
}

// $A4:9637: DMA $0C00 bytes from $B1:D000 to VRAM $3200.
void SeedOracle_JpFileSelectDma(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  uint32 off = ((0xB1u << 15) | 0x5000u) & 0x1fffff;
  memcpy(&g_zenv.vram[0x3200], rom + off, 0x0C00);
}

// $9B:EEA8 Palette_Load_OWBG3: the JP table is at $9B:E604, indexed by the
// byte table at $9B:EBC6.
void SeedOracle_JpLoadOWBG3(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  uint8 idx = SeedRomRead8(rom, 0x9BEBC6 + overworld_palette_aux3_bp7_lo);
  uint16 off = (uint16)(0xE604 + idx);
  uint16 *dst =
      &aux_palette_buffer[(0xE2 + overworld_palette_aux_or_main) >> 1];
  for (int i = 0; i < 7; i++)
    dst[i] = SeedRomRead16(rom, 0x9B0000 + off + i * 2);
}

// Read a byte/word from the seed ROM using a 24-bit LoROM address, the same
// mapping the emulated CPU uses for banks $00-$FF.
static uint8 SeedRomRead8(const uint8 *rom, uint32 addr) {
  uint32 pc = (((addr >> 16) << 15) | (addr & 0x7fff)) & 0x1fffff;
  return rom[pc];
}

static uint16 SeedRomRead16(const uint8 *rom, uint32 addr) {
  uint32 pc = (((addr >> 16) << 15) | (addr & 0x7fff)) & 0x1fffff;
  return rom[pc] | (uint16)rom[pc + 1] << 8;
}

static uint32 SeedRomPc(uint32 addr) {
  return (((addr >> 16) << 15) | (addr & 0x7fff)) & 0x1fffff;
}

// JP $82:F6B1 / $82:F891: the compressed overworld map32 hi/lo pointer
// tables, indexed by screen * 3. The US engine uses $02:F94D / $02:FB2D.
const uint8 *SeedOracle_JpOverworldMap32Data(int screen, bool lo) {
  const uint8 *rom = EmuSeedRomData();
  if (!rom)
    return NULL;
  uint32 table = lo ? 0x82f891 : 0x82f6b1;
  uint32 ea = table + (uint32)screen * 3;
  uint32 addr = SeedRomRead8(rom, ea) |
                (uint32)SeedRomRead8(rom, ea + 1) << 8 |
                (uint32)SeedRomRead8(rom, ea + 2) << 16;
  return rom + SeedRomPc(addr);
}

// JP $82:F3F5 Overworld_ParseMap32Definition reads these four tables:
// $83:8000, $83:B3C0, $84:8000 and $84:B3C0. The JP map16 ids differ from
// the US asset tables, so seed mode must read the seed ROM copies.
const uint8 *SeedOracle_JpMap32ToMap16(int table, int offset) {
  static const uint32 kTables[4] = {
    0x838000, 0x83b3c0, 0x848000, 0x84b3c0,
  };
  const uint8 *rom = EmuSeedRomData();
  if (!rom || table < 0 || table > 3)
    return NULL;
  return rom + SeedRomPc(kTables[table]) + offset;
}

// JP $80:8864 reads the map16->map8 table at $8F:8000 (US assets use
// $0F:8000). The JP table diverges from the US one at entry 0x2A5, so the
// overworld and tile detection must use the seed ROM copy.
const uint16 *SeedOracle_JpMap16ToMap8(void) {
  const uint8 *rom = EmuSeedRomData();
  if (!rom)
    return NULL;
  return (const uint16 *)(rom + SeedRomPc(0x8f8000));
}

// z3r $A4:A1BD returns the blank overworld transition tile for screens
// without a replacement script. The JP value is $0DBE (US uses $0DC4).
uint16 SeedOracle_JpOverworldBlankTile(void) {
  const uint8 *rom = EmuSeedRomData();
  if (!rom)
    return 0x0dc4;
  return SeedRomRead16(rom, 0xa4a1d0);
}

// US -> JP message ID translation. The two message lists are aligned in
// order; the JP list has one extra entry before $16, three US-only entries
// in $17-$2E, and then the long $2F-$183 block is 1:1 with a -2 shift. The
// US game-over pair $184/$185 collapses to JP $182. The $17-$2E segment is
// left as the identity until its three US-only entries are identified.
// Keep this in one place; the message show functions call it only in seed
// mode, so the normal/US path is unchanged.
uint16 SeedOracle_JpMessageId(uint16 us_id) {
  if (!EmuSeedRomActive())
    return us_id;
  if (us_id == 0x16)
    return 0x17;
  if (us_id >= 0x2F && us_id <= 0x183)
    return (uint16)(us_id - 2);
  if (us_id == 0x184 || us_id == 0x185)
    return 0x182;
  return us_id;
}

// JP $9B:F110: the 16-bit overworld tilemap word indexes this byte table,
// and the JP entries above $FF differ from the US asset used by the C engine.
uint8 SeedOracle_JpTileAttribute(uint16 idx) {
  const uint8 *rom = EmuSeedRomData();
  if (!rom)
    return 0;
  return SeedRomRead8(rom, 0x9bf110 + idx);
}

// JP $82:ABD9: the 56-word big-door animation table. The JP map16 ids are
// six lower than the US table at $82:ACDA.
uint16 SeedOracle_JpDoorAnimTile(int idx) {
  const uint8 *rom = EmuSeedRomData();
  if (!rom)
    return 0;
  return SeedRomRead16(rom, 0x82abd9 + idx * 2);
}

static int SeedPopcount40(uint64 mask) {
  int n = 0;
  while (mask) {
    n += (int)(mask & 1);
    mask >>= 1;
  }
  return n;
}

// $8E:FCB2: build the 513-word cumulative text-width table at $7F:5B00 from
// the 512 five-byte character masks at $8E:B844. The JP routine counts the set
// bits of each 40-bit mask and accumulates them (the first entry is zero).
static void SeedOracle_JpTextWidthTable(const uint8 *rom) {
  uint16 *widths = (uint16 *)(g_ram + 0x15B00);
  widths[0] = 0;
  for (int i = 0; i < 512; i++) {
    uint64 mask = 0;
    for (int j = 0; j < 5; j++)
      mask = (mask << 8) | SeedRomRead8(rom, 0x8EB844 + i * 5 + j);
    widths[i + 1] = widths[i] + SeedPopcount40(mask);
  }
}

// $9C:F37A: rebuild the JP dialogue-pointer table at $7F:71C0. Each entry is
// a 16-bit offset into the text data at $9C:8000; the table ends when the
// parser reaches $9C:F356. $9C:F261 holds the per-character advance and
// $9C:F2F9 the extra byte advance after a $FE command.
static void SeedOracle_JpTextPointers(const uint8 *rom) {
  uint8 *table = g_ram + 0x171C0;
  *(uint16 *)table = 0x8000;
  uint16 ptr = 0x8000;
  uint16 index = 0;
  for (;;) {
    uint16 old_ptr = ptr;
    uint8 c = SeedRomRead8(rom, 0x9C0000 + old_ptr);
    uint8 y = c >= 0xF6 ? c : 0xF5;
    ptr += SeedRomRead8(rom, 0x9CF261 + y);
    if (y == 0xFE) {
      uint8 y2 = SeedRomRead8(rom, 0x9C0000 + old_ptr + 1);
      if (SeedRomRead8(rom, 0x9CF2F9 + y2) & 0x80)
        ptr++;
    }
    if (y != 0xFB)
      continue;
    index += 2;
    *(uint16 *)(table + index) = ptr;
    if (ptr == 0xF356)
      break;
  }
}

// The two helpers below are the C equivalents of $9B:EF4B and $9B:EF30. They
// copy palette words from a 24-bit ROM address into the auxiliary palette
// buffer, honoring the $0AA8 destination offset.
static void SeedPaletteLoadMultipleRom(const uint8 *rom, uint32 src, int dst,
                                       int x_ents, int y_pals) {
  for (int row = 0; row <= y_pals; row++) {
    for (int i = 0; i <= x_ents; i++) {
      aux_palette_buffer[((dst + overworld_palette_aux_or_main) >> 1) + i] =
          SeedRomRead16(rom, src);
      src += 2;
    }
    dst += 0x20;
  }
}

static void SeedPaletteLoadSingleRom(const uint8 *rom, uint32 src, int dst,
                                     int x_ents) {
  for (int i = 0; i <= x_ents; i++) {
    aux_palette_buffer[((dst + overworld_palette_aux_or_main) >> 1) + i] =
        SeedRomRead16(rom, src);
    src += 2;
  }
}

// $82:C322: JP 1.0 counterpart of the US Overworld_LoadAllPalettes. It clears
// the palette buffers, sets the palette-mode variables from the JP
// initialization at $82:8022, and loads the JP palette tables. The OWBG and
// font palette data differ from the US assets, so the tables are read directly
// from the seed ROM instead of g_assets.
static void SeedOracle_JpFileSelectPaletteInit(const uint8 *rom) {
  // Zero $7E:C480 (64 words), then $7E:C500/$C580/$C600/$C680 (256 words).
  memset(aux_palette_buffer + 0xC0, 0, 0x80);
  memset(main_palette_buffer, 0, 0x200);

  // $8C:FF64
  main_palette_buffer[0] = 0;
  main_palette_buffer[0x20] = 0;
  aux_palette_buffer[0] = 0;
  aux_palette_buffer[0x20] = 0;
  COLDATA_copy0 = 0x20;
  COLDATA_copy1 = 0x40;
  COLDATA_copy2 = 0x80;

  overworld_palette_mode = 4;
  overworld_palette_aux1_bp2to4_hi = 3;
  overworld_palette_aux2_bp5to7_hi = 3;
  overworld_palette_aux3_bp7_lo = 3;
  palette_sp6r_indoors = 5;
  g_ram[0xAA9] = 0;

  // $9B:EC9E Palette_Load_SpriteMain
  uint16 off = SeedRomRead16(rom, 0x9BEC73 + ((overworld_screen_index & 0x40) ? 2 : 0));
  SeedPaletteLoadMultipleRom(rom, 0x9B0000 + 0xD218 + off, 0x122, 14, 3);

  // $9B:EEC7 Palette_Load_OWBGMain
  off = SeedRomRead16(rom, 0x9BEC3B + overworld_palette_mode * 2);
  SeedPaletteLoadMultipleRom(rom, 0x9B0000 + 0xE6C8 + off, 0x42, 6, 4);

  // $9B:EEE8 Palette_Load_OWBG1
  off = SeedRomRead16(rom, 0x9BEC13 + overworld_palette_aux1_bp2to4_hi * 2);
  SeedPaletteLoadMultipleRom(rom, 0x9B0000 + 0xE86C + off, 0x52, 6, 2);

  // $9B:EF0C Palette_Load_OWBG2
  off = SeedRomRead16(rom, 0x9BEC13 + overworld_palette_aux2_bp5to7_hi * 2);
  SeedPaletteLoadMultipleRom(rom, 0x9B0000 + 0xE86C + off, 0xB2, 6, 2);

  // $9B:EEA8 Palette_Load_OWBG3
  off = SeedRomRead8(rom, 0x9BEBC6 + overworld_palette_aux3_bp7_lo);
  SeedPaletteLoadSingleRom(rom, 0x9B0000 + 0xE604 + off, 0xE2, 6);

  // $9B:ED72 Palette_Load_SpriteEnvironment_Dungeon
  off = SeedRomRead8(rom, 0x9BEBC6 + palette_sp6r_indoors);
  SeedPaletteLoadSingleRom(rom, 0x9B0000 + 0xD446 + off, 0x1D2, 6);

  // $9B:EE52 Palette_Load_HUD
  off = SeedRomRead8(rom, 0x9BEC47 + hud_palette);
  SeedPaletteLoadMultipleRom(rom, 0x9B0000 + 0xD660 + off, 0x0, 15, 1);

  // Copy the JP font palette words $7E:C4D0..$C4DF to $7E:C6B0..$C6BF.
  memcpy(main_palette_buffer + 0xD8, aux_palette_buffer + 0xE8, 0x10);
}

// $82:8022 is the JP 1.0 counterpart of the routine the US engine implements
// as Overworld_LoadAllPalettes. It builds the text-width table at $8E:FCB2,
// updates the text pointers at $9C:F37A and rebuilds the palettes at $82:C322.
void SeedOracle_JpIntroPaletteInit(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  SeedOracle_JpTextWidthTable(rom);
  SeedOracle_JpTextPointers(rom);
  SeedOracle_JpFileSelectPaletteInit(rom);
}

// $80:E529 LoadFileSelectGraphics, including the z3randomizer hooks at
// $80:E55D/$E568 (upload the WRAM tilemap at $7E:2000 to VRAM $2000) and
// $80:E581 (LoadFileSelectVanillaItems: upload $7F:0000 to VRAM $2F00).
void SeedOracle_JpLoadFileSelectGraphics(void) {
  if (!EmuSeedRomActive())
    return;
  Decomp_spr(decomp_buffer, 0x5e);
  Do3To4High(&g_zenv.vram[0x5000], decomp_buffer);
  Decomp_spr(decomp_buffer, 0x5f);
  Do3To4High(&g_zenv.vram[0x5400], decomp_buffer);
  memcpy(&g_zenv.vram[0x2000], g_ram + 0x2000, 0xF00 * sizeof(uint16));
  // The z3randomizer hook at $A4:96C4 DMAs the $7F:4000 scratch to VRAM
  // $2F00; the JP InitTilesets leaves the last aux pack (aux0) there.
  const uint8 *rom = EmuSeedRomData();
  Decompress(decomp_buffer, rom + SeedOracle_BgPc(rom, aux_bg_subset_0));
  memcpy(&g_zenv.vram[0x2F00], decomp_buffer, 0x600);
}

// $80:E43A Graphics_LoadChrHalfSlot: expand one JP 3bpp tile pack into
// $7F:1000/$7F:1010 and let NMI subroutine 11 upload it to VRAM at
// nmi_load_target_addr*256. This is the JP variant used by the intro; the
// source is a raw pack selected by the tables at $80:E411/$80:E425.
void SeedOracle_JpLoadChrHalfSlot(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  uint8 k = load_chr_halfslot_even_odd;
  if (k == 0)
    return;  // $80:E411 holds an RTL for this case.

  uint8 palette = SeedRomRead8(rom, 0x80E425 + k);
  if (!(palette & 0x80)) {
    palette_sp6r_indoors = palette;
    g_ram[0xAA9] = 2;
    if (k == 1) {
      palette_sp6r_indoors = 0x0A;
      Palette_Load_SpriteEnvironment();
    } else {
      Palette_Load_SpriteEnvironment_Dungeon();
    }
    flag_update_cgram_in_nmi++;
  }

  uint8 yv = 0x44;
  g_ram[0x08] = 0;
  g_ram[0x09] = 0;
  uint8 next = (uint8)(k + 1);
  load_chr_halfslot_even_odd = next;
  if (next & 1) {
    load_chr_halfslot_even_odd = 0;
    if (k != 0x12) {
      g_ram[0x09] = 3;
      yv = 0x46;
      if (k == 2)
        flag_custom_spell_anim_active = 0;
    }
  }
  nmi_load_target_addr = yv;
  nmi_subroutine_index = 0x0B;

  uint8 pack = SeedRomRead8(rom, 0x80E411 + k);
  if (pack == 1)
    pack = misc_sprites_graphics_index;

  uint8 bank = SeedRomRead8(rom, 0x80D033 + pack);
  uint8 hi = SeedRomRead8(rom, 0x80D112 + pack);
  uint8 lo = SeedRomRead8(rom, 0x80D1F1 + pack);
  uint16 p0 = (uint16)(lo | hi << 8);
  uint8 bank0 = bank, bank3 = bank;
  g_ram[0x00] = (uint8)p0;
  g_ram[0x01] = (uint8)(p0 >> 8);
  g_ram[0x02] = bank0;
  g_ram[0x05] = bank3;

  uint16 p3 = (uint16)(p0 + 0x10);
  if (p3 == 0) {
    p3 = 0x8000;
    bank3++;
  }
  g_ram[0x03] = (uint8)p3;
  g_ram[0x04] = (uint8)(p3 >> 8);

  uint16 x = 0;
  g_ram[0x0C] = 0x20;
  g_ram[0x0D] = 0;
  uint16 add = (uint16)(g_ram[0x08] | g_ram[0x09] << 8);
  for (int row = 0; row < 0x20; row++) {
    // The first row applies the initial $08/$09 offset. Later rows start at
    // the second pointer left by the previous row ($80:E51F -> $80:E4C1).
    p0 = row == 0 ? (uint16)(p0 + add) : p3;
    g_ram[0x00] = (uint8)p0;
    g_ram[0x01] = (uint8)(p0 >> 8);
    p3 = (uint16)(p0 + 0x10);
    if (p3 == 0) {
      p3 = 0x8000;
      bank3++;
    }
    g_ram[0x03] = (uint8)p3;
    g_ram[0x04] = (uint8)(p3 >> 8);
    g_ram[0x05] = bank3;

    for (int i = 7; i >= 0; i--) {
      uint16 v = SeedRomRead16(rom, ((uint32)bank0 << 16) | p0);
      *(uint16 *)(g_ram + 0x11000 + x) = v;
      uint8 tmp = (uint8)((v >> 8) | (v & 0xFF));
      g_ram[0x08] = tmp;
      g_ram[0x09] = 0;  // STA $08 is a 16-bit store.
      p0 = (uint16)(p0 + 2);
      if (p0 == 0) {
        p0 = (uint16)(p3 + 1);
        bank0++;
        bank3 = bank0;
        g_ram[0x00] = (uint8)p0;
        g_ram[0x01] = (uint8)(p0 >> 8);
        g_ram[0x02] = bank0;
        g_ram[0x05] = bank3;
      } else {
        g_ram[0x00] = (uint8)p0;
        g_ram[0x01] = (uint8)(p0 >> 8);
      }

      uint16 w = SeedRomRead16(rom, ((uint32)bank3 << 16) | p3);
      uint8 wlo = (uint8)w;
      g_ram[0x0A] = wlo;
      g_ram[0x0B] = 0;  // STA $0A is a 16-bit store.
      *(uint16 *)(g_ram + 0x11010 + x) =
          (uint16)(((wlo | tmp) << 8) | wlo);
      p3 = (uint16)(p3 + 1);
      if (p3 == 0) {
        p0 = 0x8000;
        p3 = 0x8010;
        bank0++;
        bank3++;
        g_ram[0x00] = (uint8)p0;
        g_ram[0x01] = (uint8)(p0 >> 8);
        g_ram[0x02] = bank0;
        g_ram[0x03] = (uint8)p3;
        g_ram[0x04] = (uint8)(p3 >> 8);
        g_ram[0x05] = bank3;
      } else {
        g_ram[0x03] = (uint8)p3;
        g_ram[0x04] = (uint8)(p3 >> 8);
      }
      x = (uint16)(x + 2);
    }
    x = (uint16)(x + 0x10);
    g_ram[0x0C]--;
  }
}

// Decompress a JP graphics pack directly from the seed ROM. The JP pointer
// tables are the same ones used by $80:E7A6/$E7C3.
static uint32 SeedOracle_SpritePc(const uint8 *rom, int gfx) {
  uint32 src = ((uint32)SeedRomRead8(rom, 0x80D033 + gfx) << 16) |
               ((uint32)SeedRomRead8(rom, 0x80D112 + gfx) << 8) |
               SeedRomRead8(rom, 0x80D1F1 + gfx);
  return (((src >> 16) << 15) | (src & 0x7fff)) & 0x1fffff;
}

static uint32 SeedOracle_BgPc(const uint8 *rom, int gfx) {
  uint32 src = ((uint32)SeedRomRead8(rom, 0x80CFC0 + gfx) << 16) |
               ((uint32)SeedRomRead8(rom, 0x80D09F + gfx) << 8) |
               SeedRomRead8(rom, 0x80D17E + gfx);
  return (((src >> 16) << 15) | (src & 0x7fff)) & 0x1fffff;
}

static void SeedOracle_LoadRawSpriteGraphics(int vram_word, int gfx) {
  const uint8 *rom = EmuSeedRomData();
  Do3To4High(&g_zenv.vram[vram_word], rom + SeedOracle_SpritePc(rom, gfx));
}

static void SeedOracle_LoadSpriteGraphics(int vram_word, int gfx, uint8 *buf) {
  const uint8 *rom = EmuSeedRomData();
  Decompress(buf, rom + SeedOracle_SpritePc(rom, gfx));
  if (gfx == 0x52 || gfx == 0x53 || gfx == 0x5a || gfx == 0x5b ||
      gfx == 0x5c || gfx == 0x5e || gfx == 0x5f)
    Do3To4High(&g_zenv.vram[vram_word], buf);
  else
    Do3To4Low(&g_zenv.vram[vram_word], buf);
}

static void SeedOracle_LoadBackgroundGraphics(int vram_word, int gfx, int slot,
                                              uint8 *buf) {
  const uint8 *rom = EmuSeedRomData();
  Decompress(buf, rom + SeedOracle_BgPc(rom, gfx));
  if ((main_tile_theme_index >= 0x20)
          ? (slot == 7 || slot == 2 || slot == 3 || slot == 4)
          : (slot >= 4))
    Do3To4High(&g_zenv.vram[vram_word], buf);
  else
    Do3To4Low(&g_zenv.vram[vram_word], buf);
}

// $80:E6F7 LoadCommonSprites JP.
void SeedOracle_JpLoadCommonSprites(void) {
  if (!EmuSeedRomActive()) {
    LoadCommonSprites();
    return;
  }
  const uint8 *rom = EmuSeedRomData();
  SeedOracle_LoadRawSpriteGraphics(0x4400, misc_sprites_graphics_index);
  if (main_module_index != 1) {
    Do3To4Low(&g_zenv.vram[0x4800], rom + SeedOracle_SpritePc(rom, 6));
    Do3To4Low(&g_zenv.vram[0x4c00], rom + SeedOracle_SpritePc(rom, 7));
  } else {
    SeedOracle_LoadSpriteGraphics(0x4800, 0x5e, decomp_buffer);
    SeedOracle_LoadSpriteGraphics(0x4c00, 0x5f, decomp_buffer);
  }
}

// $80:E310 LoadDefaultGraphics JP: pack 0 is expanded raw to VRAM $4000 and
// the three 2bpp HUD/font packs go to $7000/$7400/$7800.
void SeedOracle_JpLoadDefaultGraphics(void) {
  if (!EmuSeedRomActive()) {
    LoadDefaultGraphics();
    return;
  }
  const uint8 *rom = EmuSeedRomData();
  const uint8 *src = rom + SeedOracle_SpritePc(rom, 0);
  uint16 *vram_ptr = &g_zenv.vram[0x4000];
  uint16 *tmp = (uint16 *)gfx_expand_temp_row;
  for (int num = 64; num; num--) {
    for (int i = 7; i >= 0; i--, src += 2) {
      *vram_ptr++ = (uint16)(src[0] | src[1] << 8);
      tmp[i] = (uint16)(src[0] | src[1]);
    }
    for (int i = 7; i >= 0; i--, src++)
      *vram_ptr++ = (uint16)(src[0] | (src[0] | tmp[i]) << 8);
  }
  Decompress(decomp_buffer, rom + SeedOracle_SpritePc(rom, 0x6a));
  memcpy(&g_zenv.vram[0x7000], decomp_buffer, 0x800);
  Decompress(decomp_buffer, rom + SeedOracle_SpritePc(rom, 0x6b));
  memcpy(&g_zenv.vram[0x7400], decomp_buffer, 0x800);
  Decompress(decomp_buffer, rom + SeedOracle_SpritePc(rom, 0x69));
  memcpy(&g_zenv.vram[0x7800], decomp_buffer, 0x800);
}

// $80:882E reads the map8->tile-attribute table at $8F:FD94. The asset in
// this build already matches the seed for the 512 used entries, but read the
// seed ROM directly so the table never depends on the US asset.
const uint8 *SeedOracle_JpMap8TileAttrTable(void) {
  static uint8 table[512];
  static bool loaded;
  if (!EmuSeedRomActive())
    return table;
  if (!loaded) {
    const uint8 *rom = EmuSeedRomData();
    for (int i = 0; i < 512; i++)
      table[i] = SeedRomRead8(rom, 0x8FFD94 + i);
    loaded = true;
  }
  return table;
}

// $A0:CC96 PreOverworld_LoadProperties_ChooseMusic: the JP overworld song
// selection. The screen index ($8A), progress ($7EF3C5), master-sword flag
// ($00 & $40), dark world ($7EF3CA) and moon pearl ($7EF357) choose the
// queued track; the seed ROM also forces screen 18 to track 7.
uint8 SeedOracle_JpOverworldMusic(void) {
  if (!EmuSeedRomActive())
    return 0;
  uint8 sc = BYTE(overworld_screen_index);
  uint8 x;
  if ((sc | 0x40) == 0x43 || (sc | 0x40) == 0x45 || (sc | 0x40) == 0x47)
    x = 2;
  else if (sc >= 0x40)
    x = sram_progress_indicator < 2 ? 3 : 2;
  else if (sc == 0x18)
    x = 7;
  else if (sc == 0)
    x = (savegame_has_master_sword_flags & 0x40) ? 2 : 5;
  else
    x = sram_progress_indicator < 2 ? 3 : 2;

  if (savegame_is_darkworld) {
    if (sc == 0x40)
      x = 0x0f;
    else if (sc == 0x43 || sc == 0x45 || sc == 0x47)
      x = 0x0d;
    else
      x = 9;
    if (!link_item_moon_pearl)
      x = 4;
  }
  if (queued_music_control == 0xf2 && x == music_unk1)
    x = 0xf3;
  return x;
}

// $80:D6AE LoadTransAuxGFX JP: the aux packs and the sprite packs come from
// the JP tables at $80:DDD7/$80:DB97 and are decompressed from the seed ROM
// into $7E:6000/$6600/$6C00/$7200 and $7E:7800/$7E00/$8400/$8A00.
void SeedOracle_JpLoadTransAuxGFX(void) {
  if (!EmuSeedRomActive()) {
    LoadTransAuxGFX();
    return;
  }
  const uint8 *rom = EmuSeedRomData();
  uint8 *dst = aux_bg_gfx_buffer_0;
  uint8 *subsets[4] = {
    &aux_bg_subset_0, &aux_bg_subset_1, &aux_bg_subset_2, &aux_bg_subset_3,
  };
  int x = aux_tile_theme_index * 4;
  for (int i = 0; i < 4; i++) {
    uint8 v = SeedRomRead8(rom, 0x80DDD7 + x + i);
    if (!v)
      continue;
    *subsets[i] = v;
    int len = Decompress(dst + i * 0x600, rom + SeedOracle_BgPc(rom, v));
    assert(len == 0x600);
  }

  dst += 0x600 * 4;  // $7E:7800
  uint8 *sprite_subsets[4] = {
    &sprite_gfx_subset_0, &sprite_gfx_subset_1,
    &sprite_gfx_subset_2, &sprite_gfx_subset_3,
  };
  x = sprite_graphics_index * 4;
  for (int i = 0; i < 4; i++) {
    uint8 v = SeedRomRead8(rom, 0x80DB97 + x + i);
    if (v)
      *sprite_subsets[i] = v;
    int len = Decompress(dst + i * 0x600,
                         rom + SeedOracle_SpritePc(rom, *sprite_subsets[i]));
    assert(len == 0x600);
  }
  incremental_counter_for_vram = 0;
}

// $80:E1DB InitializeTilesets JP. The common sprites and sprite sheets match
// the US path; only the background pack table comes from the JP ROM.
void SeedOracle_JpInitializeTilesets(void) {
  if (!EmuSeedRomActive()) {
    InitializeTilesets();
    return;
  }
  uint8 saved_subsets[4] = {
    sprite_gfx_subset_0, sprite_gfx_subset_1,
    sprite_gfx_subset_2, sprite_gfx_subset_3,
  };
  uint8 saved_sprites[0x1800];
  memcpy(saved_sprites, sprite_gfx_buffer_0, sizeof(saved_sprites));
  InitializeTilesets();
  memcpy(sprite_gfx_buffer_0, saved_sprites, sizeof(saved_sprites));

  const uint8 *rom = EmuSeedRomData();
  SeedOracle_LoadRawSpriteGraphics(0x4400, misc_sprites_graphics_index);

  // JP sprite sheets keep the previous subset when the table byte is zero.
  uint8 *subsets[4] = {
    &sprite_gfx_subset_0, &sprite_gfx_subset_1,
    &sprite_gfx_subset_2, &sprite_gfx_subset_3,
  };
  uint8 *sprite_buffers[4] = {
    sprite_gfx_buffer_0, sprite_gfx_buffer_1,
    sprite_gfx_buffer_2, sprite_gfx_buffer_3,
  };
  int sy = sprite_graphics_index * 4;
  for (int i = 0; i < 4; i++) {
    *subsets[i] = saved_subsets[i];
    uint8 v = SeedRomRead8(rom, 0x80DB97 + sy + i);
    if (v)
      *subsets[i] = v;
    SeedOracle_LoadSpriteGraphics(0x5000 + i * 0x400, *subsets[i],
                                  sprite_buffers[i]);
  }

  // JP background loads: main layers use $7F:4000 as scratch, while the aux
  // layers decompress directly into $7E:6000/$6600/$6C00/$7200.
  int y = main_tile_theme_index * 8;
  int x = aux_tile_theme_index * 4;
  uint8 main[8], aux[4];
  for (int i = 0; i < 8; i++)
    main[i] = SeedRomRead8(rom, 0x80E0B3 + y + i);
  for (int i = 0; i < 4; i++)
    aux[i] = SeedRomRead8(rom, 0x80DDD7 + x + i);
  aux_bg_subset_0 = aux[0] ? aux[0] : main[3];
  aux_bg_subset_1 = aux[1] ? aux[1] : main[4];
  aux_bg_subset_2 = aux[2] ? aux[2] : main[5];
  aux_bg_subset_3 = aux[3] ? aux[3] : main[6];

  SeedOracle_LoadBackgroundGraphics(0x2000, main[0], 7, decomp_buffer);
  SeedOracle_LoadBackgroundGraphics(0x2400, main[1], 6, decomp_buffer);
  SeedOracle_LoadBackgroundGraphics(0x2800, main[2], 5, decomp_buffer);
  SeedOracle_LoadBackgroundGraphics(0x2c00, aux_bg_subset_0, 4, aux_bg_gfx_buffer_0);
  SeedOracle_LoadBackgroundGraphics(0x3000, aux_bg_subset_1, 3, aux_bg_gfx_buffer_1);
  SeedOracle_LoadBackgroundGraphics(0x3400, aux_bg_subset_2, 2, aux_bg_gfx_buffer_2);
  SeedOracle_LoadBackgroundGraphics(0x3800, aux_bg_subset_3, 1, aux_bg_gfx_buffer_3);
  SeedOracle_LoadBackgroundGraphics(0x3c00, main[7], 0, decomp_buffer);
}

// $80:D377 DecompressAnimatedDungeonTiles JP.
void SeedOracle_JpDecompressAnimatedDungeonTiles(uint8 a) {
  if (!EmuSeedRomActive()) {
    DecompressAnimatedDungeonTiles(a);
    return;
  }
  const uint8 *rom = EmuSeedRomData();
  Decompress(decomp_buffer, rom + SeedOracle_BgPc(rom, a));
  Do3To4Low16Bit(gfx_4bpp_buffer + 0x1680, decomp_buffer, 48);
  Decompress(decomp_buffer, rom + SeedOracle_BgPc(rom, 0x5c));
  Do3To4Low16Bit(gfx_4bpp_buffer + 0x1C80, decomp_buffer, 48);
  for (int i = 0; i < 256; i++) {
    uint8 *p = gfx_4bpp_buffer + i * 2;
    uint16 v = WORD(p[0x1880]);
    WORD(p[0x1880]) = WORD(p[0x1C80]);
    WORD(p[0x1C80]) = WORD(p[0x1E80]);
    WORD(p[0x1E80]) = WORD(p[0x1A80]);
    WORD(p[0x1A80]) = v;
  }
  animated_tile_vram_addr = 0x3b00;
}

// $A4:C884: split a decimal-backed z3randomizer value into the nibbles stored
// at $7F:5003..$7F:5007. Values below 1000 use the table at $A4:C91D directly;
// larger values split into quotient/remainder base 100 first.
static void SeedOracle_JpSplitNumber(const uint8 *rom, uint16 value) {
  uint16 v;
  if (value < 1000) {
    v = SeedRomRead16(rom, 0xA4C91D + value * 2);
    g_ram[0x15007] = v & 0x0F;
    g_ram[0x15006] = (v >> 4) & 0x0F;
    g_ram[0x15005] = (v >> 8) & 0x0F;
    *(uint16 *)(g_ram + 0x15003) = 0;
    return;
  }
  uint16 rem = value % 100;
  uint16 quot = value / 100;
  v = SeedRomRead16(rom, 0xA4C91D + rem * 2);
  g_ram[0x15007] = v & 0x0F;
  g_ram[0x15006] = (v >> 4) & 0x0F;
  v = SeedRomRead16(rom, 0xA4C91D + quot * 2);
  g_ram[0x15005] = v & 0x0F;
  g_ram[0x15004] = (v >> 4) & 0x0F;
  g_ram[0x15003] = (v >> 8) & 0x0F;
}

// $A4:B0A9 / $A4:B0CD: publish the z3randomizer file-select digits as tile
// values and return them through direct-page $04..$07.
static void SeedOracle_JpPublishFileSelectDigits(const uint8 *rom) {
  uint16 value = SeedRomRead16(rom, 0xB08196);
  SeedOracle_JpSplitNumber(rom, value);
  *(uint16 *)(g_ram + 0x04) = *(uint16 *)(g_ram + 0x15004) | 0x9090;
  *(uint16 *)(g_ram + 0x06) = *(uint16 *)(g_ram + 0x15006) | 0x9090;
}

// $A1:FF87: z3randomizer follower-graphics path. It initializes its file
// select tables, converts the custom graphics packs, copies the JP font
// digits and then falls through to $80:D463 (the C LoadFollowerGraphics).
void SeedOracle_JpLoadFollowerGraphics(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();

  // $A4:C261: clear the z3randomizer input/state scratch, then bail out at
  // the SNES-port check used by its controller protocol.
  memset(g_ram + 0x1E6B, 0, 6);
  memset(g_ram + 0x1E7D, 0, 4);

  // $A0:A8B5: copy the 32-byte file-select number table to $7F:5200.
  for (int i = 0; i < 0x20; i += 2)
    *(uint16 *)(g_ram + 0x15200 + i) = SeedRomRead16(rom, 0xA0A8EC + i);

  // $A2:CACA: decompress packs $CD..$D0 into $7F:4000 and expand each 0x600
  // byte block to $7F:8000+$0018, appending 0x800 bytes per block.
  *(uint16 *)(g_ram + 0x1218) = 0;
  for (int x = 0xD0; x >= 0xCD; x--) {
    uint8 bank = SeedRomRead8(rom, 0x80CFC0 + x);
    uint8 hi = SeedRomRead8(rom, 0x80D09F + x);
    uint8 lo = SeedRomRead8(rom, 0x80D17E + x);
    uint32 src = ((uint32)bank << 16) | (hi << 8) | lo;
    uint32 pc = (((src >> 16) << 15) | (src & 0x7fff)) & 0x1fffff;
    Decompress(g_ram + 0x14000, rom + pc);
    uint16 off = *(uint16 *)(g_ram + 0x1218);
    Do3To4High16Bit(g_ram + 0x18000 + off, g_ram + 0x14000, 0x40);
    *(uint16 *)(g_ram + 0x1218) = off + 0x800;
  }
  // The JP routine runs with DP=$1200 and SP=$1400; these are the scratch
  // bytes left in RAM afterwards. Keep them for frame-parity.
  g_ram[0x1200] = 0x32;
  g_ram[0x1201] = 0xF3;
  g_ram[0x1202] = 0xE0;
  g_ram[0x1204] = 0x07;
  g_ram[0x121A] = 0x29;
  g_ram[0x121B] = 0xF3;
  g_ram[0x13FB] = 0xD2;
  g_ram[0x13FC] = 0x7F;
  g_ram[0x13FD] = 0xF4;
  g_ram[0x13FE] = 0xCA;
  g_ram[0x13FF] = 0xF2;
  g_ram[0x1400] = 0x01;

  // $A1:FF9A: copy the two 16-byte JP font rows and publish the decimal
  // digits through $CF00..$CF06.
  for (int i = 0; i < 16; i++) {
    g_ram[0x15410 + i] = SeedRomRead8(rom, 0xB0F000 + i);
    g_ram[0x15420 + i] = SeedRomRead8(rom, 0xB0F010 + i);
  }
  SeedOracle_JpPublishFileSelectDigits(rom);
  *(uint16 *)(g_ram + 0xCF00) = (uint16)(0x2400 | g_ram[0x04]);
  *(uint16 *)(g_ram + 0xCF02) = (uint16)(0x2400 | g_ram[0x05]);
  *(uint16 *)(g_ram + 0xCF04) = (uint16)(0x2400 | g_ram[0x06]);
  *(uint16 *)(g_ram + 0xCF06) = (uint16)(0x2400 | g_ram[0x07]);

  // $80:D463
  LoadFollowerGraphics();
}

// $8E:F4FB: expand one character mask into the $7E:BFC0 glyph buffer. The mask
// is read either from the JP TextCharMasks table ($8E:B844) or, for the 87
// custom glyphs flagged in the z3randomizer table at $A4:D247, from $B3:9C00
// or $B3:AC00.
static void SeedOracle_JpBuildGlyph(const uint8 *rom, int ch, uint8 *buf) {
  memset(buf, 0, 0x32);
  uint16 flag = SeedRomRead16(rom, 0xA4D247 + ch * 4);
  uint16 custom = SeedRomRead16(rom, 0xA4D249 + ch * 4);
  if (flag & 1) {
    uint32 base = *(uint16 *)(g_ram + 0x0C) ? 0xB3AC00 : 0xB39C00;
    for (int i = 0; i < 8; i++) {
      *(uint16 *)(buf + i * 2) = SeedRomRead16(rom, base + custom + i * 2);
      *(uint16 *)(buf + 0x16 + i * 2) =
          SeedRomRead16(rom, base + 0x100 + custom + i * 2);
    }
    return;
  }
  uint16 width = ((uint16 *)(g_ram + 0x15B00))[ch];
  uint64 mask = 0;
  for (int i = 0; i < 5; i++)
    mask = (mask << 8) | SeedRomRead8(rom, 0x8EB844 + ch * 5 + i);
  mask <<= 8;  // $0D starts with its low byte zero.
  int x = 3;
  int y = 0;
  while (mask) {
    x++;
    int carry = (mask >> 47) & 1;
    mask = (mask << 1) & 0xffffffffffffULL;
    if (carry)
      buf[x] = SeedRomRead8(rom, 0x0E8000 + width + y++);
  }
}

// $8E:F5BC: combine the glyph bytes in $7E:BFC0 into the two 8-word bitplane
// rows at $7F:0000/$7F:0020 used by the JP file-select tilemap upload.
static void SeedOracle_JpGlyphToRows(uint8 *buf) {
  for (int i = 0; i < 8; i++) {
    *(uint16 *)(g_ram + 0x10000 + i * 2) = *(uint16 *)(buf + i * 2);
    *(uint16 *)(g_ram + 0x10020 + i * 2) = *(uint16 *)(buf + 0x16 + i * 2);
  }
  memset(g_ram + 0x10010, 0, 0x10);
  memset(g_ram + 0x10030, 0, 0x10);

  int y = 0x10 + 5;
  for (int pass = 2; pass >= 0; pass--) {
    uint8 a = buf[y];
    for (int x = 0x0E; x >= 0; x -= 2) {
      int carry = a & 1;
      a >>= 1;
      uint8 d = g_ram[0x10011 + x];
      g_ram[0x10011 + x] = (uint8)((d >> 1) | (carry << 7));
    }
    y--;
    a = buf[y];
    for (int x = 0x0E; x >= 0; x -= 2) {
      int carry = a & 1;
      a >>= 1;
      uint8 d = g_ram[0x10010 + x];
      g_ram[0x10010 + x] = (uint8)((d >> 1) | (carry << 7));
    }
    y--;
  }
  y += 6;
  for (int pass = 2; pass >= 0; pass--) {
    uint8 a = buf[0x16 + y];
    for (int x = 0x0E; x >= 0; x -= 2) {
      int carry = a & 1;
      a >>= 1;
      uint8 d = g_ram[0x10031 + x];
      g_ram[0x10031 + x] = (uint8)((d >> 1) | (carry << 7));
    }
    y--;
    a = buf[0x16 + y];
    for (int x = 0x0E; x >= 0; x -= 2) {
      int carry = a & 1;
      a >>= 1;
      uint8 d = g_ram[0x10030 + x];
      g_ram[0x10030 + x] = (uint8)((d >> 1) | (carry << 7));
    }
    y--;
  }

  for (int x = 0x0F; x >= 0; x--) {
    int carry = (g_ram[0x10010 + x] >> 7) & 1;
    g_ram[0x10000 + x] = (uint8)((g_ram[0x10000 + x] << 1) | carry);
    carry = (g_ram[0x10030 + x] >> 7) & 1;
    g_ram[0x10020 + x] = (uint8)((g_ram[0x10020 + x] << 1) | carry);
  }
}

// $A0:B468: read a byte from the current message buffer ($7F:1200), or from
// the z3r custom-text buffer ($7F:5700) when $7F:5035 is set.
static uint8 SeedJpTextRead8(uint16 pos) {
  if (g_ram[0x15035])
    return g_ram[0x15700 + pos];
  return g_ram[0x11200 + pos];
}

static uint16 SeedJpTextRead16(uint16 pos) {
  return (uint16)SeedJpTextRead8(pos) |
         (uint16)SeedJpTextRead8((uint16)(pos + 1)) << 8;
}

// $8E:F479: masks used when the VWF glyph is shifted by the fractional bit
// position in $1CE6. Index 8 means "replace", 0 means "OR only".
static const uint8 kSeedJpGlyphMasks[9] = {
  0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00,
};

// $A4:D15D: choose the blend mask for the left/right glyph pass. ch4 is the
// value the ROM keeps in $09 (character * 4, plus one for the right pass).
static uint8 SeedJpGlyphMask(const uint8 *rom, int ch4) {
  uint16 w0 = SeedRomRead16(rom, 0xA4D247 + (ch4 & 0xFFFE));
  int y;
  if (w0 & 0x8000) {
    uint16 a = swap16(w0);
    if ((a & 0x004F) == 0) {
      y = SeedRomRead8(rom, 0xA4D1E2 + (ch4 & 3));
    } else {
      y = a & 0x0F;
      if ((ch4 & 3) != 0)
        y = SeedRomRead8(rom, 0xA4D1E2 + (ch4 & 3));
    }
  } else {
    y = SeedRomRead8(rom, 0x8EFD7C + (ch4 & 3));
  }
  if (y > 8)
    y = 8;
  return kSeedJpGlyphMasks[y];
}

// $A4:D1B3: character advance from the width table. Vanilla characters use
// the low byte of $8E:FD7C[ch4 & 3] (8 pixels); the z3r table overrides 87
// glyphs through $A4:D247/$A4:D1E2.
static uint16 SeedJpGlyphWidth(const uint8 *rom, int ch4) {
  uint16 w0 = SeedRomRead16(rom, 0xA4D247 + (ch4 & 0xFFFE));
  if (w0 & 0x8000) {
    uint16 a = swap16(w0);
    if ((a & 0x004F) == 0)
      return SeedRomRead16(rom, 0xA4D1E2 + (ch4 & 3));
    if ((ch4 & 3) == 0)
      return a & 0x000F;
    return SeedRomRead16(rom, 0xA4D1E2 + (ch4 & 3));
  }
  return SeedRomRead16(rom, 0x8EFD7C + (ch4 & 3));
}

static void SeedJpAdvanceWidth(const uint8 *rom, int ch4) {
  uint16 add = (uint16)(SeedJpGlyphWidth(rom, ch4) + text_next_position);
  text_next_position = add & 7;
  dialogue_msg_src_offs += (uint8)(add & 0xFF) >> 3;
}

// $8E:F482: blend 16 bytes of one 8x8 glyph half into the two bitplanes at
// $7F:0000+x / $7F:0010+x, shifting by $1CE6 fractional bits.
static void SeedJpBlendGlyph(const uint8 *rom, int dst, const uint8 *src,
                             int src_index, int ch4) {
  uint8 mask = SeedJpGlyphMask(rom, ch4);
  int shift = text_next_position & 7;
  int x = dst;
  for (int i = 0; i < 16; i++, x++, src_index++) {
    // ROR $00/$01 and ROR $02/$03 are two 16-bit rotates; the data pair
    // starts as $02=source, $03=0 and $00=mask, $01=$FF.
    uint16 m = (uint16)(mask | 0xFF00);
    uint16 d = src[src_index];
    for (int j = 0; j < shift; j++) {
      m = (uint16)((m >> 1) | 0x8000);
      d = (uint16)((d >> 1) | ((d & 1) << 15));
    }
    g_ram[0x10000 + x] = (uint8)((g_ram[0x10000 + x] & (m & 0xFF)) | (d & 0xFF));
    g_ram[0x10010 + x] = (uint8)((g_ram[0x10010 + x] & (m >> 8)) | (d >> 8));
  }
}

static void SeedJpRor8(uint8 *t, int idx, int carry) {
  uint8 v = t[idx];
  t[idx] = (uint8)((v >> 1) | (carry << 7));
}

// $8E:F3E5: transpose six glyph bytes into the right-half source at
// $7F:1F80, then blend that half with the second mask.
static void SeedJpExpandGlyph(const uint8 *rom, int dst, int src_index, int ch4) {
  uint8 t[17];
  memset(t, 0, sizeof(t));
  const uint8 *src = g_ram + 0xBFC0;
  int y = src_index + 5;
  for (int pass = 0; pass < 3; pass++) {
    uint8 a = src[y--];
    for (int i = 0; i < 8; i++) {
      int c = a & 1;
      a >>= 1;
      SeedJpRor8(t, 0x0F - i * 2, c);
    }
    a = src[y--];
    for (int i = 0; i < 8; i++) {
      int c = a & 1;
      a >>= 1;
      SeedJpRor8(t, 0x0E - i * 2, c);
    }
  }
  memcpy(g_ram + 0x11F80, t, 16);
  SeedJpBlendGlyph(rom, dst, g_ram + 0x11F80, 0, ch4);
}

// $8E:F2D7: draw one message character (four 8x8 halves) and advance the
// source and destination cursors.
static void SeedJpDrawChar(void) {
  const uint8 *rom = EmuSeedRomData();
  uint16 w = SeedJpTextRead16(dialogue_msg_read_pos);
  if (w != 0xFF00)
    sound_effect_2 = 0x0C;
  int x = dialogue_msg_src_offs * 2;
  uint8 color = dialogue_text_color;
  g_ram[0x1301 + x] = color;
  g_ram[0x1303 + x] = color;
  g_ram[0x1305 + x] = color;
  g_ram[0x1329 + x] = color;
  g_ram[0x132B + x] = color;
  g_ram[0x132D + x] = color;
  RenderText_Refresh();

  int ch = (int)((w >> 8) & 0x01FF);
  SeedOracle_JpBuildGlyph(rom, ch, g_ram + 0xBFC0);
  int ch4 = ch * 4;
  int pos = dialogue_msg_src_offs;
  SeedJpBlendGlyph(rom, pos * 16, g_ram + 0xBFC0, 0, ch4);
  SeedJpBlendGlyph(rom, pos * 16 + 0x140, g_ram + 0xBFC0, 0x16, ch4);
  SeedJpAdvanceWidth(rom, ch4);

  pos = dialogue_msg_src_offs;
  SeedJpExpandGlyph(rom, pos * 16, 0x10, ch4 + 1);
  SeedJpExpandGlyph(rom, pos * 16 + 0x140, 0x26, ch4 + 1);
  SeedJpAdvanceWidth(rom, ch4 + 1);

  dialogue_msg_read_pos += 2;
  vwf_line_speed_cur = vwf_line_speed;
}

// $8E:FAC2: $7A sets the line speed and consumes one byte.
static void SeedJpCmdSetSpeed(void) {
  uint8 v = SeedJpTextRead8((uint16)(dialogue_msg_read_pos + 1));
  vwf_line_speed = v;
  vwf_line_speed_cur = v;
  dialogue_msg_read_pos += 2;
}

// $8E:FA21: $74..$76 set the destination line from $8E:FD46.
static void SeedJpCmdSetLine(void) {
  const uint8 *rom = EmuSeedRomData();
  uint16 w = SeedJpTextRead16(dialogue_msg_read_pos);
  dialogue_msg_src_offs =
      SeedRomRead16(rom, 0x8EFD46 + (w & 3) * 2);
  dialogue_msg_read_pos++;
  text_next_position = 0;
}

// $8E:F8E5: $72 is the page/choice command. With no input it leaves the
// cursor unchanged; the input paths are only reachable after the replay
// presses a button.
static void SeedJpCmdScroll(void) {
  if (text_wait_countdown2 != 0) {
    if (--text_wait_countdown2 == 1)
      sound_effect_2 = 0x24;
    return;
  }
  const uint8 *rom = EmuSeedRomData();
  uint8 keys = (uint8)((filtered_joypad_L & 0xC0) | filtered_joypad_H);
  if (keys & 0xD0) {
    sound_effect_1 = 0x2B;
    text_render_state = 4;
    return;
  }
  if (keys & 0x08) {
    if (!choice_in_multiselect_box)
      return;
    choice_in_multiselect_box = 0;
  } else if (keys & 0x04) {
    if (choice_in_multiselect_box == 1)
      return;
    choice_in_multiselect_box = 1;
  } else {
    return;
  }
  sound_effect_2 = 0x20;
  int idx = choice_in_multiselect_box;
  dialogue_message_index = SeedRomRead16(rom, 0x8EF8E1 + idx * 2);
  SeedOracle_JpLoadMessageBuffer();
  text_next_position = 0;
  dialogue_msg_read_pos = 0;
  g_ram[0x1CDA] = 0;
}

// $8E:FBA7: $7F waits for input.
static void SeedJpCmdWaitKey(void) {
  if (text_wait_countdown2 != 0) {
    if (--text_wait_countdown2 == 1)
      sound_effect_2 = 0x24;
    return;
  }
  if ((filtered_joypad_H | filtered_joypad_L) != 0) {
    text_render_state = 4;
    // $A0:B478: leave the z3r custom-text mode and arm the input delay.
    dialogue_message_index = 0;
    g_ram[0x15035] = 0;
    text_wait_countdown2 = 0x1C;
  }
}

// $8E:F1A3: JP text renderer state 3.
void SeedOracle_JpRenderMessageCharacters(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  for (;;) {
    uint16 pos = dialogue_msg_src_offs;
    if (pos >= 0x63) {
      pos = 0;
      text_next_position = 0;
    } else if (pos >= 0x3B && pos < 0x50) {
      pos = 0x50;
      text_next_position = 0;
    } else if (pos >= 0x13 && pos < 0x28) {
      pos = 0x28;
      text_next_position = 0;
    }
    dialogue_msg_src_offs = pos;
    if ((pos == 0x12 || pos == 0x3A || pos == 0x62) &&
        ((text_next_position & 7) >= 6)) {
      dialogue_msg_src_offs = ++pos;
      continue;
    }

    uint16 w = SeedJpTextRead16(dialogue_msg_read_pos);
    int cmd = (int)(w & 0x7F) - 0x66;
    if (cmd < 0)
      cmd = 0;
    if (cmd == 0) {  // printable character
      if (vwf_line_speed_cur >= 2) {
        vwf_line_speed_cur--;
        break;
      }
      SeedJpDrawChar();
      if (dialogue_msg_src_offs == 0x13 ||
          dialogue_msg_src_offs == 0x3B ||
          dialogue_msg_src_offs == 0x63)
        break;
      continue;  // $8E:F2B8 jumps back to $F1A3
    }
    switch (cmd) {
    case 12: SeedJpCmdScroll(); break;
    case 14:
    case 15:
    case 16: SeedJpCmdSetLine(); break;
    case 20: SeedJpCmdSetSpeed(); break;
    case 25: SeedJpCmdWaitKey(); break;
    default: break;
    }
    break;
  }
  nmi_subroutine_index = 2;
  nmi_disable_core_updates = 2;
}

// $8E:F6A8 / z3randomizer hook at $A4:D111: copy the current glyph rows to
// $7E:2000+X*16 and $7E:2100+X*16. Custom glyphs flagged in $A4:D247 are
// copied straight from $B3:9C00/$B3:9D00 instead of the rows.
static void SeedOracle_JpCopyGlyph(const uint8 *rom, int ch, int dst_index) {
  uint16 flag = SeedRomRead16(rom, 0xA4D247 + ch * 4);
  uint16 custom = SeedRomRead16(rom, 0xA4D249 + ch * 4);
  uint32 dst0 = 0x2000 + dst_index * 16;
  uint32 dst1 = 0x2100 + dst_index * 16;
  for (int i = 0; i < 16; i += 2) {
    uint16 v0, v1;
    if (flag & 1) {
      v0 = SeedRomRead16(rom, 0xB39C00 + custom + i);
      v1 = SeedRomRead16(rom, 0xB39D00 + custom + i);
    } else {
      v0 = *(uint16 *)(g_ram + 0x10000 + i);
      v1 = *(uint16 *)(g_ram + 0x10020 + i);
    }
    *(uint16 *)(g_ram + dst0 + i) = v0;
    *(uint16 *)(g_ram + dst1 + i) = v1;
  }
}

// $8E:F572: build the JP file-select text tilemap. The loop covers 0x200
// characters (0x100 real glyphs plus 0x100 blanks). Rows of 16 tiles are
// separated by 16 empty tiles, matching the JP file-select screen layout.
void SeedOracle_JpLoadFileSelectTilemap(void) {
  if (!EmuSeedRomActive())
    return;
  const uint8 *rom = EmuSeedRomData();
  uint8 *buf = g_ram + 0xBFC0;
  g_ram[0x1CD9] = 0;
  g_ram[0x1CDA] = 0;
  int dst_index = 0;
  for (int y = 0; y < 0x200; y++) {
    uint16 swapped = (uint16)((y << 8) | (y >> 8));
    *(uint16 *)(g_ram + 0x11200) = swapped;
    SeedOracle_JpBuildGlyph(rom, y, buf);
    SeedOracle_JpGlyphToRows(buf);
    if (y == 0xCC)
      memset(g_ram + 0x10000, 0, 0x40);
    SeedOracle_JpCopyGlyph(rom, y, dst_index);
    if (++dst_index & 0x0F)
      continue;
    dst_index += 0x10;
  }
}
