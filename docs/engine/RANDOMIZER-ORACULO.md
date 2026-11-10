# R3b/R3c/R3d/R3e: o oráculo contra a ROM de uma seed

Data: 10/11/26. Estado: o oráculo **estrito** roda a ROM da seed lado a lado
e passa os frames **1–11200** sem nenhuma faixa mascarada. O replay novo sai
da casa, contorna os guardas pela esquerda e entra em **Kakariko 0x18** no
frame 9080; a música 7 é confirmada no **9128** e `queued_music_control`
continua em 2, como no JP. A rota segue pelos NPCs de Kakariko: o **bottle
vendor** no 9479 e a **snitch lady** no 9591. A rodada 47 centralizou a
tradução US→JP em `SeedOracle_JpMessageId`: as listas têm 397 mensagens US e
409 JP, com um entry JP a mais antes de `$16` (+1), três entries só US em
`$17–$2E` (o bloco `$2F–$183` fica 1:1 com -2) e o par US `$184/$185`
colapsando em JP `$182`. Os quatro ajustes locais (Priest, game over, bottle
vendor e snitch lady) saíram. No 11200 a mensagem da snitch lady está aberta
(módulo 14, mensagem `0x2D`). Apertar A no 10000 fecha/avança a mensagem e
expõe o próximo divergente no **10023**: o buffer `uvram` em `$1006` etc.
Não há write nativo observado nessa faixa; os valores do lado SNES vêm do
hook z3r/C de mensagem, então o caso é de estado/timing do render, ainda
pendente. A causa do `$0FA9` era um patch US de memória não inicializada que
inicializada que o `PatchRomSeed` não aplicava: no caminho out-of-bounds o
prep OAM da seed lia um `$04` velho. A rodada 45 aplicou o patch equivalente
na ROM da seed por assinatura (PC `0x3643A`), deixou o asm com
`BD 70 0F 85 04 64 05 B0 39` e **removeu todo o modelo `$03–$05`** (NMI,
scroll, tile detect, paleta, sprite load, tail do prep, `kill_lo/kill_hi` e a
separação de entradas); o caminho out-of-bounds do C voltou a ser
`R2 = y - sprite_z`, como no US. O `PatchRom` da seed aplica **76 dos 93
sites US por assinatura**; os 17 restantes não têm sequência equivalente na
seed (ou pertencem ao z3r) — a análise está na seção 27. A seleção de música
por tela do JP (`$A0:CC96`) está portada e é usada nas transições 0x13→0x14
e 0x14→0x15. O modo seed não
usa mais `EmuRunSeedRoutine`: as tabelas JP de larguras/texto/paletas
(`$82:8022`), o `LoadFileSelectGraphics` JP (`$80:E529`), o tilemap JP da
seleção (`$8E:F572`), o `Graphics_LoadChrHalfSlot` JP (`$80:E43A`), a
sequência JP do intro até `t=11`, o caminho z3randomizer de `$A1:FF87`, os
gráficos JP do `Intro_InitializeMemory_darken`
(`$80:E310`/`$80:E1DB`/`$80:D377`) e a entrada no file select JP foram
reimplementados em C. A F4e.4 inteira continua em aberto: criar o
arquivo, desenhar o card z3r e entrar no jogo já funcionam; os primeiros
frames do jogo ainda divergem. O replay da seed usa `Start` no frame 430 para
pular o intro JP (o JP aceita o atalho a partir do submódulo 4, `$8C:C102`),
escolhe o slot vazio, digita um kana (A no frame 517), confirma (Start no
frame 525) e seleciona o arquivo (A no frame 540).

Material usado (fora do git): `japanese.sfc` (JP 1.0, CRC32 `3322EFFC`), a seed
`xvJo08oxyJ` em `.agents/alttpr/download/`, `tools/alttpr_seed.py build` e os
recortes do `z3randomizer` em `.agents/refs/z3r/`.

## 1. Como rodar

Modo estrito (o padrão, usado pelo portão):

```sh
./zelda3 --config tests/regress.ini --headless --no-mods \
  --oracle-rom .agents/alttpr/download/seed-xvJo08oxyJ.sfc \
  --frames 512 --hash-every 1
```

- `--oracle-rom` carrega a ROM da seed e liga o "modo seed" do C. Ele exige
  `--headless`, não combina com `--emu-rom` nem com replay/load/save, e só
  aceita ROM de 2 MB com título interno começando em `VT `.
- O caminho normal (ROM US posicional, sem `--oracle-rom`) não mudou.

O replay permanente do portão é o script de entrada
`tests/replays-seed/seed-xvJo08oxyJ.txt`; o `tools/regress.sh` só o executa
quando a `japanese.sfc` e a seed existem (procura também ao lado do `.git`
principal, para funcionar em worktrees).

`--oracle-ignore-jp` continua aceito por compatibilidade, mas hoje não mascara
nada: a divergência JP do frame 1 (`$7F:1100`/VRAM `$4680`) foi implementada em
C e o oráculo estrito passa os mesmos frames sem ela.

## 2. `PatchRom` na ROM da seed

O `PatchRom` do oráculo foi escrito para a ROM US. A ROM da seed é
**JP 1.0 + z3randomizer + dados da seed**, então o que importa não é o número
de chamadas, e sim se os bytes que o patch espera ainda existem. Contando cada
entrada de array/ancilla como uma operação, o `PatchRom` US tem **93
operações** (a estimativa de "56 patches" do `RANDOMIZER-COMPAT` agrupava
algumas delas).

Classificação feita comparando `japanese.sfc`, `zelda3.sfc` (US),
`.agents/alttpr/base.sfc` (JP + z3randomizer) e a ROM da seed:

| Classe | Operações | O que fazer |
|---|---:|---|
| Bytes US ainda presentes na seed | 29 | aplicar |
| Bytes mudados pelo JP 1.0 | 58 | pular (o endereço/instrução não é o mesmo) |
| Bytes mudados pelo z3randomizer/seed | 6 | pular (código ou dado da seed) |

O `PatchRomSeed` aplica só as 29 e imprime:

```
seed rom: PatchRom applied 29 of 93 US sites, skipped 64 (JP 1.0 differences or z3randomizer overlap)
```

As 29 aplicadas são:

- `LoadSongBank` (`rom[0x888] = RTS`): o oráculo não emula a APU, e essa
  instrução é idêntica em JP e US;
- o redirecionamento do RNG (`$8D:BA71` → novo código em `$8D:FFC1`);
- 7 correções simples cujos bytes US também existem em JP (offsets de
  arquivo/PC): `0x2DEC7`, `0x4BE5E`, `0xD79A4`, `0xF0A46`, `0xF0A52`,
  `0xEF9B9` e `0xDF107`;
- 15 correções de carry/borrow de ancilla (`PatchRomBP`);
- 5 relocações de variável que ainda estão textualmente iguais em JP:
  `$01CFC6`, `$01CFD7`, `$01CFB2`, `$098157`, `$098148`.

As 6 que o z3randomizer ocupa (e que o modo seed **não** sobrescreve):

| Local | Por quê |
|---|---|
| `p[0xF4A7]` (PC `0x174A7`) | banco de código do z3randomizer |
| bloco `SmashRockPile` em `0x6FFD8` | o espaço livre US virou código do z3randomizer |
| `rom[0xDC0F2]` | chamada já redirecionada pelo z3randomizer |
| `rom[0x443FE]` (PC `0x443FE`) | **a sobreposição prevista**: o hook `org $88C3FD` (`ancilla_receive_item.asm`) substitui os 5 bytes do `LDA $2140`; o `PatchRom` US também mexe ali |
| `PatchRomWord(0xE589)` (PC `0x6589`) | `LoadFileSelectGraphics`/Hud já modificado pelo z3randomizer |
| `X 089794` (PC `0x41795`) | referência de door debris já modificada |

A estimativa de 1 sobreposição com o `hooks.asm` estava certa: só `$88:C3FD`
aparece no recorte de `hooks.asm`. As outras 5 conflitam com código do
z3randomizer que está em outros `.asm` do patch-base.

### Correção de infraestrutura

O `Snapshot` do oráculo guardava SRAM em `uint16 sram[0x2000]` (16 KB) mas
copiava `cart->ramSize` (32 KB na seed) com `memcpy`, estourando o buffer e
produzindo diferenças falsas de SRAM. Agora o snapshot tem 32 KB e as cópias
são limitadas pelo tamanho do buffer.

