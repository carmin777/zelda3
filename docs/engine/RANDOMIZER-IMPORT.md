# Importador de seeds do alttpr.com

Data: 10/11/26. Rodada 2 do R4: o motor em C continua sendo a reimplementação
**US**, mas lê a ROM de uma seed do ALttPR como **dados**. O oráculo estrito
continua no `tools/regress.sh`; ele não é mais a meta da compatibilidade.

O código novo fica em `src/mod/seed_import.c` e só age quando existe um mod
com `"seed_data"`. Sem esse mod, todos os ganchos devolvem o valor original e o
jogo é idêntico ao de antes.

## 1. O que a ROM da seed carrega

A ROM da seed é JP 1.0 + patch-base `z3randomizer` + os bytes da seed. O
importador lê os dados já aplicados na ROM; não precisa do JSON nem do
spoiler. O JSON, quando existe, só acrescenta `mode`/`goal`/`logic` em
`settings`.

### 1.1 Hash e título

| Dado | Onde |
|---|---|
| Título interno (`VT xvJo08oxyJ`) | PC `0x7FC0` |
| Cinco ícones do hash | PC `0x180215` (`SeedHash`) |
| Tipo de seed / flags | PC `0x180210` (`RandomizerSeedType`), `0x180211` (`GameType`) |
| Torneio, invertido, aviso | PC `0x180213`, `0x18004A`, `0x180212` |
| Cristais/medalhões por dungeon | PC `0x180050` (`CrystalPendantFlags_2`) |
| Abertura da Ganon's Tower / Ganon | PC `0x18019A`, `0x18019C`, `0x1801A8` |

O nome da pasta e o `id` do mod saem do título (`xvJo08oxyJ`) ou, se ele não
existir, dos cinco bytes do hash.

### 1.2 Baús

A tabela de baús da seed fica em **PC `0xE96E`**, com 168 registros de 3
bytes no formato do `z3randomizer`:

```text
uint8  item;       // id z3r do item
uint8  room_lo;
uint8  room_hi;    // bit 0x80 = baú grande
```

Isso é o inverso do asset US `kDungeonRoomChests`, que é
`uint16 room; uint8 item;`. O importador converte para o formato do motor.

O índice do baú dentro da sala é a ordem dos objetos de baú na sala, a mesma
que `dungeon.c` usa em `kDungeonRoomChests`: objetos `F99-Chest` (pequeno) e
`FB1-Big Chest` (grande), na ordem em que aparecem nas camadas da sala. Por
isso a chave do local é `(room, index)`, não só a sala.

### 1.3 Item do tio

No PC `0x2DF45`, dentro de `sprite_uncle_and_priest`, a ROM tem:

```text
A0 xx              LDY #item
9C E9 02           STZ $02E9
22 24 88 A4        JSL $A4:8824   ; OnUncleItemGet
```

O importador confere a assinatura e lê o byte `xx`. No vanilla esse byte é
`0x00` (espada e escudo); nesta seed é `0x44` (10 flechas).

### 1.4 Tabelas fixas (NPC, chefe, prêmio, item solto)

As tabelas documentadas no `z3r-tables.asm` que o importador lê:

| Tabela | PC | Conteúdo |
|---|---:|---|
| `HeartPieceIndoorValues` | `0x180000` | 7 heart pieces indoor |
| `SpriteItemValues` | `0x180010` | 8 itens de NPC (biblioteca, cogumelo, bruxa, morcego, Ether, Bombos, rupeenpc) |
| `HeartPieceOutdoorValues` | `0x180140` | 11 heart pieces/segredos outdoor, incluindo Haunted Grove |
| `HeartContainerBossValues` | `0x180150` | 10 heart containers de chefe |
| `KeyItemValues` | `0x180160` | 3 chaves (bonk/standing) |
| `FairySword` | `0x180028` | item da fada da espada |
| `SmithItem` | `0x18002A` | item do ferreiro |
| `PedestalSword` | `0x289B0` | item do pedestal da Master Sword |
| `WaterfallPotion` | `0x348FF` | item da cachoeira |
| `PyramidPotion` | `0x3493B` | item da fada da pirâmide |
| `DungeonPrizeReceiptID` | `0xC6FC` | 13 bytes; só 10 dungeons têm prêmio (índices 2,3,5,6,7,8,9,10,11,12) |

`SmithSword` (`0x3355C`) é o espelho do mesmo item do ferreiro e não entra
duas vezes na contagem.

O motor aplica essas tabelas nos sítios abaixo:

| Local | C | Regra z3r |
|---|---|---|
| `Sprite_HeartPiece` | `sprite_main.c` | `heartpieces.asm` `LoadIndoorValue`/`LoadOutdoorValue`; salas 225/226/234/283/294/295/135 e telas 3/5/0x28/0x2A/0x30/0x35/0x3B/0x42/0x4A/0x5B/0x68/0x81 |
| `Sprite_HeartContainer` | `sprite_main.c` | `LoadHeartContainerRoomValue`; salas 200/51/7/90/6/41/172/222/144/164 |
| `Sprite_BookOfMudora` | `sprite_main.c` | `LibraryItem` |
| `Sprite_E7_Mushroom` | `sprite_main.c` | `MushroomItem` |
| `Sprite_Witch`, `Sprite_BagOfPowder` | `sprite_main.c` | `WitchItem` |
| `Sprite_3A_MagicBat` | `sprite_main.c` | `MagicBatItem` |
| `Sprite_BonkKey` | `sprite_main.c` | `BonkKey_Desert` (sala 115), `BonkKey_GTower` (sala 140), `StandingKey_Hera` (sala 135) |
| `NiceThiefWithGift` | `sprite_main.c` | `RupeeNPC_MoldormCave` (sala 291), `RupeeNPC_NortheastDarkSwampCave` (sala 286) |
| `MasterSword_Main` | `sprite_main.c` | `PedestalSword` |
| Smithy `GiveTemperedSword` | `sprite_main.c` | `SmithItem` |
| `Sprite_WishPond3` | `sprite_main.c` | `WaterfallPotion`/`PyramidPotion` |
| `AncillaAdd_FallingPrize` | `ancilla.c` | `EtherItem`/`BombosItem` |
| `RoomTag_GetHeartForPrize` | `dungeon.c` | `DungeonPrizeReceiptID` |

Os nomes de local no `seed.json` são `fixed:<tabela>:<campo>` e
`prize:<dungeon>`, por exemplo
`fixed:SpriteItemValues:LibraryItem` e `prize:Eastern Palace`.

### 1.5 Lojas

`ShopTable` fica em PC `0x184800` (8 bytes por loja) e
`ShopContentsTable` em PC `0x184900` (8 bytes por entrada: id da loja, item,
preço, máximo, substituto). O importador lista as entradas até o primeiro
item `0xFF` (fim da tabela), não até o primeiro id `0xFF`; o id `0xFF` é uma
loja válida. Cada loja entra no mesmo `ModShop` da F4e.3b, com item, preço,
quantidade, **sala e porta**.

Cada registro de `ShopTable` traz `[id][room_lo][room_hi][door][zero][config]
[shopkeeper][sram_index]`. O motor copia o id da porta usada para
`PreviousOverworldDoor` (`$7F:5099`, no C `previous_overworld_door`) ao entrar
por uma porta de overworld, e `SpritePrep_Shopkeeper` procura primeiro a loja
daquela sala **e** daquela porta. O bit `0x40` de `config` significa "ignorar
a porta" e é importado como `any_door`. Sem porta conhecida (por exemplo um
save carregado já dentro da loja), a primeira entrada da sala é usada como
fallback.

Nesta seed, as salas `0x10F` e `0x112` aparecem em mais de uma porta
(`0x6F`/`0x57`/`0x60`/`0x74` e `0x58`/`0x6E`); os inventários dessas portas
são iguais nesta seed, mas a seleção por porta já está modelada.

O id `0xFF` é uma loja válida (a décima, `Dark World Lake Hylia Shop`), não um
terminador. O importador para no primeiro registro vazio da `ShopTable` e no
primeiro item `0xFF` da `ShopContentsTable`, o que recupera as 29 entradas de
loja. O spoiler da seed `LyVe6KOAvb` tem 255 entradas de item; 251 batem com a
tabela do ROM e 4 são locais de NPC/item solto (como Sahasrahla e Sick Kid)
que não estão nas tabelas fixas do z3r. Além disso o spoiler tem 4
requisitos especiais (medalhões), que não são locais de item.

### 1.6 Inventário inicial e configurações

O arquivo novo de jogo é copiado do template de SRAM em
`$B0:B000`/`$B0:B3E3` (PC `0x183000`/`0x1833E3`), 0x500 bytes, exatamente como
o `OnFileCreation` do z3r: copia `0x3D8` bytes e depois `0x10C` bytes a partir
de `0x3E3`, pula nome/validade/checksum, resolve o marcador de pós-Agahnim
(`InitProgressIndicator & 0x80`) e resolve o checksum.

Uma diferença de semântica importa: o z3r grava **capacidade bruta** de
bombas/flechas em `$F370/$F371` (10/15/... e 30/35/...), enquanto o motor US
usa **nível 0..7** nesses mesmos endereços. O importador converte os dois
bytes ao copiar o template.

As configurações do `z3randomizer` ficam no bloco `$B0:8000` (PC `0x180000`):
tipo de seed, `GameType`, torneio, invertido, flags de HUD, RNG, limites de
progressivos, abertura da Ganon's Tower, etc. O importador grava o bloco
inteiro em `z3r_settings` no `seed.json`, com nomes estáveis, além de
`settings` (modo/objetivo/logic do JSON e os alvos de cristais).

