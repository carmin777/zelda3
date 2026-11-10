# A Link to the Islands v2.5 — mapa da ROM e o que o zelda3 precisa

Análise da ROM `altti.sfc` ("The Legend of Zelda: A Link to the Islands" v2.5,
Darklink7884/SePH, hack 5636 do romhacking.net) contra a ROM US do zelda3.
Este documento não contém a ROM nem dados extraídos dela; só endereços,
nomes de assets e descrições.

ROM analisada:

| | US (`zelda3.sfc`) | Hack (`altti.sfc`) |
|---|---|---|
| tamanho | 1.048.576 bytes (0x100000) | 3.145.728 bytes (0x300000) |
| SHA-1 | `6D4F10A8B10E10DBE624CB23CF03B88BB8252973` | `51BA60F13EECE49BE035B30494AE8C37D1A22ADD` |
| mapeamento | LoROM | LoROM |

Convenção de endereço usada aqui: endereço SNES `$BB:AAAA` vira offset de
arquivo `((BB & 0x7f) * 0x8000) + (AAAA & 0x7fff)`. O primeiro 1 MB são os
bancos `$00-$1F`; os 2 MB novos são os bancos `$20-$5F` (com espelhos em
`$A0-$DF`). Os comentários `// 8xxxxx` do C usam o espelho `$80+`; por isso
`$82:AA7D` e `$02:AA7D` são o mesmo lugar físico.

> **Correção (I1, 09/11/26).** Os "ponteiros na metade baixa do banco"
> (`$0A:2D14`…) eram **leitura da tabela errada**: em LoROM, `$2000-$5FFF`
> dos bancos `$00-$3F` é I/O, não ROM. O hack moveu a tabela de tiles do
> mapa de masmorra de `$0A:FBE4` para `$0A:FBB2`, e o import lia o
> endereço fixo da US. O assert `(ea & 0x8000)` está certo e voltou. **As
> afirmações sobre sprites e segredos de masmorra "na metade baixa do banco
> `$09`" e o volume de 83,5 KB podem ter a mesma causa** (tabela de
> ponteiros movida, lida no endereço US) e precisam ser reverificadas
> lendo o operando do código antes do passo 7.
>
> **Correção (I2, 09/11/26).** Reverificado: a tabela de sprites de dungeon do
> Islands está em `$09:D2E1` (a instrução `LDA $D2E1,Y` em `$89:C297`), não em
> `$09:D62E`. Com a tabela certa são **3.988 bytes únicos** (221 listas), o que
> cabe folgado em `kDungeonSpriteOffs` (`uint16`). Os 83,5 KB e os ponteiros na
> “metade baixa do banco” eram lixo da tabela antiga. O `import-rom` agora
> extrai e importa as salas, cabeçalhos, baús, sprites, segredos, pits,
> entradas e starting points.
>
> **Correção (I3, 09/11/26).** O overworld também é extraído do hack e
> importado. As tabelas de áreas, gfx/paleta, música, placas, tamanho de mapa,
> hi/lo comprimidos, map32→map16, map16→map8, segredos, exits, viagem de
> pássaro/redemoinho, entradas, buracos e sprites foram localizadas pelo
> operando da instrução original (`assets/overworld_tables.py`). Na US, no
> Islands e no Redux todas continuam nos mesmos endereços da US, exceto a
> instrução de `$82:AA7D`, que o Islands troca por `JSL $24:8000`; o extrator
> segue o gancho e lê `$82:A5EC` na rotina nova. A tabela de áreas do Islands
> muda em 8 posições e cria as áreas-cabeça 25, 28, 32, 35 e 36 (e as
> equivalentes escuras); ela virou a tabela de mod `kOverworldAreaHeads`.
>
> Segredos: o tipo `$A6` da área 23 e o `$E6` da área 32 são reais e foram
> preservados como tipos especiais crus; as áreas-cabeça 117, 119, 122, 123,
> 124 e 127 têm posições ímpares/fora da faixa e ficam sem itens, com
> relatório. O Islands tem 21 exits com screen `$FF`, um buraco com área `$FF`
> e uma entrada com área `$FFFF`; esses slots usam sentinelas. Nada estourou o
> formato fixo do C.
>
> Início: o “jogo novo” do port continua na casa do Link (sala 260). No
> Islands o exit importado da sala 260 aponta para a **área 16**, não para a
> 10; o `kStartingPoint_*` do hack é importado mas não é consultado por esse
> caminho. O atalho opcional de Kakariko do Redux **não está** na ROM
> `redux.sfc` (a área 34 e vizinhas não mudam no YAML), então não há o que
> importar.

## 1. Mapa da ROM

### 1.1 Primeiro 1 MB

Percentual de bytes diferentes por banco de 32 KB (comparação byte a byte):

