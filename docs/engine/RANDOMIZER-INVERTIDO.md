# Modo invertido do ALttPR no motor em C

Data: 10/11/26. Este documento desenha o **mundo invertido** do ALttPR no
zelda3 e registra a primeira fatia implementada. O modo só existe quando as
duas condições valem ao mesmo tempo:

- um mod de seed ativo com `"features": {"Z3rRules": true}`;
- `z3r_settings.inverted = 1`, isto é, o byte `InvertedMode` (`$B0:804A`,
  PC `0x18004A`) da ROM da seed.

Sem essa combinação todo gancho novo devolve o valor original. A verificação
frame a frame do jogo sem mods continua sendo o portão.

## 1. O que o z3r muda

As referências usadas são `z3randomizer` (`inverted.asm`,
`invertedmaps.asm`, `invertedstatic.asm`, `bugfixes.asm`,
`darkworldspawn.asm`, `entrances.asm`, `flute.asm`, `glitched.asm`,
`music.asm`, `hooks.asm`, `tables.asm`, `initsramtable.asm`) e a tabela de
hooks em `.agents/refs/z3r/z3r-hooks.asm`.

Legenda de estado:

- **feito**: a primeira fatia já cobre o caso;
- **dados**: depende de bytes da seed ou de uma tabela estática que o
  importador ainda não lê;
- **novo**: comportamento novo do motor US, sem equivalente direto em C;
- **pendente**.

### 1.1 Início

| Origem no z3r | Endereço | Efeito no invertido | Motor US |
|---|---|---|---|
| `InvertedMode` | PC `0x18004A` | liga todo o modo | **feito**: `z3r_settings.inverted` |
| `InitSRAMTable` / `InitCurrentWorld` / `InitStartingEntrance` / `InitProgressIndicator` | PC `0x183000`, `0x1833CA`, `0x1833C8`, `0x1833C5` | o template da seed grava o mundo, a entrada inicial e o progresso | **feito**: `save_template` importado e applied; sem template, a regra da rodada 1 |
| `AllowStartFromSingleEntranceCave` | `$82:D9B9` (PC `0x159B9`) | posiciona o Link em cavernas de uma entrada usando `StartingAreaExitTable` | **feito** para a saída da caverna de início; entrance shuffle pendente |
| `AllowStartFromExit` | `$82:8496` (PC `0x10496`) | `ShouldStartatExit` para começar na saída da entrada | **feito**: `Module1B_SpawnSelect` troca para o modo overworld |
| `StartingAreaExitOffset` | PC `0x180240` | linha da `StartingAreaExitTable` por ponto de início | **feito**: importado e aplicado no início |
| `StartingAreaOverworldDoor` | PC `0x180247` | porta de overworld do início em caverna | **feito** no importador (`ModSeedImport_StartingDoor`) |
| `ShouldStartatExit` | PC `0x180350` | começa na saída da entrada | **feito**: regra + prova C |
| `StartingAreaExitTable` | PC `0x180250` | sala, área e coordenadas de saída da caverna | **feito**: linha 0 usada no início da seed |

Comportamento sem template (seed só de settings): o arquivo novo nasce com
`CurrentWorld = $40`, `StartingEntrance = 0`, `ProgressIndicator = 2` e
`ProgressFlags = $14`. O carregamento (`Module05_LoadFile`) troca a sala de
respawn padrão do Mundo das Trevas (a pirâmide, sala `0x20` → tela `$5B`)
pela sala da casa (`0x104`); a saída usa a linha da casa na `kExitData`
(tela `$2C`) e `ModInverted_ExitScreen` troca para `$6C`.

Com a seed real `LyVe6KOAvb` o template tem `StartingEntrance=1` e a linha 1
do starting point é a sala `0x112` (a Sanctuary invertida, uma caverna). A
linha 0 da `StartingAreaExitTable` leva à tela `$53` (`Unctuary Grounds`). O
motor C carrega a sala `0x112` e sai para `$53`, igual ao emulador; o menu
de spawn do ROM (módulo 27) é pulado no C, que carrega direto a linha 1. A
regra de `0x104`/`$6C` fica só para o caso sem template.

### 1.2 Pérola da Lua e coelho

O z3r troca a decisão `DecideIfBunny`:

| Origem no z3r | Endereço | Efeito | Motor US |
|---|---|---|---|
| `DecideIfBunny` chamado no carregamento do overworld | `$82:8413` (PC `0x10413`) | sem pérola, o Mundo das Trevas vira coelho no normal; no invertido quem vira coelho é o Mundo da Luz | **feito** em `PreOverworld_LoadProperties` |
| `DecideIfBunnyByScreenIndex` | `$87:AA44` (PC `0x3AA44`) | mesma regra na travessia de mundo (espelho) | **feito** em `LinkState_CrossingWorlds` |
| `DecideIfBunny` no feixe de coelho | `$87:8F32` (PC `0x38F32`) | o feixe continua temporário, mas só é permanente no mundo que exige a pérola | **feito** em `Link_SplashUponLanding` |
| `FixAga2Bunny` | `$82:9E7C` (PC `0x11E7C`) | depois do Agahnim 1, troca o mundo e decide o coelho | **feito**: `KillAghanim_Func12` + prova gdb |
| `FixBunnyOnExitToLightWorld` | `bugfixes.asm` | corrige o coelho ao sair do Mundo das Trevas | **pendente** |
| `SetDeathWorldChecked` / `DoWorldFix` | `darkworldspawn.asm` | morte antes do Agahnim 1 volta para o lado inicial; no invertido, o Mundo das Trevas | **feito**: `Death_Func15` com bugfix de dungeon e save&quit; prova gdb |
| `Ancilla23_LinkPoof` | `$88:D3BC` (PC `0x453BC`) | o poof de transformação escolhe o mundo pela pérola | **feito** |
| `music.asm` | `PreOverworld_LoadProperties`, `Overworld_FinishMirrorWarp`, `BirdTravel_LoadTargetAreaMusic` | tema de coelho no mundo que exige a pérola | **feito parcial** nos dois primeiros; o pássaro falta |

