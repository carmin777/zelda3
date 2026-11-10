#include "local_coop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../snes/dma.h"
#include "../../snes/ppu.h"
#include "../audio.h"
#include "../config.h"
#include "../spc_player.h"
#include "../variables.h"
#include "../zelda_rtl.h"
#include "coop.h"

// Local split-screen state. The game variables are macros into g_ram, so RAM
// has to be copied between the live buffer and each instance. Everything else
// (SRAM, PPU/VRAM, DMA and the SPC player) is reached through g_zenv pointers
// and is swapped without copying. Player 0 keeps the objects that
// ZeldaInitialize() created; player 1 gets a deep copy made by
// LocalCoop_Start().

typedef struct LocalCoopPlayerState {
  uint8 *ram;
  uint8 *sram;
  Ppu *ppu;
  Dma *dma;
  SpcPlayer *player;
} LocalCoopPlayerState;

static LocalCoopPlayerState s_inst[2];
static bool s_active;
static int s_active_index;

bool LocalCoop_IsEnabled(void) {
  return g_config.coop_local;
}

bool LocalCoop_IsActive(void) {
  return s_active;
}

bool LocalCoop_IsVertical(void) {
  return g_config.coop_local_vertical;
}

static void LocalCoop_CopyPpu(Ppu *dst, const Ppu *src) {
  memcpy(dst, src, sizeof(*dst));
  // renderBuffer points into the previous frame's output buffer. It is set
  // again at the start of every rendered frame.
  dst->renderBuffer = NULL;
}

static void LocalCoop_CopyDma(Dma *dst, const Dma *src) {
  memcpy(dst, src, sizeof(*dst));
  dst->snes = NULL;
}

static void LocalCoop_CopyPlayer(SpcPlayer *dst, const SpcPlayer *src) {
  Dsp *dsp = dst->dsp;
  memcpy(dst, src, sizeof(*dst));
  dst->dsp = dsp;
  dst->reg_write_history = NULL;
  memcpy(dsp, src->dsp, sizeof(*dsp));
  dsp->apu_ram = dst->ram;
}

static bool LocalCoop_AllocPlayer1(void) {
  LocalCoopPlayerState *src = &s_inst[0];
  LocalCoopPlayerState *dst = &s_inst[1];

  dst->ram = (uint8 *)malloc(0x20000);
  dst->sram = (uint8 *)malloc(0x2000);
  dst->ppu = ppu_init(NULL);
  dst->dma = dma_init(NULL);
  dst->player = SpcPlayer_Create();
  if (dst->ram == NULL || dst->sram == NULL || dst->ppu == NULL ||
      dst->dma == NULL || dst->player == NULL) {
    fprintf(stderr, "local coop: out of memory cloning player 2\n");
    return false;
  }

  memcpy(dst->ram, src->ram, 0x20000);
  memcpy(dst->sram, src->sram, 0x2000);
  LocalCoop_CopyPpu(dst->ppu, src->ppu);
  LocalCoop_CopyDma(dst->dma, src->dma);
  LocalCoop_CopyPlayer(dst->player, src->player);
  return true;
}

void LocalCoop_Start(void) {
  if (s_active)
    return;
  if (!g_config.coop_local) {
    memset(s_inst, 0, sizeof(s_inst));
    return;
  }

  s_inst[0].ram = (uint8 *)malloc(0x20000);
  s_inst[0].sram = g_zenv.sram;
  s_inst[0].ppu = g_zenv.ppu;
  s_inst[0].dma = g_zenv.dma;
  s_inst[0].player = g_zenv.player;
  if (s_inst[0].ram == NULL) {
    fprintf(stderr, "local coop: out of memory cloning player 1\n");
    exit(1);
  }
  memcpy(s_inst[0].ram, g_ram, 0x20000);

  if (!LocalCoop_AllocPlayer1()) {
    memset(&s_inst[1], 0, sizeof(s_inst[1]));
    fprintf(stderr, "local coop: giving up (the window would show only one player)\n");
    exit(1);
  }

  s_active = true;
  s_active_index = 0;
  ZeldaSetAudioInstance(s_inst[0].player);
  LocalCoop_Select(0);
  fprintf(stderr, "local coop: two native game states, %s layout\n",
          g_config.coop_local_vertical ? "vertical" : "horizontal");
}

void LocalCoop_Stop(void) {
  if (!s_active)
    return;
  LocalCoop_Select(0);

  free(s_inst[0].ram);
  free(s_inst[1].ram);
  free(s_inst[1].sram);
  ppu_free(s_inst[1].ppu);
  dma_free(s_inst[1].dma);
  free(s_inst[1].player->dsp);
  free(s_inst[1].player);
  memset(s_inst, 0, sizeof(s_inst));

  s_active = false;
  ZeldaSetAudioInstance(NULL);
  ZeldaSetAudioWritesSuppressed(false);
}

void LocalCoop_Select(int index) {
  if (!s_active)
    return;
  if (index < 0 || index > 1)
    index = 0;

  // Save the live RAM back to the previous instance and load the new one.
  // This is the only bulk copy in the per-frame switch; see COOP.md for the
  // measured cost.
  if (index != s_active_index) {
    memcpy(s_inst[s_active_index].ram, g_ram, 0x20000);
    memcpy(g_ram, s_inst[index].ram, 0x20000);
  }

  LocalCoopPlayerState *inst = &s_inst[index];
  s_active_index = index;
  g_zenv.ram = g_ram;
  g_zenv.sram = inst->sram;
  g_zenv.ppu = inst->ppu;
  g_zenv.vram = inst->ppu->vram;
  g_zenv.dma = inst->dma;
  g_zenv.player = inst->player;

  // Only player 2 owns the embedded ROM oracle in local mode. Player 1 is
  // C-only, so the oracle state stays at the same frame for player 2.
  ZeldaSelectStateRecorder(index);
  ZeldaSetEmuOracleEnabled(index == 1);
  ZeldaSetAudioWritesSuppressed(index != 0);
  Coop_SelectInstance(index);
}

int LocalCoop_OutputWidth(int pane_width) {
  if (g_config.coop_local && !g_config.coop_local_vertical)
    return pane_width * 2;
  return pane_width;
}

int LocalCoop_OutputHeight(int pane_height) {
  if (g_config.coop_local && g_config.coop_local_vertical)
    return pane_height * 2;
  return pane_height;
}