| banco | arquivo | dif. | o que é |
|---|---|---|---|
| `$00` | `0x000000-0x007FFF` | 2,5% | código de reset/NMI/IRQ, tabelas de ponteiros de gráficos (`$00:CF80/$D05F/$D13E`, `$00:CFF3/$D0D2/$D1B1`), tabelas de gfx/paleta do overworld (`$00:FC9C/$00:FD1C`), hooks de MSU em `$80:80D9/$80:80F3/$80:740F/$80:79C3` |
| `$01` | `0x008000-0x00FFFF` | 7,6% | tabela de baús `$01:E96E`, dados/ponteiros de sprites de dungeon (`$01:DB69`, `$01:E32D-$E6AE`), hooks de código (ex.: `$81:DABD`, `$81:DAFC`) |
| `$02` | `0x010000-0x017FFF` | 11,3% | tabelas de entradas/saídas (`$02:C813`, `$02:DD8A`), cabeçalhos de área do overworld (`$02:A5EC`), ponteiros comprimidos do overworld (`$02:F94D/$02:FB2D`), hooks e código novo em `$02:C4C0/$02:C500` |
| `$03` | `0x018000-0x01FFFF` | 21,1% | tabelas map32→map16 (`$03:8000`, `$03:B400`), dados/código auxiliares |
| `$04` | `0x020000-0x027FFF` | 12,4% | tabela antiga de cabeçalhos de sala `$04:F502` (ficou obsoleta), tabelas map32→map16 (`$04:8000`, `$04:B400`), código novo em `$04:EC20-$04:EC70` |
| `$05` | `0x028000-0x02FFFF` | 1,7% | pequenos patches de código/dados (progresso/story) |
| `$06` | `0x030000-0x037FFF` | 1,6% | pequenos patches |
| `$07` | `0x038000-0x03FFFF` | 1,8% | código novo em `$07:F8D0/$07:F918/$07:FF20` (item switch/cancelar voo), patch do espelho em `$07:A951` |
| `$08` | `0x040000-0x047FFF` | 0,5% | patches pequenos (ex.: flechas de prata em `0x44353`) |
| `$09` | `0x048000-0x04FFFF` | 27,6% | sprites de dungeon (`$09:D62E`), ponteiros de sprites do overworld (`$09:C881/$09:C901/$09:CA21`), código/hooks |
| `$0A` | `0x050000-0x057FFF` | 57,3% | mapas de dungeon (`$0A:F605/$0A:FBE4`), blocos de objetos de sala (10 salas), dados antigos do overworld |
| `$0B` | `0x058000-0x05FFFF` | 96,8% | **todos** os 160 pares hi/lo comprimidos do overworld (cada um descomprime para 256 bytes) |
| `$0C` | `0x060000-0x067FFF` | 58,6% | blocos de objetos de sala (131 salas), tilemaps de interface 1-4 realocados |
| `$0D` | `0x068000-0x06FFFF` | 0,4% | patches pequenos |
| `$0E` | `0x070000-0x077FFF` | 16,9% | créditos/final (`$0E:B038/$0E:B178/$0E:B93D/$0E:BF4C`), segunda parte do diálogo (`$0E:DF40`), código novo em `$0E:FB70/$0E:FCA1/$0E:FF00` |
| `$0F` | `0x078000-0x07FFFF` | 3,6% | tabelas de atributos, patches de sala/barreira |
| `$10-$18` | `0x080000-0x0C7FFF` | 48-98% | packs comprimidos de sprite/background (`kSprGfx`/`kBgGfx`) recompactados; os ponteiros continuam nos bancos `$10-$18` |
| `$19` | `0x0C8000-0x0CFFFF` | 0% | inalterado |
| `$1A` | `0x0D0000-0x0D7FFF` | 63,4% | código de sprites (`$9A:xxxx`), alterado pelos sprites novos |
| `$1B` | `0x0D8000-0x0DFFFF` | 39,5% | paletas (`$1B:D734`, `$1B:D218`, `$1B:D308`, `$1B:D630`, `$1B:D648`, `$1B:D660`, `$1B:E6C8`...), tabela de segredos do overworld (`$1B:C2F9`) |
| `$1C` | `0x0E0000-0x0E7FFF` | 95,7% | diálogo principal (`kDialogue`, `$9C:8000`) |
| `$1D` | `0x0E8000-0x0EFFFF` | 1,3% | patches pequenos de final/código |
| `$1E` | `0x0F0000-0x0F7FFF` | 1,7% | patches pequenos de final/código |
| `$1F` | `0x0F8000-0x0FFFFF` | 98,3% | tabela de ponteiros de objetos de sala `$1F:8000` e blocos de objetos de 179 salas |

Observação importante: boa parte das diferenças pequenas em bancos de código é
só a troca de banco `$0x` por `$8x` (`JSL $00:E914` virou `JSL $80:E914`, por
exemplo). Isso não muda comportamento; são o mesmo banco físico.

### 1.2 Os 2 MB novos

| arquivo | banco SNES | conteúdo |
|---|---|---|
| `0x108000` | `$A1:8000` | rotina de boot: liga FastROM (`$420D`), desliga a tela e volta para `$80:801B` |
| `0x116000-0x117FFF` | `$22:E900-$22:F0FF` | código de seleção de faixa do MSU-1; cada rotina testa o status do MSU (`$2002 == $53`) e escreve `$2002-$2007` |
| `0x118000-0x11FFFF` | `$23:8000-$23:FFFF` | bloco grande de código novo: init/rotinas do item switch L/R e do autofill de magic (`$23:8000`), e handler de NMI/IRQ em `$23:F000-$23:F6FF` (MSU/código novo) |
| `0x120000-0x1200FF` | `$24:8000-$24:80FF` | quebra-cabeça da praia (área `$3F`) e tabela de combinação em `$24:8200` |
| `0x140000-0x1405FF` | `$28:8000` | dados novos (≈1,3 KB) usados pelo código novo; não correspondem a nenhum asset do `restool`; provavelmente tabelas de cutscene/final. Não identificado com certeza |
| `0x150000-0x151FFF` | `$2A:8000-$2A:9FFF` | tabela de offsets dos cabeçalhos de sala (`$2A:8000`, 296 entradas) e os cabeçalhos de 14 bytes a partir de `$2A:8250`; o resto do banco é o fim dos cabeçalhos/folga |

O resto dos bancos `$20-$5F` é preenchimento (`$00`/`$FF`), exceto pequenas
sobras. Não há gráficos comprimidos nos bancos novos: os ponteiros de
`Decomp_spr`/`Decomp_bg` continuam apontando para `$10-$18`.

