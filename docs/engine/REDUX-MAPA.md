# REDUX-MAPA — o que o hack Redux exige do zelda3

Análise feita no worktree `agent/R0-redux-mapa`, sobre o zelda3 em
`6e91d77` e o fonte do Redux no commit `37c3098`. **Nenhum arquivo de
código do jogo ou da engine foi alterado**; este documento é o único
artefato da tarefa.

## 1. Escopo, método e licença

- Fonte do Redux: `.agents/redux-src/`
  (GPL-3, ShadowOne333), commit `37c3098`.
- ROMs analisadas: `redux.sfc` e `redux-retrans.sfc`, ambas 2 MB, LoROM,
  FastROM (bancos `$80+`), montadas a partir da ROM US do zelda3. As duas
  estão com o conjunto padrão do Redux: `!newgfx = 1`,
  `!retranslation = 0/1` e **todos os opcionais de `code/optional.asm`
  comentados**. Conferi isso nos bytes das ROMs (tabela de full hearts
  original, sem mirror bilateral, sem Pikit sem roubo, etc.).
- O Redux é montado em três camadas: (a) `map_layouts.ips` é aplicado
  antes do assembly; (b) `code/main.asm` inclui os `.asm`; (c)
  `scompress` injeta os gráficos de `code/gfx/*.bin`. O subtítulo
  "Triforce of the Gods" é um IPS separado (`subtitle_layouts.ips`).
- Para achar a rotina C, usei o endereço SNES do `org`/`%fseek` e o
  comentário `// 87a006` do port. Quando o endereço não tem comentário,
  localizei a rotina pelo comportamento e pelo uso das mesmas tabelas.
  Onde não consegui fechar a correspondência, isso está dito na seção 9.
- **Licença**: o Redux é GPL-3 e o zelda3 é MIT. Este documento descreve
  o comportamento com palavras próprias e cita arquivo:linha do Redux;
  nenhum código, tabela ou texto do Redux foi copiado para o repositório.
  Exemplo de citação usada na tabela: `code/gameplay/misc.asm:14-15`
  (`org $09F6A8`), que muda a fairy de 56 para 64 unidades de vida.

## 2. Resumo por classe

São **70 linhas** de mudança na tabela (53 da base + 17 opcionais).

| Classe | Quantidade | Significado |
|---|---:|---|
| **A — já existe no zelda3** | 33 | opção de `zelda3.ini`, `DimFlashes`, MSU, ou comportamento que já é o padrão do port |
| **B — dados importáveis** | 20 | gráficos, paletas, textos, tabelas, salas e mapas |
| **C — script Lua** | 1 | dá para fazer com a API atual (`zelda.on` + `zelda.state`/`zelda.link`) |
| **D — gancho novo** | 10 | falta evento/filtro/opção em um ponto do C |
| **E — comportamento novo (F4e)** | 6 | menu de 24 itens, HUD/magic meter, timer, pá que desenterra itens |

A maioria dos bugfixes do DX/Redux já está coberta por `MiscBugFixes`,
`DimFlashes` ou pelas opções de `[Features]`. O que realmente falta é
**conteúdo** (salas/mapas/texto) e **comportamento novo** (menu/HUD/timer
e a pá). A seção 6 propõe a ordem de um mod `redux` local.

## 3. Tabela da base (53 linhas)

Legenda: “Redux” é `arquivo:linha` (e o `org`/`%fseek` quando ajuda);
“zelda3” é a rotina C equivalente.

