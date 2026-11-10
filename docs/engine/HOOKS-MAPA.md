# HOOKS-MAPA — pontos de evento no código C

Levantamento feito no commit `52cc8aa` (branch `agent/F1-mapa`). Nenhum
arquivo `.c`/`.h` foi alterado. As linhas citadas são as do código atual e
podem andar se os arquivos mudarem.

Objetivo: mostrar onde cada evento da F1 acontece no C, o que já está
disponível naquele ponto e se ele dispara uma vez por evento ou todo frame
enquanto o estado dura.

Observação geral para todos os pontos: `ZeldaRunFrameInternal` também é
chamado pelo caminho de comparação com o emulador
(`src/zelda_cpu_infra.c:330-351`). Portanto qualquer hook colocado aqui roda
também no regress e precisa ser no-op quando não há mod ativo.

---

## 1. Início e fim de cada frame do jogo

### Localização

- `src/zelda_rtl.c:285-294` — `ZeldaRunFrameInternal`, o ponto mais externo
  pedido no briefing.
- `src/zelda_rtl.c:257-263` — `ZeldaRunGameLoop`, que incrementa
  `frame_counter` e chama o roteamento de módulos.
- `src/nmi.c:138-153` — `Interrupt_NMI`, chamado no fim de
  `ZeldaRunFrameInternal`.
- `src/zelda_rtl.c:687-752` — `ZeldaRunFrame`, chamador externo; é ele que
  decide entre C e emulador e chama `ZeldaPushApuState()` no fim (linha 749).

### Trecho

```c
void ZeldaRunFrameInternal(uint16 input, int run_what) {
  if (animated_tile_data_src == 0)
    ZeldaInitializationCode();

  if (run_what & 2)
    ZeldaRunPolyLoop();
  if (run_what & 1)
    ZeldaRunGameLoop();
  Interrupt_NMI(input);
}
```

### Dados disponíveis

- `input` — estado bruto do joypad recebido por `ZeldaRunFrame`.
- `run_what` — bit 1 = rodar o game loop (`ZeldaRunGameLoop`); bit 2 = rodar
  o renderizador polyhedral (`ZeldaRunPolyLoop`). Pode haver frame só de poly.
- `frame_counter` (`src/variables.h:13`) — contador de 8 bits incrementado em
  `ZeldaRunGameLoop` (`src/zelda_rtl.c:258`), uma vez por game loop.
- `main_module_index` (`src/variables.h:3`), `submodule_index`
  (`src/variables.h:4`), `subsubmodule_index` (`src/variables.h:122`).
- `nmi_boolean`, `nmi_subroutine_index` (`src/variables.h:10`),
  `nmi_disable_core_updates` (`src/variables.h:588`).
- `is_replay`/`state_recorder` — usados por `ZeldaRunFrame`; não são
  parâmetros de `ZeldaRunFrameInternal`.

Na primeira chamada, `animated_tile_data_src == 0` faz
`ZeldaInitializationCode()` rodar antes de tudo (`src/zelda_rtl.c:286-287`).
Se o hook de início precisar do jogo já inicializado, ele deve ficar depois
dessa chamada.

### Cadência

- Hook no começo de `ZeldaRunFrameInternal` (antes da linha 286): uma vez por
  chamada, inclusive frames só de poly.
- Hook no fim (depois da linha 293): uma vez por chamada, depois do game loop
  e do NMI.
- Se o evento desejado é “frame de jogo” e não “frame de render”, o ponto
  correto é em volta de `ZeldaRunGameLoop` (`src/zelda_rtl.c:291-292`), que só
  roda quando `run_what & 1`.

### Observações

- `Interrupt_NMI` não é uma interrupção assíncrona neste port: é chamada
  síncrona no fim do frame. É onde VRAM, OAM e CGRAM são atualizados
  (`src/nmi.c:172-273`).
- `ZeldaRunFrameInternal` pode ser pulado no caminho normal: em
  `ZeldaRunFrame` (`src/zelda_rtl.c:742-747`), se `g_emu_runframe` existe e
  `enhanced_features0 == 0` e `dialogue_flags == 0`, quem roda é o emulador.
- O caminho de comparação chama `ZeldaRunFrameInternal` de qualquer forma
  (`src/zelda_cpu_infra.c:351`), então hooks que alteram RAM quebram o regress
  sem mods.

---

## 2. Troca de módulo principal

### Localização

- `src/misc.c:151-180` — tabela `kMainRouting[28]`, que mapeia
  `main_module_index` para a função do módulo.
- `src/misc.c:324-326` — `Module_MainRouting`, chamado todo frame por
  `ZeldaRunGameLoop` (`src/zelda_rtl.c:260`).
- Entradas dos módulos mais relevantes:
  - `Module00_Intro` — `src/ending.c:506`
  - `Module01_FileSelect` — `src/select_file.c:194`
  - `Module05_LoadFile` — `src/misc.c:542`
  - `Module_PreDungeon` — `src/dungeon.c:6408`
  - `Module07_Dungeon` — `src/dungeon.c:6504`
  - `Module08_OverworldLoad` — `src/overworld.c:415`
  - `Module09_Overworld` — `src/overworld.c:704`
  - `Module0E_Interface` — `src/messaging.c:277`
  - `Module12_GameOver` — `src/messaging.c:598`
  - `Module13_BossVictory_Pendant` — `src/misc.c:594`
  - `Module14_Attract` — `src/attract.c:372`
  - `Module15_MirrorWarpFromAga` — `src/misc.c:650`
  - `Module16_BossVictory_Crystal` — `src/misc.c:658`
  - `Module17_SaveAndQuit` — `src/misc.c:868`
  - `Module18_GanonEmerges` — `src/ending.c:262`
  - `Module19_TriforceRoom` — `src/ending.c:343`
  - `Module1A_Credits` — `src/ending.c:1279`
  - `Module1B_SpawnSelect` — `src/messaging.c:382`

### Trecho

```c
void Module_MainRouting() {  // 8080b5
  kMainRouting[main_module_index]();
}
```

### Mapa dos índices

| Índice | Módulo | Significado prático |
|---:|---|---|
| 0 | `Module00_Intro` | intro / sequência inicial |
| 1 | `Module01_FileSelect` | seleção de arquivo |
| 2 | `Module02_CopyFile` | copiar arquivo |
| 3 | `Module03_KILLFile` | apagar arquivo |
| 4 | `Module04_NameFile` | nomear personagem |
| 5 | `Module05_LoadFile` | carregar arquivo para o jogo |
| 6 | `Module_PreDungeon` | preparação de dungeon |
| 7 | `Module07_Dungeon` | dungeon |
| 8 | `Module08_OverworldLoad` | carregar overworld |
| 9 | `Module09_Overworld` | overworld |
| 10 | `Module08_OverworldLoad` | idem 8 (repetido na tabela) |
| 11 | `Module09_Overworld` | idem 9 (repetido na tabela) |
| 12 | `Module_Unknown0` | `assert(0)` (`src/misc.c:203`) |
| 13 | `Module_Unknown1` | `assert(0)` (`src/misc.c:207`) |
| 14 | `Module0E_Interface` | menu/pausa e também diálogo |
| 15 | `Module0F_SpotlightClose` | fechamento de spotlight |
| 16 | `Module10_SpotlightOpen` | abertura de spotlight |
| 17 | `Module11_DungeonFallingEntrance` | entrada caindo em dungeon |
| 18 | `Module12_GameOver` | morte / game over |
| 19 | `Module13_BossVictory_Pendant` | vitória de chefe com pingente |
| 20 | `Module14_Attract` | attract/demo |
| 21 | `Module15_MirrorWarpFromAga` | mirror warp após Agahnim |
| 22 | `Module16_BossVictory_Crystal` | vitória de chefe com cristal |
| 23 | `Module17_SaveAndQuit` | salvar e sair |
| 24 | `Module18_GanonEmerges` | Ganon emerge |
| 25 | `Module19_TriforceRoom` | sala da Triforce |
| 26 | `Module1A_Credits` | créditos |
| 27 | `Module1B_SpawnSelect` | seleção de spawn |

### Dados disponíveis

- `main_module_index` (`src/variables.h:3`) — módulo principal atual.
- `submodule_index` (`src/variables.h:4`) — subestado do módulo.
- `subsubmodule_index` (`src/variables.h:122`) — subestado mais fino.
- `saved_module_for_menu` (`src/variables.h:159`) — módulo para onde voltar
  depois de menu/diálogo.

### Cadência

- `Module_MainRouting` roda todo frame; a função do módulo atual roda todo
  frame enquanto o módulo está ativo.
- A troca em si é uma atribuição a `main_module_index` e acontece uma vez.
  Para gerar “módulo mudou”, é preciso comparar o valor antes/depois do
  roteamento (ou instrumentar as atribuições).
- `submodule_index` muda com muito mais frequência, inclusive várias vezes no
  mesmo frame. Não confundir subestado com troca de módulo.

### Observações

- Não existe um módulo “title screen” separado na tabela. O mais próximo é a
  intro (`Module00_Intro`, índice 0) seguida do attract (`Module14_Attract`,
  índice 20); a seleção de arquivo é o índice 1.
- O índice 14 é usado tanto para o menu/pausa quanto para diálogo
  (`Main_ShowTextMessage` grava `main_module_index = 14`, ver seção 6). Para
  distinguir, olhe `submodule_index`/`messaging_module`.
- Há várias atribuições diretas de `main_module_index` espalhadas. Alguns
  exemplos: `src/ending.c:643` (intro → attract), `src/ending.c:660` e
  `src/select_file.c:495` (→ file select), `src/misc.c:575` (load file →
  overworld load), `src/dungeon.c:6483` (→ dungeon), `src/player.c:171` e
  `src/overworld.c:570` (→ game over), `src/dungeon.c:2527` (→ crystal
  victory), `src/dungeon.c:2529` (→ pendant victory), `src/dungeon.c:2515`
  (→ mirror warp), `src/ending.c:466` (→ credits).

---

## 3. Entrar em sala de dungeon e trocar de área no overworld

### 3.1 Sala de dungeon

#### Localização

- `src/dungeon.c:2544-2545` — `Dungeon_LoadRoom` → `Dungeon_LoadHeader`.
- `src/dungeon.c:3670-3744` — `Dungeon_LoadHeader`, que lê o header da sala
  nova, flags de portas, paletas, sprites etc.
- `src/dungeon.c:6688-6699` — `Module07_02_01_LoadNextRoom`, chamado durante
  a transição entre salas.
- `src/dungeon.c:8273-8290` — `Dungeon_LoadAndDrawRoom`, chamado na entrada
  de uma dungeon vinda do overworld.
- `src/dungeon.c:7829-7873` — `Module11_02_LoadEntrance`, entrada caindo.