## 2. Dados e formatos

A tabela abaixo usa os nomes de `src/mod/asset_names.c`. "Formato igual" quer
dizer que o layout de bytes é o mesmo da ROM US; "importável hoje" é o que o
`tools/modkit.py import-rom` atual consegue fazer sem mudanças.

| asset(s) | onde está no hack | formato | importável hoje? |
|---|---|---|---|
| `kDungeonRoom`, `kDungeonRoomOffs`, `kDungeonRoomDoorOffs` | tabela de ponteiros de 3 bytes em `$1F:8000`; alvos nos bancos `$0A` (10 salas), `$0C` (131), `$1F` (179) | igual: floor/layout + 3 camadas de objetos + portas; as 320 entradas terminam em `$FFFF` | **não** pelo `import-rom` (ele usa o YAML US); o dado em si é parseável e cabe em `uint16` (≈43 KB concatenados) |
| `kDungeonRoomHeaders`, `kDungeonRoomHeadersOffs` | tabela de offsets em `$2A:8000` (296 entradas `uint16`); cabeçalhos de 14 bytes em `$2A:8250` | registros de 14 bytes iguais aos US, mas a tabela tem 296 entradas e mora no banco `$2A`; a tabela antiga `$04:F502` continua na ROM mas está obsoleta | **não**; precisa de extrator próprio e de padding para 320 entradas |
| `kDungeonRoomChests` | `$01:E96E`, 504 bytes | igual: `uint16 room` (bit `0x8000` = baú grande) + `uint8 item` | **não** pelo fluxo atual; o dado é lido direto e é trivial de importar |
| `kDungeonSprites`, `kDungeonSpriteOffs` | tabela de ponteiros em `$09:D2E1` (lida por `LDA $D2E1,Y` em `$89:C297`); ponteiro 0 = sala sem sprites | igual no layout (sort byte + triplas + `0xFF`); o volume único é **3.988 bytes** (221 listas) | **sim** (I2): cabe em `kDungeonSpriteOffs` (`uint16`); a leitura antiga em `$09:D62E` dava lixo e os 83,5 KB |
| `kDungeonSecrets` | tabela de ponteiros em `$01:DB69`; dados no banco `$01`; ponteiro 0 = sala sem segredos | igual (triplas + `$FFFF`); 730 bytes únicos | **não** pelo fluxo atual; parseável e cabe |
| `kEntranceData_*`, `kStartingPoint_*` | tabelas fixas em `$02:C813`, `$02:CD45`, `$02:CE4F`, `$02:CF59`, `$02:D063`, `$02:D16D`, `$02:D277`, `$02:D381`, `$02:D406`, `$02:D48B`, `$02:D510`, `$02:D595`, `$02:D61A`, `$02:D69F`, `$02:D724`, `$02:D82E`; starting points em `$02:DB6E` etc. | igual (133 entradas de 8 bytes em `relativeCoords`, etc.), mas há slots não usados com valores `0xFF` e IDs duplicados | **não** pelo fluxo atual; parseável com validação/sanitização |
| `kOverworld_Hibytes_Comp`, `kOverworld_Lobytes_Comp` | ponteiros de 24 bits em `$02:F94D` e `$02:FB2D`; todos os 160 pares apontam para o banco `$0B` | igual: cada pack descomprime para **256 bytes**; 160 hi + 160 lo | **sim** (I3): a extração lê a tabela pelo operando do código e compila o YAML do hack |
| `kOverworldMapIsSmall`, `kOverworldAuxTileThemeIndexes`, `kOverworldBgPalettes`, `kOverworld_SignText`, `kOwMusicSets`, `kOwMusicSets2` | `$00:FC9C`, `$00:FD1C`, `$07:F51D`, `$02:C303`, `$02:C403` | igual; valores alterados | **sim** (I3): tabelas localizadas pelo código |
| `kBirdTravel_*`, `kWhirlpoolAreas`, `kOverworld_Entrance_*`, `kFallHole_*`, `kExitData_*`, `kSpExit_*` | `$02:EAE5`, `$1B:B96F`, `$1B:B800`, `$02:DD8A`, `$02:E6E1-$02:E841` | igual; valores/áreas alterados | **sim** (I3), com sanitização de exits/buracos/entradas inválidos |
| `kOverworldSecrets_Offs`, `kOverworldSecrets` | ponteiros em `$1B:C2F9`; dados a partir de `$1B:C3FB` | igual no papel, mas **há dados inválidos**: a área 23 tem um registro com tipo `0xA6`; as áreas 117-127 apontam para fora da tabela e produzem posições ímpares/tipos aleatórios | **sim** (I3): tipos crus preservados; áreas com posição inválida ficam sem itens e são listadas |
| `kOverworldSpriteOffs`, `kOverworldSprites`, `kOverworldSpriteGfx`, `kOverworldSpritePalettes` | ponteiros em `$09:C881` (estágio 0), `$09:C901` (1), `$09:CA21` (2); dados no banco `$09` | igual (triplas + `0xFF`); ~3,2 KB únicos | **sim** (I3): tabelas e dados localizados pelo código; cabe no formato atual |
| `kSprGfx`, `kBgGfx` | tabelas de ponteiros lidas pelo código em `$00:E772`/`$00:E78F`; tabelas paralelas em `$00:CFF3/$D0D2/$D1B1` (sprites, 108) e `$00:CF80/$D05F/$D13E` (fundos, 115); alvos nos bancos `$10-$18` | igual; 96/108 ponteiros de sprite e 114/115 de fundo mudaram; a descompressão funciona (150.579 e 177.664 bytes descomprimidos) | **sim em princípio**: o `import-rom` já lê essas tabelas do código; só esbarra no hash e nos asserts de endereço |
| `kOverworldMapGfx`, `kLightOverworldTilemap`, `kDarkOverworldTilemap`, `kOverworldMapPaletteData` | `$18:C000` (0x4000), `$0A:C727` (4096), `$0A:D727` (1024), `$0A:DB27` (256 words) | igual; o mapa claro e o gfx do mapa mudaram | **sim em princípio** (endereços fixos); bloqueado pelo hash |
| `kPalette_*`, `kHudPalData` | `$1B:D734`, `$1B:D218`, `$1B:D308`, `$1B:D630`, `$1B:D648`, `$1B:D660`, `$1B:E6C8`, `$1B:E86C`, `$1B:E604`, `$1B:E544`, `$1B:D70A` | igual; 34 diferenças em `kPalette_DungBgMain`, 58 em armor/gloves, 14 em shield, etc. | **sim em princípio** (endereços fixos) |
| `kDungMap_FloorLayout`, `kDungMap_Tiles` | ponteiros em `$0A:F605` e `$0A:FBE4`; dados no banco `$0A` | igual; o hack **moveu** a tabela de ponteiros de tiles para `$0A:FBB2` (a rotina em `$8A:E63F` lê `LDA $FBB2,X`); em `$0A:FBE4` há outros dados | **sim** (I1): o import lê o endereço da tabela no operando da instrução |
| `kBgTilemap_0..5` | fontes fixas `$0C:DD6D`, `$0C:E7BF`, `$0C:E2A8`, `$0C:E63C`, `$0C:E456`, `$0E:DA9C` | tilemap 1 e 5 continuam no mesmo lugar; tilemaps 2, 3 e 4 foram movidos dentro do banco `$0C` (para `0x665D4`, `0x6676E`, `0x66685`); tilemap 0 (tela de título) mudou e não foi encontrado pelo conteúdo US | **não**: o `import-rom` lê endereços fixos e importaria lixo para 0/2/3/4; falta localizar as fontes novas (código de NMI ou varredura) |
| `kDialogue`, `kDialogueFont`, `kDialogueMap` | `$9C:8000` e `$8E:DF40` | igual: 397 mensagens, mesma codificação/dicionário US; `--extract-dialogue` funcionou depois de liberar o hash | **sim** (IX): o `import-rom` extrai as 397 mensagens, compila `kDialogue`/`kDialogueFont` e importa o que difere; o texto entra no slot `us` e os demais slots do `.dat` são preservados |
| `kEnding_Credits_Text`, `kEnding_Credits_Offs`, `kEnding_MapData`, `kEnding0_*` | `$0E:B178` (1989 bytes), `$0E:B93D` (394 words), `$0E:B038` (160 words), `$0E:BF4C` (917), `$0E:C2E1` (17 words) | texto/offsets mudaram; o final e as cutscenes foram reescritos | **não recomendado** sem validar o renderer; o `import-rom` lê créditos por padrão de código e pode não entender o formato novo |
| `kLinkGraphics` | vem de `assets/linksprite.png`, não da ROM | o hack usa sprites de Link do Minish Cap | **sim** (IX): o `import-rom` lê o bloco de 0x7000 bytes em `$10:8000` (banco lido do `NMI_DoUpdates`), gera `linksprite.png` e importa `kLinkGraphics`; as paletas do Link entram por `kPalette_ArmorAndGloves`/`kPalette_MainSpr` |
| `kSoundBank_*` | compilado de `assets/sound/`, não da ROM | o hack usa MSU-1 externo | **não** pelo `import-rom`; MSU é config do port |