| # | Mudança | Redux | O que faz de fato | zelda3 | Classe | Notas |
|---:|---|---|---|---|---|---|
| 1 | FastROM | `code/loz3-dx/loz3-dx.asm:18-20`, `FastROM-ASM.asm:28-60`, `FastROM-JSL.asm`, `FastROM-JML.asm`, `FastROM-tables.asm` | Liga `$420D`, muda vetores NMI/IRQ e reescreve bancos de JSL/JML/DL para `$80+` | não há modelo de velocidade de barramento no port | A | Irrelevante para o C; a ROM FastROM é lida normalmente. Não confundir com “mudança de dados”. |
| 2 | MSU-1 | `code/loz3-dx/loz3-dx.asm:30`, `loz3-msu.asm:95-333` | Substitui o NMI do SPC, mapeia faixas, faz fade e espera fanfares/final | `src/audio.c:48-210`; `EnableMSU`, `MSUVolume`, `ResumeMSU` | A | O zelda3 já tem MSU PCM, OPUZ e Deluxe; o Redux tem MSU PCM e uma `track_list` própria. O FMV do opcional não existe no port. |
| 3 | Kholdstare derretendo | `Kholdstare_shell.asm:8-18` (`$006C58`, `$006CBB`) | Faz o gelo sumir devagar em vez de instantaneamente | `src/load_gfx.c:1106` (`KholdstareShell_PaletteFiltering`), sob `MiscBugFixes` | A | Equivalente; o port usa `0x50`/`0x40` e copia 8 palavras. |
| 4 | Dash do Ancient Stone Tablets | `ast_dashing.asm:12-111` (`$02857D`, `$028B13`, `$07911B`, `$099F32`) | Permite virar correndo e continuar o dash entre telas; cancela em água/escada | `src/player.c:1257`, `src/dungeon.c:6780`, `src/overworld.c:566` (`TurnWhileDashing`) | A | Parecido, mas o opcional “Dashing Stops On Screen Transition” mostra que o Redux separa “virar” de “continuar entre telas”; o zelda3 tem uma opção só. |
| 5 | Luva/paleta de bunny | `bunny_glove_fix.asm:11-26` (`$0ED73C`, `$1BEE1D`) | Mantém a paleta da luva em mapa, Ether, eletrocussão, Pikit e bunny | `src/load_gfx.c:1887-1901` (`LoadActualGearPalettes`, `LoadGearPalettes_bunny`, `Palette_UpdateGlovesColor`) | A | `Palette_UpdateGlovesColor` só roda com `MiscBugFixes`; `LoadGearPalettes_bunny` é chamado nos pontos certos. |
| 6 | Cane of Somaria (magia) | `cane_somaria_fix.asm:12-37` (`$07AED4`) | Evita o underflow/uso repetido da vara que causa bugs | `src/ancilla.c:6871` (`Ancilla_TerminateSelectInteractives`) limpa `0x2c` e `bitmask_of_dragstate` | A | Correspondência provável, não confirmada byte a byte; ver seção 9. |
| 7 | Cape pull | `cape_fix.asm` (`$0DAA65`) | Não deixa puxar objeto com a capa sem gastar magia | `src/player.c:3309-3315` (`Link_HandleCape_passive_LiftCheck`), sob `MiscBugFixes` | A | Equivalente. |
| 8 | Coletar item com espada | `collect_items_sword.asm:12-97` (`$06F725`) | Espada coleta coração/rúpia/etc. no chão | `src/sprite.c:2555` (`CollectItemsWithSword`) | A | Equivalente. |
| 9 | Contador de mortes | `death_counter.asm:12-47` (`$09F597`) | Save-and-quit e “não salvar” não contam morte | `src/messaging.c:779-801` (`Death_Func15(count_as_death)`), sob `MiscBugFixes` | A | Equivalente. |
| 10 | Flicker do menu | `flickering_fix.asm:12-61` (`$08832D`, `$00876B`) | Corrige piscada de DMA no menu/select do SNES | o port desenha em software, sem essa corrida de DMA | A | Não se aplica ao renderizador do port. |
| 11 | Flauta/ocarina | `flute_fix.asm:12-87` (`$0AB78F`, `$0AB7D6`, `$0AB8ED`) | Cancela o voo com B e acerta o ícone no mapa | `src/messaging.c:960-1013` (`CancelBirdTravel`) | A | Parecido; o ícone no mapa é tratado por asset/VRAM no port. |
| 12 | L/R troca de item | `l_r_item_switch.asm:13-140` | Troca o item equipado em Y ciclando só itens possuídos, com garrafas | `src/hud.c:1540` (`ItemSwitchLR`) | A | Parecido; o zelda3 também permite X/L/R e reordenar com Y+direção. |
| 13 | Mothula e espada L4 | `mothula_l4.asm` (`$028136`, `$06E83F`) | Mothula passa a levar dano de L4 e spin L3 | `src/sprite_main.c:22496-22502` e `src/sprite.c:2024-2032`, sob `MiscBugFixes` | A | Equivalente. |
| 14 | Pot com espada | `pott_l2sword.asm` (`$01DABD`, `$01DAFC`) | Qualquer espada quebra potes | `src/dungeon.c:5566-5571` (`BreakPotsWithSword`) | A | Diferença: o zelda3 exclui `link_sword_type == 1`; o Redux, com o `bra`, aceita L1. |
| 15 | Escudo após Pikit | `shield_fix.asm:12-36` (`$06D173`) | Restaura a paleta do escudo roubado ao usar Ether | `src/sprite.c:1421-1423`, sob `MiscBugFixes` | A | Equivalente. |
| 16 | Water Gate | `bugfixes.asm:20-21` (`org $01CBAC`) | Deveria floodar só o corredor, não a sala toda | `src/dungeon.c:4838` (`RoomTag_WaterGate`) | A | **Não fecha**: a ROM US já tem `04` em `$01CBAC`; o comentário do Redux diz que o original era `0C`. No ROM fornecido o patch é no-op. |
| 17 | Dano do sword beam | `bugfixes.asm:43-44` (`org $06EC8A`) | Faz o beam ter dano de espada L2 (classe de dano 2) | `src/ancilla.c:487-504`; `kAncilla_Damage[0xc] = 1`; filtro `SwordBeamDamage` em `src/mod/hooks.c` e `mods-examples/redux-ganchos` | D | O valor é uma classe de dano, não pontos de vida; a classe 2 é a da espada mestra no golpe normal. |
| 18 | Whirlpool/música | `bugfixes.asm:56-69` | Evita reiniciar a música ao usar whirlpool depois do mapa | `src/overworld.c:1440` (`Module09_2E_Whirlpool`) | A | O port usa outro caminho de áudio; não há correção explícita, mas também não reproduzi o bug. |
| 19 | Chime do portal de gelo | `bugfixes.asm:76-84` (`$04ECA0`, `$1BC902`) | Toca o chime ao levantar a pedra do portal | `src/overworld.c:3636`, sob `MiscBugFixes` | A | Equivalente. |
| 20 | Saída de dungeon com Somaria | `bugfixes.asm:90-121` | Evita estado de trapdoor preso ao sair com bloco de Somaria | `src/dungeon.c:3722-3780` e `:6634` (`dung_flag_trapdoors_down`) | A | Correspondência provável; não achei um teste específico. |
| 21 | SFX do Moldorm | `bugfixes.asm:127-131` | Corrige o som de quique na cabeça | tabelas de SFX do port | A | O port não usa o mesmo endereço de SFX; não confirmei equivalência. |
| 22 | Paleta de bunny | `bunny_palette.asm:11-26` (`$0ED6C8`, `$1BEDFB`) | Usa a paleta de armadura 3 quando Link é bunny | `src/load_gfx.c:1897` (`LoadGearPalettes_bunny`) | A | Equivalente. |
| 23 | VC/photosensitivity | `vc_patches.asm:8-42` | Ether mais longo, flash do título mais fraco, selo de Ganon mais lento, paletas de dungeon | `DimFlashes`: `src/config.c:378`, `src/ending.c:1214`, `src/player.c:2710`, `src/main.c:1173`, `src/load_gfx.c:1925` | A | Mesma intenção; os valores não são idênticos (ex.: título usa `0x05` no port, o Redux `0x1511`). |
| 24 | Skip da intro | `title_skip.asm:7` (`org $0CC122`) | Qualquer botão pula a sequência do título | `src/ending.c:507` (`SkipIntroOnKeypress`) | A | Equivalente (o port reduz o limiar de 8 para 4). |
| 25 | Max consumables | `gameplay/max_consumables.asm:6-64` | Bombas até 60, flechas até 99; ajusta contadores e tabelas decimais | `src/hud.c:26-27`, `src/sprite_main.c:11480`, `:11517`; checagens em `hud.c:489-495` e `sprite_main.c:24268/25254/25273` | D | Não é uma tabela só: há arrays duplicados, checagens de loja e display. Precisa de opção/registro de tabelas. |
| 26 | Bug das rúpias do pond | `max_consumables.asm:56-70` | Não deixa o `stz` de 16 bits zerar heart pieces | `src/sprite_main.c:11380-11433` grava `link_rupees_in_pond` (`uint8`) separado de `link_heart_pieces` | A | O port já não tem o bug. |
| 27 | Ícones de chefe no mapa | `gameplay/misc.asm:7-10` (`$0AEE79`) | Reposiciona ícones de chefes no mapa da dungeon | `src/messaging.c:71` (`kDungMap_Tab28`) | A | A tabela do port é idêntica (`-1,-1,1,1,6,...`). |
| 28 | Fairy restaura mais | `gameplay/misc.asm:14-15` (`$09F6A8`) | Passa `link_hearts_filler` de 56 para 64 na ressurreição | `src/messaging.c:862-866` (`GameOver_InitializeRevivalFairy`) | D | Constante `56`; sem opção. |
| 29 | Drop do pingente 3 | `gameplay/misc.asm:24-25` (`$098C26`) | Faz o drop usar a posição normal em vez do caso especial do palácio 20 | `src/ancilla.c:5975-6030` (`AncillaAdd_FallingPrize`), condição `cur_palace_index_x2 == 20` | D | Sem opção; o caso especial está no C. |
| 30 | Túnicas azul/vermelha | `gameplay/misc.asm:33-40` (`$01EA69`, `$07B54A`) | Baú da Torre dá azul; o alternativo passa a dar vermelha | `src/player.c:3816-3837` (`kReceiveItemAlternates[0x22]`) + asset `kDungeonRoomChests` | D | O baú é dado B (asset); a tabela de alternativos é `static const` no C. |
| 31 | Direção do golpe | `gameplay/misc.asm:44-79` (`$079AC7`, `$07FA60`) | Permite mudar a direção durante o charge do spin e golpear para o último lado | `src/player.c:2176` (`Link_CheckForSwordSwing`) / `HandleSwordControls` | D | Sem opção; é lógica de input/animação. |
| 32 | Cavernas da Death Mountain | `layouts/mountain_caves_fix.asm:12-55` | Troca as saídas `00EF`/`00FF` e entradas `1E`/`1F` | dados de sala/overworld (`kEntranceData_*`) | B | A extração do Redux confirma a troca na área 5 (`room: 255` ↔ `239`). |
| 33 | Primeiro warp da ocarina | `layouts/ocarina_warp.asm:7` (`$0AB763`) | Move o primeiro ponto de warp para perto da Heart Container | `src/messaging.c:153-156` (`kBirdTravel_*`) | D | Tabelas `static const`; não há registry para elas. |
| 34 | Pá que desenterra itens | `gameplay/shovel_treasures.asm:6-102` | No overworld, cavar pode gerar coração, magia ou rúpia | `src/player.c:2584-2620` (`LinkItem_Shovel`) | E | Comportamento novo; não existe evento de “cavou”. |
| 35 | VWF do Redux | `text/vwf.asm:4` (`$0ECADF`) | Tabela de larguras do novo diálogo | `assets/sprite_sheets.py:157` (`font_redux`) | B | `import-rom` extrai a fonte e importa `kDialogueFont` quando difere. |
| 36 | Diálogo Redux | `text/dialogue1.asm:23-32`, `dialogue2.asm:27-36` | Muda o script inteiro | `src/messaging.c:2265-2309` e `:2512-2532`; assets `kDialogue`/`kDialogueFont` | B | `import-rom` extrai e importa `kDialogue` no slot `us`; funciona para Redux e Retranslation. |
| 37 | Créditos Redux | `text/credits.asm:159-554` | Muda textos/offsets/tabelas dos créditos | `src/ending.c` + `kEnding_Credits_Text`/`Offs` | B | `import-rom --assets kEnding_Credits_Text,kEnding_Credits_Offs` importa; as tabelas pequenas de fonte ainda são C. |
| 38 | Cabelo do Link e Rei | `gfx/palettes.asm:7-31` | Link loiro e Rei de vermelho | assets `kPalette_ArmorAndGloves`; o byte do Rei não está no import padrão | B | `import-rom` detecta `kPalette_ArmorAndGloves` (150 bytes); o byte `$0E9E90` precisa de asset/registro à parte. |
| 39 | Menu de 24 itens | `menus/24items_menu_withgfx.asm:8-887` | 24 slots, layout 6×4, HUD novo, magic meter horizontal | `src/hud.c:8` (`kNewStyleInventory = 0`) tem boa parte do código, mas **desligada em compile-time** | E | O port já tem `kHudItemInVramPtr_New[24]`, garrafas separadas e caminhos de 24 itens; falta toggle em runtime e o timer. |
| 40 | Shovel como item separado | `menus/24items_menu_shovel.asm:11-183` | Pá vira slot permanente no menu novo | `src/hud.c:1021`, `:1269` (caminhos `kNewStyleInventory`) | E | O código existe atrás do mesmo `kNewStyleInventory`; precisa do menu novo ligado. |
| 41 | Flauta não pode ser desenterrada de novo | `menus/24items_flute_fix.asm:11-25` (`$1BB1E0`) | Evita pegar a flauta repetidamente quando a pá não a substitui | `src/hud.c` já trata `kHudItem_Flute` e `kHudItem_Shovel` | A | O port tem os dois itens; a checagem específica do Redux não é necessária se o menu novo estiver correto. |
| 42 | Timer no menu | `menus/24items_menu_timer.asm:5-260` | Conta o tempo de jogo e desenha no menu/HUD | não existe no port | E | O port não tem timer de partida. Um overlay simples caberia em Lua (`FrameDraw`), mas o timer dentro do menu é tilemap/VRAM. |
| 43 | L/R no menu de 24 | `menus/l_r_item_switch_24items.asm:15-260` | Cicla 24 slots/itens e garrafas com L/R | `src/hud.c:1540` (`ItemSwitchLR`) | A | A opção do port já cobre o conceito; o layout de 24 slots é a parte E. |
| 44 | Gráficos novos (scompress) | `code/gfx/Redux.bin`, `Subtitle.bin`, `GreenAgahnim.bin`, `AgahnimSubtitle.bin`; `Makefile:337` | Injeta os tiles novos do menu/HUD e variantes de Agahnim | assets `kSprGfx`/`kBgGfx`; `src/assets.h:139-141` | B | `import-rom` achou 9 packs em `kSprGfx` (26,66,67,81,85,93,105,106,107) e 3 em `kBgGfx` (25,113,114). |
| 45 | Layouts de mapa | `code/layouts/map_layouts.ips` (251 registros, ~178 KB) | Muda salas, mapas de overworld, entradas e objetos | assets/YAML de sala/overworld; `assets/restool.py`, `compile_resources.py:675-703` | B | Importável em princípio, mas o `modkit` não tem comando para importar conteúdo YAML; ver seção 5. |
| 46 | Subtítulo do título | `code/layouts/subtitle_layouts.ips` (10 registros) | Troca o tilemap do título para “TRIFORCE OF THE GODS” | `kBgTilemap_0` / assets de título | B | Só existe na variante com subtítulo; não está nas ROMs fornecidas. |
| 47 | Ice Palace estilo GBA | `ReadMe.md:70`; `map_layouts.ips` | Muda o layout das salas `09E` e `0BE` | YAML de sala | B | A extração do Redux difere nas salas 158 e 190 (nomes ZScream 0x9E/0xBE). |
| 48 | Porta do Santuário | `ReadMe.md:76`; `map_layouts.ips` | Restaura a porta/entrada da sala 0x12 | YAML de sala | B | A extração difere na sala 18. |
| 49 | Estrela de Davi / hieróglifos | `ReadMe.md:74-75`; `map_layouts.ips` + gráficos | Restaura conteúdo japonês na Eastern Palace (0xB8) | YAML/gráficos | B | Não consegui isolar a mudança de gráfico; a sala 184 não apareceu na lista de diffs. |
| 50 | Estátuas de caveira | `ReadMe.md:77` | Troca blocos por estátuas em Skull Woods (0x49) | YAML de sala/gráficos | B | A sala 73 difere; a mudança é de camada/objeto. |
| 51 | Placa do Zora | `ReadMe.md:78` | Restaura a placa perto do domínio Zora | YAML de sala/overworld | B | Não localizei uma sala específica nos diffs; pode estar no mapa comprimido. |
| 52 | Cannonball Soldier | `ReadMe.md:82` | Põe o inimigo 0x6B na sala 0xB0 da Ganon's Tower | YAML de sala; `src/sprite_main.c:1418` já tem `Sprite_6B_CannonTrooper` | B | A sala 176 difere; a IA do inimigo já existe no port. |
| 53 | Retranslation | `text/retranslation/retranslation_main.asm:1-17` (inclui `dialogue_ret1/2`, `credits_ret`) | Script baseado no japonês | mesmos assets de diálogo/fonte | B | `import-rom` extrai e importa `kDialogue`/`kDialogueFont` também no Retranslation (testado); o formato de texto é o US. |