Valores desta seed (`xvJo08oxyJ`) e estado no motor C:

| Campo | PC | Valor | Motor |
|---|---:|---:|---|
| `RandomizerSeedType` | `0x180210` | `0` (casual) | metadado |
| `GameType` | `0x180211` | `0x04` (só item) | metadado; não há door shuffle nesta seed |
| `WarningFlags` | `0x180212` | `0` | metadado |
| `TournamentSeed` | `0x180213` | `1` | metadado; HUD de torneio não implementado |
| `InvertedMode` | `0x18004A` | `0` | **primeira fatia** em `RANDOMIZER-INVERTIDO.md`: início no Mundo das Trevas e regra da pérola/coelho; tilemaps, entradas e Agahnim/torre pendentes |
| `GanonsTowerOpenTarget` / `Mode` | `0x18019A` / `0x18019C` | `4` / `1` | **aplicado**: `ModSeedImport_GanonsTowerOpen` compara a contagem de cristais com o alvo (`goalitem.asm` `CheckTowerOpen`) |
| `GanonVulnerableTarget` / `Mode` | `0x1801A6` / `0x1801A8` | `7` / `4` | **aplicado** para modos 0..11: modo 4 é o desta seed; 7/8 usam `CrystalBossCount`; 10/11 usam `TotalItemCounter`; modo 6 é sempre invulnerável, como no `goalitem.asm` |
| `GoalItemIcon` / `Requirement` | `0x180165` / `0x180167` | `0x280E` / `0` | requerimento `0`: sem goal items nesta seed; itens `0x6B`/`0x6C` incrementam o `GoalCounter` |
| `TurnInGoalItems` | `0x180194` | `1` | aplicado ao limiar do modo 5; `0` significa vitória instantânea no z3r e fica registrado como metadado, sem transição automática de ending neste fluxo |
| `RupoorDeduction` | `0x180036` | `0` | aplicado no item `0x59` |
| `Upgrade5/10BombsRefill` | `0x180080`–`0x180081` | `50`/`50` | aplicado nos itens `0x51`/`0x52` |
| `Upgrade5/10ArrowsRefill` | `0x180082`–`0x180083` | `70`/`70` | aplicado nos itens `0x53`/`0x54` |
| `Progressive*Limit/Replacement` | `0x180090`–`0x180099` | `4/3/2/4/2`, reposição `0x47` | aplicado no mapeamento dos progressivos |
| `StartingAreaOverworldDoor` | `0x180247` | `0,0,0,0,0,0,0` | importado; usado quando um starting point tem porta |
| `HUDDungeonItems` | `0x180045` | `0x0F` | **aplicado**: bits de chave/big key/map/compass em `Hud_DrawEquipmentBox` e `Hud_UpdateHud` |
| `ItemCounterHUD`, `MapHUDMode` | `0x180039` / `0x18003A` | `0` / `0` | **aplicado**: `TotalItemCounter/TotalItemCount` em `Hud_DrawSeedItemCounter`; `MapHUDMode` desenha chaves de baú encontradas/totais em `Hud_DrawSeedMapHud` (bit 1 exige o mapa, bit 2 mostra sempre) |
| `MapMode`, `CompassMode` | `0x18003B` / `0x18003C` | `1` / `1` | **aplicado**: layout exige mapa (`MapMode=1`) e o ícone de chefe exige bússola (`CompassMode=1`) |
| `HeartBeep`, `MenuSpeed` | `0x180033` / `0x180048` | `0x20` / `0x08` | **aplicado** no intervalo do beep e na descida/subida do menu |
| `NoBGM`, `FastFanfare` | `0x18021A` / `0x18021B` | `0` / `0` | **aplicado**: `NoBGM` consome pedidos de música no NMI (exceto a trilha 8 do espelho) e `FastFanfare` pula a espera de fanfarra de pendant/cristal |
| `ByrnaInvulnerability` | `0x18004F` | `1` | **aplicado**: `Ancilla31_ByrnaSpark` usa o valor em `link_disable_sprite_damage` |
| `FakeBoots`, `QuickSwap`, `GenericKeys` | várias | `0`, `0`, `0` | **aplicado**: `FakeBoots` libera o bit de dash em `$879C22`; `QuickSwap` habilita a troca com L/R sem `ItemSwitchLR`; `GenericKeys` usa um contador compartilhado e o slot 0 no load/save |

O estado inicial (template de SRAM) vem do bloco `$B0:B000`; as regras de
comportamento vêm da opção `Z3rRules` (seção 5). Os campos marcados como
"aplicado" são lidos do `z3r_settings` e usados pelo motor. A única flag de
comportamento ainda pendente é `InvertedMode`, que exige um modo de jogo
inteiro (R5).

### 1.7 Textos e dicas