### 2.1 Observações de formato

- **Salas.** A tabela de 3 bytes em `$1F:8000` continua com 320 entradas e
  aponta para o bloco de objetos (floor/layout + camadas). O código do hack
  **não** usa mais a tabela de cabeçalhos em `$04:F502`: `Dungeon_LoadHeader`
  (`src/dungeon.c:3672`, rotina `$81:B564`) foi remendado para ler
  `$2A:8000[room]` como offset e buscar o cabeçalho em `$2A:xxxx` (banco
  `$2A`, base carregada em `$0F`). A tabela nova tem 296 entradas úteis
  (0..295); a entrada 296 em diante já cai dentro dos cabeçalhos. Para o
  zelda3, que indexa 320 salas, é preciso preencher 296..319 com um
  cabeçalho dummy ou repetir o último.
- **Sprites de dungeon.** O ponteiro 0 significa "sala sem sprites". O dado
  real do Islands está no banco `$09`, mas a tabela de ponteiros foi movida
  para `$09:D2E1`; lida a tabela certa, são **3.988 bytes únicos** (221
  listas), que cabem em `kDungeonSpriteOffs` (`uint16`). O valor de 83.519
  bytes vinha de ler `$09:D62E`, onde o hack deixou outros dados. (Correção do
  I2; a versão original deste bullet está preservada na nota do topo.)
- **Segredos de dungeon.** Mesmo caso de ponteiro 0 e endereços baixos, mas
  o volume é pequeno (730 bytes únicos).
- **Overworld.** Os 160 pares comprimidos estão todos no banco `$0B` e
  descomprimem para 256 bytes cada, exatamente como o C espera. A tabela de
  cabeçalhos de área (`$02:A5EC`) mudou em 8 posições; isso cria novas áreas
  cabeça (por exemplo 25, 28, 32, 35, 36, 89, 92, 96, 99, 100) que não
  existem no YAML US.