## 4. Tabela dos opcionais (17 linhas)

`code/optional.asm` tem todos comentados; os arquivos abaixo vêm de
`code/optional/*.asm` e/ou `patches/Optional/*.ips`. A ROM fornecida é a
padrão, então nenhum deles está ativo.

| # | Opcional | Redux | O que faz | zelda3 | Classe | Notas |
|---:|---|---|---|---|---|---|
| O1 | Desligar o dash turning | `code/optional/disable_dash_turn.asm:5-13`; `patches/Optional/Disable Dash Turning.ips` | Volta o comportamento original de não virar correndo | `TurnWhileDashing = 0` | A | Basta a opção desligada; não é um mod. |
| O2 | Dash para na transição | `patches/Optional/Dashing Stops On Screen Transition.ips` (1 byte em `$010B13`) | Desfaz a continuação do dash entre telas | `TurnWhileDashing` (não separa) | A | O port tem uma opção só; se quisermos paridade exata, falta separar “continuar entre telas”. |
| O3 | FMV do MSU | `code/optional/enable_fmv.asm:1-671`; IPS de ~1,8 KB | Habilita a faixa 35/FMV no MSU | `src/audio.c` toca MSU, mas não vídeo FMV | E | Não há pipeline de vídeo no port; é subsistema novo, não um gancho pequeno. |
| O4 | Agahnim verde | `code/gfx/palettes/GreenAgahnim.asm:6-10`; IPS | Volta a paleta verde do Agahnim | asset de paleta `kPalette_SpriteAux1` | B | Precisa de uma ROM variante ou aplicar o IPS; as ROMs fornecidas estão na paleta Redux. |
| O5 | Agahnim verde + subtítulo | `code/gfx/palettes/AgahnimSubtitle.asm:6-10`; IPS | Verde + tilemap do subtítulo | assets de paleta/título | B | Mesma observação de O4/O15. |
| O6 | Max bombas/flechas 99 | `code/optional/max_bomb-arrow_amounts.asm:5-16`; IPS | Bombas 20..99, flechas 30..99 | ver #25 | D | Mesma implementação de #25; é só uma variante de dados. |
| O7 | Mirror nos dois mundos | `code/optional/mirror_both_worlds.asm:7-9`; IPS | Permite usar o espelho no Light World | `MirrorToDarkworld` (`src/player.c:2944`) | A | Equivalente. |
| O8 | Mover blocos infinitamente | `code/optional/move_blocks_infinitely.asm:8-52`; IPS | Move o mesmo bloco várias vezes e bloqueia hookshot através dele | `src/dungeon.c:5490-5510` (`dung_flag_movable_block_was_pushed`) | D | Falta opção/estado; o port tem a flag de “bloco já empurrado”. |
| O9 | Bombas/flechas originais | `code/optional/original_bomb-arrow_amounts.asm:5-16`; IPS | Volta 10..50 e 30..70 | `src/hud.c:26-27` já é o original | A | É o padrão do port; não fazer nada. |
| O10 | Cabelo rosa original | `code/optional/original_pink_hair.asm:5-19`; IPS | Volta o cabelo rosa | paleta padrão do port é a original (rosa) | A | O padrão do zelda3 já é o original; o Redux é que muda para loiro. |
| O11 | Pikit não rouba escudo | `code/optional/pikit_cannot_steal_shields.asm:7-16`; IPS | Nunca deixa o Pikit levar escudo 1/2 | `src/sprite_main.c:19900-19925` (roubo de escudo em `Sprite_AA_Pikit`) | D | O port tem o roubo original; falta opção/filtro. |
| O12 | Remover timer do HUD | `patches/Optional/Remove HUD Timer.ips` | Remove o timer do menu de 24 itens | não existe timer no port | E | Só faz sentido depois de implementar o timer (E). |
| O13 | Remover beep de vida baixa | `code/optional/remove_low_health_beep.asm:5-6`; IPS | Silencia o beep | `DisableLowHealthBeep` (`src/hud.c:508`) | A | Equivalente. |
| O14 | Começar com corações cheios | `code/optional/full_hearts.asm:8-11`; IPS | Tabela `kHealthAfterDeath` passa a dar vida cheia ao carregar | `src/messaging.c:204-207` (`kHealthAfterDeath`) | C | Um script em `SramLoad` pode fazer `zelda.link.health = zelda.link.max_health`; para paridade exata do Redux, a tabela pode virar override B. |
| O15 | Subtítulo “Triforce of the Gods” | `patches/Optional/Triforce of the Gods Subtitle.ips`; `code/gfx/palettes/Subtitle.asm:6-12` | Muda o tilemap e a paleta do título | assets de título | B | Precisa de ROM variante/IPS; não está nas ROMs fornecidas. |
| O16 | HUD original | `patches/Original HUD/*.ips`, `patches/Backups/*.ips` | Volta os gráficos/tilemaps do HUD original | assets `kSprGfx`/`kBgGfx` | B | É uma variante de dados; útil para um mod “HUD original”. |
| O17 | Atalho de Kakariko | `code/optional/kakariko_shortcut.asm:9-144`; `map_data.bin`/`map_data2.bin`/`object_data.bin` | Adiciona o atalho com pedra levantável perto da casa do ferreiro | conteúdo de sala/overworld; a mecânica de pedra já existe | B | Só existe no fonte (não há IPS em `patches/Optional`); precisa de importação de conteúdo. |