Regra implementada: `ModInverted_PearlRequired(world)` devolve
`(world >= $40)` no jogo normal e `((world & $40) == 0)` no invertido. Ela é
aplicada nos pontos que decidem `link_is_bunny`, `link_player_handler_state`
e a música de coelho. A RAM de estado (`link_item_moon_pearl`,
`link_is_bunny*`) e o save (`$7EF357`) são os mesmos.

### 1.3 Entradas, saídas e passagens

| Origem no z3r | Endereço | Efeito | Motor US |
|---|---|---|---|
| `LockAgahnimDoors` | `$9B:BC94` (PC `0xDBC94`) | no invertido, a porta da Ganon's Tower considera o overlay e o botão A | **feito**: `ModSeedImport_AgahnimDoorLocked` + prova gdb |
| `FlagAgahnimDoor` | `$89:9BBA` (PC `0x49BBA`) | liga o bit `$20` de `OverworldEventDataWRAM+$43` no invertido | **feito**: overlay + `ScrapBuffer72=0x28` ao abrir a porta |
| `PreventEnterOnBonk` | `$9B:BD05` (PC `0xDBD05`) | não entra em portas durante o bonk do espelho | **pendente** |
| `AnimatedEntranceFix` | `$9B:CAC4` (PC `0xDCAC4`) | pula animações de entrada no Mundo da Luz invertido | **pendente** |
| `CheckHole` / `ExtraHole_*` | PC `0x180300` | buracos extras para entradas embaralhadas | **feito**: `Overworld_GetPitDestination` consulta os pares antes do Houlihan; prova gdb `123` vs `130`; queda real pendente |
| `TurtleRockEntranceFix` | `hooks.asm` | abre a entrada da Turtle Rock ao sair | **pendente** |
| `TurtleRockPegSolved` | `$84:E7AE` (PC `0x267AE`) | quebra-cabeça dos pegs já resolvido | **feito**: `HandlePegPuzzles` + prova gdb; emulador pendente |
| `HardcodedRocks` | `$82:EC8D` (PC `0x16C8D`) | remove a pedra do Mundo da Luz e cria a do Mundo das Trevas | **feito** + prova gdb |
| `TurtleRockPegs` / `PegProbability` | `$84:E7B9` | pegs e probabilidade | **pendente** |

As trocas de porta/caverna que o briefing chama de "a casa e a Sanctuary
mudam de lugar" não estão em `inverted.asm`: o z3r só tem o gancho de início
e os patches de mapa. A troca de portas/entradas vem da lista `patch` da seed
(tabelas de entrada/saída e portas de overworld). O importador ainda não lê
essas tabelas; a primeira fatia resolve só a saída da casa (`$2C` → `$6C`).
Para uma seed invertida real, o caminho correto é importar a tabela de
entradas/saídas da ROM (como já se faz com baús e lojas), não fixar mais
telas no C.

### 1.4 Tiles, colisão e passagens do overworld

| Origem no z3r | Endereço | Efeito | Motor US |
|---|---|---|---|
| `Overworld_LoadNewTiles` | `$82:ED51` e `$82:EC2E` (PC `0x16D51`, `0x16C2E`) | aplica os patches de tilemap por tela | **feito**: os writes são importados da ROM em `overworld_patches` e aplicados em `Overworld_LoadAmbientOverlay` |
| `OverworldMapChangePointers` | `invertedmaps.asm` (banco `$A4`) | 29 telas: LW 03,05,07,0E,10,14,1B,29,30,31,32,33,35,38,3A,3C; DW 43,44,45,47,4E,50,54,5B,6F,70,73,75,78 | **feito**: 29 telas importadas |
| comandos de stripe / RLE / RLEINC / cópia / skip | `invertedmaps.asm` | formato de dados dos patches | **feito** no parser do importador |
| `InvertedTileAttributeLookup` / `Inverted_TR_TileAttributes` | `$A1:A000` (`invertedstatic.asm`) | colisão especial da Turtle Rock no invertido | **feito**: tabela importada e usada no estágio map8 (`$80:886E`) na tela `0x47`; prova C + emulador |
| `Overworld_CreatePyramidHoleModified` | `$9B:C2A7` (PC `0xDC2A7`) | buraco da pirâmide em posições diferentes | **feito** + prova gdb |
| `Draw_PyramidOverlay` | `$87:FF5F` (PC `0x3FF5F`) | não desenha o overlay da pirâmide no invertido | **feito** no `Overworld_LoadEventOverlay`; sem prova visual |
| `map5B_pick_warp_tile` | `invertedmaps.asm` | escolhe o tile do warp conforme o progresso | **feito**: regra `warp` no dado importado |
| `map1B_check_aga` | `invertedmaps.asm` | escolhe o mapa do castelo conforme o evento da torre | **feito**: listas `hole`/`no_hole` no dado importado |

O patch de tiles é dados estáticos do `z3randomizer`, não dados por seed:
pode ser portado uma vez para uma tabela C ou extraído dos bytes da ROM da
seed. O que muda por seed é o gatilho `InvertedMode` e, quando houver
entrance shuffle, a tabela de entradas.