- **Segredos do overworld.** A tabela não é confiável nas áreas altas. O
  extrator precisa limitar-se às áreas com ponteiro válido e parar em
  `0xFFFF`, ignorando tipos desconhecidos; senão o build quebra (foi o
  primeiro erro da extração com o `restool`).
- **Entradas.** O formato é o mesmo, mas há slots não usados com quadrante
  `0xFF` e IDs de entrada duplicados na tabela do overworld; o compilador
  atual usa `assert` e quebra. Precisa sanitizar.
- **Texto.** O diálogo do hack continua no formato US e decodifica 397
  mensagens. Os nomes de itens e o HUD continuam nas tabelas de
  `src/mod/game_tables.def`; o `import-rom` lê essas tabelas direto da ROM no
  endereço fixo e compara com a US, então as tabelas que o hack não moveu
  devem funcionar quando o hash for aceito.
- **Gráficos.** As tabelas de ponteiros de `Decomp_spr`/`Decomp_bg` estão
  intactas e o `import-rom` as lê corretamente; os packs continuam nos bancos
  `$10-$18`, só mudaram de lugar dentro deles. Isso é a parte mais adiantada.
- **Tilemaps.** O ponto fraco: o `restool` fixa as fontes em
  `compile_resources.py`, mas o hack moveu 2-4 e trocou a 0. Sem achar as
  fontes novas, `kBgTilemap_2..4` sairiam errados.

## 3. Assembly do hack

Legenda de classe:

- **A** — já existe como opção no `zelda3.ini`, e o comportamento bate.
- **C** — dá para fazer em Lua hoje.
- **D** — precisa de um gancho novo no C.
- **E** — comportamento novo, candidato a F4e.

### 3.1 Tabela de desvios verificados

| desvio | rotina zelda3 | o que o código novo faz | classe |
|---|---|---|---|
| `$80:8016` `JML $A1:8000` | reset, antes de `$80:801B` | liga FastROM, desliga a tela e volta | E/C (infraestrutura de boot; o port já tem o equivalente, não precisa importar) |
| `$80:8034` `JSL $23:8000` | init após ligar NMI | init das variáveis do item switch/autofill de magic; o bloco `$23:8000` também tem rotina que lê `$F6`, `$0202`, `$7EF36E` | A/E (item switch é A; autofill é E) |
| `$80:80D9` `JSL $22:E900`; `$80:80F3` `JSL $22:E950` | NMI/loop de áudio | seleção de faixa MSU-1; testa `$2002 == $53` e escreve `$2002-$2007` | A (`EnableMSU`) |
| `$80:82E2` `JSL $23:F640` | handler de IRQ | wrapper de IRQ/status MSU; em `$23:F680` testa MSU e desvia para `$0C:CF83` | A (`EnableMSU`) |
| `$80:740F` `JSL $22:EA80`; `$80:79C3` `JSL $22:E9F0` | rotinas de áudio/NMI | mais seleção de faixa MSU | A (`EnableMSU`) |
| `$82:AA7D` `JSL $24:8000` | `OverworldHandleTransitions` (`src/overworld.c:837`), antes de `new_area` (linha 870) | quebra-cabeça da área `$3F` (ver 3.2) | D/E |
| `$82:8B13` `BEQ`→`BRA` | `Dungeon_ResetTorchBackgroundAndPlayerInner` (`src/dungeon.c:6778`) | não entra no bloco que cancela o dash ao mudar de direção; permite virar correndo | A (`TurnWhileDashing`) |
| `$82:88F9` e `$82:A55A` `JSL $07:F8D0` | `Module07_00_PlayerControl` (`src/dungeon.c:6598`) e `Module09_00_PlayerControl` (`src/overworld.c:797`) | troca de item com L/R; usa `$7EF33F/$7EF34F/$7EF35B` e `$0202` | A (`ItemSwitchLR`) |
| `$81:DABD` `JML $0E:FB70` | `HandleItemTileAction_Dungeon` (`src/dungeon.c:5566`) | permite quebrar vasos com espada Lv2+ quando B está pressionado; replica a condição do C | A (`BreakPotsWithSword`) |
| `$81:DAFC` `JML $0E:FF00` | continuação de `HandleItemTileAction_Dungeon` | amplia o hitbox de coleta para a espada | A (`CollectItemsWithSword`) |
| `$82:857D` `JSL $07:FF33` | `LoadOWMusicIfNeeded` (`src/dungeon.c:6501`) / fluxo de voo | cancela o transporte de pássaro/flauta | A (`CancelBirdTravel`, inferido) |
| `$87:A951` 4 bytes `STA $9F9F9F,X` no lugar de `AND #$40/BNE` | `LinkItem_Mirror` (`src/player.c:2930`, condição nas linhas 2943-2948) | remove o teste de Mundo Sombrio e faz o caminho externo sempre tocar o SFX de erro e retornar; o espelho em dungeon continua funcionando | D (ver 3.2) |
| `$81:C4B8`, `$81:C501`, `$81:C592`, `$81:C65F` `JSL $0E:FCA1` | rotinas de room tag (`src/dungeon.c:4436` em diante) | se `submodule == $0F`, limpa `$012F`; senão seta `$11 = 5`; parece correção de tag/transição | D (não identifiquei a intenção; precisa traçar) |
| `$82:8136` `JSL $84:EC60`; `$80:876B` `JSL $02:C4E0`; `$8A:B8ED` `JSL $84:EC20`; `$89:F597` `JSL $04:EC70` | `Module05_LoadFile` (`src/misc.c:542`), startup, código de sprite | usa flags em `$7F5006/$7F6884/$7F6885` e `$7EF403`; parece lógica de evento único/cutscene/story | E |
| `$88:832D` `JSL $02:C4C0` | `Ancilla_ExecuteAll` (`src/ancilla.c:662`) | ajuda de ancilla/tile (provavelmente bugfix de Somaria/Kholdstare); não identifiquei | D/E |
| `$80:876B` `JSL $02:C4E0` | startup (`src/zelda_rtl.c:352` ou vizinho) | init de `$0AF4`; parte do conjunto de patches de cutscene/story | E |