O z3randomizer não usa o formato comprimido de diálogo do motor US. A ROM da
seed carrega um bloco de texto bruto no formato JP em PC `0xE0000` (SNES
`$9C:8000`), com:

- tabela de ponteiros de 16 bits construída pela mesma varredura do
  `SeedOracle_JpTextPointers` (`$9C:F37A`), usando as tabelas de avanço em
  `$9C:F261`/`$9C:F2F9`;
- um byte de glifo por caractere (`A` = `0xAA`, `a` = `0xD0`, espaço =
  `0xFF`), dígitos `0xA0..0xA9`, apóstrofo `0x9D`;
- comandos `$F7/$F8/$F9` (linhas 1/2/3), `$F6/$FA` (scroll), `$FC`
  (velocidade), `$FD` (glifo especial de dois bytes), `$FE` (comando de dois
  ou três bytes) e `$FB` (fim da mensagem).

`tools/alttpr_seed.py` lê esse bloco, converte o texto para a sintaxe do
modkit (`[1]`, `[2]`, `[3]`, `[Scroll]`, `[Speed NN]`, `[Number NN]`,
`[Choose]`), valida com o compressor US e grava `content/dialogue.txt`. A
tabela `SeedOracle_JpMessageId` alinha o índice US (0-based, usado pelo C) ao
índice JP; o `dialogue.txt` usa o id 1-based do modkit, então o índice C `N`
vira a linha `N+1`. Mensagens com glifos/commands sem equivalente seguro ficam
com o texto US original.

Para esta seed o extrator gera **174 mensagens**, incluindo as dicas de
telepatas/placas e os textos de itens como "Some cash is in Misery Mire",
"Ether requires a bomb" e "A Heart Container is in Turtle Rock". As mensagens
que só têm ícones/menus, ou usam comandos de layout não mapeados, são puladas.

O `kDialogueFont` não muda: os glifos convertidos são letras/dígitos do
alfabeto US. Textos que dependem de ícones especiais do z3r (botões, corações,
kanji) aparecem sem o ícone ou ficam no texto US de base. A placa do pedestal
(`Sprite_B3_PedestalPlaque`) usa as mensagens US `0xB6`/`0xB7`; elas estão na
sobreposição e aparecem como as dicas JP `0xB4`/`0xB5` da seed. O texto de
cutscene `MSPedestalText` do jogo vanilla não existe como bloco separado na
ROM da seed; ele continua no asset US e não é usado pela placa.

## 2. Ids de item do z3r para o motor US

O motor US só entende com segurança ids até **`0x4B`**: as tabelas de
recebimento (`kReceiveItemMsgs`, `kValueToGiveItemTo`,
`kMemoryLocationToGiveItemTo`, `kReceiveItemGfx`, etc.) têm 0x4C entradas.
Passar um id z3r `0x5E` ou `0xA6` direto para `Link_ReceiveItem` indexaria
fora do array. Além disso, `Link_PerformOpenChest` rejeita `sign8(item)`, ou
seja, ids `0x80..0xFF`; por isso o filtro do baú já devolve o id US mapeado,
não o byte z3r cru.

| Faixa z3r | Exemplos | Regra |
|---|---|---|
| `0x00..0x4B` | espadas, escudos, itens, rupees, heart pieces | passa direto (id US) |
| `0x5E..0x65` | Progressive Sword/Shield/Armor/Glove/Bow | escolhe o próximo item US conforme `link_sword_type`, `link_shield_type`, `link_armor`, `link_item_gloves`, `link_item_bow` |
| `0x70..0x7F` | Map de uma dungeon | `0x33` + dungeon de destino |
| `0x80..0x8F` | Compass de uma dungeon | `0x25` + dungeon de destino |
| `0x92..0x9F` | Big Key de uma dungeon | `0x32` + dungeon de destino |
| `0xA0..0xAF` | Small Key de uma dungeon | `0x24` + dungeon de destino |
| `0x4C..0x5D` | Max Bombs/Arrows, Half/Quarter Magic, Rupoor, clocks | regra própria quando `Z3rRules` está ligado; fallback seguro quando não está |
| `0x66..0x6F`, `0x90`, `0x91` | RNG pool, goal/server, skull/reserved | fallback seguro (`0x47` ou small key) |

**Dungeon de destino:** o item z3r carrega a dungeon (GT, TR, Thieves, Hera,
Ice, Skull, Mire, Dark, Swamp, Aga, Desert, Eastern, Castle, Sewers). O
`ModSeedImport_MapItem` guarda o índice de destino num contexto de recebimento;
`AncillaAdd_ItemReceipt` usa esse índice para ligar o bit em
`link_dungeon_map`/`link_compass`/`link_bigkey`, ou para somar a chave em
`link_keys_earned_per_dungeon[dungeon]` quando o destino não é a dungeon
atual. O item visual continua sendo o genérico US (Map/Compass/Big Key/Small
Key).