#### Trecho

```c
void Module07_02_01_LoadNextRoom() {  // 828a5b
  Dungeon_LoadRoom();
  ResetStarTileGraphics();
  LoadTransAuxGFX_sprite();
  subsubmodule_index++;
  overworld_map_state = 0;
  BYTE(dungeon_room_index2) = BYTE(dungeon_room_index);
  Dungeon_ResetSprites();
```

#### Dados disponíveis

- `dungeon_room_index` (`src/variables.h:112`) — sala de destino; já foi
  atualizada pelo código de transição antes de `Dungeon_LoadRoom`.
- `dungeon_room_index_prev` (`src/variables.h:113`) — sala anterior.
- `dungeon_room_index2` (`src/variables.h:475`) — cópia usada para sprites.
- `cur_palace_index_x2` (`src/variables.h:405`) — dungeon/palácio atual.
- `dung_cur_floor` (`src/variables.h:114`) — andar atual.
- `player_is_indoors` (`src/variables.h:14`).
- `dung_hdr_*` — campos lidos do header em `Dungeon_LoadHeader`
  (`src/dungeon.c:3696-3719`).
- `dung_savegame_state_bits` (`src/variables.h:401`) — flags de baús, portas,
  etc.
- `dung_loade_bgoffs_h_copy`/`dung_loade_bgoffs_v_copy`
  (`src/variables.h:541-542`) — offsets de câmera da sala.

#### Cadência

- `Dungeon_LoadRoom` roda uma vez por carregamento de sala; é chamado tanto
  na transição normal (`Module07_02_01_LoadNextRoom`) quanto na entrada da
  dungeon (`Dungeon_LoadAndDrawRoom`) e em transições especiais.
- `Dungeon_LoadHeader` roda uma vez por carga de sala.
- O evento “entrou na sala X” deve ser emitido depois de
  `Dungeon_LoadHeader`/`Dungeon_LoadRoom`, quando `dungeon_room_index` e os
  flags da sala já estão válidos.

#### Observações

- `Dungeon_LoadRoom` é chamado também para recarregar a mesma sala em alguns
  efeitos; um evento cego pode repetir. Se for necessário “sala realmente
  mudou”, compare `dungeon_room_index` com `dungeon_room_index_prev` ou com o
  último valor emitido.
- `Dungeon_LoadAndDrawRoom` (`src/dungeon.c:8274`) chama `Dungeon_LoadRoom`
  e é o ponto de entrada vindo do overworld.

### 3.2 Área do overworld

#### Localização

- `src/overworld.c:790-852` — `OverworldHandleTransitions`, chamado por
  `Module09_00_PlayerControl` (`src/overworld.c:784`). É onde a borda da tela
  troca `overworld_screen_index`/`overworld_area_index`.
- `src/overworld.c:909-915` — `Module09_LoadNewMapAndGFX`, que carrega o mapa
  novo.
- `src/overworld.c:2239-2253` — `OverworldTransitionScrollAndLoadMap`,
  construção das stripes durante o scroll.
- `src/overworld.c:854-873` — `Overworld_LoadGFXAndScreenSize`, ajusta
  tamanho do mapa, offsets, tema de tiles e pacote de sprites.

#### Trecho

```c
  uint8 old_screen = overworld_screen_index;
  if (old_screen == 0x2a)
    sound_effect_ambient = 0x80;

  uint8 new_area = kOverworldAreaHeads[pushed] | savegame_is_darkworld;
  BYTE(overworld_screen_index) = new_area;
  BYTE(overworld_area_index) = new_area;
```

#### Dados disponíveis

- `old_screen`, `new_area` — telas anterior/nova (locais).
- `overworld_screen_index` (`src/variables.h:97`) — tela atual; os 6 bits
  baixos são a tela e o bit 0x40 indica Dark World.
- `overworld_area_index` (`src/variables.h:404`) — área carregada.
- `current_area_of_player` (`src/variables.h:583`) — área calculada a partir
  de Link.
- `overworld_screen_trans_dir_bits` (`src/variables.h:407`) e
  `overworld_screen_transition` (`src/variables.h:412`) — direção/tipo da
  transição.
- `savegame_is_darkworld` (`src/variables.h:1120`) — 0 = Light World,
  0x40 = Dark World.
- `link_x_coord`/`link_y_coord` — posição de Link no momento da transição.
- `player_is_indoors` (`src/variables.h:14`).

#### Cadência

- `OverworldHandleTransitions` roda todo frame enquanto Link anda, mas o
  bloco que troca de tela só executa quando a borda é cruzada. A atribuição
  de `overworld_screen_index` é uma vez por troca.
- `Module09_LoadNewMapAndGFX` roda no subestado de carregamento, não todo
  frame de gameplay.
- `OverworldTransitionScrollAndLoadMap` roda durante a transição de scroll,
  uma vez por pedaço/stripe carregado, não uma vez por área.

#### Observações

- Para “área mudou”, o ponto mais direto é logo depois de
  `BYTE(overworld_screen_index) = new_area;` (`src/overworld.c:824`), onde
  `old_screen` e `new_area` estão ambos disponíveis.
- A transição pode ser interrompida/reiniciada; `overworld_screen_transition`
  e `overworld_screen_trans_dir_bits2` controlam o estado do scroll.
- O overworld também tem “special overworld” e transições de entrada/saída
  (`Overworld_EnterSpecialArea`, `Overworld_LoadOverlays`), que não passam
  necessariamente por `OverworldHandleTransitions`.

### 3.3 Light World ↔ Dark World

#### Localização

- `src/overworld.c:1272-1292` — `Module09_MirrorWarp`, caso 0: espelho no
  overworld. O toggle está na linha 1286.
- `src/dungeon.c:2494-2517` — `PrepareDungeonExitFromBossFight`, caso
  `dungeon_room_index == 0x20`: saída de Agahnim para o Dark World. O toggle
  está na linha 2506.
- `src/messaging.c:777-786` — `Death_Func15` volta para Light World quando
  `sram_progress_indicator < 3`.
- `src/ending.c:2438` — `EndSequence_32` grava `savegame_is_darkworld = 0x40`.
- `src/overworld.c:3206` — fallback de pit (sala Chris Houlihan) grava
  `savegame_is_darkworld = 0`.
- `src/ending.c:472` — fim da sequência grava `savegame_is_darkworld = 0`.

#### Trecho

```c
    savegame_is_darkworld ^= 0x40;
    word_7E04C8 = 0;
    BYTE(overworld_screen_index) = BYTE(overworld_area_index) = (overworld_screen_index & 0x3f) | savegame_is_darkworld;
```

#### Dados disponíveis

- `savegame_is_darkworld` (`src/variables.h:1120`).
- `overworld_screen_index`/`overworld_area_index` — o bit 0x40 é combinado
  com a tela.
- `link_item_moon_pearl` (`src/variables.h:1087`) — Moon Pearl; influencia
  se Link vira bunny no Dark World.
- `link_item_mirror` (`src/variables.h:1083`).
- `sram_progress_indicator` (`src/variables.h:1115`).
- `sram_progress_flags` (`src/variables.h:1116`).

#### Cadência

- Cada toggle é uma atribuição única; os pontos acima são one-shot.
- O evento “mudou de mundo” deve ser emitido comparando o valor antes/depois
  (ou no ponto exato), não todo frame.

#### Observações

- O valor não é um booleano C separado: o código usa 0 ou 0x40 e faz XOR
  (`^= 0x40`). Não trate os outros bits como parte do “mundo”.
- O toggle do espelho só ocorre se `BYTE(overworld_screen_index) < 0x80`
  (`src/overworld.c:1276`); se já estiver no Dark World, a função retorna.
- A saída de Agahnim também muda `sram_progress_indicator = 3` e o módulo
  para 21 (`src/dungeon.c:2503-2517`).

---

## 4. Link recebe um item

### Localização

- `src/player.c:1885-1916` — `Link_ReceiveItem`, a função central de
  recebimento.
- `src/misc.c:713-844` — `AncillaAdd_ItemReceipt`, que efetivamente altera o
  inventário/status e cria a ancilla de item.
- `src/ancilla.c:3361-3533` — `Ancilla22_ItemReceipt`, animação/entrega e
  mensagem pós-item.

### Trecho

```c
void Link_ReceiveItem(uint8 item, int chest_position) {  // 8799ad
  if (link_auxiliary_state) {
    link_auxiliary_state = 0;
    link_incapacitated_timer = 0;
    countdown_for_blink = 0;
    link_state_bits = 0;
  }
  link_receiveitem_index = item;
```

### Dados disponíveis

- `item` / `link_receiveitem_index` (`src/variables.h:236`) — **id do item**
  recebido. É o dado principal do evento.
- `chest_position` — posição do baú quando o recebimento vem de baú; -1/0
  nos outros casos.
- `item_receipt_method` (`src/variables.h:250`) — método do recebimento;
  o código usa 0, 1, 2 e 3 em caminhos diferentes (baú, NPC, milestone).
- `link_auxiliary_state` (`src/variables.h:56`),
  `link_player_handler_state` (`src/variables.h:69`),
  `link_pose_for_item` (`src/variables.h:238`).
- `link_health_current` (`src/variables.h:1100`), `link_health_capacity`
  (`src/variables.h:1099`), `link_rupees_goal` (`src/variables.h:1092`),
  `link_hearts_filler` (`src/variables.h:1105`), `link_magic_power`
  (`src/variables.h:1101`), `link_bottle_info` (`src/variables.h:1091`).
- `link_which_pendants` (`src/variables.h:1107`), `link_has_crystals`
  (`src/variables.h:1112`), `link_sword_type` (`src/variables.h:1088`),
  `link_shield_type` (`src/variables.h:1089`), `link_armor`
  (`src/variables.h:1090`), `link_item_gloves` (`src/variables.h:1084`).

### Tabelas de id → RAM/valor

- `kMemoryLocationToGiveItemTo[76]` — `src/misc.c:60-80`.
- `kValueToGiveItemTo[76]` — `src/misc.c:81-101`.
- `kReceiveItemGfx[76]` — `src/misc.c:53-59`.
- `kReceiveItem_Tab1/2/3` — `src/misc.c:32-52`.

Essas tabelas são o mapa “id do item → endereço de RAM/valor”. Exemplos que
aparecem no código: `0x20` = cristal (seção 9), `0x37`/`0x38`/`0x39` =
pingentes (seção 9), `0x26`/`0x3e` = heart container (seção 7/8), `0x17` =
heart piece (usa `link_heart_pieces`), `0x2e`/`0x4`/`0x5`/`0x42`/`0x44`/
`0x31`/`0xe` = itens de loja (seção 12).

### Cadência