Não encontrei um desvio separado para "autofill magic meter" fora do bloco
`$23:8000`: o código que lê `$7EF36E` e mexe no contador de magic fica lá.
Também não encontrei um desvio específico para "camera shake fix": o patch
documentado em `E8000: 7F 80 -> 01 FF` já está `01 FF` na ROM US, então não
há diferença a importar; o zelda3 usa as próprias estruturas de sala e não
tem o bug do Hyrule Magic.

### 3.2 Caso 1: quebra-cabeça da praia (área `$3F`)

O gancho é em `$82:AA7D`, dentro de `OverworldHandleTransitions`. A instrução
original é `LDA $02A5EC,X` (carrega `kOverworldAreaHeads[pushed]`); o hack
troca por `JSL $24:8000`. O código novo:

- começa com dois `NOP` e um `CMP #$3F` (o documento do autor fala em
  `AND #$3F; CMP #$3F`, mas o binário v2.5 tem `NOP NOP`; portanto só casa
  a tela antiga exatamente `$3F`, não `$7F`);
- se não for a área `$3F`, carrega o cabeçalho de área normal e zera o
  contador `$1CF4`;
- se for, compara o índice novo (`X`) com uma tabela de 4 bytes em
  `$24:8200`; a sequência gravada é **cima, baixo, direita, cima**
  (o FAQ do autor fala "Up, Down, Left, Right", mas a tabela do binário é
  outra);
- a cada acerto incrementa `$1CF4`; se chegar a 4, toca o SFX `$1B`, usa
  `X = $37` e carrega a área `$37` (a praia/Beach Temple);
- se o índice não for o próximo da sequência, zera o contador;
- para as direções válidas (`$36` esquerda, `$40` direita, `$47` baixo,
  `$37` cima), ajusta coordenadas de Link/câmera (`$23`, `$E1`, `$E3`,
  `$615`, `$617`, `$700`, etc.) em 2 pixels (ou 0x10 no eixo vertical) e
  força `X = $3F` para continuar a transição normal.

No zelda3, `OverworldHandleTransitions` calcula `new_area` e só depois emite
`AreaEnter` (linha 873). Não existe um evento antes da transição que possa
vetar ou trocar o destino. Para rodar isso como mod, falta um gancho do tipo
"filtro de transição do overworld" (recebe tela antiga, índice novo,
destino, e pode devolver outro destino/negar). Isso é **classe D**; se for
implementado direto na engine, vira **E** (F4e). O estado da combinação
(`$1CF4`) pode ficar em RAM do mod/Lua.

**Implementado no AX (09/11/26):** o filtro `OverworldTransitionTarget` da
F4e.1 existe e o AX o usa para a combinação. Para o desvio "volta para a mesma
tela" não iniciar uma rolagem inválida, `OverworldHandleTransitions` passou a
tratar "destino igual à tela atual" como veto quando há assinante do filtro;
o mod recoloca o Link na borda oposta da mesma tela. O replay de feature
`tests/replays-features/ax-beach-veto.sav` cobre o caminho; sem mod o
comportamento não muda. O gancho de recarga de magia também não precisou de C
novo: o evento `FrameStart` já permite `state_write`.

### 3.3 Caso 2: espelho proibido no Mundo Sombrio

Em `LinkItem_Mirror` (`src/player.c:2930`), a condição das linhas 2943-2948
retorna cedo quando Link está ao ar livre e **não** está no Mundo Sombrio
(bit `0x40` de `overworld_screen_index`). No assembly US isso é
`LDA $8A; AND #$40; BNE +7`; se o bit estiver setado, ele pula o erro e usa
o espelho. O hack troca os 4 bytes por `STA $9F9F9F,X` (o autor chama de
NOP; na prática é um store para `$9F:9F9F,X`, fora da RAM, que não altera o
fluxo). Sem o teste, o caminho externo sempre toca o SFX `$3C` e retorna. O
espelho continua funcionando em dungeon/interior, que é o que o hack quer
(você só volta ao Mundo da Luz pelos portais).

O zelda3 tem `MirrorToDarkworld`, que é o **oposto** (permite usar o espelho
na Luz para ir ao Mundo Sombrio). Não existe opção "proibir espelho no Mundo
Sombrio". A forma mais limpa é **classe D**: uma flag nova (por exemplo
`NoMirrorInDarkWorld`) ou um gancho de "uso de item" que possa cancelar o
efeito. Lua pura não consegue hoje porque não há evento de uso de espelho
antes do efeito; `RoomEnter`/`AreaEnter` só avisam depois.

## 4. Teste real do importador

Comando executado (pasta temporária fora de `mods/`; ela precisa de um
`mod.json` mínimo com `id` e `api`):

```sh
python3 tools/modkit.py import-rom \
  altti.sfc \
  /tmp/altti-mod
```

Resultado: **falhou** (exit 1), antes de importar qualquer asset. O erro é o
portão de hash do `assets/util.py`:

```text
modkit: compiling translated assets from altti.sfc
modkit: error: translated asset compilation failed (exit 1)
...
ROM with hash 51BA60F13EECE49BE035B30494AE8C37D1A22ADD not supported.
```