**Regras próprias (com `Z3rRules`):**

| Id | Efeito |
|---|---|
| `0x4C` | bombas no máximo (nível 7, filler 50) |
| `0x4D` | flechas no máximo (nível 7, filler 70) |
| `0x4E` | sobe a magia para half magic (até 2) e enche a magia |
| `0x4F` | quarter magic (consumo 2) e enche a magia |
| `0x51`/`0x52` | +5/+10 bombas (um/dois níveis) |
| `0x53`/`0x54` | +5/+10 flechas (um/dois níveis) |
| `0x59` | Rupoor: desconta `RupoorDeduction` (10 por padrão) |

**Cuidado com a F4e.5:** os ids de item novo da F4e.5 são `0x80..0x9F`, a
mesma faixa que o z3r usa para Compass/Big Key. Não dá para registrar um item
F4e.5 e devolver o id z3r direto. Quando um item z3r-only precisar de um item
novo, o importador tem de alocar um id F4e.5 livre e manter uma tabela
separada `id z3r -> id F4e.5`, sem reutilizar o byte z3r.

## 3. Formato do mod gerado

`tools/alttpr_seed.py import` escreve, por padrão, em
`mods-seeds/<seed>/` (pasta ignorada pelo git):

```text
mods-seeds/xvJo08oxyJ/
  mod.json
  seed.json
```

`mod.json`:

```json
{
  "id": "seed-xvJo08oxyJ",
  "name": "ALttPR seed xvJo08oxyJ",
  "version": "1.0.0",
  "api": 1,
  "enabled": true,
  "features": { "Z3rRules": true },
  "seed_data": "seed.json"
}
```

`seed.json` tem `format: 1`, `seed`, `rom` (md5/crc32/título), `settings`,
`z3r_settings` (o bloco `$B0:8000` nomeado), `save_template` (0x500 bytes em
hex) e `locations`. Cada local tem `id`, `type`, `name`, `item` (id z3r) e
`item_name`; baús acrescentam `room`, `index`, `big` e `record`; lojas
acrescentam `shop`, `slot`, `price`, `room`, `door` e `any_door`.

Com `--build`, o `import` também grava `content/dialogue.txt` e roda
`tools/modkit.py build`, que cria `build/kDialogue.bin` e o
`asset_overrides` do manifesto. Sem `--build`, o conteúdo fica só como fonte
e o usuário roda o build antes de jogar. `--no-dialogue` desliga a extração
(usado pelas provas filtradas do regress, que não testam texto).

O campo `seed_data` é lido por `src/mod/mods.c` e entregue a
`ModSeedImport_LoadFile`. Se o JSON for inválido, o mod é recusado; se for
válido, o loader liga `g_mods_change_content`, então a comparação byte a byte
com a ROM fica desligada enquanto o mod estiver ativo.

## 4. Ganchos no motor

| Local do jogo | Gancho | O que faz |
|---|---|---|
| `OpenChestForItem` (`dungeon.c`) | `ModSeedImport_ChestItem(room, index, item)` | troca o item do baú pequeno ou grande pelo item da seed para aquela sala/índice, já mapeado para um id US seguro |
| `Link_ReceiveItem` (`player.c`) | `ModSeedImport_MapItem(item)` | converte id z3r em id US, guarda o destino de dungeon ou a regra especial |
| `AncillaAdd_ItemReceipt` (`misc.c`) | `ModSeedImport_Take*` | aplica a dungeon de destino e as regras de Max Bombs/Magic/Rupoor no momento do recebimento |
| `Uncle_InPassage` (`sprite_main.c`) | `ModSeedImport_UncleItem(0)` | troca o `Link_ReceiveItem(0, 0)` pelo item do tio da seed |
| `Sprite_HeartPiece` (`sprite_main.c`) | `ModSeedImport_HeartPieceItem` | resolve heart piece/tablet/standing key pela sala/tela |
| `Sprite_HeartContainer` (`sprite_main.c`) | `ModSeedImport_BossItem` | item do chefe pela sala |
| `RoomTag_GetHeartForPrize` (`dungeon.c`) | `ModSeedImport_PrizeItem` | prêmio da dungeon |
| `SpritePrep_Shopkeeper` (`sprite_main.c`) | `ModShop` | lojas da seed com preço/item, sala e porta |
| `Overworld_ToolAndTileInteraction` (`overworld.c`) | `previous_overworld_door` | guarda `lx+1` ao entrar numa porta de overworld |
| `Dungeon_LoadEntrance` (`dungeon.c`) | `ModSeedImport_StartingDoor` | aplica `StartingAreaOverworldDoor` em starting points |
| `NameFile_DoTheNaming` (`select_file.c`) | `ModSeedImport_InitNewSave` | template de SRAM do arquivo novo |
| `Text_LoadCharacterBuffer` (`messaging.c`) | `kDialogue` + `DialogueText` | textos do z3r pelo modkit; o evento só existe com mod de seed |
| `AncillaAdd_ItemReceipt` (`misc.c`) | `kModEv_SeedItem` | evento de trace com item e dungeon de destino (substitui o antigo `fprintf`) |
| `AncillaAdd_GTCutscene` (`ancilla.c`) | `ModSeedImport_GanonsTowerOpen` | `GanonsTowerOpenTarget/Mode`; emite `GtOpen` |
| `Sprite_CheckDamageFromLink` (`sprite.c`) | `ModSeedImport_GanonVulnerable` | `GanonVulnerableTarget/Mode` no hook `$86F2C8` |
| `ModSeedImport_MapItem` (`seed_import.c`) | `GoalCounter` | itens `0x6B`/`0x6C` incrementam o contador de goal items |
| `Hud_DrawEquipmentBox`, `Hud_UpdateHud` (`hud.c`) | `HUDDungeonItems` | mapa/bússola/big key/chaves no HUD |
| `DungeonMap_*` (`messaging.c`) | `MapMode`/`CompassMode` | layout só com mapa; ícone de chefe com bússola |
| `Ancilla31_ByrnaSpark` (`ancilla.c`) | `ByrnaInvulnerability` | valor escrito em `link_disable_sprite_damage` |
| `Hud_RefillLogic`, `Hud_BringMenuDown` (`hud.c`) | `HeartBeep`, `MenuSpeed` | intervalo do beep e passo do menu |
| `PlayerHandler_04_Swimming`, `Bonk/JumpDownLedge` | `Z3rRules` | regras de comportamento iguais em toda seed |

