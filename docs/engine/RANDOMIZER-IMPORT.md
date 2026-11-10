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
preço, máximo, substituto). O importador lista as entradas até `0xFF` e
cadastra cada loja no mesmo `ModShop` da F4e.3b, com item, preço e quantidade.
Lojas que compartilham o mesmo byte baixo de sala (a seed usa `0x12` e `0x0f`
em mais de uma porta) são cadastradas pela última entrada; o motor US não tem
a variável `PreviousOverworldDoor` do z3r para separar as portas.

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
progressivos, abertura da Ganon's Tower, etc. O `seed.json` guarda as
principais em `settings`; o estado inicial vem do template de SRAM e as
regras de comportamento vêm da opção `Z3rRules` (seção 5).

### 1.7 Textos e dicas

Os textos do z3r não são tabelas de itens. Na ROM da seed eles passam pelo
renderizador VWF e pelos buffers do `z3randomizer`; não são um bloco fixo que
o importador possa copiar como os baús. O caminho previsto é o mecanismo de
diálogo do modkit (`kDialogue`/`kDialogueFont`) com a tabela
`SeedOracle_JpMessageId` alinhando os ids US→JP. Isso **não foi implementado
nesta rodada**; ver a seção 8.

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
`save_template` (0x500 bytes em hex) e `locations`. Cada local tem `id`,
`type`, `name`, `item` (id z3r) e `item_name`; baús acrescentam `room`,
`index`, `big` e `record`; lojas acrescentam `shop`, `slot`, `price` e `room`.

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
| `SpritePrep_Shopkeeper` (`sprite_main.c`) | `ModShop` | lojas da seed com preço/item |
| `NameFile_DoTheNaming` (`select_file.c`) | `ModSeedImport_InitNewSave` | template de SRAM do arquivo novo |
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
coverage: 248 of 248 locations applied
          (chest 167/167, fixed 44/44, prize 10/10, shop 26/26, uncle 1/1)
mod table matches seed: 248/248
item rules in applied locations: approx 3, bigkey 11, compass 10,
          direct 174, map 11, progressive 12, smallkey 27
```

A ressalva é a loja: as 26 entradas entram no `seed.json` e no `ModShop`, mas
lojas que compartilham o byte baixo de sala (`0x12`, `0x0f`) são cadastradas
pela última porta; o motor US ainda não tem o `PreviousOverworldDoor` para
escolher a porta exata.

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
- **chave de outra dungeon**: `tests/replays-seed/seed-key.sav` abre o baú da
  sala `0xAA` (Eastern Palace) que contém a Small Key de Misery Mire; o
  `seed-dungeon target=7` confirma o destino;
- **inventário inicial**: `f4e-item-novo.sav` cria o arquivo e o
  `verify-proof start` confere 9 campos do template (capacidades convertidas,
  progresso, vida, espada/escudo/armadura).

Os comandos de apoio são `tools/alttpr_seed.py import`, `coverage`,
`verify-replay` e `verify-proof`.

## 8. O que falta / ressalvas

1. **Textos e dicas** do z3r: o modkit já sabe aplicar `kDialogue` e
   `kDialogueFont`, e `SeedOracle_JpMessageId` alinha US→JP, mas a extração do
   blob VWF da ROM da seed não foi feita. Textos de itens novos também ficam
   pendentes.
2. **Lojas com portas compartilhadas**: falta modelar
   `PreviousOverworldDoor` (`$7F5099`) para separar as lojas de mesmo byte
   baixo de sala.
3. **Lojas com portas compartilhadas**: falta modelar
   `PreviousOverworldDoor` (`$7F5099`) para separar as lojas de mesmo byte
   baixo de sala.
4. **Regras z3r além do subconjunto provado**: clocks, RNG pool, itens
   programáveis, contadores de bonk/save/quit e o restante de `stats.asm`
   continuam pendentes.
5. **Colisão de ids com a F4e.5** (`0x80..0x9F`): documentada; ainda não há
   tabela `id z3r -> id F4e.5`.
6. **Itens soltos/NPCs fora das tabelas**: se aparecerem em dados de sprite
   de sala/overworld, o extrator precisa lê-los pela estrutura da sala, como o
   `import-rom` já faz para salas de hack.