Ou seja, o `LoadedRom(..., support_multilanguage=True)` só aceita os hashes
listados em `ZELDA3_SHA1`. Antes disso o comando já tinha conseguido ler as
tabelas de ponteiros de gráficos do hack (o código de `Decomp_spr`/`Decomp_bg`
está intacto) e decodificar as tabelas derivadas do letreiro de fim de jogo
(8 letras).

Para separar "hash" de "formato", repeti o teste numa cópia temporária do
repositório, só para medir os próximos obstáculos:

1. **Só liberando o hash:** falha em `compile_resources.print_dungeon_map`,
   porque `util.get_byte` exige `(ea & 0x8000)` e os ponteiros de tiles dos
   mapas de dungeon agora são `$0A:2D14`, `$0A:2D2A`... (metade baixa do
   banco). O mesmo assert também derrubaria sprites/segredos de dungeon e
   qualquer ponteiro na metade baixa.
2. **Liberando hash e o assert de endereço:** falha em
   `print_overworld_tables`, porque o `import-rom` compila o "traduzido" com
   o YAML **US** do cache, mas a tabela de áreas do hack diz que a área 25
   (e depois 28, 32, 35, 36, 89, 92, 96, 99, 100) é cabeça de área e não
   existe `overworld/overworld-25.yaml` no cache US. Ou seja, o desenho atual
   do `import-rom` não importa salas/overworld; ele só troca gráficos,
   paletas, mapas e tabelas.
3. **Extração com o `restool` (hash liberado, tolerância mínima):** quebra
   primeiro nos segredos do overworld (tipo `0xA6` na área 23, posições
   ímpares e áreas 117-127 apontando para fora da tabela), depois em
   ponteiros de sprite 0 e em tipos de sprite/efeito fora das tabelas US.
   Com tolerância, extraiu 292 YAMLs de dungeon, 118 de overworld e 397
   linhas de diálogo antes de parar; o diálogo sozinho
   (`--extract-dialogue`) completou as 397 mensagens.

Conclusão do teste: o dado do hack é majoritariamente parseável, mas o
`import-rom` atual não foi desenhado para ele. O que já funcionaria se o hash
fosse aceito: `kSprGfx`, `kBgGfx` (via tabelas lidas do código),
`kOverworldMapGfx`, paletas, `kDungMap_*` (depois do assert) e as tabelas de
`src/mod/game_tables.def`. O que não funciona: salas, cabeçalhos, baús,
sprites/segredos de dungeon, overworld, entradas e texto.

**Atualização I3 (09/11/26):** as ressalvas acima sobre salas, entradas e
overworld foram resolvidas. O `import-rom` extrai o overworld do hack, importa
as tabelas que diferem e usa `map32_to_map16.txt` regerado da ROM de origem
(sem isso o Redux renderizava tiles aleatórios). O teste visual e a lista de
tabelas estão em `docs/engine/MODS.md`.

## 5. O que falta no modkit

1. **Aceitar a ROM.** Adicionar o SHA-1 do hack a uma lista de "ROMs
   conhecidas" ou criar `--allow-unknown-rom` / `--base-hash` para o
   `import-rom`. Hoje o `util.load_rom(..., True)` recusa antes de qualquer
   trabalho.
2. **~~Não assumir `bit 15`.~~** Errado (ver a correção no topo): o assert
   está certo; o problema é ler tabelas em endereço fixo da US quando o
   hack as moveu. O I1 passou a ler o endereço pelo operando do código.
3. **Extrair do hack, não do YAML US.** Criar um caminho de importação que
   leia as tabelas do hack e gere conteúdo:
   - salas: tabela de objetos em `$1F:8000` + cabeçalhos em `$2A:8000`
     (296 entradas, padding para 320) + baús `$01:E96E` + sprites
     `$09:D62E` + segredos `$01:DB69` + tele mensagens `$87:F61D` + pits
     `$80:990C`;
   - overworld: cabeçalhos `$02:A5EC`, hi/lo `$02:F94D/$02:FB2D`, gfx/paleta
     `$00:FC9C/$00:FD1C`, sign text `$07:F51D`, entradas/holes
     `$1B:B96F/$1B:B800`, exits `$02:DD8A`, segredos `$1B:C2F9`, sprites
     `$09:C881/$09:C901/$09:CA21`;
   - entradas e starting points: `$02:C813` etc., com sanitização;
   - diálogo: `$9C:8000/$8E:DF40` no formato US, gerando `dialogue.txt`.
4. **Sanitizar e padding.** Ignorar ponteiros 0, entradas não usadas
   (`0xFF`), tipos desconhecidos e as áreas inválidas de segredos do
   overworld. Preencher tabelas fixas (320 cabeçalhos, 320 sprites, 133
   entradas) com valores seguros.
5. **~~Resolver o limite dos sprites de dungeon.~~** Feito no I2: a tabela
   certa (`$09:D2E1`) tem só 3.988 bytes únicos e cabe no formato atual. Não é
   preciso mudar `kDungeonSpriteOffs` para `uint32`.
6. **Achar os tilemaps novos.** O `print_tilemaps` fixa as fontes; o hack
   moveu 2-4 e trocou a 0. É preciso ler o código de NMI ou varrer os packs
   comprimidos e identificar as fontes novas.
7. **~~Diálogo e Link.~~** Feito no IX: o `import-rom` extrai as 397
   mensagens no formato US, compila `kDialogue` (e `kDialogueFont` quando a
   fonte muda) e importa `kLinkGraphics` do bloco `$10:8000`. Comandos de
   texto desconhecidos são listados e a importação do diálogo é pulada, sem
   inventar valores.