## 3. Primeira divergência e lista ordenada

Método: rodar o oráculo frame a frame, ler as diferenças de RAM/VRAM/SRAM e,
para saber **quem escreveu primeiro**, usar watchpoints de hardware do gdb em
`g_emulated_ram` e em `cart->ram`. O PC que aparece é o do CPU emulado.

### Antes do passo 3 (sem o comportamento seed no C)

Saída real com `g_seed_rom = 0` (via gdb, antes do primeiro frame) e
`--oracle-ignore-jp` (ou seja, só as diferenças JP mascaradas), frame 1:

```
@1: Memory compare failed (mine != theirs, prev):
0x0000BC: 00 != 10 (00)
0x00F42E: 00 != 01 (00)
0x015072: 00 != 01 (00)
  total of 35 failed bytes
@1: SRAM compare failed (mine != theirs, prev):
0x001FFC: 00 != 01 (00)
0x001FFE: 00 != 04 (00)
  total of 2 failed bytes
```

(O total de 35 inclui as 32 bytes JP de `0x11100..0x1111F`; a VRAM também
difere em `0x4680..0x468F`, pelo mesmo motivo JP.)

### Lista ordenada

1. **Init do z3randomizer (`$A1:FF3A..FF84`)** — comportamento do
   z3randomizer, **implementado**. Antes de entrar no main loop, o init:
   - grava `RomVersionSRAM` em `$701FFC` (`00 01 00 04`: `0x00040001`);
   - grava `OneMindId` em `$7F5072` (`1`);
   - grava `PlayerSpriteBank` em `$7E00BC` (`0x10`).
   O C não fazia nada disso; a comparação via `0xBC`, `0x15072` e a SRAM
   `0x1FFC/0x1FFE`. (`0xF42E` é a causa seguinte, o frame hook.) O modo seed
   agora grava os três valores no init, como a ROM.

2. **JP 1.0 × US: `Graphics_LoadChrHalfSlot` (`$80:E43A`)** — **diferença
   JP, não é z3randomizer**, **implementada em C**. No frame 1, a ROM JP
   expande um pack 3bpp em `$7F1000`/`$7F1010` e deixa o NMI subrotina 11
   copiar para a VRAM via `$0116`; o C seguia o `Graphics_LoadChrHalfSlot` US,
   que expande direto para outra VRAM. O watchpoint da primeira escrita em
   `$7F1100` cai em `PC $80:E4D9`, dentro dessa rotina (não em `$80:E529`).
   Saída real antes da implementação:

   ```
   @1: Memory compare failed (mine != theirs, prev):
   0x011100: 0000 != 7FFE (0000)
   ...
   0x01111E: 3300 != FF7F (0000)
     total of 32 failed bytes
   @1: VRAM compare failed (mine != theirs, prev):
   0x004680: 0000 != 7FFE (0000)
   ...
   0x00468F: 3300 != FF7F (0000)
   ```

   Agora `SeedOracle_JpLoadChrHalfSlot` reproduz a rotina JP, incluindo a
   seleção de paleta, os offsets `$08/$09`, o pack por `$0AAA` e o upload NMI;
   o frame 1 estrito passa sem máscara.

3. **Frame hook do z3randomizer (`org $808056`, `FrameHookAction`)** —
   comportamento do z3randomizer, **implementado**. O hook envolve
   `Module_MainRouting`: o código original roda primeiro e depois o contador
   `LoopFrames` (`$7EF42E`, 32 bits) é incrementado. A ordem importa: durante
   os primeiros frames do file select o próprio jogo zera a área de save (e o
   contador); incrementar antes de `Module_MainRouting` fazia o valor voltar a
   zero. Com o incremento depois, os frames 1–8 passam.

4. **Paleta da seleção de arquivo (`org $8CCDA5`, `SetFileSelectPalette`)** —
   comportamento do z3randomizer, **implementado** em
   `src/mod/seed_oracle.c` (`SeedOracle_FileSelectPalette`). O hook
   `$A4:96F4` faz:
   - `GameMode != 4`: copia 33 words de `GFX_HUD_Palette` (`$B3:8000`, PC
     `0x198000`) para `PaletteBuffer` e incrementa `NMICGRAM`;
   - `GameMode == 4`: `hud_palette = 1` e `Palette_Load_HUD`.

   Nos dois casos cai em `Palette_SelectScreen` (`$9B:EF96`), que o C já
   implementa como `Palette_LoadForFileSelect`. A chamada fica em
   `FileSelect_EraseTriforce` (equivalente US `$8C:CDF9` / JP `$8C:CD91`).

5. **Tabela em `$7F5B02..`** — **JP 1.0, não z3randomizer**, **implementada
   em C**. A origem é a rotina `$8E:FCB2`: ela lê entradas de 5 bytes de
   `TextCharMasks` (`$8E:B844`) e grava 513 words cumulativos em `$7F:5B00`
   (o primeiro em `$7F:5B02`; a última iteração grava `$7F:5F00`). Valores
   reais do começo: `0x1A`, `0x33`, `0x49`, `0x62`, `0x7E`, ... O valor de
   cada entrada é o popcount do mask de 40 bits, somado ao anterior. A rotina
   é chamada pelo wrapper `$82:8022`, junto com `$9C:F37A` (tabela de
   ponteiros de texto) e `$82:C322` (reconstrói as paletas JP e seta
   `$0AB3=4`, `$0AB4/$0AB5=3`, `$0AB8=3`, `$0AB1=5`). O C seguia o caminho US
   (`Overworld_LoadAllPalettes`, modo 5, `sp0l=11`). Agora as três rotinas
   estão em `src/mod/seed_oracle.c` e leem as tabelas JP direto da ROM da
   seed, sem executar asm emulado.

6. **Frame 10: tilemap do file select JP** — **implementado em C**. O lado
   ROM chama `$8E:F572`, que para cada um dos 512 caracteres monta o glifo em
   `$7E:BFC0` por `$8E:F4FB`, converte para os bitplanes `$7F:0000/$7F:0020`
   por `$8E:F5BC` e copia para `$7E:2000+X*16`/`$7E:2100+X*16` pelo wrapper
   `$8E:F6A8` (z3randomizer em `$A4:D111`; 87 glifos customizados vêm de
   `$B3:9C00/$B3:9D00`). O C agora faz o mesmo em
   `SeedOracle_JpLoadFileSelectTilemap`, chamado no lugar de
   `LoadItemGFXIntoWRAM4BPPBuffer` quando o modo seed está ativo. A
   divergência antes era:

   ```
   @10: Memory compare failed (mine != theirs, prev):
   0x002008: 00 != 38 (00)
   0x00200A: 0000 != 10EF (0000)
   0x00200C: 0000 != 7E81 (0000)
   ...
   ```

   Depois dele continuam candidatas: SRAM estendida de 32 KB (`$701000+`),
   nomes/ícones do hash/`DrawPlayerFile`, HUD/itens progressivos
   (`$B0:8000`), RNG por chefe e os ganchos de loja/menu.

7. **Frame 11: gráficos JP do intro (`$80:D271`)** — **implementado em C**.
   O `Intro_Init_Continue` JP tem 12 casos (0–11), não 11: `t=9` é o tilemap
   (`$8E:F572`), `t=10` é o `LoadItemGFXIntoWRAM4BPPBuffer` JP (`$80:D271`,
   que expande para `$7E:9000+` via `$80:D577`/`$80:D58E`/`$80:D65C`) e
   `t=11` é o follower graphics JP (`$A1:FF87` → `$80:D463`). O C estava no
   caso 10 US (`LoadFollowerGraphics`) e deixava `$7E:9000+` zerado. Saída
   antes:

   ```
   @11: Memory compare failed (mine != theirs, prev):
   0x009486: 00 != 01 (00)
   0x009488: 00 != 03 (00)
   0x00948A: 00 != 03 (00)
   ...
   ```

   O `Intro_Init_Continue` em modo seed agora segue a ordem JP até `t=11`.
   Os frames 1–11 passam no modo estrito.