- `Link_ReceiveItem` é chamada uma vez por recebimento. As chamadas vêm de
  baús (`Link_PerformOpenChest`), NPCs (`src/sprite_main.c`), lojas
  (`ShopItem_HandleReceipt`) e itens especiais.
- `AncillaAdd_ItemReceipt` roda uma vez por recebimento e faz a mutação de
  inventário; `Ancilla22_ItemReceipt` roda todo frame enquanto a ancilla de
  item existe (animação, mensagem, som).

### Observações

- **Nem todo pickup passa por `Link_ReceiveItem`.** Rúpias, corações,
  flechas, bombas e mágica de inimigos no chão são absorvidos por
  `Sprite_HandleAbsorptionByPlayer` (`src/sprite.c:1371-1419`), que soma
  direto em `link_rupees_goal`, `link_hearts_filler`, etc. Rúpias de baú/
  prêmio também podem passar por `Ancilla_AddRupees` (`src/ancilla.c:6937-6952`).
- Coração/rúpia, portanto, precisam de um evento separado se o barramento
  quiser cobrir “pegou moeda/coração no chão”.
- `AncillaAdd_ItemReceipt` pode chamar `Link_ReceiveItem` de novo no caso de
  milestone de heart piece (`src/ancilla.c:3420-3422`), então um hook ingênuo
  em `Link_ReceiveItem` pode ver dois eventos para o mesmo item.

---

## 5. Abrir um baú (antes do item ser dado)

### Localização

- `src/player.c:3795-3816` — `Link_PerformOpenChest`, chamado pelo handler
  de Link quando o botão de ação é usado no baú.
- `src/dungeon.c:5704-5801` — `OpenChestForItem`, que lê o conteúdo do baú,
  marca o baú como aberto e devolve o id do item.
- `src/dungeon.c:5828-5915` — `OpenMiniGameChest`, para o baú do minigame
  (`tile == 0x63`).

### Trecho

```c
  int chest_position = -1;
  uint8 item = OpenChestForItem(index_of_interacting_tile, &chest_position);
  if (sign8(item)) {
    item_receipt_method = 0;
    return;
  }
  assert(chest_position != -1);
  item_receipt_method = 1;
```

### Dados disponíveis

- `index_of_interacting_tile` (`src/variables.h:89`) — tile do baú
  (0x58–0x5d; 0x63 = minigame).
- `item` — id do item que saiu do baú (retorno de `OpenChestForItem`).
- `chest_position` — posição do baú para a animação.
- `dungeon_room_index`/`dungeon_room_index2` — sala atual.
- `dung_chest_locations` (`src/variables.h:581`) — posições dos baús.
- `dung_savegame_state_bits` (`src/variables.h:401`) — flags de baú aberto.
- `dung_num_chests_x2` (`src/variables.h:479`).
- `link_bigkey` — chave grande, usada no baú grande.
- `player_is_indoors`, `link_direction_facing`, `link_auxiliary_state`,
  `item_receipt_method`.

### Cadência

- `Link_PerformOpenChest` pode ser chamada em frames consecutivos enquanto o
  handler está ativo, mas o guard `if (link_direction_facing ||
  item_receipt_method || link_auxiliary_state) return;` e a atribuição de
  `item_receipt_method = 1` fazem o caminho efetivo disparar uma vez por
  baú.
- `OpenChestForItem` roda uma vez por abertura.

### Observações

- **O baú já foi marcado como aberto antes de `Link_ReceiveItem`.** Em
  `OpenChestForItem`, `dung_savegame_state_bits |= kChestOpenMasks[...]`
  acontece em `src/dungeon.c:5748`/`5752`, e a leitura do conteúdo em
  `data = chest_data[2]` (`src/dungeon.c:5741`). Se um mod quiser trocar o
  conteúdo, o ponto mais seguro é logo depois dessa leitura, antes de
  `Link_ReceiveItem`; se quiser só reagir “abriu baú”, o ponto em
  `Link_PerformOpenChest` antes da linha 3815 serve.
- `kDungeonRoomChests` vem dos assets (asset 8), não é um array C literal;
  ver seção 12.
- Baú grande usa `OpenBigChest` (`src/dungeon.c:5803`) e exige `link_bigkey`.

---

## 6. Começar e terminar uma caixa de diálogo

### Localização

- **Início (pedido):** `src/misc.c:944-952` — `Main_ShowTextMessage`.
- **Início (carga real do texto):** `src/messaging.c:2134-2150` —
  `Text_Initialize`/`Text_Initialize_initModuleStateLoop`.
- **Leitura da mensagem:** `src/messaging.c:2265-2309` —
  `Text_LoadCharacterBuffer`.
- **Fim normal:** `src/messaging.c:2499-2510` — `RenderText_Draw_Finish`.

### Trecho (início)

```c
void Main_ShowTextMessage() {  // 8ffdaa
  if (main_module_index != 14) {
    byte_7E0223 = 0;
    messaging_module = 0;
    submodule_index = 2;
    saved_module_for_menu = main_module_index;
    main_module_index = 14;
  }
}
```

### Trecho (carga da mensagem)

```c
void Text_LoadCharacterBuffer() {  // 8ec4e2
  MemBlk dictionary = FindIndexInMemblk(g_zenv.dialogue_blk, 0);
  MemBlk dialogue = FindIndexInMemblk(g_zenv.dialogue_blk, 1);
  MemBlk text_str = FindIndexInMemblk(dialogue, dialogue_message_index);
```

### Trecho (fim)

```c
  messaging_module = 0;
  submodule_index = 0;
  main_module_index = saved_module_for_menu;
}
```

### Dados disponíveis

- `dialogue_message_index` (`src/variables.h:833`) — **id da mensagem**.
- `messaging_module` (`src/variables.h:820`) — submodo do diálogo.
- `dialogue_msg_read_pos` (`src/variables.h:821`) — posição de leitura.
- `saved_module_for_menu` (`src/variables.h:159`) — módulo para onde voltar.
- `main_module_index` (`src/variables.h:3`), `submodule_index`
  (`src/variables.h:4`), `subsubmodule_index` (`src/variables.h:122`).
- `g_zenv.dialogue_blk` / `g_zenv.dialogue_font_blk` — blocos de assets
  carregados por `ZeldaSetLanguage` (`src/zelda_rtl.c:754-774`).

### Cadência

- `Main_ShowTextMessage` é chamado uma vez por pedido de mensagem; ele só
  age se `main_module_index != 14`, então não reabre o diálogo em loop.
- `Text_Initialize`/`Text_LoadCharacterBuffer` rodam uma vez por diálogo
  (quando o módulo 14 entra no subestado de texto).
- `RenderText` (`src/messaging.c:2121-2123`) roda todo frame enquanto o
  diálogo está aberto.
- `RenderText_Draw_Finish` roda uma vez no fim normal e devolve o módulo.

### Observações

- O id da mensagem já está em `dialogue_message_index` antes de
  `Main_ShowTextMessage`; quem chama define o id. Exemplos:
  `src/player.c:3787-3791` (placa/telefone), `src/dungeon.c:5718-5719`
  (mensagem de chave), `src/sprite.c:929-938` (NPC).
- O texto é lido dos assets em `Text_LoadCharacterBuffer`; a fonte é lida
  por caractere em `VWF_RenderSingle` (`src/messaging.c:2512-2532`). Ver
  seção 11.
- O mesmo módulo 14 também é usado pelo menu/pausa; para “diálogo” filtre
  `messaging_module`/submódulo ou o estado de texto.
- Há caminhos de menu que também devolvem `main_module_index =
  saved_module_for_menu` (ex.: `DungMap_RestoreOld`, `src/messaging.c:2858-2868`),
  mas o fim normal de mensagem é `RenderText_Draw_Finish`.

---

## 7. Link toma dano / morre / usa fada

### 7.1 Dano

#### Localização

- `src/player.c:148-178` — `Link_ControlHandler`, bloco `if
  (link_give_damage)`.
- `src/sprite.c:2724-2735` — `Sprite_AttemptDamageToLinkPlusRecoil`, que
  calcula o dano de colisão com sprite.
- `src/overworld.c:407-413` — `TakeDamageFromPit`.
- `src/player.c:1568-1573` — `DungeonPitDoDamage`.

#### Trecho

```c
        Ancilla_Sfx2_Near(38);
        number_of_times_hurt_by_sprites++;
        uint8 new_dmg = link_health_current - dmg;
        if (new_dmg == 0 || new_dmg >= 0xa8) {
          mapbak_TM = TM_copy;
          mapbak_TS = TS_copy;
          saved_module_for_menu = main_module_index;
          main_module_index = 18;
```

#### Dados disponíveis

- `link_give_damage` (`src/variables.h:343`) — dano pendente; o bloco só
  entra quando é diferente de zero.
- `link_health_current` (`src/variables.h:1100`),
  `link_health_capacity` (`src/variables.h:1099`).
- `link_cape_mode` (`src/variables.h:61`),
  `link_disable_sprite_damage` (`src/variables.h:351`),
  `countdown_for_blink` (`src/variables.h:291`).
- `number_of_times_hurt_by_sprites` (`src/variables.h:708`).
- `link_armor` (`src/variables.h:1090`) — usado no cálculo em
  `kPlayerDamages`.
- `sprite_bump_damage` (`src/variables.h:697`) — classe de dano do sprite.
- `main_module_index` (`src/variables.h:3`), `saved_module_for_menu`
  (`src/variables.h:159`).

#### Cadência

- O bloco de dano roda todo frame, mas só efetiva quando
  `link_give_damage != 0`; na prática, uma vez por hit.
- `Sprite_AttemptDamageToLinkPlusRecoil` roda na checagem de colisão e seta
  `link_give_damage` uma vez por contato válido.
- `TakeDamageFromPit`/`DungeonPitDoDamage` são chamados uma vez na queda.

#### Observações

- O dano de sprite é calculado em
  `kPlayerDamages[3 * (sprite_bump_damage[k] & 0xf) + link_armor]`
  (`src/sprite.c:2730`); a tabela está em `src/sprite.c:264-267`.
- A morte não acontece em uma função “Death”; ela é detectada aqui quando
  `new_dmg == 0` e troca o módulo para 18. Há outro caminho em
  `src/overworld.c:566-573` (após afogamento/queda) e no fluxo de
  `Death_Func15` (`src/messaging.c:777`).
- `new_dmg >= 0xa8` é o underflow de uint8 (dano maior que a vida).

### 7.2 Morte / game over

#### Localização

- `src/player.c:167-176` — troca para `main_module_index = 18`.
- `src/overworld.c:566-573` — troca para 18 quando `link_health_current == 0`.
- `src/messaging.c:598-602` — `Module12_GameOver`, entrada do módulo.
- `src/messaging.c:207-224` — tabela `kModule_Death[16]`.