## 5. O que já é importável de verdade

### 5.1 `import-rom` nos dois ROMs

Rodei, em pasta temporária fora de `mods/`:

```sh
python3 tools/modkit.py import-rom redux.sfc .agents/tmp-redux-import/redux
python3 tools/modkit.py import-rom redux-retrans.sfc .agents/tmp-redux-import/retrans
```

**Importante:** os ROMs montados do commit `37c3098` têm SHA-1
`708e1d56…` (Redux) e `25d56b08…` (Retranslation) e **não estão** na
tabela `ZELDA3_SHA1` de `assets/util.py:16-30` (que só conhece duas
releases antigas do Redux). Para rodar a prova, adicionei esses dois
SHA-1 temporariamente em `assets/util.py` e reverti com
`git checkout -- assets/util.py`; o repositório final não tem essa
mudança. Sem ela, o `import-rom` falha com “ROM with hash … not
supported”. Isso vale também para `restool --extract-dialogue`.

Saída resumida do Redux (a Retranslation é igual, mudando só o tamanho
dos créditos: 1965 em vez de 1984 bytes):

```text
modkit: 15 asset(s) differ from the US base:
  import [interface] kSprGfx                       133479 ->  132091 bytes
  import [interface] kBgGfx                        119899 ->  118526 bytes
  skip   [other    ] kMovableBlockDataInit            396 ->     396 bytes
  skip   [other    ] kTorchDataInit                   288 ->     288 bytes
  skip   [other    ] kGeneratedWishPondItem           256 ->     256 bytes
  skip   [other    ] kGeneratedBombosArr              256 ->     256 bytes
  skip   [text     ] kEnding_Credits_Text            1989 ->    1984 bytes
  skip   [text     ] kEnding_Credits_Offs             788 ->     788 bytes
  skip   [other    ] kEnding0_Offs                     34 ->      34 bytes
  skip   [other    ] kEnding0_Data                    917 ->     917 bytes
  skip   [other    ] kPalette_DungBgMain             3600 ->    3600 bytes
  skip   [other    ] kPalette_ArmorAndGloves          150 ->     150 bytes
  skip   [other    ] kPalette_SpriteAux1              336 ->     336 bytes
  skip   [other    ] kOverworld_Hibytes_Comp        25696 ->   24791 bytes
  skip   [other    ] kOverworld_Lobytes_Comp        35667 ->   35404 bytes
modkit: 2 asset(s) imported -> build/
modkit: 7 table(s) differ from the US base:
  import [table    ] kHudEquipmentDungeonItemText      32 bytes at $8ded09
  import [table    ] kHudItemText                     640 bytes at $8df1c9
  import [table    ] kHudBottlesItemText              256 bytes at $8df449
  import [table    ] kHudMushroomItemText              32 bytes at $8df549
  import [table    ] kHudFluteItemText                 64 bytes at $8df569
  import [table    ] kHudMirrorItemText                32 bytes at $8df5a9
  import [table    ] kHudBowItemText                   96 bytes at $8df5c9
modkit: kSprGfx: 9 pack(s) changed: 26, 66, 67, 81, 85, 93, 105, 106, 107
modkit: kBgGfx: 3 pack(s) changed: 25, 113, 114
```