8. **Frame 12: hooks z3randomizer do caso `t=11`** — **implementado em C**.
   A ROM executa `$A1:FF87` (`$A4:C261`, `$A0:A8B5`, `$A2:CACA`, cópia para
   `$7F:5410`/`$7F:5420` e `$A4:B0CD`) antes de `$80:D463`. O C agora
   reproduz:
   - a limpeza de `$1E6B..$1E70`/`$1E7D..$1E80` de `$A4:C261`;
   - a tabela de 32 bytes em `$7F:5200` de `$A0:A8B5`;
   - os packs `$CD..$D0` de `$A2:CACA` (decompressão para `$7F:4000` e
     expansão para `$7F:8000+$18`, com os scratch bytes de DP/stack);
   - os dígitos decimais de `$A4:C884`/`$A4:B0A9`, `$CF00..$CF06` e as duas
     linhas de fonte em `$7F:5410`/`$7F:5420`.

   A divergência antes era:

   ```
   @12: Memory compare failed (mine != theirs, prev):
   0x001200: 0000 != F332 (0000)
   0x00CF00: 0000 != 2490 (0000)
   0x015005: 00 != 02 (00)
   0x015202: 00 != 40 (00)
   ...
   ```

   Os frames 12–26 passam no modo estrito depois disso.

9. **Frame 27: gráficos JP do `Intro_InitializeMemory_darken`** —
   **implementado em C**. A ROM JP chama `$80:E310` (LoadDefaultGraphics JP),
   `$80:E1DB` (InitializeTilesets JP) e `$80:D377`
   (DecompressAnimatedDungeonTiles JP). O C agora usa versões seed-aware que
   leem os packs das tabelas JP (`$80:D033/$D112/$D1F1` e
   `$80:CFC0/$D09F/$D17E`), reutilizam os sprites comuns e recarregam os
   backgrounds com os packs JP (`main0=0`, `aux0=114`, etc.). Antes divergiam
   `$7E:6000+`/`$7E:7800+`, `$BF..` e a VRAM; depois o frame 27 passa.

10. **Frames 30–512: animação JP do intro e entrada no file select** —
    implementado em C, um bloco por vez:
    - frame 30: tabelas JP de OAM (`IntroSpriteEnt`) do copyright e dos
      triângulos, com flags `$2b`/`$6b` e os dez caracteres do copyright JP;
    - frames 31–343: limites JP dos triângulos (`$50/$28/$50` em
      `$8C:C5A1`) e o resto da animação;
    - frame 344–430: a ROM pedia um handshake de APU (`$A4:C140`) que o
      oráculo não respondia; o modo seed agora ecoa as escritas das portas
      `$2140..$2143` só no oráculo (`Snes.apu_port_echo`), sem SPC;
    - frame 431: o JP aceita o atalho de `Start` a partir do submódulo 4
      (`$8C:C102`), não do 8 como o US;
    - frame 432: `Module_SelectFile_0` JP (`$8C:CC89`), incluindo as
      variantes JP de `LoadDefaultGraphics`/`InitializeTilesets`, a paleta
      `hud_palette=1`/`palette_main_indoors=0`, a tabela JP de
      `Palette_Load_OWBG3` (`$9B:E604`) e o scratch `$7F:4000` deixado pelo
      pack aux0.

    - frame 433: a paleta JP do file select copia **oito** palavras de
      `$9B:D226/$D244` (o C copiava só sete) e o `EraseTileMaps_triforce` JP
      (`$80:8333`) preenche a VRAM `$0000` com `$0180` e `$6000` com `$0188`;
    - frame 434: o JP `$8C:CDAE` arma `$14=6`; o NMI `$00:90EB` usa a tabela
      de ponteiros `$937A/$9383/$938C` e, para 6, a tabela de stripes em
      `$8C:DDE8`, interpretada pelo mesmo `$92A1` do C (`HandleStripes14`);
    - frame 435: o JP `$8C:CDBA`/`$A4:93C6` preenche `$7E:1000..$1400` com
      `$0188`, sobrescreve os 16 cabeçalhos de linha, liga a tela
      (`INIDISP=0F`, `nmi_disable_core_updates=0`) e chama
      `$A4:9B21`/`$A4:9637` (VRAM `$6060` e `$3200`).

    - frame 436: o `FileSelect_Main` JP desenha a fada em `$14,$42` (não
      `$1C,$4A`); quando não há slot usado, o C não força o upload da tabela
      US. Sem isso a VRAM `$2A9` recebia bytes de código.
    - frames 437–510: o file select fica estável; o replay solta `Start` e
      aperta `A` no frame 510 para selecionar o slot vazio;
    - frames 511–512: o JP `$8C:D764` inicializa o nome com `$018C` em
      `$700500..$700516`/`name[0..3]` e `$3E`/`$019C`; o estado 0 chama
      `$8C:CD98` (`$80:8333` + paleta) e o upload `$A4:98DE`
      (`HandleStripes14`) cobre as linhas `$62B1/$62D1/$62F1...`.

11. **Frames 511–542: nome JP, card da seleção e entrada no jogo** —
    **implementado**. Fechou o frame 513 e avançou até 542:
    - o upload `$A4:98DE` passou para o NMI, **depois** do
      `HandleStripes14` do `$14=5`, com gate `$10/$11 == 4/2`; o estado 1
      não o chama mais;
    - o estado 2 do Module04 usa `SeedOracle_JpNameFile` (`$8C:D9C7`):
      26 células de OAM + coração, digitação A/B, confirmação com Start,
      cursor 0..11 e as tabelas `$8C:D7DF`/`$8C:D97B`;
    - `SeedOracle_JpMaybeForceFileName` implementa o hook `$A4:9823`
      (`$B0:C041`), mas o caminho forçado não foi exercitado pela seed;
    - o finalize `$A4:88E7`/`$A4:B198` copia o template `$B0:B000`/
      `$B0:B3E3`, calcula o checksum com a cadeia de carry do ADC
      (`$5AC3` no arquivo criado) e espelha em `$704000`; o C volta com
      `$10=1`/`$11=1`;
    - a seleção em modo seed confere o checksum em `$3E1` e desenha o card
      z3r `DrawPlayerFile` (`$A4:8B8D`) em
      `SeedOracle_JpFileSelectDrawFile`, com `$D518` (nome/vida) e
      `$D594`/`$D6C0` (OAM); o header `$1002/$1402` e o OAM passaram a
      bater no frame 530;
    - `CopySaveToWRAM`/`Module05_LoadFile` preservam o buffer de nome em
      `$7F:6000`, `$701FFE=4`, `$7EF4F0/$7F5090=FF` e a mensagem JP
      `$182`; o load usa `SeedOracle_JpLoadDefaultGraphics`.
12. **Frames 543–781: motor de texto JP e primeira mensagem** —
    **implementado**. As listagens estão em `.agents/refs/jp-loadfile.md`
    e `.agents/refs/jp-vwf.md` (fora do git):
    - `Text_Initialize` em modo seed segue `$8E:EE10/$EE53`: copia os 32
      bytes de `$8E:FD07` para `$1CD0` (o que também põe
      `messaging_module=1`), chama `$FBCC` (posição da janela), expande a
      mensagem, limpa `$7F:0000..$077F`, zera `$1CD9/$1CDD/$1CE6` e põe
      `$17=2`/`$0710=2`; não chama o loader US nem
      `Attract_DecompressStoryGFX`;
    - `SeedOracle_JpLoadMessageFont` implementa o hook z3r `$A4:D0ED`:
      zera `A12` e descomprime os packs `$67/$68` pelas tabelas JP
      `$80:D033/$D112/$D1F1` em `$7F:4000`/`$7F:4800` (os dois blocos de
      2 KB que o NMI usa como fonte);
    - `SeedOracle_JpLoadMessageBuffer` implementa `$8E:EE86`: lê o offset
      da tabela `$7F:71C0[$1CF0]`, expande `$F6..$FE` (com o dispatch de
      `$8E:EF85`, incluindo o glifo de 4 linhas vindo de `$70:03D9` e a
      posição `$8E:FD3E`), grava words em `$7F:1200` e mantém
      `$1CD9/$1CDD`; `$FB` grava `$7F7F` e termina;
    - o renderer `$F0F8` foi portado nos estados do load: `$F109`/`$F138`
      (borda com marcador `$2D00`, 21 tiles do meio e terminador único) e
      `$FC38`/`$FC5F` (tilemap `$3980+` e refresh com marcador `$2700`,
      20 entradas por linha);
    - o estado 3 `$F1A3` (VWF JP) foi portado: clamp de linha em
      `$1CDD` 0x00/0x28/0x50, comandos `$72`/`$74..$76`/`$7A`/`$7F`,
      leitura de words em `$7F:1200` (ou `$7F:5700` no texto custom),
      `$1CD9`/`$1CDD`/`$1CE6` e o caminho de impressão com quatro tiles
      por caractere;
    - `SeedJpBlendGlyph` (`$8E:F482`) usa os rotates de 16 bits de
      `$00/$01` (SEC) e `$02/$03` (CLC); `SeedJpExpandGlyph` (`$8E:F3E5`)
      transpoõe seis bytes com RORs de 8 bits em `$7F:1F80`; a largura vem
      de `$8E:FD7C`/`$A4:D1B3`, e `SeedOracle_JpBuildGlyph` reaproveita as
      máscaras `$8E:B844` e os 87 glifos custom de `$A4:D247`/`$B3:9C00`;
    - o A no frame 550 fecha a mensagem `$182`; o jogo mostra a mensagem
      `$0A` e o A no frame 780 a fecha. O frame 782 diverge no tilemap do
      HUD (`$7E:C700`/`hud_tile_indices_buffer`), ainda com as tabelas US;
      `sound_effect_ambient_last` (`$131`) também difere nesse frame.
    A listagem de referência do nome continua em
    `.agents/refs/jp-namefile.md`. O `$A2:CACA` não entra neste caminho.