#### Dados disponíveis

- `main_module_index` (`src/variables.h:3`), `submodule_index`
  (`src/variables.h:4`).
- `link_health_current` (`src/variables.h:1100`),
  `link_health_capacity` (`src/variables.h:1099`).
- `death_var2`, `death_var4`, `death_var5`, `death_save_counter` — estado
  interno do game over (ver `src/messaging.c:598-915`).
- `saved_module_for_menu` (`src/variables.h:159`).

#### Cadência

- A transição para 18 é one-shot.
- `Module12_GameOver` roda todo frame enquanto o game over dura; os
  subestados da tabela `kModule_Death` avançam um por vez.

### 7.3 Fada

#### Localização

- **Fada engarrafada:** `src/player.c:2489-2494` — `LinkItem_Bottle`, branch
  `b == 6`.
- **Spawn da fada:** `src/sprite.c:4302-4312` — `ReleaseFairy`, sprite
  `0xe3`.
- **Revival automática:** `src/messaging.c:207-224` (submódulos 10–15),
  `src/messaging.c:858-863` (`GameOver_InitializeRevivalFairy`),
  `src/ancilla.c:5388-5458` (`RevivalFairy_Main`),
  `src/ancilla.c:5482-5525` (`RevivalFairy_MonitorHP`).

#### Trecho

```c
  } else if (b == 6) { // fairy
    link_item_in_hand = 0;
    if (ReleaseFairy() < 0)
      goto fail;
    link_bottle_info[btidx] = 2;
    Hud_Rebuild();
```

#### Dados disponíveis

- `link_bottle_info` (`src/variables.h:1091`) — conteúdo das garrafas;
  `2` = vazia após usar.
- `btidx` — índice da garrafa usada.
- `link_health_current` (`src/variables.h:1100`),
  `link_health_capacity` (`src/variables.h:1099`).
- `link_is_bunny` (`src/variables.h:62`), `link_is_in_deep_water`.
- `sprite_state[0..15]`, `sprite_type` (`src/variables.h:729`).
- `ancilla_step`, `ancilla_item_to_link`, `ancilla_type`.

#### Cadência

- `ReleaseFairy` roda uma vez no uso da garrafa e cria um sprite.
- `RevivalFairy_Main`/`RevivalFairy_MonitorHP` rodam todo frame enquanto a
  sequência de revival dura.
- `GameOver_InitializeRevivalFairy` roda uma vez na entrada do submódulo 10.

#### Observações

- “Usar fada” pode significar duas coisas no código: soltar a fada da
  garrafa (`ReleaseFairy`) ou ser revivido automaticamente no game over
  (`RevivalFairy_Main`). As duas passam por caminhos diferentes.
- `RevivalFairy_MonitorHP` considera `link_health_current ==
  link_health_capacity` ou `== 0x38` e `is_doing_heart_animation`
  (`src/ancilla.c:5483`).

---

## 8. Sprite criado/destruído; inimigo morre

### Localização

- **Spawn dinâmico:** `src/sprite.c:4240-4268` —
  `Sprite_SpawnDynamically`/`Sprite_SpawnDynamicallyEx`.
- **Carga de sala (indoor):** `src/sprite.c:3638-3663` —
  `Dungeon_LoadSingleSprite`.
- **Carga por proximidade (outdoor):** `src/sprite.c:3799-3845` —
  `Overworld_LoadProximaSpriteIfAlive`.
- **Carga da lista do overworld:** `src/sprite.c:3721-3738` —
  `Overworld_LoadSprites`.
- **Destruição em massa:** `src/sprite.c:3595-3621` — `Sprite_DisableAll`.
- **Destruição individual:** `src/sprite.c:3947-3965` — `Sprite_KillSelf`.
- **Morte de inimigo:** `src/sprite.c:2940-2991` — `Sprite_DoTheDeath`;
  `src/sprite.c:3020-3031` — `SpriteDeath_Func4`.
- **Morte “manual”/flag de sala:** `src/sprite.c:3632-3636` —
  `Sprite_ManuallySetDeathFlagUW`.

### Trecho (spawn)

```c
int Sprite_SpawnDynamicallyEx(int k, uint8 what, SpriteSpawnInfo *info, int j) {  // 9df65f
  do {
    if (sprite_state[j] == 0) {
      sprite_type[j] = what;
      sprite_state[j] = 9;
      info->r0_x = Sprite_GetX(k);
      info->r2_y = Sprite_GetY(k);
      info->r4_z = sprite_z[k];
```

### Trecho (morte)

```c
void SpriteDeath_Func4(int k) {  // 86fa25
  if (sprite_type[k] == 0xa2 && Sprite_CheckIfScreenIsClear())
    Ancilla_SpawnFallingPrize(4);
  Sprite_ManuallySetDeathFlagUW(k);
  num_sprites_killed++;
```

### Dados disponíveis

- slot `j`/`k` — 0..15 (sprites); `src/sprite.c:3596` percorre de 15 até 0.
- `sprite_state` (`src/variables.h:724`) — o código usa valores como 0, 8,
  9, 10, 11, mas não há enum nomeado; não inferimos os nomes.
- `sprite_type` (`src/variables.h:729`) — id do sprite/inimigo.
- `sprite_graphics` (`src/variables.h:723`).
- `sprite_health` (`src/variables.h:732`).
- `sprite_bump_damage` (`src/variables.h:697`) — dano de contato.
- `sprite_give_damage` (`src/variables.h:698`) — dano pendente recebido.
- `sprite_flags`, `sprite_flags2`, `sprite_flags3`, `sprite_flags4`,
  `sprite_defl_bits`, `sprite_delay_main`, `sprite_N`, `sprite_N_word`.
- `num_sprites_killed` (`src/variables.h:707`) — incrementado em
  `SpriteDeath_Func4` (`src/sprite.c:3024`).

### Cadência

- `Sprite_SpawnDynamicallyEx` roda uma vez por spawn dinâmico; escolhe o
  primeiro slot livre (`sprite_state[j] == 0`).
- `Dungeon_LoadSingleSprite`/`Overworld_LoadProximaSpriteIfAlive` rodam uma
  vez por sprite carregado da lista da sala/área; gravam `sprite_state = 8`.
- `Sprite_DisableAll` roda em resets de sala/módulo e limpa todos os slots de
  uma vez.
- `Sprite_KillSelf` roda uma vez no kill individual.
- `Sprite_DoTheDeath` roda uma vez quando o inimigo entra em morte; pode
  mudar para drop (`PrepareEnemyDrop`) em vez de sumir, então “morreu” e
  “sumiu” podem ser momentos diferentes.
- `SpriteDeath_Func4` roda no fim da morte e incrementa
  `num_sprites_killed`.

### Observações

- `Sprite_DisableAll` também zera ancillas, overlords e garnishes; um evento
  “sprite destruído” por sprite individual deve usar `Sprite_KillSelf`/
  `Sprite_DoTheDeath`, não `Sprite_DisableAll`.
- `Sprite_ManuallySetDeathFlagUW` não destrói o sprite: só marca na máscara
  da sala que ele já morreu, para não reaparecer.
- `Sprite_DoTheDeath` tem caminhos que retornam antes de `sprite_state = 0`
  (pikit, drop de item, prêmio). Nesses casos o sprite pode virar outro
  sprite (`PrepareEnemyDrop`), então o evento “inimigo morreu” deve ser
  disparado antes desses `return` se o mod precisar do tipo original.
- `Sprite_KillSelf` tem guarda `sprite_defl_bits[k] & 0x40` e
  `player_is_indoors` (`src/sprite.c:3948`).

---

## 9. Chefe derrotado / cristal ou pingente obtido

### 9.1 Chefe derrotado

#### Localização

- `src/dungeon.c:2494-2535` — `PrepareDungeonExitFromBossFight`, ponto
  central pós-chefe.
- Chamadas: `src/sprite.c:676` (explosão de chefe), `src/dungeon.c:4599`
  (`RoomTag_Agahnim`), `src/sprite_main.c:23333` (maiden), e
  `src/sprite_main.c:23694` (Agahnim).
- `src/dungeon.c:4582-4593` — `RoomTag_GetHeartForPrize`, que decide se o
  prêmio do chefe ainda deve cair.

#### Trecho

```c
void PrepareDungeonExitFromBossFight() {  // 80f945
  SavePalaceDeaths();
  SaveDungeonKeys();
  dung_savegame_state_bits |= 0x8000;
  Dungeon_FlagRoomData_Quadrants();

  int j = FindInByteArray(kDungeonExit_From, BYTE(dungeon_room_index), countof(kDungeonExit_From));
  assert(j >= 0);
  BYTE(dungeon_room_index) = kDungeonExit_To[j];
```

#### Dados disponíveis

- `dungeon_room_index` (`src/variables.h:112`),
  `cur_palace_index_x2` (`src/variables.h:405`),
  `dung_cur_floor` (`src/variables.h:114`).
- `dung_savegame_state_bits` (`src/variables.h:401`) — o bit 0x8000 indica
  chefe derrotado.
- `savegame_is_darkworld` (`src/variables.h:1120`),
  `sram_progress_indicator` (`src/variables.h:1115`).
- `saved_module_for_menu` (`src/variables.h:159`),
  `main_module_index` (`src/variables.h:3`).
- `link_which_pendants` (`src/variables.h:1107`),
  `link_has_crystals` (`src/variables.h:1112`).

#### Cadência

- Roda uma vez quando o chefe é considerado derrotado; dependendo do chefe,
  pode ser depois da animação/explosão, não no frame exato do HP zero.
- A escolha do módulo seguinte é one-shot (`src/dungeon.c:2515-2529`).

#### Observações

- O ponto não é “HP do chefe chegou a zero”: é “a dungeon vai sair da luta
  de chefe”. Para um evento de morte de chefe, o mais próximo é o caller em
  `SpriteDeath`/`RoomTag`, mas ele varia por chefe.
- `RoomTag_GetHeartForPrize` só spawna o prêmio se o bit do cristal/pingente
  ainda não está setado (`src/dungeon.c:4586-4590`).

### 9.2 Pingente

#### Localização

- `src/misc.c:736-742` — `AncillaAdd_ItemReceipt`, branch dos itens
  `0x37`/`0x38`/`0x39`.
- O recebimento em si passa por `Link_ReceiveItem` (seção 4).

#### Trecho

```c
  } else if ((t = 4, j == 0x37) || (t = 1, j == 0x38) || (t = 2, j == 0x39)) {
    *p |= t;
    if ((*p & 7) == 7)
      savegame_map_icons_indicator = 4;
    overworld_map_state++;
```

#### Dados disponíveis