### 1.5 Pirâmide, castelo, Agahnim e Ganon's Tower

| Origem no z3r | Endereço | Efeito | Motor US |
|---|---|---|---|
| `GanonTowerInvertedCheck` | `$9A:F5C1` (PC `0xD75C1`) | no invertido a cachoeira não some/troca como no normal | **feito** no teste da cachoeira; sem prova dinâmica |
| `GanonTowerAnimation` | `$88:CDAC` (PC `0x44DAC`) | animação alternativa da quebra do selo | **feito** + prova gdb |
| `Electric_Barrier` | `$86:891E` (PC `0x3091E`) | marca a barreira elétrica como morta | **feito** + prova gdb |
| `GetAgahnimDeath` / `Bugfix_SetWorldOnAgahnimDeath` | `glitched.asm` | no invertido troca o mundo para Luz depois do Agahnim 2 e para Trevas depois do Agahnim 1 | **feito parcial**: a troca de `PrepareDungeonExitFromBossFight` já inverte o bit; falta o Agahnim 2 e a opção de bugfix |
| `MosaicLevel` / `GanonPyramidRespawn` | `darkworldspawn.asm` | respawn da pirâmide e morte em dungeon | **pendente** |
| `GoalItemIcon` / `GanonsTowerOpenTarget` | `goalitem.asm`, PC `0x180165`, `0x18019A` | abertura da torre por cristais | **feito** pela F4e/R4 (não é específico do invertido) |

### 1.6 Espelho

| Origem no z3r | Endereço | Efeito | Motor US |
|---|---|---|---|
| `MirrorBonk` | `$87:A9AC` (PC `0x3A9AC`) | tabela de retângulos que força o bonk do espelho no invertido | **feito**: regra no uso real do espelho (`DoSwordInteractionWithTiles_Mirror`); prova C dentro/fora |
| `PreventEnterOnBonk` | `$9B:BD05` | não entra em porta no bonk do espelho | **pendente** |
| `LinkState_CrossingWorlds` | `player.c` (hook `$87:AA44`) | a travessia escolhe coelho pelo mundo invertido | **feito** na regra do coelho |
| `FixBunnyOnExitToLightWorld` | `bugfixes.asm` | restaura Link ao sair do Mundo das Trevas | **pendente** |
| `FixAga2Bunny` | `$82:9E7C` | coelho depois do Agahnim 2 | **feito** + prova gdb |

### 1.7 Flauta e pato

| Origem no z3r | Endereço | Efeito | Motor US |
|---|---|---|---|
| `FreeDuckCheck` | `$87:A3E2` (PC `0x3A3E2`) | no invertido, ativa o pato no weathervane de Kakariko sem a música, se a flauta estiver ativa | **pendente** |
| `IncrementFlute` / `FloodGateAndMasterSwordFollowerReset` | `hooks.asm` | correções de flauta/floodgate que convivem com o invertido | **pendente** |

### 1.8 Textos

| Origem no z3r | Endereço | Efeito | Motor US |
|---|---|---|---|
| ponteiro do letreiro esquerdo do HC invertido | `$87:F52D` (PC `0x3F52D`) | mensagem `$190` | **feito**: traduzida para `$3C`, diálogo importado e tabela de sign aplicada |
| ponteiro do letreiro direito do HC invertido | `$87:F52F` (PC `0x3F52F`) | mensagem `$191` | **feito**: traduzida para `$3E`, diálogo importado e tabela de sign aplicada |
| `textrenderer.asm` / `newfont_inverted.bin` | banco `$B3` | fonte alternativa para o texto invertido | **feito**: `NewFontInverted` importada em `content/font.png`; screenshot em `.agents/shots/r5-inv-font-side.png` |
| `Music_HC`, `Music_TRock*` etc. | `tables.asm` | temas por área | **pendente** |

### 1.9 Música e mundo

| Origem no z3r | Endereço | Efeito | Motor US |
|---|---|---|---|
| `PreOverworld_LoadProperties` (música) | `music.asm` | tema de coelho no mundo que exige a pérola | **feito parcial** |
| `Overworld_FinishMirrorWarp` | `music.asm` | mesma regra no fim do warp do espelho | **feito parcial** |
| `BirdTravel_LoadTargetAreaMusic` | `music.asm` | mesma regra no pássaro | **pendente** |
| `DoWorldFix` / `SetDeathWorldChecked` | `darkworldspawn.asm` | mundo ao carregar/morrer | **feito** em `Death_Func15`; prova gdb |
| `FixFakeWorld` | `bugfixes.asm` | corrige o mundo falso | **pendente** |

## 2. Dados da seed versus comportamento novo

### 2.1 O que dá para importar da ROM da seed

- `InvertedMode` em PC `0x18004A` (já importado como
  `z3r_settings.inverted`).
- O template de SRAM em PC `0x183000..0x1834FF` (já importado como
  `save_template`), incluindo `CurrentWorld`, `StartingEntrance`,
  `ProgressIndicator`, `ProgressFlags`, itens iniciais e capacidades.
- `StartingAreaOverworldDoor` em PC `0x180247`, `StartingAreaExitOffset`
  em `0x180240`, `StartingAreaExitTable` em `0x180250`, `ShouldStartatExit`
  em `0x180350` e `ExtraHole_*` em `0x180300` (importados).
- As tabelas de starting point de `$82:D8D2` (7 linhas) e as 29 telas de
  `OverworldMapChangePointers` (lidas da ROM e aplicadas no C).