Pedindo todos os assets explícitos (`--assets kSprGfx,kBgGfx,…`), os 15
assets são importados, inclusive paletas, créditos e os dois
`kOverworld_*_Comp`; os que mudam de tamanho usam `allow_resize`. Isso
não foi jogado, só gerado.

**O que o padrão importa:** `kSprGfx`, `kBgGfx` e as 7 tabelas de HUD.
**O que fica de fora por padrão:** paletas, créditos, `kOverworld_*_Comp`
e as tabelas de dados gerados.

### 5.2 Por que importar só os gráficos quebra o menu

O `import-rom` padrão aplica `kSprGfx`/`kBgGfx` do menu de 24 itens, mas
o C continua no layout de 20 itens (`kNewStyleInventory = 0`). Tela real,
com o mod temporário e o save “Chapter 10”:

- `.agents/shots/menu-ch10-vanilla.png` — menu original, “FIREROD” correto.
- `.agents/shots/menu-ch10-redux.png` — mesmos gráficos do Redux no layout
  de 20 itens: nomes deslocados (“FIRE ROD”) e ícones fora de lugar.

Ou seja, **gráficos e tabelas do menu Redux são um pacote fechado com a
classe E**. Não dá para importá-los sozinhos num mod “só dados”.

### 5.3 Salas e mapas