- `j`/`link_receiveitem_index` — id do pingente (`0x37`, `0x38`, `0x39`).
- `p` — ponteiro para a RAM de `link_which_pendants`.
- `link_which_pendants` (`src/variables.h:1107`) — bits dos pingentes.
- `savegame_map_icons_indicator` — usado para o ícone no mapa.
- `overworld_map_state` (`src/variables.h:188`).

#### Cadência

- Uma vez por pingente recebido.

### 9.3 Cristal

#### Localização

- `src/ancilla.c:3807-3813` — `ItemReceipt_TransmuteToRisingCrystal`.
- `src/ancilla.c:3815-3835` — `Ancilla_RisingCrystal`; o bit é setado na
  linha 3828.
- O recebimento do item `0x20` passa por `Link_ReceiveItem` (seção 4).

#### Trecho

```c
    if (!submodule_index) {
      link_has_crystals |= kDungeonCrystalPendantBit[BYTE(cur_palace_index_x2) >> 1];
      submodule_index = 0x18;
      subsubmodule_index = 0;
      memset(aux_palette_buffer + 0x20, 0, sizeof(uint16) * 0x60);
```

#### Dados disponíveis

- `link_has_crystals` (`src/variables.h:1112`) — bits dos cristais.
- `kDungeonCrystalPendantBit[13]` — `src/zelda_rtl.c:47`.
- `cur_palace_index_x2` (`src/variables.h:405`).
- `submodule_index`/`subsubmodule_index` — avançam para a cutscene.

#### Cadência

- `Ancilla_RisingCrystal` roda todo frame enquanto o cristal sobe; o bit só
  é setado uma vez, quando `y < 0x49` e `submodule_index == 0`.

#### Observações

- O cristal usa o mesmo mecanismo de item receipt (`ancilla_item_to_link ==
  0x20`), mas a aquisição real do bit é nesta função, não em
  `Link_ReceiveItem`.

---

## 10. Salvar/carregar o jogo (SRAM) e save states

### 10.1 SRAM do jogo

#### Localização

- `src/messaging.c:257-268` — `SaveGameFile`, grava o save atual.
- `src/messaging.c:2088-2119` — `CopySaveToWRAM`, carrega o save para a RAM
  de trabalho.
- `src/zelda_rtl.c:871-879` — `ZeldaReadSram` (arquivo `saves/sram.dat`).
- `src/zelda_rtl.c:881-889` — `ZeldaWriteSram` (grava e renomeia o antigo
  para `saves/sram.bak`).
- Chamadas de carga: `src/select_file.c:352` (file select),
  `src/messaging.c:827` (game over), e `src/main.c:388` (startup).
- Chamadas de save: `src/messaging.c:818` e `831` (game over),
  `src/ending.c:2439` (fim), `src/select_file.c:136` e `870` (apagar/editar).

#### Trecho

```c
void SaveGameFile() {  // 80894a
  int offs = ((srm_var1 >> 1) - 1) * 0x500;
  memcpy(g_zenv.sram + offs, save_dung_info, 0x500);
  memcpy(g_zenv.sram + offs + 0xf00, save_dung_info, 0x500);
```

#### Dados disponíveis

- `srm_var1` (`src/variables.h:1359`) — slot selecionado (na SRAM).
- `save_dung_info` (`src/variables.h:1060`) — bloco de save de 0x500 bytes.
- `save_ow_event_info` (`src/variables.h:1061`) — eventos do overworld.
- `g_zenv.sram` — SRAM de 8192 bytes.
- `sram_progress_indicator` (`src/variables.h:1115`),
  `sram_progress_flags` (`src/variables.h:1116`).
- `word_7EF4FE` — checksum do save.

#### Cadência

- `SaveGameFile` roda uma vez por save explícito (save-and-quit, game over,
  fim do jogo).
- `CopySaveToWRAM` roda uma vez por carregamento de arquivo.
- `ZeldaReadSram` roda uma vez no startup; `ZeldaWriteSram` roda uma vez por
  gravação.

#### Observações

- `SaveGameFile` grava duas cópias do bloco (normal e backup) e calcula o
  checksum; `ZeldaWriteSram` renomeia o arquivo antigo para `.bak`.
- O carregamento não passa por `SaveGameFile`; quem carrega é
  `CopySaveToWRAM`.
- O save não é só no file select: morte e final do jogo também chamam
  `SaveGameFile`.

### 10.2 Save states (`SaveLoadSlot`)

#### Localização

- `src/zelda_rtl.c:794-815` — `SaveLoadSlot`, chamado pelo teclado em
  `src/main.c:600-608`.
- `src/zelda_rtl.c:533-559` — `StateRecorder_Save`.
- `src/zelda_rtl.c:484-531` — `StateRecorder_Load`.
- `src/zelda_rtl.c:393-402` — `LoadSnesState`.
- `src/zelda_rtl.c:404-410` — `SaveSnesState`.
- `src/zelda_rtl.c:364-377` — `InternalSaveLoad`, que serializa APU, DSP,
  DMA, PPU, SRAM e RAM.

#### Trecho

```c
void SaveLoadSlot(int cmd, int which) {
  char name[128];
  if (which & 256) {
    if (cmd == kSaveLoad_Save)
      return;
    sprintf(name, "saves/ref/%s", kReferenceSaves[which - 256]);
  } else {
    sprintf(name, "saves/save%d.sav", which);
  }
```

#### Dados disponíveis

- `cmd` — `kSaveLoad_Save` (0), `kSaveLoad_Load` (1), `kSaveLoad_Replay`
  (2), definidos em `src/zelda_rtl.h:63-65`.
- `which` — número do slot; `which & 256` indica save de referência em
  `saves/ref/`.
- `state_recorder` (`src/zelda_rtl.c:428`) — histórico de inputs, snapshot
  base, modo replay, contadores.
- `g_zenv` — APU, DSP, DMA, PPU, SRAM; `g_ram` — RAM de 0x20000.
- `state_recorder.replay_mode`, `total_frames`, `replay_frame_counter`.

#### Cadência

- `SaveLoadSlot` roda uma vez por tecla de save/load/replay.
- `StateRecorder_Save`/`Load` rodam uma vez por operação de arquivo.
- `SaveSnesState`/`LoadSnesState` rodam uma vez por snapshot; também são
  usados internamente por `StateRecorder_ClearKeyLog` e pelo snapshot base.

#### Observações

- `SaveLoadSlot` não avisa se o `fopen` falhou; o load simplesmente não
  acontece.
- O save state inclui a SRAM e a RAM inteira; para hooks de “carregou
  estado”, o evento deve ser emitido depois de `StateRecorder_Load`/
  `LoadSnesState`, quando `g_ram` já foi restaurada.
- `SaveSnesState` move a tabela HDMA de `g_ram+0x1DBA0` para `0x1b00` antes
  de serializar (`src/zelda_rtl.c:405`); `LoadSnesState` desfaz depois
  (`src/zelda_rtl.c:397`).

---

## 11. Gráficos do Link, paletas, diálogo/fonte e override de assets

### 11.1 Carregamento dos assets

#### Localização

- `src/main.c:290-291` — `LoadAssets()` e `LoadLinkGraphics()` no startup.
- `src/main.c:811-852` — `LoadAssets`, que lê `zelda3_assets.dat` (ou aplica
  BPS na ROM) e monta `g_asset_ptrs`/`g_asset_sizes`.
- `src/main.c:880-882` — `FindInAssetArray`.
- `src/assets.h:125-126` — `kLinkGraphics` (asset 57).
- `src/assets.h:197-199` — `kDialogue` (94), `kDialogueFont` (95),
  `kDialogueMap` (96).
- `src/zelda_rtl.c:754-774` — `ZeldaSetLanguage`, que escolhe os blocos de
  diálogo/fonte e grava em `g_zenv.dialogue_blk`, `dialogue_font_blk` e
  `dialogue_flags`.

#### Trecho

```c
  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    if ((uint64)offset + size > length)
      Die("Assets file corruption");
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }
```

#### Dados disponíveis

- `g_asset_ptrs[]` / `g_asset_sizes[]` — `src/assets.h:7-8`.
- `kNumberOfAssets`, offsets e tamanhos do arquivo.
- `g_config.link_graphics` — caminho opcional de ZSPR.
- `g_zenv.dialogue_blk`, `g_zenv.dialogue_font_blk`,
  `g_zenv.dialogue_flags`.

#### Cadência

- `LoadAssets` e `LoadLinkGraphics` rodam uma vez no startup.
- `ZeldaSetLanguage` roda uma vez no startup (e pode ser chamado de novo se
  o idioma mudar).

#### Observações

- `LoadAssets` usa o buffer do arquivo inteiro e aponta `g_asset_ptrs` para
  dentro dele; os assets não são copiados individualmente. Overrides
  precisam acontecer depois de `LoadAssets` e antes do primeiro uso.
- `LoadAssets` já modifica `kPalette_DungBgMain` quando
  `kFeatures0_DimFlashes` está ligado (`src/main.c:847-851`), então
  “asset intocado” não é garantido.

### 11.2 Gráficos do Link

#### Localização

- `src/main.c:775-805` — `ParseLinkGraphics`/`LoadLinkGraphics`, override
  opcional de ZSPR.
- `src/misc.c:328-452` — `NMI_PrepareSprites`; a escolha dos endereços de
  DMA a partir de `link_dma_graphics_index`, `link_dma_var1`..`var5` está em
  `392-422`.
- `src/nmi.c:172-197` — `NMI_DoUpdates`, que copia de `kLinkGraphics` para a
  VRAM a cada frame.
- `src/load_gfx.c:485-499` — `DecompressSwordGraphics`/
  `DecompressShieldGraphics`.

#### Trecho

```c
  if (!nmi_disable_core_updates) {
    memcpy(&g_zenv.vram[0x4100], &kLinkGraphics[dma_source_addr_0 - 0x8000], 0x40);
    memcpy(&g_zenv.vram[0x4120], &kLinkGraphics[dma_source_addr_1 - 0x8000], 0x40);
    memcpy(&g_zenv.vram[0x4140], &kLinkGraphics[dma_source_addr_2 - 0x8000], 0x20);

    memcpy(&g_zenv.vram[0x4000], &kLinkGraphics[dma_source_addr_3 - 0x8000], 0x40);
```

#### Dados disponíveis

- `kLinkGraphics` / `kLinkGraphics_SIZE` — asset 57.
- `link_dma_graphics_index` (`src/variables.h:152`),
  `link_dma_var1`..`link_dma_var5` (`src/variables.h:153-157`).
- `dma_source_addr_0`, `dma_source_addr_3`, `dma_source_addr_9`, etc.
  (`src/variables.h:626-635`).