8. **Teste de fumaça.** Rodar headless com o mod gerado, conferir
   `--mod-trace` (salas, baús, entradas) e validar visualmente os gráficos
   importados. `tools/regress.sh` não se aplica ao mod (mods desligam a
   comparação lado a lado).

## 6. Plano em passos

Ordem por resultado:

1. **Plumbing do modkit** (pequeno): hash, assert de endereço, flags de
   importação. Sem isso nada mais é testável.
2. **Gráficos/paletas/mapas** (médio): `kSprGfx`, `kBgGfx`,
   `kOverworldMapGfx`, paletas, `kDungMap_*`; localizar os tilemaps 0/2/3/4.
   É o que o `import-rom` quase faz hoje.
3. **Salas, baús e cabeçalhos** (grande): extrator da tabela de objetos e
   da tabela nova de cabeçalhos, padding para 320, `kDungeonRoom*`,
   `kDungeonRoomChests`, `kDungeonRoomTeleMsg`, `kDungeonPitsHurtPlayer`.
4. **Overworld** (grande): extrator das tabelas de áreas, hi/lo, entradas,
   holes, exits, segredos, sprites e cabeçalhos; sanitização dos segredos
   inválidos. É o passo que dá a "cara" do hack.
5. **Entradas e starting points** (pequeno/médio): ler as tabelas fixas,
   validar e gerar `kEntranceData_*`/`kStartingPoint_*`.
6. **Diálogo e texto** (pequeno/médio): extrair 397 mensagens, gerar
   `dialogue.txt`, compilar `kDialogue`; tele mensagens/sign text entram nos
   passos 3/4.
7. **Sprites de dungeon**: feito no I2. A tabela certa é `$09:D2E1` e o dado
   único (3.988 bytes) cabe no formato `uint16` atual; ponteiros 0 e tipos
   desconhecidos são tratados na extração.
8. **Comportamento ASM** (médio/grande): ligar as opções A no
   `zelda3.ini`; criar ganchos para espelho no Mundo Sombrio (D),
   quebra-cabeça da praia (D/E) e autofill de magic (E); revisar as rotinas
   de story/cutscene (`$84:ECxx`, `$82:C4xx`, `$0E:FCA1`).
9. **Teste e ajuste** (médio/grande): headless, salas, transições, baús,
   entradas, overworld, gráficos; comparar com o hack original.

## 7. Os 5 maiores obstáculos

1. **Suposições do ferramental**: hash fixo, `assert (ea & 0x8000)`,
   endereços fixos de tilemaps/YAML. Cada uma derruba o importador em um
   ponto diferente.
2. **O `import-rom` compila o "traduzido" com o YAML US**: ele não foi
   feito para importar salas/overworld; sem um extrator do hack, o máximo
   que se consegue é gráfico/paleta/mapa.
3. **Cabeçalhos de sala realocados e truncados**: `$2A:8000` com 296
   entradas, `$04:F502` obsoleto. Quem seguir a tabela antiga importa
   cabeçalho errado.
4. ~~**Volume dos sprites de dungeon**~~: resolvido no I2. A tabela de
   sprites estava em `$09:D2E1`; com ela, o volume é 3.988 bytes e cabe em
   `uint16`. O obstáculo real era a tabela de ponteiros, não o formato.
5. **Dados sujos + ASM fora do ini**: segredos inválidos, ponteiros 0,
   tipos desconhecidos, entradas duplicadas; e os hacks de espelho/praia/
   magic não têm opção pronta. Exige validação tolerante e ganchos novos.

## 8. O que não consegui confirmar

- A intenção exata dos blocos `$23:8000`, `$84:EC20-$84:EC70`,
  `$02:C4C0-$02:C500` e `$0E:FCA1`; descrevi pelo que eles leem/escrevem,
  não por traço completo.
- Se o item switch L/R e o autofill de magic do hack se comportam
  exatamente como `ItemSwitchLR`/as features do zelda3.
- Se as notas de "hex edits" do autor foram aplicadas na v2.5. Várias não
  batem com os bytes da ROM: `$03/F498`, `$02/DA81` e `$0F/6062` não têm os
  valores citados; o Watergate em `0xCBAD` está `0x0C` no hack (o `0x04`
  da nota é a correção, não aplicada); `$02/EBD5` mudou, mas não é o
  `$B0 $38 -> $EA $EA` documentado; `E8000` já é `01 FF` na US. Só confirmei
  o espelho (`$87:A951`), as flechas de prata (`0x44353`) e o `2EDE0`
  (`02 -> 03`).
- O que é o dado de `$28:8000` (≈1,3 KB); provavelmente cutscene/final, mas
  não achei a referência de código com certeza.
- Se as 296 entradas de cabeçalho cobrem todas as salas realmente usadas;
  as salas 296-319 parecem não ter cabeçalho válido no hack.
- Se os gráficos/tilemaps importados ficam visualmente corretos; só
  descomprimi e comparei tamanhos, não renderizei.
- Se o mapeamento de faixas MSU do hack bate com o MSU/MSU Deluxe do
  zelda3; não testei áudio.
- ~~Se dá para reduzir os sprites de dungeon abaixo de 64 KB.~~ No I2 a
  tabela certa (`$09:D2E1`) deu 3.988 bytes únicos; o valor de 83,5 KB era
  leitura da tabela antiga.

## 9. Comandos de verificação usados

```sh
make -j2
tools/regress.sh
```

Resultado real: `make -j2` terminou sem warnings novos; `tools/regress.sh`
terminou com `regress: all 18 replays and 3 feature replays passed` e uma
linha `ok` para cada replay.

Nenhum código do jogo/engine foi alterado; este documento é a única mudança
versionada.