13. **Frames 782–799: HUD JP e primeiros passos** — **implementado**:
    - a listagem do HUD/z3r está em `.agents/refs/jp-hud.md` (fora do git):
      `$8D:FA96` copia 165 words de `$A1:86B0` para `$7E:C700` com
      `MVN $A1,$7E`; `$8D:FAA5`/`$FBB1` atualizam os contadores, corações e
      mágica;
    - `Hud_Update_Inventory` em modo seed usa a tabela z3r e **sempre**
      mostra quatro dígitos de rupees (sem suprimir o zero à esquerda);
      bombas/flechas/chaves ficam em x=13/14, 16/17 e 19. O frame 782 fecha
      com `hud_tile_indices_buffer` idêntico;
    - o `Module_PreDungeon` US seta `$012D=5` logo na entrada; o JP não.
      O guard `EmuSeedRomActive()` evita o `$131=5` no frame 782;
    - a mensagem do Priest/Sanctuary usa `$17` no JP (`Priest_Chillin`), não
      `$16`; o índice agora bate no frame 800;
    - o `Down` é reaplicado no frame 783, depois de a mensagem `$0A` fechar.
      O estrito acompanha o Link até 799; o frame 800 difere só em `$131`
      (0 no C, 5 no SNES), porque o JP ainda seta o ambiente 5 no início da
      mensagem `$17`. É o próximo comportamento.

14. **Frames 800–1572: Sanctuary, saída e primeiros passos** —
    **implementado**:
    - o ambiente 5 do frame 800 vem do hook z3r `$A4:C1B2`, que seta
      `$012D=5` e salta para `$82:8296`; o C reproduz o write no fim do
      landing da Sanctuary (`Module07_0F_01_OperateSpotlight`), e o
      `$131` bate;
    - o A no frame 805 fecha a mensagem do Priest; o Down no 815 move o
      Link e o estrito acompanha até a saída;
    - a saída para o overworld (`Dung_HandleExitToOverworld`) chama o
      hook z3r `$A0:B0FC`: incrementa o contador de 16 bits
      `ScreenTransitions` em `$7EF43C` (se `$7EF443` não estiver setado,
      sem gravar 999). O frame 1554 passa a bater;
    - Right/Down em 1500/1510 alinham o Link com a porta; o estrito
      acompanha até 1572. A listagem está em
      `.agents/refs/jp-transition.md` (fora do git);
    - o frame 1573 divergia no **load do overworld**: `$6CA8/$6CB8`, o
      HUD (`$C727/$C767/$C7F3...`) e a tabela de larguras de texto
      `$7F:5B00` (C `5555`, SNES `0000`). Fechado na seção 15.

15. **Frame 1573: load do overworld JP** — **implementado**:
    - o `$7F:5B00` no JP é a tabela de larguras do texto (`$8E:FCB2`), não a
      `Overworld_SetSongList` US; em modo seed a função US não escreve mais
      nesse endereço;
    - `SeedOracle_JpInitializeTilesets` (`$80:E1DB`) é usada no
      `PreOverworld_LoadProperties`, corrigindo `$7E:6CA8/$6CB8` e o VRAM
      `$3470/$3478` (aux pack JP);
    - os blanks do HUD usam `$207F` no overworld (`$C7F3/$C7F5/$C833/$C835`)
      e o key counter sem chave usa `$207F`; dentro da dungeon continuam
      `$7F`/`$247F`;
    - `Hud_RemoveSuperBombIndicator` é chamado no load do overworld;
    - o frame 1573 (primeiro frame jogável) fecha. A listagem está em
      `.agents/refs/jp-owload.md` (fora do git);
    - o frame 1574 divergia em `$7E:4000`/`$7E:0500` (tilemap/replacement
      state do overworld JP), `$7E:14080` e `$131` (a tabela de música JP
      não fica em `$7F:5B00`). Fechado na seção 16.

16. **Frames 1574–1575: tilemap do overworld JP e tile de transição** —
    **implementado**:
    - o JP lê o map32 comprimido pelas tabelas de ponteiros `$82:F6B1`
      (hi) e `$82:F891` (lo), indexadas por `screen * 3`; o US usa
      `$02:F94D`/`$02:FB2D`. Em modo seed `GetOverworldHibytes/Lobytes`
      devolve o ponteiro da ROM da seed;
    - as quatro tabelas map32->map16 do JP ficam em `$83:8000`,
      `$83:B3C0`, `$84:8000` e `$84:B3C0` (não nos endereços US
      `$03:8000`/`$03:B400`/`$04:8000`/`$04:B400`); o
      `Overworld_ParseMap32Definition` usa a cópia da ROM da seed;
    - a tabela map16->map8 do JP fica em `$8F:8000` (o asset US usa
      `$0F:8000`) e diverge a partir da entrada `0x2A5`;
      `GetMap16toMap8Table` usa a cópia da seed;
    - o `$012D` do overworld JP vem de `$82:83E6` (5, ou 1 se
      `$7EF3C5 < 2`) e de `$82:AE3B` (5); a `Overworld_LoadOverlays2` JP
      só força 1 no overlay de chuva (`$9F`, `$82:AF63`). Em modo seed a
      linha US que lia `overworld_music[]` não roda;
    - o tile de transição em branco do JP é `$0DBE` (o US usa `$0DC4`),
      devolvido pelo hook z3r `$A4:A1BD`; `Overworld_DrawQuadrantsAndOverlays`
      e `SomeTileMapChange` usam o valor da ROM da seed;
    - frames 1574 e 1575 fecham. A listagem está em
      `.agents/refs/jp-owtilemap.md` (fora do git);
    - o frame 1576 divergia só em `$FA9` (high byte de `dungmap_var7`):
      o portal de espelho (sprite `$6C`, slot 15) é desenhado com
      coordenadas iniciais diferentes no JP. Fechado na seção 17.

17. **Frame 1576: portal de espelho e o rascunho `$04`** — **implementado**:
    - o `Sprite_PrepOamCoordOrDoubleRet` do SNES, no caminho out-of-bounds
      (`$86:E47C`), pula o load do z (`$86:E43C`) e recalcula `R2` com o
      `$04` velho da zero page; esse `$04` é o high word do ponteiro de
      dispatch da NMI, gravado em `$80:80B5` a partir de
      `$80:8061/$80:807D/$80:8099` (índice = módulo principal);
    - em modo seed o `SeedOracle_JpNmiDispatchScratch` lê essas tabelas da
      ROM da seed no início do `Sprite_Main`, e o `Sprite_PrepOamCoord` usa
      o `$04` velho no caminho out-of-bounds do sprite `$6C`;
    - o frame 1576 fecha; o oráculo segue até 1593 com o Link andando.

18. **Frame 1594: tabela JP da porta grande** — **implementado**:
    - a divergência vinha da tabela de animação da porta grande
      (`Overworld_DoMapUpdate32x32`): o C usava a tabela US em `$82:ACDA`
      (`DA8 DA9 DAA DAB...`), o JP/seed usa `$82:ABD9`, com todos os 56
      ids de map16 6 menores (`DA2 DA3 DA4 DA5...`);
    - `SeedOracle_JpDoorAnimTile` lê a tabela da ROM da seed; o
      `memorized_tile_value` e o stripe de VRAM passam a bater;
    - o frame 1594 fecha e o oráculo segue até 1645.