- `nmi_disable_core_updates` (`src/variables.h:588`).
- `link_sword_type` (`src/variables.h:1088`),
  `link_shield_type` (`src/variables.h:1089`),
  `link_armor` (`src/variables.h:1090`).

#### Cadência

- `NMI_PrepareSprites` roda uma vez por game loop
  (`src/zelda_rtl.c:261`).
- `NMI_DoUpdates` roda uma vez por NMI/frame.
- `DecompressSwordGraphics`/`DecompressShieldGraphics` rodam em cargas,
  troca de equipamento e item receipt, não todo frame.
- `ParseLinkGraphics`/`LoadLinkGraphics` rodam uma vez no startup.

#### Observações

- O upload do Link usa `kLinkGraphics` diretamente no NMI; um override de
  asset precisa trocar o ponteiro/conteúdo antes desse upload.
- `NMI_PrepareSprites` também calcula animação de tiles
  (`animated_tile_data_src`, `src/misc.c:424-431`) e outros DMA sources.

### 11.3 Paletas

#### Localização

- `src/load_gfx.c:1901-1911` — `LoadGearPalettes`, paletas de espada,
  escudo e armadura.
- `src/load_gfx.c:2057-2061` — `Palette_Load_LinkArmorAndGloves`.
- `src/load_gfx.c:1725-1739` — `Dungeon_LoadPalettes`.
- `src/load_gfx.c:1699-1723` — `Overworld_LoadAllPalettes`.
- `src/nmi.c:211-213` — upload de `main_palette_buffer` para CGRAM quando
  `flag_update_cgram_in_nmi` está setado.

#### Trecho

```c
void LoadGearPalettes(uint8 sword, uint8 shield, uint8 armor) {  // 8ed6e8
  const uint16 *src = kPalette_Sword + (sword && sword != 255 ? sword - 1 : 0) * 3;
  Palette_LoadMultiple_Arbitrary(src, 0x1b2, 2);

  src = kPalette_Shield + (shield ? shield - 1 : 0) * 4;
  Palette_LoadMultiple_Arbitrary(src, 0x1b8, 3);

  src = kPalette_ArmorAndGloves + armor * 15;
  Palette_LoadMultiple_Arbitrary(src, 0x1e2, 14);
```

#### Dados disponíveis

- `kPalette_Sword` (asset 82), `kPalette_Shield` (83),
  `kPalette_ArmorAndGloves` (81).
- `link_sword_type` (`src/variables.h:1088`),
  `link_shield_type` (`src/variables.h:1089`),
  `link_armor` (`src/variables.h:1090`),
  `link_item_gloves` (`src/variables.h:1084`).
- `main_palette_buffer`/`aux_palette_buffer` — buffers de paleta.
- `flag_update_cgram_in_nmi` (`src/variables.h:8`).
- `palette_main_indoors`, `palette_sp0l`, `palette_sp5l`, `palette_sp6l`.

#### Cadência

- `LoadGearPalettes` roda ao equipar/trocar item, ao carregar área e em
  algumas animações.
- `Dungeon_LoadPalettes`/`Overworld_LoadAllPalettes` rodam por carga de
  área/dungeon.
- O upload para CGRAM roda no NMI quando o flag está setado.

#### Observações

- `LoadGearPalettes` copia para `aux_palette_buffer` e
  `main_palette_buffer` via `Palette_LoadMultiple_Arbitrary`; o NMI depois
  copia `main_palette_buffer` para a CGRAM.
- Um override de paleta precisa acontecer antes do upload do NMI; trocar
  `main_palette_buffer` depois não adianta.

### 11.4 Diálogo e fonte

#### Localização

- `src/messaging.c:2265-2309` — `Text_LoadCharacterBuffer`, lê
  `g_zenv.dialogue_blk` (asset 94).
- `src/messaging.c:2512-2532` — `VWF_RenderSingle`, lê
  `g_zenv.dialogue_font_blk` (asset 95) por caractere.
- `src/load_gfx.c:934-935` — `TransferFontToVRAM`, copia `kDialogueFont(0)`
  para a VRAM.
- `src/zelda_rtl.c:754-774` — escolha de idioma/blocos.
- `src/assets.h:197-199` — macros `kDialogue`, `kDialogueFont`,
  `kDialogueMap`.

#### Trecho

```c
  const uint8 *kFontData = FindIndexInMemblk(g_zenv.dialogue_font_blk, 0).ptr;
  uint8 width = FindIndexInMemblk(g_zenv.dialogue_font_blk, 1).ptr[c];
  assert(width <= 8);
```

#### Dados disponíveis

- `dialogue_message_index` (`src/variables.h:833`) — id da mensagem.
- `g_zenv.dialogue_blk`, `g_zenv.dialogue_font_blk`,
  `g_zenv.dialogue_flags`.
- `kDialogue(idx)`/`kDialogueFont(idx)`/`kDialogueMap(idx)` — assets 94/95/96.
- `messaging_text_buffer`, `messaging_buf`, `vwf_*`.

#### Cadência

- `Text_LoadCharacterBuffer` roda uma vez por diálogo.
- `VWF_RenderSingle` roda por caractere renderizado.
- `TransferFontToVRAM` roda em file select, créditos e fim
  (`src/select_file.c:172`, `src/ending.c:189`, `src/ending.c:2422`).

#### Observações

- O texto e a fonte são lidos diretamente dos blocos de assets; para
  override de diálogo/fonte, o ponto é trocar os blocos/ponteiros antes de
  `Text_LoadCharacterBuffer`/`VWF_RenderSingle`.
- `ZeldaSetLanguage` escolhe o bloco por idioma e grava `dialogue_flags`;
  `Text_DecodeCmd` usa esse flag para decidir US/EU (`src/messaging.c:2206-2261`).

---

## 12. Tabelas de conteúdo para mods de dados

### 12.1 Conteúdo dos baús

#### Localização

- `src/assets.h:27-28` — `kDungeonRoomChests` (asset 8).
- `src/dungeon.c:5704-5801` — `OpenChestForItem`, leitura em
  `src/dungeon.c:5735-5741`.

#### Trecho

```c
    chest_data = kDungeonRoomChests;
    for (i = 0; i < kDungeonRoomChests_SIZE; i += 3, chest_data += 3) {
      chest_room = *(uint16 *)chest_data;
      if ((chest_room & 0x7fff) == dungeon_room_index && --chest_idx < 0) {
        data = chest_data[2];
```

#### Formato e dados

- Cada entrada tem 3 bytes: `uint16 room` + `uint8 item`.
- Bit 0x8000 do `room` indica baú grande (`src/dungeon.c:5742`).
- `data` é o id do item entregue.
- `dungeon_room_index`, `dung_chest_locations`, `dung_savegame_state_bits`.

#### Cadência

- Lida uma vez por abertura de baú.
- A tabela é um asset carregado no startup, não um array C `const`.

### 12.2 Itens e preços das lojas

#### Localização

- `src/sprite_main.c:7638-7672` — `SpritePrep_Shopkeeper`, escolhe os itens
  por sala com `kShopKeeperWhere` (`src/sprite_main.c:7643`).
- `src/sprite_main.c:25164-25173` — `ShopKeeper_SpawnShopItem`.
- `src/sprite_main.c:24974-24990` — `Sprite_BB_Shopkeeper`, dispatch dos
  itens.
- `src/sprite_main.c:25326-25331` — `ShopItem_HandleCost`, desconta
  `link_rupees_goal`.
- Preços estão literais nas funções:
  - `ShopItem_RedPotion150` — 150 (`src/sprite_main.c:25145-25161`)
  - `ShopItem_FighterShield` — 50 (`src/sprite_main.c:25175-25196`)
  - `ShopItem_FireShield` — 500 (`src/sprite_main.c:25198-25219`)
  - `ShopItem_Heart` — 10 (`src/sprite_main.c:25230-25245`)
  - `ShopItem_Arrows` — 30 (`src/sprite_main.c:25248-25264`)
  - `ShopItem_Bombs` — 50 (`src/sprite_main.c:25267-25282`)
  - `ShopItem_Bee` — 10 (`src/sprite_main.c:25286-25301`)
- `kFortuneTeller_Prices[4]` — `src/sprite_main.c:1022`.

#### Trecho

```c
bool ShopItem_HandleCost(int amt) {  // 9ef39e
  if (amt > link_rupees_goal)
    return false;
  link_rupees_goal -= amt;
  return true;
}
```

#### Dados disponíveis

- `link_rupees_goal` (`src/variables.h:1092`) — contador de rúpias (usado
  pelas lojas apesar do nome).
- `sprite_subtype2[k]` — índice do item de loja.
- `sprite_graphics[k]`, `sprite_state[k]`.
- `item_receipt_method` (`src/variables.h:250`).

#### Cadência

- `SpritePrep_Shopkeeper` roda uma vez ao carregar a sala da loja.
- `ShopItem_*` roda todo frame enquanto o item está ativo, mas o custo só é
  descontado quando `ShopItem_CheckForAPress` retorna verdadeiro.
- `ShopItem_HandleReceipt` roda uma vez na compra.

#### Observações

- **Não existe uma tabela única de “itens da loja” e “preços”.** Os itens
  são escolhidos por `SpritePrep_Shopkeeper`/`kShopKeeperWhere` e os preços
  são constantes literais dentro de cada função `ShopItem_*`. Para um mod de
  dados, será preciso criar uma tabela ou instrumentar essas funções.
- A loja de poções é outra função (`SpritePrep_PotionShop`,
  `src/sprite_main.c:6722`), com itens próprios.
- `ShopItem_HandleReceipt` chama `Link_ReceiveItem` (`src/sprite_main.c:25305-25313`),
  então o evento de item da seção 4 também cobre compras.

### 12.3 Dano de inimigos

#### Localização

- `src/sprite.c:156-173` — `kSpriteInit_BumpDamage[243]` — classe de dano
  de contato por tipo de sprite.
- `src/sprite.c:264-267` — `kPlayerDamages[30]` — dano final por classe e
  armadura.
- `src/sprite.c:2724-2735` — `Sprite_AttemptDamageToLinkPlusRecoil`, cálculo
  do dano ao Link.
- `src/sprite.c:138-155` — `kSpriteInit_Health[243]` — vida inicial.
- `src/sprite.c:110-119` — `kEnemyDamages[128]`.
- `src/assets.h:123-124` — `kEnemyDamageData` (asset 56).
- `src/overworld.c:2595-2603` — `DecompressEnemyDamageSubclasses`, que
  expande o asset em `enemy_damage_data`.
- `src/sprite.c:2293-2297` — `Sprite_ApplyCalculatedDamage`, dano causado
  pelo Link ao inimigo.

#### Trecho