- Os 23 retângulos do `MirrorBonk` (dado importado e regra aplicada em `LinkState_CrossingWorlds`).
- Textos e dicas: o importador de texto já lê o bloco JP e gera
  `kDialogue`; os letreiros do HC invertido são duas mensagens a mais.
- Baús, lojas, NPCs e prêmios: já cobertos pela F4e/R4, independentes do
  modo.

### 2.2 O que é comportamento estático do z3r

Estes itens não mudam de seed para seed; são a implementação do modo no
patch-base:

- os 29 patches de tilemap de `invertedmaps.asm`;
- a colisão especial da Turtle Rock (`invertedstatic.asm`);
- a tabela de bonk do espelho (`MirrorBonk`);
- a animação da torre, a barreira elétrica, o buraco/overlay da pirâmide;
- os pegs da Turtle Rock e as pedras fixas;
- a ativação do pato no weathervane;
- os checks de música e de coelho.

Podem ser portados para C uma vez (dados + ganchos) ou extraídos da ROM da
seed como um blob estático. Como o motor C é a reimplementação US, o
caminho natural é portar os dados para `src/mod/` e manter os ganchos
no-op sem `Z3rRules` + `InvertedMode`.

### 2.3 O que é novo no motor US

- O caminho de arquivo novo do Mundo das Trevas no motor US sempre usa o
  respawn da pirâmide. O invertido precisa de uma regra de início (feita na
  primeira fatia para o caso sem template).
- A saída da casa (`kExitData` → tela `$2C`) precisa virar `$6C` no
  invertido (feito).
- A decisão de coelho está espalhada por `overworld.c`, `player.c`,
  `ancilla.c`, `messaging.c` e `misc.c`; no z3r ela é centralizada em
  `DecideIfBunny`/`DecideIfBunnyByScreenIndex`. A primeira fatia criou o
  helper `ModInverted_PearlRequired` e aplicou nos pontos principais.
- O motor US não tem a tabela de entradas/saídas do z3r nem o formato de
  `StartingAreaExitTable`; as tabelas de starting point, a saída da caverna
  e os tilemaps das telas agora são importados. A tabela normal de
  entradas/saídas e o entrance shuffle ainda exigem importação/ganchos.

## 3. Fatias implementadas

### 3.1 Rodada 1: portão, início e pérola/coelho

Arquivos novos:

- `src/mod/inverted.h`, `src/mod/inverted.c`: gate, regra da pérola, regra
  de arquivo novo e troca da tela de saída da casa.

Arquivos alterados:

- `src/select_file.c`: chama `ModInverted_OnNewSave` depois de aplicar o
  template (ou a inicialização US).
- `src/misc.c`: `Module05_LoadFile` consome o início invertido; a regra de
  mundo na morte antes do Agahnim 1 foi invertida.
- `src/overworld.c`: `PreOverworld_LoadProperties` e o fim do warp do
  espelho usam `ModInverted_PearlRequired`; a música de coelho acompanha o
  mundo que exige a pérola.
- `src/player.c`: `LinkState_CrossingWorlds`, `Link_SplashUponLanding` e o
  reset pós-morte usam a regra invertida.
- `src/ancilla.c`: o poof de transformação (`Ancilla23_LinkPoof`) usa a
  regra invertida quando o mod está ativo.
- `src/messaging.c`: a morte antes do Agahnim 1 define o mundo invertido e
  força/solta o coelho conforme a pérola.

Provas da rodada 1 (settings forjado, sem template): início no Mundo das
Trevas com `savegame_is_darkworld=0x40`, `overworld_screen_index=0x6C`,
`link_is_bunny=0` sem pérola; o mesmo save na tela `0x13` sem pérola vira
`link_is_bunny=1`; com `inverted=0` o `state` é idêntico ao jogo sem mods.
Screenshot: `.agents/shots/r5-inverted-start.ppm`.

### 3.2 Rodada 2: seed invertida real

Arquivos novos/alterados:

- `tools/alttpr_seed.py` passou a ler da ROM da seed as tabelas que o
  invertido usa como dado:
  - `starting_points`: o bloco de 7 linhas em `$82:D8D2` (rooms,
    `relative_coords`, scroll, player/camera, blockset, floor, palace,
    startingBg, quadrantes e portas);
  - `overworld_patches`: as 29 telas de `OverworldMapChangePointers`
    interpretadas pelo mesmo command stream do z3r (stripe, RLE,
    RLEINC, cópia, skip/skip-if/inverted-only e comandos customizados);
  - `mirror_bonk`: 23 retângulos do `MirrorBonk` (tabela do build);
  - `z3r_settings.starting_area_exit_offset`, `starting_area_exit_table`,
    `should_start_at_exit` e `extra_hole_*`.
- `src/mod/seed_import.c/.h`: carrega essas tabelas, aplica as tabelas de
  starting point por cima dos assets `kStartingPoint_*` e os writes de
  tilemap em `g_ram`.
- `src/misc.c`: o arquivo novo com `StartingEntrance=1` e
  `StartingAreaExitOffset[1]!=0` carrega a sala do starting point
  (`0x112`) pelo caminho normal; `Dungeon_LoadEntrance` aplica a linha da
  `StartingAreaExitTable` no cache de saída (`AllowStartFromSingleEntranceCave`).
- `src/dungeon.c`: `ModInverted_ApplyStartingAreaExit` depois do bloco de
  starting point; não altera `RoomIndex` (a sala vem da tabela importada).
- `src/overworld.c`: `ModInverted_ApplyOverworldPatches` depois dos dois
  `Map16ToMap8` que correspondem aos hooks `$82:EC2E`/`$82:ED51`.

Resultado com a seed `LyVe6KOAvb`:

- import: **251/251** locais (chest 167, fixed 44, prize 10, shop 29,
  uncle 1), com a décima loja de id `0xFF` (`Dark World Lake Hylia Shop`)
  recuperada. O spoiler tem 255 entradas de item: 251 batem com a ROM e 4
  são locais de NPC/item solto (como Sahasrahla e Sick Kid) fora das tabelas
  fixas do z3r. Há ainda 4 requisitos especiais (medalhões), que não são
  locais de item.
- início: o template da seed tem `ProgressIndicator=2`,
  `StartingEntrance=1` e `CurrentWorld=0`; o runtime do SNES fica com
  `CurrentWorld=0x40`. A linha 1 do starting point é a sala `0x112`
  (Storyteller Cave/General Store 2), e a linha 0 da
  `StartingAreaExitTable` leva à tela `0x53` (`Unctuary Grounds`). O motor
  US e o emulador terminam na mesma sala `0x112`, com 3 corações, sem
  espada/escudo e sem coelho, e saem para a mesma tela `0x53`.
- mapa: 0x5B (castelo no lugar da pirâmide), 0x43 e 0x47 mostram o mesmo
  tilemap do emulador. A tela 0x47 usa a tabela
  `Inverted_TR_TileAttributes` importada; a paleta main/aux12 é importada,
  e Aux3 continua US (a tabela JP não está no mesmo endereço na ROM da
  seed).

Screenshots lado a lado (emulador à esquerda, motor C à direita), fora do
git em `.agents/shots/`:

- `r5-inv-side-start.png` — início na sala `0x112`;
- `r5-inv-side-exit.png` — saída para a `Unctuary Grounds` (`0x53`);
- `r5-inv-side-5b.png` — tela `0x5B` com o castelo invertido;
- `r5-inv-side-43.png` — tela `0x43`;
- `r5-inv-side-47.png` — tela `0x47` (Turtle Rock).

Para chegar às telas 0x5B/0x43/0x47 foi usada, só nas provas, uma cópia da
ROM da seed com a linha 0 da `StartingAreaExitTable` apontando para a tela
alvo; a tabela de tilemap e o restante da ROM são os da seed. Essa cópia
não entra no git.

### 3.3 Rodada 3: merge com a R4 e pendências

- O merge com `engine/modloader` manteve as duas famílias de dados em
  `seed_import.c/.h`: as tabelas do invertido e as flags/contadores da R4
  (`total_item_count`, `crystal_pendant_flags`, `GenericKeys`, eventos
  `SeedSetting` etc.).
- `Inverted_TR_TileAttributes`: a tabela de 512 bytes é importada
  (`tr_tile_attributes`) e usada por `Overworld_GetTileAttributeAtLocation`
  na tela `0x47` quando o invertido está ativo.
- `MirrorBonk`: os 23 retângulos são importados e a regra foi aplicada em
  `LinkState_CrossingWorlds` (bonk com `RecoilWall` + `LinkState_Recoil`).
  A prova dinâmica de um retângulo ainda não foi feita.
- `ShouldStartatExit`/`ExtraHole_*`: a rodada 4 ligou as duas regras; ver
  3.4.
- A tela `0x43` foi refeita no mesmo estado: o C em `link_y=0xA0`,
  `BG2VOFS=0x40`, `camera_y=0xAF` e o emulador forçado aos mesmos valores
  via NWA. As duas imagens ficam iguais; o bege da comparação anterior era
  o fade/transição, não a paleta. No estado estável o C e o emulador usam
  os mesmos índices (`palette_mode=3`, aux 18/19/4). As tabelas JP×US de
  `kPalette_OverworldBgMain`/`BgAux12` diferem em entradas não usadas pela
  tela `0x43`; a tabela Aux3 não existe no mesmo endereço na ROM JP
  (z3r), então a importação de paleta cobre main/aux12 e deixa Aux3 em US.
- As paletas main/aux12 são importadas em `overworld_palettes` e
  aplicadas nos assets 87/88; a tela `0x43` não muda com isso, o que
  confirma que a diferença antiga era de estado.

### 3.4 Rodada 4: colisão TR, MirrorBonk, start-at-exit e ExtraHole

- `Inverted_TR_TileAttributes` foi religado no ponto correto. O JML da seed
  em `$80:886E` substitui a leitura de `$8F:FD94` (map8 -> atributo), não
  `overworld_tileattr`. O C agora faz
  `rv = ModSeedImport_TileAttribute(t & 0x1FF, GetMap8toTileAttr()[t & 0x1FF])`.
  A tentativa anterior trocava `overworld_tileattr` e por isso não mudava a
  colisão.
- Prova dinâmica da TR: estado forçado na tela 0x47 em `x=0xF08`, `y=0x80`,
  com Down. No C com a tabela importada o Link termina em `y=0xF8`; no C
  com a tabela JP base (`$8F:FD94`) ele fica em `y=0x80`. O emulador da
  seed real, no mesmo estado (NWA), também termina em `y=0xF8`. A prova C
  virou `inverted-proof tr-step (0x47)` no regress.
- `MirrorBonk`: a prova da rodada 4 estava errada. O estado 20 não é
  `LinkState_CrossingWorlds`; no C ele aponta para `LinkState_0F`
  (assert/stub). `LinkState_CrossingWorlds` é o estado 25, mas o caminho
  atual do espelho nunca o define. A prova foi removida do regress. A
  função `ModInverted_MirrorBonk` foi conferida por gdb: retorna 1 dentro
  do retângulo `(0x600,0x610)` da tela 0x43 e 0 fora. Falta ligar o
  caminho real do espelho; ver 3.5.