O `restool.py --extract-from-rom` do repositório **só aceita a ROM US**
(`assets/restool.py:34`, `assets/util.py:31-89`). Com o SHA-1 do Redux
adicionado temporariamente e forçando `load_rom(..., True)`, a extração
de salas/overworld foi até o fim (35 salas e 4 áreas diferem), mas
quebrou em `decode_sprite_sheets()` porque `assets/sprite_sheets.py` usa
as tabelas de ponteiros comprimidos da ROM US, e o Redux/FastROM tem
outras. O `import-rom` já sabe ler essas tabelas de ponteiros
(`tools/modkit.py:302-345`), então o conserto é reutilizar essa leitura
na extração de sprites.

O que dá para afirmar:

- As salas e áreas são legíveis no Redux nos endereços US; a extração
  produziu YAML plausível. As diferenças incluem Ice Palace (158/190),
  Santuário (18), Skull Woods (73), Ganon's Tower (176), além de 30
  outras salas e as áreas 3, 5, 27 e 91.
- **Não** dá para importar `kDungeonRoom*` pelo `import-rom`: esses
  assets são compilados de `dungeon/*.yaml` (`assets/compile_resources.py:
  675-703`), e o `import-rom` copia o YAML base da US para o workspace;
  ele nunca extrai as salas do ROM traduzido. O mesmo vale para
  `kEntranceData_*`/`kStartingPoint_*`.
- O que faltaria no modkit: um comando `import-content` (ou equivalente)
  que (1) rode a extração de salas/overworld no ROM do Redux com as
  tabelas de ponteiros dele, (2) compile o YAML resultante com
  `compile_resources`, (3) compare com a base US e grave os assets
  `kDungeonRoom*`, `kEntranceData_*`, `kOverworld_*` etc. como
  overrides. Hoje o modkit só compara assets que ele mesmo compilou da
  base US.
- `kOverworld_Hibytes_Comp`/`Lobytes_Comp` são lidos direto do ROM
  traduzido (`compile_resources.py:192-204`) e por isso aparecem como
  diferentes; eles podem ser importados como asset, mas sozinhos não
  trazem os objetos/entradas do YAML e não foram validados em jogo.

### 5.4 Texto, fonte e créditos

- `restool.py --extract-dialogue -r redux.sfc` funciona (com o SHA-1
  reconhecido): gera `dialogue_redux.txt` e `font_redux.png`. Testei
  também no `redux-retrans.sfc`; o diálogo muda e a fonte é gerada.
- O `import-rom` agora extrai e importa `kDialogue`/`kDialogueFont` de
  Redux e Retranslation no slot `us`, preservando os outros slots de idioma
  do `zelda3_assets.dat` em uso; não é preciso `--languages redux` só pelo
  texto em inglês. Comandos de texto desconhecidos são listados e a
  importação do diálogo é pulada, sem inventar valores.
- Créditos: `import-rom --assets kEnding_Credits_Text,kEnding_Credits_Offs`
  importa os dois assets; as tabelas pequenas de fonte/mapeamento dos
  créditos continuam em C (`src/ending.c`), e não há prova visual do
  resultado.

## 6. Ordem sugerida para um mod “redux” local