```c
void Sprite_AttemptDamageToLinkPlusRecoil(int k) {  // 86f3db
  if (countdown_for_blink | link_disable_sprite_damage)
    return;
  link_incapacitated_timer = 19;
  Sprite_ApplyRecoilToLink(k, 24);
  link_auxiliary_state = 1;
  link_give_damage = kPlayerDamages[3 * (sprite_bump_damage[k] & 0xf) + link_armor];
```

#### Dados disponíveis

- `sprite_bump_damage` (`src/variables.h:697`) — usado como índice em
  `kPlayerDamages`.
- `link_armor` (`src/variables.h:1090`).
- `link_give_damage` (`src/variables.h:343`).
- `sprite_type` (`src/variables.h:729`), `sprite_health`
  (`src/variables.h:732`).
- `enemy_damage_data` — tabela expandida em RAM (não é asset direto).
- `damage_type_determiner` — calculado em `Sprite_ApplyCalculatedDamage`
  (`src/sprite.c:2282-2290`).

#### Cadência

- As tabelas `kSpriteInit_*`/`kPlayerDamages`/`kEnemyDamages` são lidas na
  criação do sprite e no cálculo do hit, não todo frame.
- `DecompressEnemyDamageSubclasses` roda no file select
  (`src/select_file.c:228`), uma vez.

#### Observações

- `kSpriteInit_BumpDamage`, `kSpriteInit_Health` e `kEnemyDamages` são
  arrays `const`/`static const` (provavelmente em `.rodata`); um mod não deve
  escrever neles em runtime sem garantir que a memória é gravável.
- `kEnemyDamageData` é asset; pode ser trocado antes de
  `DecompressEnemyDamageSubclasses` ou o resultado pode ser alterado em
  `enemy_damage_data` depois.
- `kPlayerDamages` tem 30 entradas e é indexado por
  `3 * (sprite_bump_damage & 0xf) + link_armor`; o código só usa índices
  válidos para as classes de dano existentes.

---

## 13. Tabela-resumo

| # | Evento | Ponto principal | Cadência | Dados-chave |
|---:|---|---|---|---|
| 1 | início/fim de frame | `src/zelda_rtl.c:285-294` (`ZeldaRunFrameInternal`); game loop em `257-263`; NMI em `293` | 1× por chamada/frame | `input`, `run_what`, `frame_counter`, `main_module_index` |
| 2 | troca de módulo | `src/misc.c:324-326` + tabela `151-180` | roteamento todo frame; troca 1× | `main_module_index`, `submodule_index`, `saved_module_for_menu` |
| 3a | sala de dungeon | `src/dungeon.c:2544-2545`, `3670-3744`, `6688-6699` | 1× por carga de sala | `dungeon_room_index`, `dungeon_room_index_prev`, `cur_palace_index_x2`, `dung_cur_floor` |
| 3b | área do overworld | `src/overworld.c:790-852`, `909-915` | 1× por borda cruzada | `overworld_screen_index`, `overworld_area_index`, `savegame_is_darkworld`, `old_screen`/`new_area` |
| 3c | Light ↔ Dark | `src/overworld.c:1286` (espelho), `src/dungeon.c:2506` (Agahnim), `src/messaging.c:783` (morte), `src/ending.c:2438`/`472` | 1× por toggle | `savegame_is_darkworld` (0/0x40), `overworld_screen_index`, `sram_progress_indicator` |
| 4 | Link recebe item | `src/player.c:1885-1916` (`Link_ReceiveItem`); mutação em `src/misc.c:713-844` | 1× por recebimento | `link_receiveitem_index`, `item_receipt_method`, `chest_position` |
| 5 | abrir baú | `src/player.c:3795-3816`; leitura em `src/dungeon.c:5704-5801` | 1× por baú (guard) | `index_of_interacting_tile`, `item`, `chest_position`, `dungeon_room_index` |
| 6 | diálogo começa/termina | início `src/misc.c:944-952`; carga `src/messaging.c:2265-2309`; fim `2499-2510` | começa/fim 1×; render todo frame | `dialogue_message_index`, `messaging_module`, `saved_module_for_menu` |
| 7 | dano/morte/fada | dano `src/player.c:148-178`; morte `167-176`; fada `2489-2494`/`src/ancilla.c:5388-5525` | dano 1× por hit; morte 1×; revival todo frame | `link_give_damage`, `link_health_current`, `link_bottle_info`, `link_armor` |
| 8 | sprite criado/destruído/morre | spawn `src/sprite.c:4240-4268`; kill `3947-3965`; morte `2940-2991`; `3020-3031` | 1× por spawn/kill/morte; reset em massa | `sprite_state`, `sprite_type`, `sprite_health`, `sprite_bump_damage`, `num_sprites_killed` |
| 9 | chefe/cristal/pingente | chefe `src/dungeon.c:2494-2535`; pingente `src/misc.c:736-742`; cristal `src/ancilla.c:3824-3834` | 1× por chefe/prêmio | `dung_savegame_state_bits & 0x8000`, `link_which_pendants`, `link_has_crystals`, `cur_palace_index_x2` |
| 10 | SRAM e save state | SRAM `src/messaging.c:257-268`/`2088-2119`; state `src/zelda_rtl.c:794-815`, `484-559` | 1× por save/load | `srm_var1`, `save_dung_info`, `state_recorder`, `cmd`/`which` |
| 11 | assets do Link/paletas/diálogo/fonte | Link `src/nmi.c:172-197` + `src/misc.c:392-422`; paletas `src/load_gfx.c:1901-1911`; texto `src/messaging.c:2265-2309`/`2512-2532`; fonte `src/load_gfx.c:934-935` | gráficos/paletas por frame/área; texto 1× por diálogo | `kLinkGraphics`, `link_dma_*`, `kPalette_*`, `dialogue_message_index`, `dialogue_blk`/`dialogue_font_blk` |
| 12 | tabelas de dados | baús `src/dungeon.c:5735-5741`; lojas `src/sprite_main.c:24974-25331`; dano `src/sprite.c:156-173`/`264-267`/`2293-2297` | lidas na ação correspondente | `kDungeonRoomChests`, `link_rupees_goal`, `sprite_bump_damage`, `kPlayerDamages` |

---

## 14. Observações finais para a F1

- Vários “eventos” são atribuições de variável, não chamadas de função
  (`main_module_index`, `savegame_is_darkworld`, `sprite_state`). Para o
  barramento, o caminho mais seguro é instrumentar as funções centrais
  listadas acima e comparar estado antes/depois onde a atribuição é direta.
- Hooks em `ZeldaRunFrameInternal` e nos caminhos de NMI rodam também no
  regress/comparação. Qualquer escrita de RAM nesses hooks precisa ser
  estritamente no-op sem mod ativo.
- O jogo usa o mesmo índice de módulo 14 para menu e diálogo; o mesmo
  `sprite_state` para fases diferentes de sprite; e o mesmo
  `savegame_is_darkworld` para mundo e bit de tela. O evento deve expor
  estado, não só o número bruto.
- Tabelas `static const` (`kSpriteInit_*`, `kPlayerDamages`,
  `kEnemyDamages`) não são assets e podem estar em memória somente leitura.
  Tabelas de conteúdo grande (`kDungeonRoomChests`, `kEnemyDamageData`,
  diálogo/fonte) vêm de `zelda3_assets.dat` e são o ponto natural para
  override de dados.
- O baú marca `dung_savegame_state_bits` antes de `Link_ReceiveItem`; se o
  mod quiser trocar o conteúdo, o hook de “abriu baú” deve acontecer antes
  da mutação ou o conteúdo deve ser sobrescrito dentro de `OpenChestForItem`.
- O jogo não tem um módulo “title screen” separado: `Module00_Intro` é a
  intro e `Module14_Attract` é o attract/demo. O file select é o módulo 1.

---

## 15. Filtros e decisões (F4e.1)

O mecanismo fica em `src/mod/hooks.{c,h}`. No código do jogo, cada ponto usa
`MOD_FILTER(kModFilter_X, valor, arg)` ou `MOD_DECIDE(kModDecide_X, padrao,
arg)`. Sem assinante, o macro devolve o valor/padrão original e não chama
nada. Os nomes, a faixa do valor e o tipo estão na tabela única
`kModHooks`; Lua, `--mod-trace` e esta documentação usam os mesmos nomes.

| Gancho | Tipo | Ponto no C | Endereço original | Arg |
|---|---|---|---|---|
| `SwordBeamDamage` | filtro | `Ancilla_CheckDamageToSprite_aggressive`, `src/ancilla.c` (só `type == 0x0c`, o raio de `AddSwordBeam`) | `86ecbd` | slot do sprite atingido |
| `FairyReviveHealth` | filtro | `GameOver_InitializeRevivalFairy`, `src/messaging.c` | `89f6a4` | vida máxima |
| `MaxBombs` | filtro | `ModHooks_MaxBombsForLevel`; chamado por `Hud_RefillLogic`/`Hud_Update_Inventory` (`src/hud.c`) e `Sprite_BombShop_Bomb`/`ShopItem_Bombs` (`src/sprite_main.c`) | `8ddb92`, `8dfc09`, `9ee190`, `9ef2f0` | nível de bomba |
| `MaxArrows` | filtro | `ModHooks_MaxArrowsForLevel`; chamado pelos mesmos pontos de HUD e `ShopItem_Arrows` (`src/sprite_main.c`) | `8ddb92`, `8dfc09`, `9ef2af` | nível de flecha |
| `ReceiveItemAlternate` | filtro | `Link_PerformOpenChest`, `src/player.c` | `87b574` | id do item no baú |
| `BirdTravelDestination` | filtro | `FluteMenu_LoadTransport`, `src/overworld.c` (as tabelas `kBirdTravel_*` ficam em `src/messaging.c:153-157`) | `82ec39` | byte reservado de `birdtravel_var1` |
| `OverworldTransitionTarget` | filtro | `OverworldHandleTransitions`, `src/overworld.c`, logo após calcular `new_area` | `82a9c4` | direção (bits de direção) |
| `LinkDamage` | filtro | `Link_ControlHandler`, `src/player.c`, e `DungeonPitDoDamage`/`TakeDamageFromPit` para buraco | `87807f`, `879502`, `81ffd9` | fonte: 0 sprite, 1 ancilla, 2 buraco, 3 ambiente |
| `PendantDropSpecialCase` | decisão | `AncillaAdd_FallingPrize`, `src/ancilla.c` | `898bc1` | índice do item que cai |
| `SwordTurnWhileCharging` | decisão | `Link_CheckForSwordSwing`, `src/player.c`, antes de `HandleSwordControls` | `879cd9` | direção atual |
| `BlockAlreadyPushed` | decisão | `PushBlock_CheckForPit`, `src/dungeon.c` | `81d8d4` | índice do bloco |
| `PikitStealShield` | decisão | `Sprite_AA_Pikit`, `src/sprite_main.c`, no ramo `j == 4` | `9e8bbf` | slot do sprite |
| `MirrorAllowed` | decisão | `LinkItem_Mirror`, `src/player.c`, substituindo a condição de mundo | `87a91a` | mundo atual (0 ou `0x40`) |
| `SwordBeamSpawn` | decisão | `AddSwordBeam`, `src/ancilla.c`, no início | `8ff67b` | direção atual |
| `PotBreakSwordLevel` | decisão | `HandleItemTileAction_Dungeon`, `src/dungeon.c`, no teste da opção `BreakPotsWithSword` | `81dabb` | nível da espada (`link_sword_type`) |

