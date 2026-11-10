# R2: ler uma seed do alttpr.com e rodá-la no emulador

> **Atualização R3a (09/11/26):** o `--emu-rom` virou o modo jogável descrito
> em `RANDOMIZER.md`: janela, áudio, teclado/gamepad, `seed.srm` persistente,
> SNI/EmuNWA lendo o emulador e asserts do PPU estritos fora do modo seed.
> Esta página continua sendo o registro da investigação e dos primeiros
> ajustes no emulador.

Investigação de 09/11/26. O objetivo era responder três perguntas:

1. a ROM de uma seed do alttpr.com pode ser montada localmente a partir da
   ROM JP 1.0, do BPS do patch-base e do JSON da seed?
2. o que dá para ler dessa ROM sem o spoiler completo (esta seed é de
   torneio, então o JSON só publicou `meta`)?
3. a ROM completa roda no emulador `snes/` do projeto, sem o `PatchRom` do
   oráculo, e a que velocidade?

A resposta curta é **sim** para as três. O caminho de emulador é viável e
mais barato do que o caminho nativo para "jogar a seed inteira"; o caminho
nativo continua sendo o que ganha recursos e desempenho depois.

## 1. Montar a ROM da seed

Ferramenta nova: `tools/alttpr_seed.py`, sem dependências externas e sem
rodar binário baixado. O BPS é aplicado em Python com a mesma semântica de
`src/util.c:ApplyBps` (inclusive o CRC do próprio patch).

```
python3 tools/alttpr_seed.py build \
  --jp-rom japanese.sfc \
  --base .agents/alttpr/download/base-79bbb4ad512838ab01fc06b7971f656c.bps \
  --seed .agents/alttpr/download/seed-xvJo08oxyJ.json \
  -o .agents/alttpr/download/seed-xvJo08oxyJ.sfc
```

Conferências feitas pelo comando:

- ROM JP: 1.048.576 bytes, CRC32 `3322EFFC`;
- BPS: `sourceSize`, `sourceCRC32`, `targetCRC32` e CRC do patch;
- base remontada: MD5 `79bbb4ad512838ab01fc06b7971f656c`, igual ao `md5`
  informado em `h-xvJo08oxyJ.json` e ao nome do `.bps`;
- patch da seed: 295 entradas, 19.878 bytes, todos dentro da ROM.

Resultado desta seed:

```
seed: xvJo08oxyJ
title: VT xvJo08oxyJ
size: 2097152
md5: 7fb82df51785fdf306ba181ab0f64ec1
crc32: EBF5BDB9
```

A cadeia é reproduzível de ponta a ponta: JP 1.0 + BPS = base 2 MB com o
MD5 do build; base + lista da seed = ROM da seed.

## 2. Ler a seed

```
python3 tools/alttpr_seed.py info \
  .agents/alttpr/download/seed-xvJo08oxyJ.sfc \
  --json .agents/alttpr/download/seed-xvJo08oxyJ.json
```

Saída resumida desta seed:

```
title: VT xvJo08oxyJ
seed hash: 0B 1A 13 1D 04
seed icons: Lamp, Shield, Cane, Map, Mushroom
randomizer: type=0 game_flags=0x04 warning=0x00
randomized: item
mode: normal (open/standard not stored in ROM)
json mode: open
json goal: ganon
json logic: NoGlitches
crystals: 7: Eastern Palace, Swamp Palace, Palace of Darkness,
          Skull Woods, Ice Palace, Tower of Hera, Thieves' Town
json entry crystals: ganon=7 tower=random
```

Os cinco ícones usam a tabela `ALPHA_*` de `hashalphabet.asm` do
`z3randomizer`, não os ids de item. O screenshot da seleção de arquivo
mostra exatamente lâmpada, escudo, cajado, mapa e cogumelo.

O `info` lê as tabelas nomeadas de `tables.asm` (`$B0:8000` em diante):
corações fixos, NPCs, bosses, espada do pedestal, ferreiro, poção da
cachoeira, chaves de bonk, flags de cristal/pendente, receitas de prêmio de
dungeon e o `SeedHash`. Também decodifica a tabela de baús de dungeon
(formato `[item][room lo][room hi|big]`, 126 registros): 32 itens
"principais" nesta seed, incluindo espada progressiva, luva, armadura,
escudo, bow, moon pearl, fire rod, lamp, book, powder, cane, bombos e quake.

Limitações honestas:

- o spoiler desta seed é de torneio: o JSON só tem `meta`, sem
  `locations`. Por isso não foi possível comparar doze posições contra o
  spoiler. O `info` compara 12 bytes documentados contra a lista `patch` do
  JSON (12/12 nesta seed) e avisa que o spoiler não veio;
- os baús aparecem por **room id** (ex.: `room 0x073 big = Progressive
  Lifting Glove`). Nomear cada baú exigiria a tabela de locations do ALttPR,
  que não está nos arquivos de referência deste repositório;
- `GameType=0x04` é item randomization (bit 2); open/standard/inverted não
  fica totalmente no ROM: `InvertedMode` distingue inverted, mas
  open/standard só veio no `meta` do JSON;
- a contagem de cristais usa `CrystalPendantFlags_2` (`0x40 = cristal`);
  as receitas de prêmio (`DungeonPrizeReceiptID`) são impressas à parte.

## 3. Rodar a ROM no emulador embutido