- `ShouldStartatExit`: `Module1B_SpawnSelect` usa a escolha do menu; quando
  a flag está ligada, troca para `main_module_index=8` com a sala do ponto
  de início, e `LoadOverworldFromDungeon` carrega a saída. Prova C:
  escolha 2 com a flag -> sala 0xF1, tela 3, `player_is_indoors=0`, módulo 8;
  com a flag zerada -> interior, módulo 7. Virou
  `inverted-proof should-start-at-exit`.
- `ExtraHole_*`: `Overworld_GetPitDestination` consulta os 16 pares
  `pos/area` antes do fallback Houlihan. A importação no C estava
  incompleta (o JSON tinha os arrays, mas `ParseZ3rSettings` não os
  copiava); a rodada 4 passou a preencher `extra_hole_map16/area/entrance`.
  Prova dinâmica com gdb na seed real: com `(0x140, 0x1B) -> 123`,
  `Overworld_GetPitDestination` deixa `which_entrance=123`; com o map16
  desativado (`0xFFFF`) cai no Houlihan (`130`). A queda a partir de uma
  tela carregada pelo caminho normal ainda não foi provada.
- A tela 0x43 continua como na rodada 3: a diferença era fade; ver 3.3.

Provas executadas:

- TR: C com a tabela importada termina em `y=0xF8`; C com a tabela JP base
  (`$8F:FD94`) fica em `y=0x80`. No emulador da seed real, com a mesma
  posição `x=0xF08,y=0x80` e Down, o `link_y` final também é `0xF8`
  (NWA pause/write/resume; a ROM de prova só tem a linha 0 da
  `StartingAreaExitTable` apontando para a tela 0x47).
- ExtraHole: chamada direta a `Overworld_GetPitDestination` via gdb na seed
  real, com `pos=0x140`, `area=0x1B`: com o par importado
  `which_entrance=123`; com `extra_hole_map16[0]=0xFFFF`,
  `which_entrance=130` (Houlihan).
- ShouldStartatExit: linha `ok inverted-proof should-start-at-exit` do
  `tools/regress.sh`.
- MirrorBonk: gdb `ModInverted_MirrorBonk(0x600,0x610)=1` e
  `(0x100,0x100)=0` na tela 0x43; a prova de movimento não é válida
  porque o estado 20 é `LinkState_0F`, não `CrossingWorlds`.

### 3.5 Rodada 5: Agahnim, torre e morte pós-Agahnim

- `Electric_Barrier`: `SpritePrep_AgahnimsBarrier` liga
  `save_ow_event_info[screen] |= 0x40` quando o invertido está ativo.
  Prova gdb: tela 0x43, evento 0x40 ligado e `sprite_graphics[0]=4`.
- `GanonTowerInvertedCheck`: `Waterfall` só chama
  `AncillaAdd_GTCutscene` quando o invertido **não** está ativo e a tela é
  0x43.
- `GanonTowerAnimation`: em `Ancilla43_GanonsTowerCutscene` o ramo invertido
  troca o cutscene por `sound_effect_2=0x1b`, `music_control=2`,
  `sound_effect_1=9` e não incrementa o passo. Prova gdb: invertido
  `sfx2=1b sfx1=9 music=2 special=0 step=2`; normal
  `special=5 step=3`.
- `HardcodedRocks`: `Overworld_HandleOverlaysAndBombDoors` não escreve as
  duas pedras nas telas 0x33/0x2F no invertido. Prova gdb: invertido
  `dung_bg2[340]=0`; normal `0x20f`.
- `Overworld_CreatePyramidHoleModified`: o buraco invertido usa
  `0x440/0xE39`, `0x4BC/0xE3A`, `0x53C/0xE3A` e `0x5BE/0x490`. Prova gdb:
  invertido `p440=e39 p4bc=e3a p53c=e3a p5be=490`; normal `p3bc=e3f
  p440=e44`.
- `Draw_PyramidOverlay`: `Overworld_LoadEventOverlay` não desenha os tiles
  0xE3F..0xE47 nos casos 90/91/92/99/100 no invertido.
- `LockAgahnimDoors`: `ModSeedImport_AgahnimDoorLocked` implementa os
  estilos 0/1/2, a posição do Link, a torre aberta e o overlay. Prova gdb:
  `atdoor_locked=1` (com estilo 2, sem cristais), `overlay_locked=0` e
  `away_locked=0`.
- `FlagAgahnimDoor`: ao abrir a porta no invertido o C liga
  `save_ow_event_info[0x43] |= 0x20`; o `ScrapBuffer72=0x28` do hook
  original não foi portado.
- `FixAga2Bunny`/morte pós-Agahnim: `KillAghanim_Func12` usa
  `fix_fake_world` e `bugfix_set_world_on_agahnim_death`; o mundo vira Luz
  no invertido e Trevas no normal, e o coelho segue a regra da pérola.
  Prova gdb: invertido `world=0 handler=17 bunny=1 music=4`; normal
  `world=40 handler=17 bunny=1 music=4`.
- `Death_Func15`: prova gdb com progresso 2 -> mundo 0x40; progresso 3 ->
  mundo inalterado (0). O caso de dungeon com bugfix e save&quit foi
  completado na rodada 6; ver 3.6.

Provas executadas na rodada 5:

- `Electric_Barrier`: `event40=64 gfx=4`.
- `GanonTowerAnimation`: `inv sfx2=1b sfx1=9 music=2 special=0 step=2`;
  `normal sfx2=0 sfx1=0 music=0 special=5 step=3`.
