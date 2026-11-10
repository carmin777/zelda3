# Tarefa V1-descricao-frame — R-1: descrição do frame

Leia `AGENTS.md` e **`docs/engine/RENDER-DESENHO.md`** (aprovado; esta tarefa é a etapa **R-1**). O desenho de plugins nativos
(R-2) e o voxel (R-4) **não** fazem parte desta tarefa.

## Contexto do código

- O frame é desenhado em `ZeldaDrawPpuFrame` (`src/zelda_rtl.c`): para cada
  linha, `ppu_runLine` e depois `SimpleHdma_DoLine` nos canais 6 e 7. O
  **HDMA muda registradores do PPU no meio do frame** (rolagem por linha,
  janelas do holofote, modo 7 do mapa) e há a troca de `BG3HOFS/VOFS` na
  linha 128 (`irq_flag`). Por isso o estado do PPU **não é um só por
  frame**: a descrição tem que guardar o que muda por linha.
- O renderizador fica em `snes/ppu.c` (`PpuBeginDrawing`, `ppu_runLine`,
  `PpuDrawWholeLine`, `PpuDrawSprites`, janelas, matemática de cor,
  modo 7, `extraLeftRight`/`Height240` do widescreen).
- Camada semântica: `dung_bg1_attr_table`/`dung_bg2_attr_table`
  (masmorras), `overworld_tileattr` (overworld), `link_z_coord`,
  `sprite_z`, tipo de cada sprite, sala/área, andar.

## O que fazer

1. **Estrutura da descrição do frame** (C puro, em `src/mod/`, por exemplo
   `frame_desc.{c,h}`), versionada (`kFrameDescVersion`):
   - estado do PPU no começo do frame (modo, BGs com base de tilemap e de
     tiles, tamanho, rolagem, prioridade, telas principal/secundária,
     janelas, matemática de cor, mosaico, modo 7, brilho);
   - **as mudanças por linha** (registrador, valor, linha) vindas do HDMA e
     da troca da linha 128;
   - referências (não cópias, se possível) a VRAM, CGRAM e OAM do frame;
   - sprites decodificados da OAM (x, y, tile, paleta, prioridade, tamanho,
     flip) e, quando der, **o dono** (Link, slot de sprite com o tipo,
     ancilla) e o `z`;
   - camada semântica: atributo de colisão de cada tile 8×8 visível em cada
     camada, sala/área/andar, posição da câmera no mundo.
2. **Captura**: montada em `ZeldaDrawPpuFrame` só quando alguém pediu
   (opção de linha de comando ou `[Graphics]`, desligada por padrão).
   Desligada, **nada muda e nada custa** além de um teste de flag.
3. **Prova principal**: um renderizador que desenha **a partir da
   descrição** (pode reaproveitar as funções de `ppu.c` alimentando um
   `Ppu` de rascunho com o estado e as mudanças por linha) e um modo de
   checagem que compara, a cada frame, os pixels dele com os do renderizador
   atual. Rode a checagem em todos os replays do `tools/regress.sh`
   (os 19 e os de feature), em 224 e em 240 linhas e com widescreen
   (`extraLeftRight`) ligado: **0 pixels diferentes**. Se algum caso não
   fechar (por exemplo o modo 7 com correção de perspectiva), diga qual e
   por quê, sem esconder.
4. **Visualizador de depuração**: um dump de um frame (JSON ou texto, mais
   um PNG da camada semântica colorida por tipo de atributo) para
   `--frame-desc-dump <frame>` ou parecido, em `.agents/shots/`, fora do
   git.
5. Documente em `docs/engine/RENDER.md` (novo): a estrutura, a versão, como
   ligar, o que ainda não tem dono/semântica, e o que a R-2 vai precisar.

## Regras

- **Não mude o comportamento do jogo**: a simulação não pode ler nada da
  descrição; o oráculo e os hashes continuam iguais.
- Mexa o mínimo possível em `snes/ppu.c` (outros agentes também mexem perto
  do PPU); prefira código novo em `src/mod/`.
- Não commite screenshots, dumps nem nada tirado da ROM.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final; a checagem de pixels (quantos frames, em
quais replays e configurações, 0 diferenças); custo por frame com a captura
ligada e desligada; o dump de um frame de masmorra e um do overworld.