Opção nova no R2 (hoje também o modo jogável): `--emu-rom <arquivo>`. Ela
não usa `EmuInitialize`/`PatchRom`, não roda a reimplementação C e não exige
assets. Carrega o cartucho inteiro no `snes/`; com `--headless` usa entrada
roteirizada, hash de estado e screenshot PPM.

```
./zelda3 --headless --emu-rom .agents/alttpr/download/seed-xvJo08oxyJ.sfc \
  --frames 3800 \
  --input-script .agents/shots/r2-input-startgame.txt \
  --screenshot .agents/shots/r2-seed-game-3800.ppm
```

O que precisou ser ajustado no emulador:

| Problema | Correção |
|---|---|
| `snes_loadRom` só aceitava SRAM de 8 KB; a seed anuncia 32 KB | `cart_load` realoca a SRAM conforme o header (o caminho normal continua com 8 KB) |
| o emulador não tinha mais um runner de frame completo | `snes_runFrame` novo em `snes/snes_other.c`, chamado só por `--emu-rom` |
| `LoadSongBank` da ROM espera a APU responder em `$2140`; a leitura não avançava a APU e o jogo ficava em busy-wait | `snes_readBBus` faz catch-up da APU também nas leituras (no caminho nativo a APU não é ciclada, então é no-op) |
| asserts do PPU só aceitavam OBSEL=2 e BGMODE 7/9 | asserts removidos/relaxados; o renderer recebe o modo e desenha o que suporta |
| a PPU nasce com `extraLeftRight=96` (large screen); o buffer do teste é 256 px | `--emu-rom` fixa `extraLeftRight=0` |

O `PatchRom` do oráculo **não** é aplicado. A ROM roda como cartucho
completo, inclusive o upload de SPC feito por ela mesma. O áudio é
emulado (APU/DSP) mas ainda não é enviado para SDL; o teste headless só
mantém o estado.

### Medidas e roteiro

- 3 execuções de 1.200 frames, 256x224, renderer novo, sem janela:
  **350,5 / 354,8 / 360,1 fps** (cerca de 6x tempo real). O gargalo é o
  frame completo em C, não o PPU.
- título por volta do frame 1.200 (20 s de jogo);
- seleção de arquivo: `Start` no frame 1.300; screenshot em 1.700 mostra
  os cinco ícones;
- entrada no jogo: nome padrão, `END`, `Start`; em 3.800 o Link já está
  na casa, com o HUD e os itens iniciais da seed.

Screenshots em `.agents/shots/` (fora do git):

- `r2-seed-title-1200.png` — título JP da ROM da seed;
- `r2-seed-fileselect-1700.png` — file select com os 5 ícones;
- `r2-seed-hash-row2.png` — recorte ampliado dos ícones;
- `r2-seed-game-3800.png` — começo do jogo.

### O que falta para ser jogável

> Em R3a os itens 1 a 3 abaixo foram implementados; o item 4 foi feito só na
> parte de SNI/SRAM. Ver `RANDOMIZER.md`.

1. **Vídeo e áudio de verdade**: hoje `--emu-rom` é headless e escreve PPM;
   falta apresentar o framebuffer na janela/SDL e tocar os samples do DSP.
2. **SRAM persistente**: a SRAM de 32 KB existe em memória, mas não é
   gravada/lida do disco entre execuções.
3. **Entrada interativa**: só o `--input-script`; falta mapear teclado/
   controle para `snes->input1->currentState`.
4. **Integração com o resto do produto**: tracker/SNI, fast-forward, pausa,
   save states (o `snes_saveload` já existe, mas não está exposto) e a
   política de qual ROM/seed está ativa.
5. **Qualidade de emulação**: o runner novo foi validado visualmente e por
   hashes, mas não tem regressão frame a frame contra hardware; os asserts
   relaxados do PPU merecem um teste próprio daqui para frente.

## 4. Recomendação

**Emulador primeiro.** Para o sentido A de compatibilidade (a seed do
alttpr.com roda igual), rodar a própria ROM é o caminho mais curto e mais
honesto: não há 730 hooks do `z3randomizer` para reimplementar, e o
precedente ALTTP-DX mostra que a comunidade aceita esse modelo. O custo
inicial já foi pago neste protótipo: montar, ler e rodar a seed funciona.

Ordem sugerida:

1. transformar `--emu-rom` em um modo de jogo de verdade (vídeo, áudio,
   SRAM, input, save states);
2. usar o emulador como referência para o R3 nativo: a RAM da ROM da seed
   é o oráculo dos endereços extras do `z3randomizer`;
3. manter o motor nativo para o jogo vanilla e para os recursos que só ele
   tem (renderer, mods, MSU, SNI), com o modo nativo de seed entrando por
   recurso e provado contra a ROM no emulador.

O caminho nativo de seed continua desejável, mas não deve ser o primeiro
portão: ele exige reproduzir JP 1.0 + `z3randomizer` + as tabelas da seed,
e o emulador já entrega a seed completa hoje.

## 5. Reprodução

```
make -j2
tools/regress.sh
python3 tools/alttpr_seed.py build --jp-rom ... --base ... --seed ... -o seed.sfc
python3 tools/alttpr_seed.py info seed.sfc --json seed.json
./zelda3 --headless --emu-rom seed.sfc --frames 3800 --input-script ... --screenshot ...
```