O mod deve ser gerado localmente (como `mods/ptbr-graficos`), com
`build/` fora do git. A ordem abaixo evita o menu quebrado da seção 5.2.

1. **`redux-opcoes` (não é mod; é preset de `zelda3.ini`)** — ligar o que
   já existe: `TurnWhileDashing=1`, `CollectItemsWithSword=1`,
   `BreakPotsWithSword=1` (ciente da diferença do L1),
   `MirrorToDarkworld=1`, `DisableLowHealthBeep=1`,
   `SkipIntroOnKeypress=1`, `MiscBugFixes=1`, `DimFlashes=1`,
   `ItemSwitchLR=1`, `CancelBirdTravel=1` e MSU se for o caso. Hoje um
   mod não pode ligar `[Features]`; isso precisaria de um campo novo no
   manifesto ou de um preset separado.
2. **`redux-dados-seguros` (B)** — tabelas de texto do HUD só depois do
   menu de 24 itens, ou com remapeamento; paletas
   (`kPalette_ArmorAndGloves`, `kPalette_SpriteAux1`), créditos e, se
   validado, `kOverworld_*_Comp`. Sem `kSprGfx`/`kBgGfx` do menu.
3. **`redux-menu24` (E)** — implementar o toggle de runtime do
   `kNewStyleInventory`, o magic meter horizontal, o timer e o layout de
   24 slots; só então importar `kSprGfx`/`kBgGfx` e as 7 tabelas de HUD
   do Redux. Esse é o pacote que faz o menu ficar igual ao Redux.
4. **`redux-mapa` (B)** — depois do `import-content` do modkit, importar
   as 35 salas e 4 áreas; revisar as mudanças de Ice Palace, Santuário,
   Skull Woods, Ganon's Tower, Death Mountain e o primeiro warp.
5. **`redux-gameplay` (D/E)** — sword beam, fairy 64, drop do pingente 3,
   túnica azul/vermelha, max consumables, pá que desenterra, direção do
   golpe, Pikit, blocos infinitos. Cada um é independente; os de menor
   risco são os que já têm tabela/flag.
6. **Variantes** — Agahnim verde, subtítulo, HUD original, cabelo rosa,
   full hearts, FMV. As de gráfico são B; full hearts é C; FMV é E e
   provavelmente fica fora até existir vídeo no port.

## 7. Ganchos novos (classe D)

Todos os pontos abaixo são candidatos a evento/filtro/opção da F4e; a
maioria é pequena, mas nenhum existe hoje.

| Gancho | Onde | O que precisa |
|---|---|---|
| Dano do sword beam | `src/ancilla.c:501-514` (`kAncilla_Damage`) | tornar a tabela substituível (por asset/table override) ou flag “beam = L2”. |
| Max consumables | `src/hud.c:26-27`, `src/sprite_main.c:11480/11517`, checagens em `hud.c:489-495`, `sprite_main.c:24268/25254/25273` | opção `ReduxConsumables` ou registry para as quatro tabelas; cuidado com lojas e display. |
| Fairy 64 | `src/messaging.c:862-866` | opção/constante de quanto a fairy ressuscita. |
| Drop do pingente 3 | `src/ancilla.c:5975-6030` | flag para ignorar o caso especial `cur_palace_index_x2 == 20`. |
| Túnica azul/vermelha | `src/player.c:3816-3837` (`kReceiveItemAlternates`) + `kDungeonRoomChests` | mover `kReceiveItemAlternates` para `game_tables.def` (`MOD_TABLE_BYTES`) e/ou permitir patch do baú. |
| Direção durante o charge | `src/player.c:2176` (`Link_CheckForSwordSwing`) e `HandleSwordControls` | evento/filtro de input ou opção `SwordTurnWhileCharging` que atualize `link_direction_facing` antes de `CalculateSwordHitBox`. |
| Primeiro warp | `src/messaging.c:153-156` (`kBirdTravel_*`) | expor as tabelas como asset/table override. |
| Blocos infinitos | `src/dungeon.c:5490-5510` (`dung_flag_movable_block_was_pushed`) e `src/player.c:4320`/`:4967` | opção/estado por sala para não marcar o bloco como “já movido”; bloquear hookshot durante o movimento. |
| Pikit sem roubo | `src/sprite_main.c:19900-19925` (`Sprite_AA_Pikit`) | opção/filtro `PikitSteal` para pular o ramo do escudo. |

O opcional “Dashing Stops On Screen Transition” (O2) é uma nuance de A:
o port tem `TurnWhileDashing`, mas não separa “virar” de “continuar entre
telas”. Se quisermos os dois opcionais do Redux, entra um gancho pequeno
em `src/dungeon.c:6780`/`src/overworld.c:566`; não é necessário para o
comportamento padrão.

## 8. O que a classe E pede da F4e

A F4e precisa oferecer, no mínimo:

1. **Layout de menu em runtime**, não `enum` de compile-time. O port já
   tem `kNewStyleInventory` e `kHudItemInVramPtr_New[24]`
   (`src/hud.c:8-47`), mas o valor é fixo. Um mod precisa declarar
   “menu de 24 slots” e o engine escolher os caminhos de 6×4, garrafas
   separadas, pá permanente e nomes.
2. **HUD/magic meter/timer**: o Redux troca o medidor vertical por um
   horizontal, pinta o medidor de azul com meia magia e desenha o timer
   no menu. O port só tem `Hud_Update_Magic()` vertical
   (`src/hud.c:1424-1439`). F4c (`FrameDraw`) resolve um overlay, mas
   não o tilemap do menu.
3. **Definição de item/slot**: id, nome, ícone, máximo, efeito de uso,
   slot de garrafa e ordem. Hoje `kHudItemText`/`kHudBottlesItemText`
   são tabelas C (`src/hud.c:208-320`) e os ids são fixos.