19. **Frames 1576/1646: tempo de vida de `$03–$05`** — **implementado**:
    - a regra por `sprite_type == $6C` foi removida. O `$04` velho agora é
      um valor persistente modelado por `s_jp_nmi_scratch`;
    - a NMI JP grava `$03/$04/$05` a partir de
      `$80:8061/$807D/$8099` (índice = módulo principal);
      `SeedOracle_JpNmiDispatchScratch` faz o mesmo no `Interrupt_NMI`;
    - o caminho de scroll vertical JP (`$82:BAFA`) faz `stz $04` quando
      `link_y_vel != 0`; `SeedOracle_JpClearNmiScratch` reproduz isso no
      `Overworld_OperateCameraScroll`;
    - o out-of-bounds `$86:E47C` lê o `$04` velho
      (`SeedOracle_JpNmiScratchValue`) nos módulos 9/16, sem olhar o tipo
      do sprite;
    - os frames 1576 e 1646 passam; o oráculo segue até 1647.

20. **Frame 1648: blank do HUD** — **implementado**:
    - o `Hud_RemoveSuperBombIndicator` voltou a gravar `0x7F`, como o JP
      `$8A:FDB0`; é o valor do refresh de floor/key;
    - o load do overworld usa `Hud_LoadOverworldBlankTiles`, que lê as
      quatro células da tilemap HUD JP `$A1:86B0` (0x207F), o caminho do
      upload `$8D:FA9F`, separado do refresh;
    - os frames 1573 e 1648 passam os dois; o oráculo segue até 1670.

21. **Frames 1671–1673: o escritor `$04` do scroll** — **implementado**:
    - o vigia de escrita SNES mostrou que `$82:BCBC` grava `$04` com a
      delta acumulada do scroll vertical (2 passos → `$04=2`; 1 passo →
      `$04=1`), depois do `stz $04` em `$82:BAFA`;
    - `Overworld_OperateCameraScroll` agora chama
      `SeedOracle_JpSetNmiScratchDelta(r4)` com o acumulador `r4` do
      loop vertical, em modo seed; o modelo de `$03–$05` usa esse valor
      no out-of-bounds, sem mexer nos copies de scroll;
    - os frames 1576, 1646 e 1671–1673 passam; o oráculo segue até 1700.

22. **Frame 1701: o rascunho do tile detect** — **implementado**:
    - o vigia mostrou que `$87:CF1A` (`Player_TileDetectNearby`) grava a
      segunda coluna de tile (`x1`) em `$04/$05`; o out-of-bounds do
      sprite lê esse valor;
    - `Player_TileDetectNearby` agora chama
      `SeedOracle_JpSetNmiScratchDelta(x1)` em modo seed;
    - o frame 1701 fecha.

23. **Frame 1799: tabela JP de atributo de tile** — **implementado**:
    - `Probe_CheckTileSolidity` (`$8D:C26E`) lê o word de 16 bits do
      `dung_bg2` (`$7E:2000`) e usa o valor inteiro como índice da tabela
      `$9B:F110`; acima de `0xFF` essa tabela JP difere do asset US
      (`kSomeTileAttr`) — no frame 1799 o índice `0x037E` dá `0x01` no JP
      e `0x40` no US;
    - `Overworld_ReadTileAttribute` usa `SeedOracle_JpTileAttribute` (lê a
      ROM da seed) em modo seed;
    - o frame 1799 fecha.

24. **Frame 2019: contador de dano z3r** — **implementado**:
    - o guarda azul acerta o Link e o SNES grava `$7EF46A += 4` pelo hook
      z3r `$A0:B2DA` (o de poço é `$A0:B2B9`, +8), respeitando
      `$7EF443`/StatsLocked e saturando em `0xFFFF`;
    - `SeedOracle_JpAddDamageCounter` é chamado em `Link_ControlHandler`
      (dano de sprite) e `DungeonPitDoDamage` (poço);
    - o oráculo segue até 2100.

25. **Transição de tela do overworld (frame 2247)** — **implementado**:
    - o JP `$82:AA87` chama o hook z3r `$A4:89EF` (`$A0:B0FC` incrementa
      `$F43C`; `$A0:81FF` seta `$0ABF=1` e `$021B=1`) e depois
      `$80:D6AE`; `Module09_LoadAuxGFX` agora chama
      `SeedOracle_JpScreenTransition` e `OverworldScrollTransition` seta
      `$021B` em modo seed;
    - `SeedOracle_JpLoadTransAuxGFX` lê as tabelas JP `$80:DDD7`/`$80:DB97`
      e descomprime os packs da ROM da seed para `$7E:6000/$6600/$6C00/$7200`
      e `$7E:7800/$7E00/$8400/$8A00`, em vez da tabela US;
    - `OverworldTransitionScrollAndLoadMap` modela o `$82:F165` (limpa
      `$04` e preserva `$05`) e o `Sprite_PrepOamCoordOrDoubleRet`
      modela o `$00/$02` que o `Sprite_KillSelf` deixa no caminho
      out-of-bounds;
    - a transição 0x13→0x14 passa; o oráculo segue até 2700.

26. **Música do overworld JP** — **implementado**:
    - o JP não usa a lista US em `$7F:5B00` (lá fica a tabela de larguras);
      a escolha por tela é `$A0:CC96` (hook
      `PreOverworld_LoadProperties_ChooseMusic`), agora portada em
      `SeedOracle_JpOverworldMusic`: progresso (`$7EF3C5`), flag da
      master sword (`$00 & 0x40`), dark world (`$7EF3CA`), moon pearl
      (`$7EF357`) e a tela 0x18 → faixa 7;
    - `Overworld_FinalizeEntryOntoScreen` chama essa seleção em modo seed
      e acerta `music_control` quando a faixa muda; `queued_music_control`
      permanece em 2, como no JP (o SNES não o usa nessa transição);
    - nas transições o JP também usa os casos específicos de
      `$82:A968`/`$82:A999`: tela antiga 0x2A → ambiente `0x80`; 0x68 com
      `music_unk1==0x0E` → `music_control=9`; 0x18 (antiga ou nova) →
      `music_control=0xF1` (o z3r troca o `bcs` por NOP, então o fade é
      sempre);
    - a transição 0x13→0x14 devolve faixa 2 e passa; a transição de
      Kakariko 0x18 está provada no estrito: a tela entra no 9080 e
      `music_unk1` vira 7 no **9128** (`queued_music_control` permanece 2,
      como no JP), estável até o 9400.