- Pirâmide: `inv p440=e39 p4bc=e3a p53c=e3a p5be=490`; normal
  `p3bc=e3f p440=e44`.
- Pedras: `inv rock33=0`; `normal rock33=20f`.
- Porta: `atdoor_locked=1 overlay_locked=0 away_locked=0`.
- Aga 2: `inv world=0 handler=17 bunny=1 music=4`; normal `world=40...`.
- Morte: `preaga world=40 postaga world=0`.

- `ExtraHole`: a tela 0x1B carregada normalmente no ponto `pos=0x140`
  tem map16 `0x463` (atributo normal 1), não um poço. Por isso a queda real
  não acontece nesse par; a prova de função (gdb, `123` vs `130`) segue
  válida, mas o par pode ser um buraco de outra entrada/modo. A queda real
  continua pendente.

Achado da rodada: a prova de `MirrorBonk` da rodada 4 não exercitava
`LinkState_CrossingWorlds`. O handler 20 é `LinkState_0F` (assert/stub) e
o handler 25 (`CrossingWorlds`) nunca é definido pelo caminho atual do
espelho. A linha `inverted-proof mirror-bonk` foi removida do regress.
`ModInverted_MirrorBonk` continua correto em nível de função (gdb:
1 dentro / 0 fora), mas falta ligar o caminho de uso do espelho.

### 3.6 Rodada 6: espelho, morte, HC e NPCs

- **Espelho real**: `DoSwordInteractionWithTiles_Mirror` consulta
  `ModInverted_MirrorBonk` antes de decidir a troca de mundo. A prova C usa
  o espelho de verdade: com `current_item_y=20`, `link_item_mirror=2` e Y na
  tela 0x43, dentro do retângulo `(0x600,0x610)` o Link termina em
  `link_player_handler_state=2`/`submodule_index=0`; fora, em 20/35 (warp
  normal). Virou `inverted-proof mirror-bonk use`. O hook antigo em
  `LinkState_CrossingWorlds` continua existindo, mas não é necessário para
  o bonk.
- **Morte em dungeon/save&quit**: `Death_Func15` deixa de forçar o mundo em
  morte real de dungeon quando `bugfix_pre_aga_dw_dungeon_death` está
  ligado; save&quit (`count_as_death=false`) e morte outdoor continuam
  aplicando `DoWorldFix`. Prova gdb: bugfix ligado -> `bugfix_dungeon=0`;
  desligado -> `40`; `savequit=40`; `outdoor=40`.
- **FlagAgahnimDoor**: ao abrir a porta, o C liga
  `save_ow_event_info[0x43] |= 0x20` e grava `scratch_b=0x28`, fechando o
  restante do hook.
- **Textos do HC**: a tabela de sign da seed traz `$190/$191` nas posições
  8/9 (PC `$87:F52D/$F52F`). Como o motor US não tem essas mensagens, o
  importador traduz para os IDs US `$3C/$3E` e sobrescreve o diálogo 60/62
  com o texto JP importado; `ModSeedImport_ApplySignText` grava a tabela
  `kOverworld_SignText[8/9]`. Prova gdb: `hc8=3c hc9=3e` no invertido e
  `a7/a7` no baseline; `ok inverted-proof hc-sign dialogue 0x3C/0x3E`.
- **4 NPCs**: `read_npc_items` lê `Old Man:1`, `Sick Kid:1`, `Sahasrahla:1`
  e `Catfish:1` do spoiler e grava `npc_items` (`0x1F`, `0x16`, `0x44`,
  `0x17` nesta seed). `ModSeedImport_NpcItem` é usado nos quatro sprites
  (Old Man, Sick Kid, Sahasrahla e Catfish). Prova C: forçando o sprite do
  Sahasrahla no estado de entrega, a seed dá 10 flechas
  (`link_num_arrows=8` após o clamp) e o baseline sem `npc_items` dá as
  botas (`link_item_boots=1`, `link_num_arrows=0`). Virou
  `inverted-proof npc-item sahasrahla`.
- **Regress**: 65 linhas `ok`, incluindo as novas provas de espelho, HC e
  NPC.

### 3.7 Rodada 7: pegs, fonte, NPCs e rota

- **TurtleRockPegSolved**: `HandlePegPuzzles` retorna sem alterar som/estado
  quando o invertido está ativo, como o `$20` do hook z3r. Prova gdb com
  `screen=7`, `event[7]=0` e `word_7E04C8=0`: invertido deixa
  `word=0/sfx=0`; normal faz `word=2/sfx=0x2d`. A comparação no emulador não
  foi concluída; o estado forjado C está documentado.
- **Fonte invertida**: a seed carrega `NewFontInverted` em `$B3:AC00`.
  `tools/alttpr_seed.py` gera `content/font.png` a partir dos 4096 bytes e o
  modkit empacota em `kDialogueFont`. O modkit agora força
  `allow_resize` para `kDialogueFont` (a asset base tem dois idiomas e o
  override tem um). Prova visual:
  `.agents/shots/r5-inv-font-side.png` (esquerda fonte base, direita
  invertida) e `ok inverted-proof font NewFontInverted`.
- **NPCs**: Old Man (`0x1F`), Sick Kid (`0x16`), Sahasrahla (`0x44`) e
  Catfish (`0x17`) saem de `npc_items`. Provas C:
  - Old Man: `link_item_moon_pearl=1`, `link_item_mirror=0`; baseline
    `moon_pearl=0`, `mirror=2`.
  - Sick Kid: `link_item_bug_net=0`; baseline `1` (o item 0x16 é passado
    pelo código).
  - Sahasrahla: 10 flechas (`link_num_arrows=8` após o clamp) contra botas
    no baseline.
  - Catfish: `ModSeedImport_NpcItem(3)=0x17`; gdb
    `Link_ReceiveItem(0x17)` incrementa `link_heart_pieces`, enquanto o
    baseline `0x11` (Quake) não incrementa. O spawn dinâmico do peixe não
    foi provado; a atribuição em `Catfish_RegurgitateMedallion` usa o valor.