4. **Eventos/filtros de gameplay**: `on_dig` (pá), `on_block_push`,
   `on_menu_draw`/`on_menu_input`, `on_item_use`, `on_damage_calc` e
   `on_pikit_steal`. `ItemReceive`, `ChestOpen`, `SpriteKilled`,
   `LinkDeath`, `BossDefeated`, `SramLoad/Save` e `FrameDraw` já existem
   (`src/mod/events.h:7-24`).
5. **Estado por save**: timer e ordem do inventário precisam de campos
   próprios (o Redux usa RAM de save; o port usa `g_ram`/SRAM). Um mod
   não deve escrever em offsets crus sem `state_write`.
6. **Pacote de assets casado**: o menu de 24 itens só funciona com os
   gráficos/tilemaps do Redux (`kSprGfx`/`kBgGfx` e packs específicos).
   O manifesto do mod precisa poder exigir “este layout + estes assets”
   como uma unidade, senão o resultado é o menu quebrado da seção 5.2.

## 9. O que não consegui confirmar

- **Water Gate (#16)**: o `org $01CBAC` do Redux é no-op na ROM US
  fornecida; o comentário “Originally LDA #$000C” não bate com os bytes.
  Pode ser que o Redux tenha sido montado sobre uma ROM já modificada, ou
  o comentário esteja errado. Não inventei o comportamento.
- **Cane of Somaria (#6, #20)**: a correspondência entre `$07AED4` e
  `Ancilla_TerminateSelectInteractives`/`dung_flag_trapdoors_down` é
  provável, mas não consegui provar que o port cobre exatamente o mesmo
  caso (saída de dungeon com bloco).
- **Whirl (#18), Moldorm (#21)**: o port usa outro caminho de áudio/SFX;
  não reproduzi os bugs nem confirmei que o comportamento é idêntico.
- **King of Hyrule (#38)**: o byte `$0E9E90` não apareceu no
  `import-rom` padrão; não sei se está em `kPalette_MiscSprite_Indoors`,
  em outra paleta ou em uma tabela de índice de sprite.
- **Salas/mapas (seção 5.3)**: a extração do Redux usou os endereços US
  e quebrou na parte de sprites; as 35 salas/4 áreas são plausíveis, mas
  algumas diferenças (ex.: `house_exit_door` da sala 260 e duplicação de
  `Chests`) podem ser artefato da extração, não mudança intencional.
- **Menu de 24 itens (E)**: não compilei o port com
  `kNewStyleInventory = 1`; a conclusão de que “boa parte já existe” vem
  da leitura de `src/hud.c`. Não há prova visual do menu 24 com os
  assets do Redux.
- **`import-rom` com todos os assets**: os 15 overrides foram gerados,
  mas não foram jogados; não sei se a combinação de
  `kOverworld_*_Comp` + paletas + créditos é consistente em jogo.
- **Variantes opcionais**: não há ROM com Agahnim verde, subtítulo, HUD
  original ou FMV para testar; a análise é do `.asm`/IPS.
- **Retranslation**: extraí diálogo/fonte, mas não montei um `.dat` com
  `--languages redux`; a parte de créditos/fonte pequena pode precisar
  de ajuste.

## 10. Prova

### 10.1 Comandos

```sh
# import-rom (com os SHA-1 locais adicionados temporariamente em assets/util.py)
python3 tools/modkit.py import-rom redux.sfc .agents/tmp-redux-import/redux
python3 tools/modkit.py import-rom redux-retrans.sfc .agents/tmp-redux-import/retrans

# todos os assets explicitamente
python3 tools/modkit.py import-rom redux.sfc .agents/tmp-redux-import/redux-all \
  --assets kSprGfx,kBgGfx,kBgTilemap_0,kBgTilemap_1,kBgTilemap_2,kBgTilemap_3,kBgTilemap_4,kBgTilemap_5,kOverworldMapGfx,kEnding_Credits_Text,kEnding_Credits_Offs,kPalette_DungBgMain,kPalette_ArmorAndGloves,kPalette_SpriteAux1,kOverworld_Hibytes_Comp,kOverworld_Lobytes_Comp,kMovableBlockDataInit,kTorchDataInit,kGeneratedWishPondItem,kGeneratedBombosArr,kEnding0_Offs,kEnding0_Data

# extração de diálogo/fonte (workspace temporário, para não escrever em assets/)
python3 assets/restool.py --extract-dialogue -r redux.sfc
python3 assets/restool.py --extract-dialogue -r redux-retrans.sfc
```

### 10.2 Screenshots

PNGs em `.agents/shots/` (fora do git):

- `title-redux-1200.png` — tela de título com o mod importado temporário.
- `menu-ch10-vanilla.png` — menu original (save Chapter 10).
- `menu-ch10-redux.png` — o mesmo menu com `kSprGfx`/`kBgGfx` e as
  tabelas do Redux: prova visual de que o layout de 20 itens não aceita
  os assets do menu de 24.

O mod temporário usado foi
`.agents/tmp-mods-redux/redux-import/` (gerado pelo `import-rom`), com
`mod.json` apontando para `build/`; não foi versionado.

## 11. Conclusão curta

O Redux não é “um mod de dados” do zelda3. Cerca de um terço das
mudanças já é opção do port; outro terço é dados que o modkit consegue
importar (gráficos, paletas, tabelas de HUD, créditos) ou quase
consegue (salas/mapas, diálogo). O núcleo que falta é o **menu/HUD de 24
itens com timer** (E) e alguns **pontos de comportamento** (D): dano do
beam, max consumables, fairy, drop do pingente, túnica, direção do
golpe, warp, blocos e Pikit. A ordem de menor risco é: opções do ini →
dados seguros → menu 24 → mapas → gameplay novo.