27. **Movimento extra depois de 2700** — **em andamento**:
    - o replay usa o caminho direto `Up`+`Right`, sobrevive à natação sem
      flippers e chega à tela 0x15; o oráculo passa até 6000;
    - na tela 0x15 Link morre (`link_health_current==0`,
      `main_module_index==18`); no game-over o C agora constrói o tilemap de
      texto JP em `$7E:2000/$2100` (`$89:F430` → `$0E:F572`), recarrega a
      mensagem 3 em `RenderText_PostDeathSaveOptions` (`$8E:EE25` → `$EE63`),
      usa o upload BG3 de `$780` bytes do JP (`$80:8D02`) e posiciona a fada
      do menu em `$3C/$81..` (`$89:F674`);
    - a confirmação do menu (`A` no 6000) executa o save z3r: slot 0,
      extended save de `$7F:6000` para `$700500`, checksum de
      `$000–$4FD` + `$500–$14FF`, backup em `$704000` e contadores de morte
      `$7EF3FF`/`$7EF449`;
    - a rodada 39 reproduziu a aritmética exata do `$A4:B198`: o `ADC` de
      16 bits encadeia o carry de cada soma; o C que só truncava a soma
      dava 7 a menos. Com a cadeia de carry o checksum fecha
      (`$4344`) e o 6001 passa;
    - o replay continua no Link's House, sai para a tela 0x13, contorna os
      guardas do castelo e chega à tela 0x1D com 20 de vida; o estrito
      passa até 11022;
    - no 11023 a transição vertical para a tela 0x25 divergia em 2 no scroll
      Y (`$618/$61A`, C `0x724/0x722` × SNES `0x726/0x724`); era o
      `RecoverPositionAfterDrowning` outdoor, que no JP restaura o low do
      scroll do cache (`camera_y_coord_scroll_low_cached`); o C só restaurava
      o high. O branch JP agora restaurar low/high e o 11023 fecha;
    - a rota chega à tela 0x1D e para na água; segurar Down mantém Link
      vivo por mais tempo, mas ainda não há passagem para 0x25. O próximo
      passo é a navegação da água/rota até 0x18, sem mudar comportamento US;
    - a rodada 41 adicionou o modo C-only `--seed-rom` e a ferramenta
      `tools/seed_route.py` (busca em beam com `--dump-state`, saúde, água,
      interior e distância de tela). A ferramenta gerou
      `tools/seed_route_kakariko.txt`, que sai da tela 0x13 pela esquerda e
      chega à tela 0x18 (x=820,y=1510, 20 de vida) em C-only;
    - as rodadas 42–44 modelaram o rascunho `$03–$05` para mascarar o
      `$0FA9` (scroll horizontal/vertical, tile detect, paleta, sprite load,
      tail do prep e `kill_lo/kill_hi`). A rodada 45 identificou a causa
      real: o `PatchRom` US move o load do z (`LDA $0F70,X / STA $04 /
      STZ $05`) para antes do `BCS +$40` do prep OAM, e o `PatchRomSeed` não
      aplicava esse site porque só olhava o offset US. O patch agora é
      aplicado por assinatura em **PC `0x3643A`**; o asm da seed passou a
      ter `BD 70 0F 85 04 64 05 B0 39` e o caminho out-of-bounds não lê mais
      um `$04` velho. Todo o modelo `$03–$05` foi removido, inclusive a
      separação de entradas do prep, `Palette_LoadMultiple` e
      `Overworld_LoadProximaSpriteIfAlive`; o C voltou a
      `R2 = y - sprite_z`, igual ao US;
    - o `PatchRomSeed` agora aplica **76 dos 93 sites US** por assinatura:
      69 patches diretos + o prep OAM + o thunk de
      `BufferAndBuildMap16Stripes_Y` em `$02:FCD2` (5 sites: thunk e quatro
      JSR) + a rotina nova do RNG. Os 17 restantes continuam pulados porque
      a sequência US não existe na seed ou o z3r substituiu o site
      (relocação de chamada `$06:6589`, Smithy_Frog, APU `$44:3FE`/`$44:607`,
      ancilla `$48:16C`/`$48:16F`/`$49:D0C`/`$49:D0F`, Hud_Rebuild
      `$6D:FAD`, rotina nova `$6F:FD8`, arrays z3r `$7F:DE1`–`$7F:DFA` e o
      hook de door anim `$DC:0F2`);
    - a rota nova passa no estrito **1–11200**: entra em 0x18 no frame 9080,
      `music_unk1` vira 7 no **9128** e `queued_music_control` fica em 2,
      como no JP. O replay permanente e o golden agora são essa rota (11200
      frames), com `11200 -` no fim;
    - o frame **9479** não era HDMA: é `dialogue_message_index` (`$1CF0`),
      escrito por `Sprite_BottleVendor`. O C usava o ID US `0xD1`; o JP usa
      `0xCF` (`$85:EADF`). `BottleVendor_Msg` subtrai 2 em modo seed e o
      frame fecha;
    - o próximo era **9591**, também `$1CF0`: `Sprite_OldSnitchLady` usava
      `0x2F` (US) e o JP usa `0x2D` (`$85:E772`); a mensagem interna era
      `0xAD` × `0xAB` (`$85:E6DA`). `OldSnitchLady_Msg` subtrai 2 em modo
      seed e a rota segue até **11200**;
    - apertar A no 10000 (para fechar a mensagem da snitch lady) diverge no
      **10023** no buffer `uvram` (`$1006`/`$1008`/`$100E`...). O lado C
      mantém os tiles de `RenderText_Refresh` (`0x114B`/`0x3545`...), o lado
      SNES tem `0x08E8`/`0x08FE`...; um vigia em `snes_write` não vê write
      nativo nessa faixa, então os valores do lado SNES vêm do hook z3r/C de
      mensagem. O `RenderText_Draw_Finish` do C não é chamado no fechamento
      (o `SeedJpCmdScroll` põe `text_render_state=4`, mas o caminho de
      render não chega ao finish). O replay permanente não pressiona A e
      fica no módulo 14; o caso fica pendente;
    - a rodada 47 centralizou a tradução de IDs em `SeedOracle_JpMessageId`
      e removeu os quatro ajustes locais. O alinhamento das listas: US 397 ×
      JP 409; um entry JP-only antes de `$16` (+1); três entries US-only em
      `$17–$2E`; o bloco `$2F–$183` é 1:1 com -2 (confirmado pelos tamanhos
      dos segmentos entre os anchors `$2F/$2D`, `$3B/$39`, `$AD/$AB`,
      `$D1/$CF` e `$184/$182`); `$184/$185` → `$182`. O trecho `$17–$2E`
      fica em identidade porque os três entries US-only não foram
      identificados com certeza, e `$186+` também fica em identidade; o
      estrito de 11200 passa com a tabela central;
    - os 17 patches US restantes foram reexaminados: o APU `$2140` de
      `$44:3FE`/`$44:607` não existe no banco 8 da seed; as referências de
      `ancilla_arr23` (`$48:16C`/`$48:16F`), `AncillaAdd_AddAncilla_Bank09`
      (`$49:D0C`/`$49:D0F`) e os arrays `$7F:DE1`–`$7F:DFA` estão em
      rotinas/tabelas do z3r com bytes diferentes; `Hud_Rebuild`
      (`$6D:FAD`), `Smithy_Frog` (`$33:2B7`), a chamada `$06:6589`, a rotina
      nova `$6F:FD8` e o hook de door anim `$DC:0F2` também não têm
      equivalente JP. Ficam pulados e documentados;
    - o desvio anterior pelos guardas (`Left`/`Down`/`Right`/`Up`) também
      levava ao game-over, só que mais cedo (3679), e foi aposentado;
    - os hooks z3r `BonkRecoil` (`$A4:8785`: `$29=0x16` sem boots e com
      `$7F50CE != 1`, senão `0x24`) e `JumpDownLedge` (`$A4:8766`:
      limpa `player_near_pit_state` quando não tem boots e `$5B==1`)
      foram portados em `LinkApplyTileRebound`/`LinkHop_HoppingSouthOW`;
    - o vigia de escrita no C mostrou que no 3149 as últimas chamadas de
      `Link_HandleVelocity`/`Link_HandleCardinalCollision` deixam
      `$27/$28/$30/$31` em zero; os valores 16/`0xEC` aparecem **depois**,
      no caminho de natação (`link_player_handler_state==4`,
      `HandleSwimStrokeAndSubpixels`/
      `Player_SomethingWithVelocity_TiredOrSwim`), enquanto o SNES para;
    - os dois hooks sozinhos não fecham o 3149;
    - no frame 3148 os dois lados estão em água profunda com
      `link_player_handler_state==4` e `$27/$28=16/236`, mas sem flippers
      (`$7EF356=0`); o C saía cedo de `PlayerHandler_04_Swimming` e deixava
      as velocidades velhas, o SNES zerava;
    - a rodada 37 executou só os contadores e zerou as velocidades no
      caminho fake-flipper; ainda faltavam `$2B` (subpixel), `$3E/$6E`,
      `$328/$336/$33A/$33E` e `$341/$343/$35B`;
    - a rodada 38 removeu o retorno pós-contadores em modo seed: o handler
      JP inteiro roda, inclusive a natação/subpixel e as flags de água;
      o 3149 e o 3150 fecham e o oráculo estrito segue até 3966;
    - a mesma rodada fechou o game-over (tilemap de letras, recarga da
      mensagem, upload BG3 JP, fada do menu) e o caminho de save do menu de
      morte; o estrito segue até 6000 e para só no checksum do save;
    - o caminho normal (sem seed) mantém o retorno US quando não há
      flippers; nenhuma mudança de comportamento foi feita no modo normal.

### Ferramenta de rota em malha fechada

`tools/seed_route.py` roda o C em modo seed sem o SNES (`--seed-rom`), lê
`--dump-state` a cada rajada de inputs e escolhe a próxima direção por beam
search. A pontuação usa a distância da tela de overworld, a borda na direção
do alvo, vida, água profunda, interior e novidade de célula. Exemplo:

```
tools/seed_route.py --start 8180 --targets 0x18 --beam 4 --step 180 \
    --max-steps 8 --jobs 4 --out /tmp/route.txt
```