Todos são no-op sem um mod com `seed_data` ativo.

## 5. Regras do z3r como opção (`Z3rRules`)

A opção `[Features] Z3rRules` (e `"features": {"Z3rRules": true}` no mod) liga
as regras que o `z3randomizer` aplica em qualquer seed. Sem ela, o motor usa
o comportamento US normal.

| Regra z3r | Hook de origem | Onde o C aplica |
|---|---|---|
| nadar em água funda sem flippers / proteção do fake flipper | `$82B468` → `FakeFlipperProtection` (`$82FFC7`, `flipperkill.asm`) | `PlayerHandler_04_Swimming` deixa de retornar cedo sem flippers |
| recuo do bonk sem botas | `$87922C` → `BonkRecoil` (`$A4:8785`, `boots.asm`) | `LinkApplyTileRebound` usa `$16` no `link_actual_vel_z` |
| descer da borda sem botas | `$878966` → `JumpDownLedge` (`$A4:8766`, `boots.asm`) | `LinkHop_HoppingSouthOW` limpa `player_near_pit_state` |
| contador de dano | `$A0:B2DA`/`$A0:B2B9` (`stats.asm`) | `SeedOracle_JpAddDamageCounter`, agora também sob `Z3rRules` |
| contador de transições de tela | `$A4:89EF` → `$A0:B0FC` (`stats.asm`) | `SeedOracle_JpScreenTransition`, agora também sob `Z3rRules` |
| abertura da GT por cristais | `goalitem.asm` `CheckTowerOpen` (`$899B7F`) | `ModSeedImport_GanonsTowerOpen` e evento `GtOpen` |
| Ganon por cristais/goal | `goalitem.asm` `CheckGanonVulnerability` (`$86F2C8`) | `ModSeedImport_GanonVulnerable` |
| HUD/mapa | `newhud.asm`, `overworldmap.asm` | `HUDDungeonItems`, `MapMode`, `CompassMode` |
| itens de comportamento | `hardmode.asm`, `boots.asm` | `ByrnaInvulnerability` aplicado; os demais ficam documentados na tabela |
| áudio/UI | `heartbeep.asm`, `pendantcrystalhud.asm` | `HeartBeep`, `MenuSpeed` |

O `Z3rRules` não muda nada no jogo normal; ele é só mais um bit de
`features0`, aplicado pelo loader de mods.

## 6. Cobertura de locais (métrica de progresso)

```sh
python3 tools/alttpr_seed.py coverage \
  --rom .agents/alttpr/download/seed-xvJo08oxyJ.sfc \
  --mod mods-seeds/xvJo08oxyJ
```

O comando lista cada local com o item da seed e o item que o mod gerado dá,
marca `yes`/`no` por tipo e imprime o resumo. `--summary` mostra só o resumo.

Estado desta rodada para a seed `xvJo08oxyJ`:

```text
coverage: 251 of 251 locations applied
          (chest 167/167, fixed 44/44, prize 10/10, shop 29/29, uncle 1/1)
mod table matches seed: 251/251
item rules in applied locations: approx 3, bigkey 11, compass 10,
          direct 174, map 11, progressive 12, smallkey 27
```