- **Rota longa**: `seed_route.py --c-only` abriu o baú 0 da sala 170 do mod
  invertido (`Progressive Lifting Glove`, transformado em Power Glove
  `0x1B`); o tool passou a aceitar essa substituição progressiva. O modo
  C-only do tool só busca a partir de um snapshot de masmorra; a rota
  start→primeira masmorra pelo overworld não foi concluída. O que travou:
  o snapshot de `seed-key.sav` já está na sala 170, e o search de overworld
  usa `--seed-rom`, não o mod; falta uma rota de entrada/porta.
- **Regress**: 68 linhas `ok` (fonte, Old Man, Sick Kid, Sahasrahla,
  espelho, TR, start-at-exit etc.).

## 4. Estimativas de tamanho

| Parte | Estimativa |
|---|---:|
| Regra de início + saída da casa (rodada 1) | ~70 linhas C |
| Regra da pérola/coelho (feita) | ~60 linhas C + ~10 pontos |
| Tilemaps invertidos (29 telas) | dados no `seed.json` (~2.800 writes); ~100 linhas de loader/aplicação no C |
| Colisão da Turtle Rock | ~40 linhas + 512 bytes |
| Starting points + saída de caverna (feito) | ~150 linhas C + dados no `seed.json` |
| Pirâmide/castelo/Agahnim/torre | ~250 linhas |
| Espelho/bonk | ~150 linhas |
| Flauta/pato | ~80 linhas |
| Música | ~100 linhas |
| Textos/letreiros | ~50 linhas |
| Provas/regressão | ~150 linhas de shell + fixtures |

Total: cerca de **1.000–1.500 linhas** de C novo, mais os dados importados
(que ficam no `seed.json`, fora do git) e as provas. A maior parte do
comportamento já cabe nos ganchos existentes.

## 5. Ressalvas e o que falta confirmar

1. **Seed invertida real**: o início, a saída da caverna, as telas
   0x5B/0x43/0x47, o espelho, a fonte, os textos do HC e os NPCs têm provas
   C (e algumas no emulador). A rota automática start→primeira masmorra
   ainda não foi concluída; o que existe é o baú C-only na sala 170.
2. **Casa e Sanctuary**: a troca vem da tabela de starting points da seed
   (linha 1 = sala `0x112`), não de `inverted.asm`. A linha 0 do
   `StartingAreaExitTable` leva à `Unctuary Grounds` (`0x53`). A entrada da
   casa no Mundo das Trevas e a Sanctuary em si (portas de overworld) ainda
   não foram importadas: o importador lê a tabela de starting points e a
   tabela de saída do início, mas não a tabela normal de entradas/saídas.
3. **Tilemaps**: as 29 telas são importadas como dados e aplicadas no C.
   A tela 0x47 usa a tabela `Inverted_TR_TileAttributes` importada; a
   paleta main/aux12 é importada, e Aux3 continua US (a tabela JP não está
   no mesmo endereço na ROM da seed). A comparação da tela 0x43 no mesmo
   estado mostra que a cor antiga era fade; ver 3.3.
4. **Entradas/saídas normais**: `ShouldStartatExit` e `ExtraHole_*` estão
   ligados (ver 3.4); a tabela normal de entradas/saídas do JP (as
   escritas em `$82:E8xx`) ainda não é lida. Entrance shuffle (`GameType`
   com o bit `0x02`) continua fora de escopo.
5. **Espelho**: os 23 retângulos do `MirrorBonk` são importados e a regra
   está no caminho real de uso (`DoSwordInteractionWithTiles_Mirror`).
   A prova usa Y dentro/fora do retângulo: dentro entra em `RecoilWall`
   (estado 2); fora inicia o warp normal (estado 20, submódulo 35). Ver 3.6.
6. **Morte/save/continue**: pré-Agahnim 1, morte em dungeon com o bugfix e
   save&quit estão em `Death_Func15`; pós-Agahnim 2 (`FixAga2Bunny`) também.
   Todos com prova gdb. Ver 3.6.
9. **Agahnim/torre**: `Electric_Barrier`, `GanonTowerAnimation`,
   `GanonTowerInvertedCheck`, `HardcodedRocks`,
   `Overworld_CreatePyramidHoleModified`, `Draw_PyramidOverlay` e
   `LockAgahnimDoors` foram portados; `FlagAgahnimDoor` cobre overlay e
   `ScrapBuffer72`. `TurtleRockPegSolved` foi portado e provado no C; falta
   a prova no emulador.
7. **Textos**: os dois letreiros do HC (`$87:F52D`/`$87:F52F`, mensagens
   `$190`/`$191`) são traduzidos para `$3C`/`$3E`, com o texto importado.
   A fonte invertida (`newfont_inverted.bin`) continua pendente.
8. **Cobertura**: 251/251 locais da ROM. O spoiler tem 255 entradas de
   item: as 4 fora das tabelas fixas (Old Man, Sick Kid, Sahasrahla e
   Catfish) agora são importadas em `npc_items` e usadas nos sprites; prova
   C do Sahasrahla. Os 4 requisitos especiais (medalhões) continuam
   adicionais e não são locais de item.