Observações:

- **`BirdTravelDestination`:** o briefing pedia o ponto em `src/messaging.c`.
  As tabelas `kBirdTravel_*` estão lá, mas quem consome o índice escolhido é
  `FluteMenu_LoadTransport` em `src/overworld.c`. O filtro foi colocado no
  consumo (uma vez por warp), não no laço de seleção do menu, para não
  reescrever a escolha a cada frame. O caso especial do final (`birdtravel_var1[0] == 8`)
  não passa pelo filtro.
- **`MirrorAllowed`:** o valor padrão é a condição original inteira
  (porta, `MirrorToDarkworld`, interior e mundo). Assim o mod pode negar o
  espelho no Mundo Sombrio e também liberar o caminho que o jogo já nega, sem
  mudar nada quando não há assinante.
- **`MaxBombs`/`MaxArrows`:** os dois helpers em `src/mod/hooks.c` são a única
  porta para as tabelas `kMaxBombsForLevel`/`kMaxArrowsForLevel`. As duas
  tabelas hexadecimais locais de `sprite_main.c` (`kMaxBombsForLevelHex` e
  `kMaxArrowsForLevelHex`) são contadores de animação do preenchimento, não o
  máximo, e continuam como estavam.
- **`LinkDamage`:** o argumento é a fonte registrada pela última atribuição a
  `link_give_damage`; `sprite.c`/`sprite_main.c` marcam sprite, a bomba marca
  ancilla, os espinhos e o raio de garnish marcam ambiente, e as duas rotinas
  de buraco passam `2` diretamente. O filtro roda antes de o jogo calcular a
  morte, então devolver 0 impede a morte por qualquer fonte. O evento
  `LinkDamage` seguinte recebe o dano já filtrado.
- **`SwordBeamDamage`:** o valor é a **classe de dano** (`kAncilla_Damage`),
  não pontos de vida. `AddSwordBeam` cria `ancilla_type == 0x0c`; a antiga
  condição `0x0b` filtrava o tiro da vara de gelo. A classe 1 é a espada L1 e
  a classe 2 é a espada mestra (L2) no golpe normal (`kSprite_Func14_Damage[1]`).
- **`SwordBeamSpawn`:** a decisão fica em `AddSwordBeam`, que é o único ponto
  de criação do raio; devolver `false` não cria a âncora nem toca o som do
  beam e não muda o golpe de espada.
- **`PotBreakSwordLevel`:** a decisão só roda com a opção
  `BreakPotsWithSword` ligada e `button_b_frames != 0`; o padrão
  (`link_sword_type != 1`) mantém o comportamento original, que bloqueia só
  L1. Para L4, o mod devolve `level >= 4`.
- **`--mod-trace`:** imprime `hook <frame> Filter <Nome> <antes> -> <depois>
  arg <arg>` (ou `Decide`) somente quando o resultado final mudou.

### `DropSpawned` (evento da F4e.1b)

| Evento | Ponto no C | Endereço original | Argumentos |
|---|---|---|---|
| `DropSpawned` | `PrepareEnemyDrop` (drops de inimigo/Pikit/prêmio) e `Sprite_SpawnSecret` (arbusto, vaso, grama e demais segredos de terreno), `src/sprite.c` | `86f9d1`, `868264` | a = tipo do item, b = slot do sprite |

O evento usa `MOD_EMIT` e só é emitido quando há assinante. Em
`PrepareEnemyDrop` ele roda depois de o slot estar inicializado e antes de
`SpriteDeath_Func4`; se o handler zerar `sprite_state`, o código ainda marca o
inimigo como morto (`Sprite_ManuallySetDeathFlagUW` e `num_sprites_killed`)
e retorna sem desenhar o item. Em `Sprite_SpawnSecret` ele roda no fim da
criação, antes do próximo desenho do frame.

---

## 16. Eventos da F4e.2

Todos usam `MOD_EMIT`, então continuam no-op sem mod ativo. Os endereços são
os da ROM US no C; a coluna do `z3randomizer` mostra o `org` equivalente no
`z3r-hooks.asm` (ROM JP 1.0).

| Evento | Ponto no C | Endereço US | Ponto no `z3randomizer` | Equivalência |
|---|---|---|---|---|
| `FileLoad` (a = slot 0-based) | `Module05_LoadFile`, `src/misc.c`, logo depois de `EnableForceBlank` e antes do corpo | `828136` | `OnFileLoad`, `$82:8046`, no entry de `Module_LoadFile` (substitui o `JSL EnableForceBlank`) | Equivalente: o save já passou por `CopySaveToWRAM` e o handler vê o slot. Como no z3r, também roda no "save and continue". |
| `FileCreate` (a = slot 0-based) | `NameFile_DoTheNaming`, `src/select_file.c`, depois de `ZeldaWriteSram` | `8cda4d` (`InitializeSaveFile`, `8cdb3e`) | `OnFileCreation`, `$8C:DB4C`, substitui o `STA $701FFE` de `InitializeSaveFile` | Equivalente: os dois rodam depois de o arquivo novo ser inicializado e válido; o z3r termina o checksum no próprio handler, o nosso já chamou `ZeldaWriteSram`. |
| `DungeonEnter` (a = `cur_palace_index_x2`, b = `which_entrance`) | `Dungeon_LoadEntrance`, `src/dungeon.c`, no store de `orange_blue_barrier_state = 0` | `82d8b3` | `OnDungeonEntrance`, `$82:D8C7`, substitui `STA $7EC172` (`PegColor`) | Equivalente: é literalmente o store que o z3r substitui; palácio e entrada já estão definidos. |
| `DungeonExit` (a = palácio) | `LoadOverworldFromDungeon`, `src/overworld.c`, antes de `cur_palace_index_x2 = 0xff` | `82e4a3` | `OnDungeonExit`, `$82:E21B`, substitui `STA $040C` (`DungeonID`) | Equivalente: mesmo ponto em que o jogo limpa o `DungeonID`; aqui o palácio antigo ainda está disponível para o evento. |
| `Quit` (a = 1 se salvou) | `Death_Func15`, `src/messaging.c`, no ramo de quit (`subsubmodule_index == 1`), depois do `SaveGameFile` condicional | `89f50f` | `OnQuit`, `$89:F60B`, logo depois do `SaveGameFile` de `GameOver_SaveAndQuit` | Equivalente: mesmo ramo de "salvar e sair" (menu de morte e `Module17_SaveAndQuit`). `a = 0` quando não houve `SaveGameFile` por falta de progresso. |
| `OverworldTransition` (a = área nova, b = direção) | `OverworldHandleTransitions`, `src/overworld.c`, depois de `new_area` e do filtro, antes de `AreaEnter` | `82a9c4` | `OnOWTransition`, `$82:AA87`, no entry de `Module09_LoadAuxGFX` | **Não é o mesmo instruction point.** O z3r roda mais tarde, em toda carga de aux GFX do overworld (inclusive vindo de dungeon/área especial); o nosso roda no momento da troca por borda, onde a área nova e a direção estão disponíveis. É o mesmo momento lógico para uma transição de tela, mas com cadência mais estreita. |

Observação: os endereços JP do `z3randomizer` e os US do C diferem em vários
pontos; a comparação acima foi feita pelo código que o desvio substitui
(`things we wrote over` em `events.asm`) e pela rotina correspondente no C.

---

## 17. Substituição de rotina de sprite (F4e.3)

O ponto único é `SpriteActive_Main`, em `src/sprite_main.c` (endereço US
`869271`). Ele recebe o slot, lê `sprite_type[k]` e chamava diretamente
`kSpriteActiveRoutines[type](k)`. Agora há um desvio barato:

```c
void SpriteActive_Main(int k) {  // 869271
  uint8 type = sprite_type[k];
  if (g_mod_sprite_override_mask[type]) {
    ModSpriteOverride_Run(type, k, kSpriteActiveRoutines[type]);
    return;
  }
  kSpriteActiveRoutines[type](k);
}
```

| Item | Onde |
|---|---|
| Tabela de rotinas por tipo | `kSpriteActiveRoutines[243]`, `src/sprite_main.c` |
| Máscara de despacho | `g_mod_sprite_override_mask[256]`, `src/mod/sprite_override.c` |
| Assinatura / despacho | `ModSpriteOverride_Subscribe`, `ModSpriteOverride_Run`, `src/mod/sprite_override.c` |
| Nomes dos tipos | `src/mod/sprite_names.c`, gerado de `assets/tables.py:kSpriteNames` por `make sprite-names` |
| API Lua | `zelda.override`, `zelda.sprite.get/set`, `original()` em `src/mod/lua_script.c` e `src/mod/lua_api.c` |
| Campos do slot | tabela `kLuaSpriteFields`, `src/mod/lua_api.c` (nomes de `src/variables.h` sem `sprite_`) |

Sem override para o tipo, a máscara é zero e o caminho é exatamente o
anterior: uma leitura de array e a chamada indireta para a rotina do jogo.
Nenhuma função do módulo de mods é chamada. A tabela original continua
intacta; `original(slot)` chama `kSpriteActiveRoutines[type](slot)` através do
ponteiro salvo no contexto do tick.

Com override, `ModSpriteOverride_Run` escolhe o assinante de maior
`priority` (empate: quem carregou depois) e chama o `tick` Lua. Se o handler
falhar, desativar o mod, estourar o limite de instruções ou devolver estado
inválido, o próprio `Run` chama a rotina original no mesmo frame. O replay
`tests/replays-features/f4e-sprite-override.sav` exercita o caminho no
cavaleiro verde (`0x4B`) e o golden tem as linhas `override ...` e o hash
final.

Com `--mod-trace`, cada slot aparece na primeira chamada e depois em amostras
a cada 32 frames:

```
override 2 Sprite 4B-Knight 2 x 368 y 2848
override 96 Sprite 4B-Knight 1 x 384 y 2837
```

Assinar um override liga `g_mods_change_content`; o despacho em si não muda
nada quando a máscara está zerada.