As 29 entradas de loja continuam em `251/251`; agora cada uma leva `door` e
`any_door`, então lojas que compartilham o byte baixo de sala (`0x10F`,
`0x112`) não se sobrescrevem. Nesta seed os inventários dessas portas são
iguais, mas a chave de seleção é `(sala, porta)`.

## 7. Prova dinâmica

O teste não versiona a ROM, o mod gerado nem os itens extraídos da seed. O
`tools/regress.sh` gera os mods em um diretório temporário:

- **baú da casa + tio**: `f4e-item-novo.sav` (baú da sala `0x104` → `0x36`) e
  `tests/replays-seed/seed-uncle.sav` (tio → `0x44`);
- **NPC fixo**: `ref1` com um mod só de `fixed`; o cogumelo vira `0x2A`
  (Red Boomerang), não o `0x29` vanilla;
- **prêmio de dungeon**: `ref1` com um mod só de `prize`; Eastern Palace dá
  `0x20` (Crystal), não o pendant vanilla;
- **loja**: `f4e3-shop.sav` com um mod só de `shop`; a loja de Kakariko
  (sala `0x1f`) compra o item do meio da tabela da seed;
- **porta da loja**: `ref1` com o mesmo mod entra na sala `0x11F` pela porta
  de overworld no frame `48400`; o `previous_overworld_door` é `0x46`, igual
  ao `door` da `ShopTable`, e a compra recebe o item `0x42`;
- **chave de outra dungeon**: `tests/replays-seed/seed-key.sav` abre o baú da
  sala `0xAA` (Eastern Palace) que contém a Small Key de Misery Mire; o evento
  `SeedItem 36 7 0` (item `0x24`, dungeon `7`) confirma o destino;
- **texto/dica**: `ref1` abre a mensagem C `369` no frame `3327` da ROM da
  seed (dica do Ether/Andy). O mod gerado com `--build` instala o
  `kDialogue`; o evento `DialogueText 369 <hash> <tamanho>` é comparado com o
  texto bruto decodificado da ROM. Não é um fprintf de depuração: o evento só
  é emitido quando há um mod de seed ativo;
- **abertura da Ganon's Tower**: um fixture Lua coloca um sprite Waterfall
  sobre o Link e forja quatro cristais. Com `GanonsTowerOpenTarget=4` o trace
  mostra `GtOpen 4 4 1`; com o mesmo estado e alvo `7` (vanilla) não há
  `GtOpen`. O mod é settings-only e o fixture não contém dados da seed no git;
- **inventário inicial**: `f4e-item-novo.sav` cria o arquivo e o
  `verify-proof start` confere 9 campos do template (capacidades convertidas,
  progresso, vida, espada/escudo/armadura);
- **dano do Ganon**: `tests/replays-seed/seed-ganon.lua` forja um Ganon sobre
  o Link e quatro cristais; o modo 4 desta seed emite `GanonCheck 1 4 0`.
  Com sete cristais o mesmo fixture emite `GanonCheck 0 7 0`;
- **FakeBoots**: um A forçado com o bit de botas limpo produz
  `fakeboots run=0` com a flag desligada e `run=1` com ela ligada;
- **QuickSwap**: L sem item no slot L muda `hud_cur_item` de 11 para 2 com a
  flag ligada; sem ela o item continua 11;
- **GenericKeys**: uma chave de outra masmorra incrementa o contador
  compartilhado (`link_num_keys=1`, slot 7 vazio) com a flag ligada e o slot
  por masmorra (`slot7=1`) com ela desligada;
- **NoBGM**: um pedido `music_control=0x13` termina com `music_unk1=20` com a
  flag ligada e `19` com ela desligada;
- **FastFanfare**: com `APUI00` forçado a 1, o receipt de pendant termina em
  `f4566` com a flag ligada e fica preso no id 0x39 sem ela;
- **ItemCounterHUD / MapHUDMode**: o fixture lê o tilemap nativo em `$C700`
  e confere `077/216` na linha 4 e `2/4` de chaves na linha 2;
- **Rupoor**: um baú de Eastern Palace com item `0x59` e
  `rupoor_deduction=10` leva 100 rupees a 90;
- **espada progressiva**: o mesmo baú com `0x5E` leva a espada de 1 para 2 e,
  numa segunda cópia do fixture, de 2 para 3; o `TotalItemCounter` vai a 1;
- **smoke da seed jogada**: `tools/seed_route.py --c-only` parte do snapshot
  de `seed-key.sav`, procura uma sequência curta de input, abre o baú da sala
  `0xAA` de Eastern Palace e confere o item (chave de Misery Mire) contra o
  `seed.json`. O passo é pulado com aviso quando a ROM da seed não existe.

Os comandos de apoio são `tools/alttpr_seed.py import`, `coverage`,
`verify-replay` e `verify-proof` (`npc`, `prize`, `shop`, `shopdoor`, `key`,
`start`, `text`), além de `tools/seed_route.py --c-only` para o smoke de
baús.

