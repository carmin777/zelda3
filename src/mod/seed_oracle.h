#ifndef ZELDA3_MOD_SEED_ORACLE_H_
#define ZELDA3_MOD_SEED_ORACLE_H_

#include "../types.h"

// JP 1.0 behavior that the US-based C engine does not have. The functions are
// no-ops (or fall back to the C/US path) unless a seed ROM is loaded.
void SeedOracle_FileSelectPalette(void);
void SeedOracle_JpLoadOWBG3(void);
const uint8 *SeedOracle_JpNmiStripePointer(int idx);
void SeedOracle_JpFileSelectMainGfx(void);
void SeedOracle_JpNameEntryUpload(void);
void SeedOracle_JpNameFile(void);
void SeedOracle_JpFileSelectDrawFile(int k);
void SeedOracle_JpLoadMessageBuffer(void);
void SeedOracle_JpLoadMessageFont(void);
void SeedOracle_JpRenderMessageCharacters(void);
uint16 SeedOracle_JpHudTile(int idx);
void SeedOracle_JpScreenTransition(void);
bool SeedOracle_JpMaybeForceFileName(void);
void SeedOracle_JpFileSelectNameStripes(void);
void SeedOracle_JpFileSelectTilemap(void);
void SeedOracle_JpFileSelectDma(void);
void SeedOracle_JpIntroPaletteInit(void);
void SeedOracle_JpLoadFileSelectGraphics(void);
void SeedOracle_JpLoadFileSelectTilemap(void);
void SeedOracle_JpLoadChrHalfSlot(void);
void SeedOracle_JpLoadFollowerGraphics(void);
void SeedOracle_JpLoadCommonSprites(void);
void SeedOracle_JpLoadDefaultGraphics(void);
void SeedOracle_JpInitializeTilesets(void);
void SeedOracle_JpDecompressAnimatedDungeonTiles(uint8 a);
const uint8 *SeedOracle_JpOverworldMap32Data(int screen, bool lo);
const uint8 *SeedOracle_JpMap32ToMap16(int table, int offset);
const uint16 *SeedOracle_JpMap16ToMap8(void);
uint16 SeedOracle_JpOverworldBlankTile(void);
uint16 SeedOracle_JpMessageId(uint16 us_id);
uint16 SeedOracle_JpDoorAnimTile(int idx);
uint8 SeedOracle_JpTileAttribute(uint16 idx);
void SeedOracle_JpAddDamageCounter(uint8 damage);
void SeedOracle_JpLoadTransAuxGFX(void);
uint8 SeedOracle_JpOverworldMusic(void);
const uint8 *SeedOracle_JpMap8TileAttrTable(void);

#endif  // ZELDA3_MOD_SEED_ORACLE_H_