O replay gerado pode ser validado com `--oracle-rom`. A ferramenta exige
`--stable-frames` (default 120) e `music_unk1 == 7` para aceitar a tela 0x18,
evitando a tela transitória do scroll. Com `--oracle`, o `post_check` roda o
candidato em `--oracle-rom` e rejeita a rota se o estrito divergir antes da
tela estável; sem `--oracle`, o `post_check` usa `--seed-rom`. O arquivo
`tools/seed_route_kakariko.txt` é a rota de referência; ela chega à 0x18 em
C-only e o estrito passa 1–11200 (0x18 no 9080, música 7 no 9128, NPCs no
9479/9591). O replay permanente usa essa rota com `11200 -` no fim.

## 4. O que é diferença JP 1.0 × US

O C reimplementa a versão US. A ROM da seed é JP 1.0, então tudo que o JP
codifica diferente do US precisa de um "modo JP" no C, não de um mod. Já
identificado:

- **`Graphics_LoadChrHalfSlot`**: `$80:E43A` no JP expande o pack 3bpp em
  `$7F:1000/$7F:1010` e usa o NMI 11 para a VRAM, enquanto o US `$80:E3FA`
  expande direto para a VRAM.
- **58 das 93 operações do `PatchRom`**: o byte/instrução que o patch US
  espera não existe no JP. Exemplos: `$2C7E5` (HDMA do deserto),
  `$9816C`/`$FFDE0` (arrays de ancilla), as relocações de door debris em
  `$89:...`, `$E589` (Hud_Rebuild) e o fix de `SmashRockPile`.
- O `z3randomizer` também muda várias dessas rotinas, então parte do que
  "falta" no modo JP já está resolvida pelo próprio patch-base. É preciso
  separar caso a caso; não dá para aplicar o `PatchRom` US inteiro.
- Em particular, a maioria das relocações de variáveis de ancilla/door
  debris do `PatchRom` foi pulada porque o JP codifica a instrução de outro
  jeito. O C usa os endereços novos (`0x728` etc.); enquanto não houver modo
  JP, essas rotinas podem divergir mesmo quando o z3randomizer não tem culpa.
  Só 5 das relocações puderam ser aplicadas sem ambiguidade.

Fora do `PatchRom`, o modo seed já cobre estas diferenças JP que apareceram
no caminho, todas reimplementadas em C (nenhuma chama mais
`EmuRunSeedRoutine`):

- `LoadFileSelectGraphics` (`$80:E529` no JP): o modo seed usa a
  reimplementação em C (`SeedOracle_JpLoadFileSelectGraphics`), incluindo os
  hooks do z3randomizer `$80:E55D`/`$80:E568` (tilemap de `$7E:2000` para a
  VRAM `$2000`) e `$80:E581` (`$7F:0000` para a VRAM `$2F00`);
- a inicialização `$82:8022` (`$8E:FCB2` + `$9C:F37A` + `$82:C322`), que
  constrói a tabela de larguras de texto, os ponteiros JP de diálogo e as
  paletas JP;
- o tilemap do file select (`$8E:F4FB`/`$8E:F572`/`$8E:F5BC`/`$8E:F6A8`,
  com o hook `$A4:D111`), incluindo os 87 glifos customizados da tabela
  `$A4:D247`;
- o `Graphics_LoadChrHalfSlot` JP (`$80:E43A`), que cobre a escrita em
  `$7F:1100`/VRAM `$4680` do frame 1;
- a sequência JP do `Intro_Init_Continue` até `t=11` (`$80:D271` para item
  GFX e `$A1:FF87` → `$80:D463` para follower graphics);
- o caminho z3randomizer de `t=11` (`$A4:C261`, `$A0:A8B5`, `$A2:CACA`,
  `$A4:C884`/`$A4:B0A9` e `$A1:FF9A`), incluindo os packs `$CD..$D0`, a
  tabela `$7F:5200`, os dígitos `$CF00..$CF06` e as linhas de fonte
  `$7F:5410`/`$7F:5420`;
- os gráficos JP do `Intro_InitializeMemory_darken` (`$80:E310`,
  `$80:E1DB`, `$80:D377`), com os packs de `$80:D033/$D112/$D1F1` e
  `$80:CFC0/$D09F/$D17E`;
- a posição JP do sprite de copyright do intro (`$58` em vez de `$4C`).

O watchpoint do frame 1 (`$7F:1100`/VRAM `$4680`) cai em `$80:E4D9`, dentro de
`$80:E43A`, e não em `$80:E529`; a confusão anterior entre as duas rotinas
está resolvida.

A lista completa das 58 operações do `PatchRom` que dependem do modo JP
(PC, bytes US esperados e bytes JP reais) é:

| Operação | Local | US | JP |
|---|---|---|---|
| `36434` | `PC 0x036434` | `b0 40 bd 70 0f 85 04 64 05` | `00 c9 70 01 e2 20 b0 40 bd` |
| `thunk` | `PC 0x017F6E` | `ff ff ff ff ff ff ff ff ff ff ff` | `a0 4f 89 a5 8a f0 30 c9 40 f0 2c` |
| `f4b5` | `PC 0x0174B5` | `9d 00 05` | `21 44 68` |
| `f3dd` | `PC 0x0173DD` | `9d 00 05` | `e2 20 8b` |
| `f3ef` | `PC 0x0173EF` | `9d 00 05` | `85 00 e2` |
| `rng` | `PC 0x06FFC1` | `ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff` | `7f 20 7f 20 7f 20 7f 20 7f 20 7f 20 7f 20 7f 20` |
| `4a966` | `PC 0x04A966` | `64` | `a0` |
| `332b7` | `PC 0x0332B7` | `22 39 9f 09 fa` | `cc f3 7e da 22` |
| `44607` | `PC 0x044607` | `40 21` | `c9 18` |
| `49d0c` | `PC 0x049D0C` | `85 0e` | `a2 05` |
| `49d0f` | `PC 0x049D0F` | `a5 0e` | `4a 0c` |
| `e852` | `PC 0x00E852` | `20 7f ae 00 10 a9 00 00 20` | `ae 00 10 a9 00 00 20 97 e8` |
| `BP 06d0b6` | `PC 0x0350B6` | `e9` | `0e` |
| `BP 06ed0b` | `PC 0x036D0B` | `69` | `ac` |
| `BP 08f708` | `PC 0x047708` | `c8` | `aa` |
| `BP 07b269` | `PC 0x03B269` | `e8` | `60` |
| `2C7E5+1` | `PC 0x0147E6` | `df 01` | `02 02` |
| `9816C` | `PC 0x04816C` | `d2` | `30` |
| `ffdeb` | `PC 0x07FDEB` | `d2` | `01` |
| `ffdee` | `PC 0x07FDEE` | `d2` | `02` |
| `ffdf7` | `PC 0x07FDF7` | `d2` | `01` |
| `ffdfa` | `PC 0x07FDFA` | `d2` | `02` |
| `ddfac+1` | `PC 0x06DFAD` | `85 fa` | `a5 fa` |
| `X 01d29d` | `PC 0x00D29E` | `b6 03` | `b9 a0` |
| `X 0897a3` | `PC 0x0417A4` | `b6 03` | `bb a9` |
| `X 08a0a1` | `PC 0x0420A2` | `b6 03` | `20 4f` |
| `X 08edca` | `PC 0x046DCB` | `b6 03` | `9e 4a` |
| `X 099aa6` | `PC 0x049AA7` | `b6 03` | `18 69` |
| `X1 089797` | `PC 0x041798` | `b7 03` | `9b 8a` |
| `X1 0897a6` | `PC 0x0417A7` | `b7 03` | `9d 54` |
| `Y 01d2ae` | `PC 0x00D2AF` | `ba 03` | `e2 20` |
| `Y 08a099` | `PC 0x04209A` | `ba 03` | `a0 5e` |
| `Y 08edc5` | `PC 0x046DC6` | `ba 03` | `00 e2` |
| `Y 099aa1` | `PC 0x049AA2` | `ba 03` | `00 a8` |
| `Dir 01d2ba` | `PC 0x00D2BB` | `be 03` | `a9 1b` |
| `Dir 08a0b7` | `PC 0x0420B8` | `be 03` | `8a 0a` |
| `arr26 089fb9` | `PC 0x041FBA` | `c0 03` | `18 65` |
| `arr26 089fc0` | `PC 0x041FC1` | `c0 03` | `85 00` |
| `arr26 099c49` | `PC 0x049C4A` | `c0 03` | `34 8c` |
| `arr25 089fc3` | `PC 0x041FC4` | `c2 03` | `0e 18` |
| `arr25 089fc6` | `PC 0x041FC7` | `c2 03` | `f8 ff` |
| `arr25 08a0ae` | `PC 0x0420AF` | `c2 03` | `4f 60` |
| `arr25 08ab7c` | `PC 0x042B7D` | `c2 03` | `00 60` |
| `arr25 08aba7` | `PC 0x042BA8` | `c2 03` | `00 e0` |
| `arr25 08abb6` | `PC 0x042BB7` | `c2 03` | `58 7f` |
| `arr25 08ae92` | `PC 0x042E93` | `c2 03` | `20 cc` |
| `arr25 08bae2` | `PC 0x043AE3` | `c2 03` | `b1 03` |
| `arr25 08baff` | `PC 0x043B00` | `c2 03` | `65 00` |
| `arr25 08f429` | `PC 0x04742A` | `c2 03` | `a9 02` |
| `arr25 098e0a` | `PC 0x048E0B` | `c2 03` | `02 bd` |
| `arr25 098ebc` | `PC 0x048EBD` | `c2 03` | `18 69` |
| `arr25 09920a` | `PC 0x04920B` | `c2 03` | `9d 22` |
| `arr25 09931e` | `PC 0x04931F` | `c2 03` | `85 06` |
| `arr25 09987f` | `PC 0x049880` | `c2 03` | `9f 03` |
| `arr25 099c44` | `PC 0x049C45` | `c2 03` | `40 f0` |
| `arr22 09816e` | `PC 0x04816F` | `e1 03` | `90 18` |
| `arr22 0ffde0` | `PC 0x07FDE1` | `e1 03` | `2e 2a` |
| `arr22 0ffde7` | `PC 0x07FDE8` | `e1 03` | `1a 01` |