## 8. O que falta / ressalvas

1. **Textos com glifos/commands não mapeados**: 174 mensagens são extraídas;
   menus, kanji, ícones de botão/coração e comandos de layout que não têm
   equivalente US seguro ficam no texto original. A placa do pedestal
   (`0xB6`/`0xB7`) está coberta; o texto de cutscene `MSPedestalText` não
   aparece no bloco JP e continua no asset US. Não há tentativa de importar
   `kDialogueFont` do z3r; os textos convertidos usam o alfabeto US.
2. **Portas de loja**: a chave `(sala, porta)` está implementada e as lojas
   desta seed carregam a porta, mas os inventários das portas que dividem
   `0x10F`/`0x112` são iguais nesta seed; não há prova dinâmica que distinga
   duas portas com conteúdos diferentes.
3. **Flags de comportamento de `$B0:8000`**: as sete flags que esta seed
   deixa em zero (`ItemCounterHUD`, `MapHUDMode`, `NoBGM`, `FastFanfare`,
   `FakeBoots`, `QuickSwap`, `GenericKeys`) e os modos 6..8/10..11 de
   `GanonVulnerableMode` estão implementados e provados com settings forjados.
   `InvertedMode` tem a primeira fatia em `RANDOMIZER-INVERTIDO.md`
   (início e pérola/coelho); tilemaps, entradas, Agahnim/torre e o resto
   continuam pendentes. Duas ressalvas de fidelidade: o HUD desta engine não tem a
   mesma célula do goal indicator do z3r, então os contadores usam uma faixa
   livre do tilemap nativo; e `TurnInGoalItems=0` não dispara a transição
   automática de ending do z3r.
4. **Regras z3r além do subconjunto provado**: clocks, RNG pool, itens
   programáveis, contadores de bonk/save/quit e o restante de `stats.asm`
   continuam pendentes.
5. **Colisão de ids com a F4e.5** (`0x80..0x9F`): documentada; ainda não há
   tabela `id z3r -> id F4e.5`.
6. **Itens soltos/NPCs fora das tabelas**: se aparecerem em dados de sprite
   de sala/overworld, o extrator precisa lê-los pela estrutura da sala, como o
   `import-rom` já faz para salas de hack.
7. **Door shuffle/entrance randomization**: `GameType` desta seed é `0x04`
   (só itens), então não há portas embaralhadas. Outro `GameType` com o bit
   `0x02` exigiria importar a tabela de entradas inteira.

## 9. Jogar a seed no motor em C

Pré-requisitos: `zelda3.sfc` (US) na raiz do repositório, a ROM da seed e,
para o build do diálogo, a pasta `.modkit/` (o modkit a cria na primeira vez).

1. Gere o mod, incluindo o template de SRAM, os 251 locais e o diálogo:

   ```sh
   python3 tools/alttpr_seed.py import \
     --rom .agents/alttpr/download/seed-xvJo08oxyJ.sfc \
     --json .agents/alttpr/download/seed-xvJo08oxyJ.json \
     --build -o mods-seeds/xvJo08oxyJ
   ```

   Sem `--build`, rode depois
   `python3 tools/modkit.py build mods-seeds/xvJo08oxyJ`.

2. O `mod.json` gerado já aponta `seed_data` e `asset_overrides` para
   `build/kDialogue.bin`. Copie a pasta para o diretório de mods:

   ```sh
   mkdir -p mods
   cp -a mods-seeds/xvJo08oxyJ mods/
   ```

3. Rode o motor com esse diretório:

   ```sh
   ./zelda3 --mods-dir mods zelda3.sfc
   ```

   `mods-seeds/` e `mods/` são dados do usuário: não entram no git. Se você
   usa outro diretório, ajuste `[Mods] Directory` no `zelda3.ini` ou passe
   `--mods-dir`.

4. O mod liga `Z3rRules` e aplica o template de SRAM no arquivo novo. O jogo
   começa com o inventário da seed, as lojas/itens da seed e os textos
   extraídos. A opção `Z3rRules` pode ser desligada em `zelda3.ini` ou no
   `mod.json` (`"features": {}`) para comparar com o motor US.

O que ainda difere da ROM da seed ao jogar:

- **Textos não convertidos** (menus, kanji e ícones) usam o texto US;
- **`InvertedMode`** além da primeira fatia (início e pérola/coelho)
  descrita em `RANDOMIZER-INVERTIDO.md`; as demais flags de comportamento
  estão aplicadas mesmo quando esta seed as deixa em zero;
- **Entrance/door shuffle** não existe nesta seed; outro `GameType` com portas
  embaralhadas não é suportado;
- **Regras de stats** além de dano/transições (bonk, save/quit, etc.) e
  **itens programáveis/RNG pool** continuam no lado z3r.
