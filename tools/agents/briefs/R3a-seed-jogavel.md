# Tarefa R3a-seed-jogavel — jogar uma seed do alttpr.com no modo emulador

Leia `AGENTS.md`, `docs/engine/RANDOMIZER-COMPAT.md` e
**`docs/engine/RANDOMIZER-R2.md`** (o R2 fez a ROM da seed rodar headless
com `--emu-rom`, ~350 fps). Recomendação do R2, aprovada no plano:
**emulador primeiro**. ROMs e seeds ficam fora do git (`japanese.sfc`,
`.agents/alttpr/download/`); `tools/alttpr_seed.py build` monta a ROM.

## O que fazer

1. **Modo jogável**: `./zelda3 --emu-rom seed.sfc` (sem `--headless`) abre
   a janela normal do zelda3 e roda a ROM inteira no emulador embutido:
   - vídeo pelo mesmo caminho de saída do jogo (SDL/OpenGL, escala,
     tela cheia), 60 fps estáveis com a sincronização que o jogo já usa;
   - **áudio** do SPC/DSP do emulador pela saída de áudio do zelda3;
   - **controle**: o mesmo mapeamento de teclas e gamepad do `zelda3.ini`;
   - **SRAM persistente**: `seed.srm` ao lado da ROM da seed, gravada
     quando muda e ao sair (como o ALTTP-DX); nunca no save do jogo normal.
   - Opções que só fazem sentido no motor nativo (features, mods,
     widescreen) ficam desligadas nesse modo, com uma linha no log.
2. **SNI no modo seed**: o servidor EmuNWA (R1) passa a expor a WRAM, a
   SRAM e a ROM **do emulador** quando o modo seed está ativo, para
   trackers e o cliente do Archipelago funcionarem com a seed.
3. **Asserts do PPU**: o R2 removeu asserts de `snes/ppu.c` (OBSEL,
   modos, bits) para a ROM da seed bootar. Reative-os no caminho normal e
   relaxe só no modo `--emu-rom` (uma flag no `Ppu`/`Snes`).
4. `docs/engine/RANDOMIZER-R2.md` (ou um `RANDOMIZER.md` novo): como
   montar a seed e jogar, o que funciona e o que não.

## Verificação a relatar

1. `make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com
   todas as linhas `ok` e a linha final.
2. Headless, com roteiro de entrada: criar um arquivo na seed, jogar
   alguns segundos, salvar; o `seed.srm` aparece; rodar de novo e o
   arquivo continua lá (screenshots em `.agents/shots/`, fora do git).
3. Áudio: grave alguns segundos da saída de áudio em WAV (fora do git) e
   diga se tem som (por exemplo, RMS diferente de zero na tela de título).
4. `tools/nwa_probe.py` lendo a WRAM do emulador no modo seed: a vida e as
   rúpias do Link batem com o que a tela mostra.
5. O caminho com janela: diga se testou de verdade (provavelmente sem
   display aqui); se não, deixe claro.