O modo JP do C já cobre todas as diferenças JP encontradas até o frame 512,
então o oráculo estrito e o `--oracle-ignore-jp` (hoje sem faixas mascaradas)
param no mesmo ponto: frame 513, na digitação da entrada de nome JP.

## 5. Quanto falta

Honestamente: **muito**. A F4e.4 fala em 730 desvios do `hooks.asm`; até aqui
o modo seed cobre o init, o frame hook, a paleta do file select, as tabelas
JP de larguras/texto/paletas, o caminho JP de `LoadFileSelectGraphics`, o
tilemap JP do file select, o `Graphics_LoadChrHalfSlot` JP, a sequência JP do
intro até `t=11`, o caminho z3randomizer de `$A1:FF87`, os gráficos JP do
`Intro_InitializeMemory_darken`, a animação JP do intro e a entrada no file
select. O oráculo ainda não chega a criar um arquivo: o frame 513 para na
digitação da entrada de nome JP. Cada causa nova continua exigindo:

1. identificar o hook/rotina e o estado que ele escreve;
2. implementar o equivalente em C atrás de `EmuSeedRomActive()`;
3. rodar o oráculo até o próximo frame e repetir;
4. só então transformar o caso num replay de regressão.

Estimativa para chegar a criar um arquivo e andar no jogo: ainda faltam os
blocos grandes do file select JP (tilemap/texto, `ValidateSRAM`, seleção de
arquivo, nome, `OnFileCreation`/`InitSRAMTable`, `Module05_LoadFile`) e o
começo do overworld (paletas, HUD, itens progressivos). É trabalho de várias
sessões, não de uma. A boa notícia é que o método
(watchpoint → endereço → hook → implementação → próximo frame) está provado, o
caminho normal continua idêntico e o portão agora mede a regressão da seed.

## 6. Verificação feita

- `make -j2`: compila com `-Werror`, sem warnings.
- `make -j2 NO_LUA=1`: compila com `-Werror`, sem warnings.
- `tools/regress.sh`: `regress: all 19 replays and 11 feature replays passed`,
  mais `ok seed-oracle (seed-xvJo08oxyJ) 512 frames` quando `japanese.sfc` e a
  seed existem.
- Chamadas de `EmuRunSeedRoutine` restantes no código: **0** (só a definição
  em `src/zelda_cpu_infra.c`, ninguém chama).
- Oráculo **estrito** com a seed e o replay novo, 512 frames: passa todos e
  termina em `final 512 3dc1c0d7`; o `--oracle-ignore-jp` não mascara mais
  nada.
- Oráculo estrito com 513 frames: passa 1–512 e para na digitação do nome JP
  (VRAM `$62F1`; o C escreve `1D8A`, a ROM `1D0D`).
- Antes do eco de APU, frame 261 entrava em loop em `$A4:C14A`; depois do
  `Snes.apu_port_echo` (só no oráculo seed) os frames 261–430 passam.
- Antes do atalho JP de `Start` (submódulo 4), frame 431 divergia em
  `0x10=0400`/`0x1C`/OAM; depois o C e a ROM entram no file select juntos.
- Antes do `Module_SelectFile_0` JP, frame 432 divergia em `$6000+`, `$BF`,
  VRAM `$2F00` e na paleta; depois esses bytes batem.
- Antes do ponteiro de NMI JP (`$937A/$9383/$938C`), frame 434 divergia na
  VRAM `$6062+`; depois o `$14=6` usa a tabela `$8C:DDE8` e o frame passa.
- Antes do `$A4:93C6` JP, frame 435 divergia em `$7E:1002/$1005/$1400` e no
  `INIDISP`/`nmi_disable_core_updates`; depois a tabela e a tela batem.
- Antes do `FileSelect_Main` JP, frame 436 divergia no OAM `0x800`
  (`0x4A1C != 0x4214`) e na VRAM `$2A9`; depois a fada `$14,$42` e o upload
  condicional batem.
- Antes do nome JP, frame 511 divergia em `selectfile_var7/8` e nos nomes da
  SRAM; depois `$3E`/`$019C`, `$018C` e `$80:834B`/`$A4:98DE` batem e os
  frames 511–512 passam.
- Antes dos gráficos JP do `Intro_InitializeMemory_darken`, frame 27 falhava
  em `0xBF..`, `0x6000..`, `0x78xx` e VRAM; depois o frame 27 passa.
- Antes do `LoadCommonSprites` JP, frame 28 falhava em VRAM `0x4400`; antes
  da posição JP do copyright, frame 29 falhava em `0x1E34=4C`; depois batem.
- Antes do caminho z3randomizer de `t=11`, frame 12 falhava em
  `0x1200`, `0xCF00`, `0x15005`, `0x15202`; depois esses bytes batem e os
  frames 12–26 passam.
- Antes de `SeedOracle_JpLoadChrHalfSlot`, frame 1 falhava em
  `0x11100..0x1111F` e VRAM `0x4680..0x468F`; depois esses bytes batem.
- Antes da sequência JP do intro (`t=10`/`t=11`), frame 11 falhava em
  `0x9486=01`, `0x9488=03`, ...; depois `$7E:9000+` bate e o frame 11 passa.
- Antes da reimplementação do tilemap (R3c), frame 10 falhava em
  `0x2008`, `0x200A`, `0x200C`; depois de
  `SeedOracle_JpLoadFileSelectTilemap` esses bytes batem.
- Antes da paleta: frame 9 falhava em `0xAAC=0B`, `0xAB3=05`, `0xAB8=00`,
  `0xC342..`; depois do `SeedOracle_FileSelectPalette` e do init `$82:8022`
  esses bytes batem.
- Antes do passo 3 do R3b (`g_seed_rom = 0`): frame 1 falhava em `0xBC`,
  `0xF42E`, `0x15072`, SRAM `0x1FFC/0x1FFE`; depois do passo 3 esses bytes
  batem.

## 7. Reprodução

```sh
python3 tools/alttpr_seed.py build \
  --jp-rom japanese.sfc \
  --base .agents/alttpr/download/base-79bbb4ad512838ab01fc06b7971f656c.bps \
  --seed .agents/alttpr/download/seed-xvJo08oxyJ.json \
  -o .agents/alttpr/download/seed-xvJo08oxyJ.sfc

make -j2
tools/regress.sh

./zelda3 --config tests/regress.ini --headless --no-mods \
  --oracle-rom .agents/alttpr/download/seed-xvJo08oxyJ.sfc \
  --frames 512 --hash-every 1
```
