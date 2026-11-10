# Mods

Um mod é uma pasta com `mod.json` dentro do diretório de mods (por padrão
`mods/`). Ele pode substituir assets inteiros, remendar bytes de assets,
trocar o sprite do Link e rodar scripts Lua sem recompilar o jogo.

O carregador fica em `src/mod/mods.c`. A tabela de nomes de assets
(`kAssetNames`, em `src/mod/asset_names.c`) é gerada a partir de
`src/assets.h`:

```sh
make asset-names
```

Rode esse comando de novo se `src/assets.h` mudar.

## Manifesto

`mods/<pasta>/mod.json`:

```json
{
  "id": "exemplo",
  "name": "Exemplo",
  "version": "1.0.0",
  "api": 1,
  "enabled": true,
  "priority": 0,
  "depends": [],
  "features": { "TurnWhileDashing": true, "ItemSwitchLR": true },
  "link_sprite": "link.zspr",
  "asset_overrides": { "kDungeonRoomChests": "assets/chests.bin" },
  "table_overrides": { "kHudItemText": "build/kHudItemText.bin" },
  "patches": ["patches/chests.json"],
  "script": "main.lua"
}
```

Só `id` e `api` são obrigatórios. `api` deve ser `1`.

| Campo | Significado |
|---|---|
| `id` | Identificador único do mod. Não pode conter `/` nem `\`. |
| `name`, `version` | Informação livre. |
| `api` | Versão da API de mods. Hoje só existe a `1`. |
| `enabled` | Se `false`, o mod é ignorado (padrão: `true`). |
| `priority` | Inteiro; menor carrega primeiro (padrão: `0`). |
| `depends` | Lista de ids de mods que precisam existir e estar ativos. |
| `features` | Objeto `nome_da_opção -> booleano` que sobrepõe o `zelda3.ini` enquanto o mod estiver ativo. Veja "Features". |
| `permissions` | Lista de permissões. Hoje só existe `state_write`. Uma permissão desconhecida recusa o mod; sem o campo o mod não tem nenhuma. |
| `link_sprite` | Caminho para um arquivo ZSPR. |
| `asset_overrides` | Objeto `nome_do_asset -> arquivo` ou `nome_do_asset -> { "file": ..., "allow_resize": ... }`. |
| `table_overrides` | Objeto `nome_da_tabela -> arquivo`, sempre com o tamanho exato. Veja "Tabelas do jogo". |
| `patches` | Lista de arquivos JSON de patches. |
| `script` | Caminho para um script Lua executado quando o mod carrega. |
| `seed_data` | JSON gerado por `tools/alttpr_seed.py import` (veja `RANDOMIZER-IMPORT.md`). |

### Caminhos

Todos os caminhos do manifesto (`link_sprite`, `script`, valores de
`asset_overrides` e entradas de `patches`) são relativos à pasta do mod. O
loader recusa e desativa o mod quando o caminho é:

- vazio;
- absoluto, incluindo `/...`, `C:\...`, `C:/...` e `\\servidor\...`;
- contém qualquer componente `..`, mesmo no meio (`a/../b`).

A validação fica em `ModResolvePath()` (`src/mod/mods.c`) e deve ser reusada
por features futuras que abram arquivos de um mod.

### Nomes de assets

Use os nomes dos macros de `src/assets.h`, sem o sufixo `_SIZE`. Por exemplo:

- `kDungeonRoomChests`
- `kLinkGraphics`
- `kDialogue`
- `kPalette_ArmorAndGloves`

Assets "packed" (como `kDialogue`, `kSprGfx` e `kBgGfx`) também são aceitos
pelo nome do macro, sem `(idx)`.

## Ordem de carga

1. Todos os manifestos válidos são lidos.
2. Mods com `"enabled": false` são ignorados.
3. Um mod cujo `depends` não exista (ou esteja desativado) é recusado com uma
   mensagem clara.
4. Os mods restantes são ordenados por `priority` crescente; empate é
   desfeito por `id` crescente.
5. Dentro de um mesmo mod, a ordem é: overrides de assets, overrides de
   tabelas, patches, sprite. As `features` só entram no commit, depois que
   todo o resto do mod foi validado.
6. Quem carrega depois vence. Um override posterior substitui o asset inteiro;
   um patch posterior enxerga o conteúdo já modificado pelos mods anteriores.
   Overrides de tabela também são aplicados por último dentro do mod que os
   declara e não são vistos por patches (que só mexem em assets).

Cada mod carregado ou recusado imprime uma linha em `stderr`, por exemplo:

```
mods: loaded 'bau-da-casa' (priority 0)
mods: refused 'exemplo': missing dependency 'base'
```

## Features

O campo opcional `"features"` liga opções de jogabilidade e correção sem
editar o `zelda3.ini`. As chaves são exatamente os mesmos nomes usados no ini
e os valores são booleanos; `false` desliga:

```json
{
  "id": "opcoes",
  "api": 1,
  "features": {
    "TurnWhileDashing": true,
    "ItemSwitchLR": true,
    "DimFlashes": false
  }
}
```

Nomes aceitos:

- `ItemSwitchLR`
- `ItemSwitchLRLimit`
- `TurnWhileDashing`
- `MirrorToDarkworld`
- `CollectItemsWithSword`
- `BreakPotsWithSword`
- `DisableLowHealthBeep`
- `SkipIntroOnKeypress`
- `ShowMaxItemsInYellow`
- `MoreActiveBombs`
- `CarryMoreRupees`
- `MiscBugFixes`
- `GameChangingBugFixes`
- `CancelBirdTravel`
- `NewStyleInventory`
- `HorizontalMagicBar`
- `MenuTimer`
- `ReduxHud`
- `Z3rRules`
- `DimFlashes`

`DimFlashes` fica em `[Graphics]` no ini, mas é um bit do mesmo
`features0`; por isso também é aceito no mod. Ficaram de fora
`ExtendedAspectRatio` e os bits de vídeo que ele liga
(`ExtendScreen64`/`WidescreenVisualFixes`), as opções de áudio/MSU, as teclas
e os caminhos: nenhum deles é opção de jogabilidade/correção do `features0`,
e alguns dependem de estado de janela, placa de som ou do teclado.

### Menu de 24 itens (`NewStyleInventory`)

Liga o layout de inventário de 24 espaços que o upstream deixou desligado em
tempo de compilação (`kNewStyleInventory`): 6 colunas por 4 linhas, garrafas
em slots separados e a pá como slot permanente. Sem a opção, o jogo usa o
layout original de 20 espaços e o comportamento e a RAM são idênticos ao
assembly.

Funciona abrir e fechar o menu, navegar pelos 24 slots, equipar itens e usar
as garrafas nos slots próprios; a reordenação com Y+direção continua sendo a
da opção `ItemSwitchLR`. A opção desliga sozinha a comparação lado a lado com
a ROM, como as demais features.

O menu usa o layout largo do Redux: caixa única, título, grade de 6×4 na
posição do Redux, ícones de equipamento/abilidade na linha de baixo e a caixa
do nome do item selecionado na base. Com `kSprGfx`/`kBgGfx` e as tabelas de HUD
do Redux importados, o resultado bate com o menu do hack. Sem esses assets, os
espaços novos usam os ícones e textos originais, mas o título e as bordas do
layout largo são tiles dos packs de HUD do Redux e só ficam corretos quando
eles são importados. Ao abrir o menu o engine
recarrega os packs de HUD 0x6a/0x6b dos assets ativos, porque o Redux usa esses
packs para o título e as bordas do menu (no jogo original eles também são a
fonte do diálogo).

As opções `HorizontalMagicBar`, `MenuTimer` e `ReduxHud`, descritas abaixo,
completam o pacote de HUD do Redux, mas são independentes e continuam
desligadas por padrão.

### Barra de magia horizontal (`HorizontalMagicBar`)

Troca o medidor vertical de magia por uma barra horizontal de dez tiles, com
oito segmentos de carga, no estilo do Redux. Com a melhoria de meia magia a
barra usa a paleta azul. Sem a opção, o medidor vertical original é desenhado
exatamente como antes.

### Cronômetro no menu (`MenuTimer`)

Mostra `HH:MM` no menu de pausa, no painel direito do layout de 24. O relógio
conta o tempo de jogo em minutos (um minuto a cada 3600 frames), guarda o
valor em `$7EF339` e dá a volta em 100 horas, como o timer do Redux. A
gravação usa esse espaço reservado do save, mas o formato do arquivo não muda;
com a opção desligada o valor não é lido nem escrito.

### HUD de jogo do Redux (`ReduxHud`)

Move o HUD de jogo para o layout do Redux, sem mexer no menu de pausa:

- os corações vão para o canto superior esquerdo, dez por linha;
- a barra de magia vira a barra horizontal do Redux, logo abaixo dos corações,
  com os oito segmentos e a paleta azul da meia magia;
- os contadores de rúpias, bombas, flechas e chaves andam seis tiles para a
  direita (rúpias em `x=14..17`, bombas em `18..19`, flechas em `21..22` e
  chaves em `24`);
- a caixa do item equipado vai para o canto superior direito (`x=27..30`,
  `y=0..3`, ícone em `x=28..29`, `y=1..2`);
- o rótulo `-LIFE-` do HUD original desaparece, como no Redux.

A opção é independente de `NewStyleInventory`: ela só troca o HUD de jogo, e
o menu continua no layout de 20 ou 24 itens escolhido pela outra opção.
`ReduxHud` também liga a barra horizontal mesmo com `HorizontalMagicBar`
desligada; as duas juntas usam a posição do Redux. Sem a opção, o HUD é
desenhado exatamente como antes.

### Regras do z3randomizer (`Z3rRules`)

Liga o subconjunto de regras do `z3randomizer` que é igual em toda seed:
nadar em água funda sem flippers, recuo do bonk e descida de borda sem botas,
contadores de dano e de transições de tela, e as regras dos itens Max
Bombs/Arrows, Half/Quarter Magic e Rupoor. É a opção que o mod gerado pelo
`alttpr_seed.py` liga sozinho. Sem ela, nada muda; os endereços dos hooks z3r
estão em `RANDOMIZER-IMPORT.md`. O estado inicial da seed (template de SRAM)
não depende dessa opção: ele é aplicado pelo próprio mod.

**Save (`MenuTimer` e `ReduxHud`):** as opções não mudam o formato do save. O bloco de SRAM continua sendo
`g_ram+0xF000..0xF4FF`; o estado só do HUD (`hud_cur_item`,
`hud_inventory_order` e `hud_cur_item_x/l/r`) não é salvo e o item equipado é
recalculado ao carregar. Um save gravado com a opção ligada abre com ela
desligada e vice-versa. Um teste headless com o mesmo roteiro nos dois layouts
comparou os 66 escalares do bloco de save e não encontrou diferença.

**Precedência:** enquanto o mod está ativo, os valores dele valem por cima do
`zelda3.ini`. Entre mods, vale a mesma ordem do carregador: `priority`
crescente, então quem carrega depois vence (a maior prioridade numérica vence;
empate é desfeito por `id` crescente, então o maior id vence). Uma opção
definida como `false` também conta: ela desliga um valor que o ini ou um mod
anterior tinha ligado.

Cada opção efetivamente alterada por um mod e cada conflito entre mods é
registrado em `stderr`, com os nomes dos mods:

```
mods: feature 'TurnWhileDashing' = on ('mod-a', priority 0)
mods: feature conflict 'TurnWhileDashing': 'mod-b' (priority 10) overrides 'mod-a' (priority 0): on -> off
```

O efeito vale também no headless e durante replays: o valor do mod prevalece
sobre o `features0` que veio no snapshot do replay. Sem mods, ou com mods sem
`"features"`, o resultado é exatamente o do ini.

Nome desconhecido ou valor que não seja booleano recusa o mod:

```
mods: refused 'x': unknown feature 'Fly' (supported: ItemSwitchLR, ItemSwitchLRLimit, ...)
mods: refused 'x': 'features.TurnWhileDashing' must be a boolean
```

A carga continua transacional: se qualquer outra parte do mod falhar, nenhuma
feature dele é aplicada. O exemplo `mods-examples/opcoes-redux/` liga o
conjunto que o mapa do Redux lista como classe A.

## Overrides

`asset_overrides` troca o asset inteiro pelo conteúdo do arquivo. A forma
curta é:

```json
"asset_overrides": {
  "kDungeonRoomChests": "assets/chests.bin"
}
```

Por padrão o arquivo precisa ter **exatamente** o tamanho original do asset.
Se tiver outro tamanho, o mod é recusado e a mensagem mostra os dois tamanhos:

```
mods: refused 'bad-size': asset 'kDungeonRoomChests' override 'assets/short.bin' has 503 bytes, expected 504 (allow_resize is false)
```

Isso protege as tabelas de tamanho fixo do jogo. Para trocar o tamanho de
propósito, use a forma objeto:

```json
"asset_overrides": {
  "kDungeonRoomChests": {
    "file": "assets/chests.bin",
    "allow_resize": true
  }
}
```

`allow_resize` é opcional e vale `false` por padrão. Redimensionar só é seguro
em assets que o jogo lê pelo tamanho ou por índices válidos; assets usados como
tabelas de tamanho fixo continuam sendo responsabilidade do autor do mod.

O mod loader copia os bytes para um buffer próprio antes de publicar o asset.
A checagem de tamanho usa o tamanho original de `zelda3_assets.dat`, antes de
qualquer mod.

## Tabelas do jogo

Algumas tabelas de tamanho fixo não são assets: são arrays C que o port usa
diretamente (por exemplo, os nomes de itens do HUD). Um mod pode trocá-las
com `table_overrides`:

```json
"table_overrides": {
  "kHudItemText": "build/kHudItemText.bin"
}
```

O arquivo precisa ter **exatamente** o tamanho da tabela. Não existe
`allow_resize`: um tamanho diferente recusa o mod e mostra os dois tamanhos.
Nome desconhecido também recusa:

```
mods: refused 'exemplo': unknown table 'kNomeErrado'
mods: refused 'exemplo': table 'kHudItemText' override 'build/x.bin' has 2 bytes, expected 640
```

O registro fica em `src/mod/game_tables.def`, com três formas:

```text
MOD_TABLE(kHudItemText, 320, 0x8DF1C9)
MOD_TABLE_BYTES(kAlgumaTabelaDeBytes, 40, 0x8DF000)
MOD_TABLE_DERIVED(kGameOverTextTab1, 10)
```

`MOD_TABLE` é o formato histórico: nome do símbolo C, quantidade de `uint16`
e endereço SNES na ROM US. `MOD_TABLE_BYTES` é a mesma ideia para uma tabela
`uint8`. `MOD_TABLE_DERIVED` expõe uma tabela de bytes que **não** tem um
endereço fixo na ROM traduzida; o `import-rom` a monta decodificando o código
da ROM (hoje só o letreiro de fim de jogo). O arquivo é incluído por
`src/mod/game_tables.c` e lido por `tools/modkit.py`, então o Python não
mantém uma segunda lista. As tabelas de palavras são `uint16` e continuam
little-endian como na ROM.

As tabelas de texto/tilemap do HUD e do menu de itens que a tradução PT-BR
muda em `src/hud.c` são:

- `kHudEquipmentDungeonItemText`
- `kHudItemText`
- `kHudBottlesItemText`
- `kHudMushroomItemText`
- `kHudFluteItemText`
- `kHudMirrorItemText`
- `kHudBowItemText`
- `kHudGlovesText`
- `kHudAbilityText`

### Letreiro de fim de jogo

A tela de morte é guiada por estas quatro tabelas de `src/mod/game_over_text.c`:

| Tabela | Bytes | Conteúdo |
|---|---|---|
| `kGameOverTextLetterCount` | 1 | `N`, entre 2 e 10. Padrão 8 (US). |
| `kGameOverTextTab1` | 10 | posições intermediárias da varredura; só as `N` primeiras são usadas. |
| `kGameOverTextTab2` | 10 | posições finais; só as `N` primeiras são usadas. |
| `kGameOverTextTiles` | 40 | 4 bytes por letra: tile de cima, atributo, tile de baixo, atributo. |

Os defaults são os bytes da ROM US: `N = 8`, Tab1
`40 50 60 70 88 98 a8 40`, Tab2 `58 60 68 70 88 90 98 a0` e os 16 tiles
US com atributo `3C` nas duas metades. Com `N = 8` o código C executa
exatamente o mesmo laço da ROM US. As entradas além de `N` ficam zeradas
nos arquivos importados.

Um override de `kGameOverTextLetterCount` fora de `2..10` é recusado na
carga com mensagem clara. O loader também mantém o índice interno
`hookshot_effect_index` em 6 para `N = 8` e `N = 9` (como as ROMs US e PT) e
o limita a `N - 2` para contagens menores, para não sair dos vetores de
ancilla.

Na ROM PT-BR, `N = 9` (F I M D O J O G O), Tab1 em `$88F5B4` =
`30 40 50 68 78 90 a0 b0 30`, Tab2 em `$A0:8065` =
`50 58 60 70 78 88 90 98 a0` e a tabela de tiles em `$A0:8041`:
`40 3C 50 3C 41 3C 41 BC 42 3C 52 3C 43 3C 53 3C 44 3C 54 3C 45 3C 55 3C
44 3C 54 3C 46 3C 56 3C 44 3C 54 3C`. O `I` usa o tile `41` espelhado
(atributo `BC`) embaixo.

Sem override os valores são os mesmos do código; o jogo sem mods continua
idêntico. Qualquer override de tabela (mesmo com os mesmos bytes) liga
`g_mods_change_content`, como os assets, e a comparação lado a lado imprime
`compare: off`.

## Patches

Um arquivo de patch é um array JSON:

```json
[
  {
    "asset": "kDungeonRoomChests",
    "offset": 411,
    "expect": "04 01 12",
    "bytes": "04 01 02"
  }
]
```

| Campo | Significado |
|---|---|
| `asset` | Nome do asset (obrigatório). |
| `offset` | Deslocamento em bytes a partir do início do asset (obrigatório). |
| `bytes` | Bytes novos, em hexadecimal, com ou sem espaços (obrigatório). |
| `expect` | Bytes esperados no asset antes de aplicar (opcional). |

Se `expect` estiver presente e não bater, o patch inteiro falha e o mod é
recusado. O mod é aplicado de forma transacional: nenhum override ou patch
daquele mod fica no jogo quando uma entrada falha. Exemplo de erro:

```
mods: refused 'bau-da-casa': patch 'patches/bau.json': entry 0: asset 'kDungeonRoomChests' offset 411: expected [04 01 12], found [04 01 24]
```

Use `expect` sempre que puder: ele protege contra uma versão diferente de
`zelda3_assets.dat`.

## Sprite do Link

`"link_sprite": "link.zspr"` carrega um ZSPR no mesmo formato aceito por
`LinkGraphics` no `zelda3.ini`. O sprite do mod é carregado depois do sprite
do ini, então ele vence em caso de conflito. Os pixels vão para
`kLinkGraphics` e a paleta para `kPalette_ArmorAndGloves`/`kGlovesColor`.

## Scripts Lua

O campo `"script"` do manifesto carrega um arquivo Lua 5.4 (por exemplo
`"script": "main.lua"`). Cada mod tem o seu próprio `lua_State`, então scripts
de mods diferentes não compartilham globais nem podem se derrubar.

A cola com Lua fica em `src/mod/lua_script.c` (estado, sandbox e eventos) e
`src/mod/lua_api.c` (`zelda.link`, `zelda.item`, `zelda.ram` e `zelda.state`),
para poder ser trocada por uma cópia vendorizada no futuro.

O build normal usa o Lua 5.4 do sistema (`pkg-config --cflags --libs lua5.4`).
Para compilar sem Lua use `make NO_LUA=1`: o jogo continua funcionando, mas um
mod com `script` apenas avisa que o scripting está desativado e carrega o
restante normalmente. No Visual Studio os arquivos de Lua ficam sob o define
`ZELDA3_LUA`, desligado por padrão.

### Sandbox

Cada script recebe somente as bibliotecas `base`, `string`, `table`, `math` e
`utf8`. Não existem `io`, `os`, `package`, `debug`, `dofile`, `loadfile` nem
`require`. A função `load` aceita apenas texto (modo `"t"`); bytecode é
recusado. `print` e `zelda.log` escrevem no `stderr` com o prefixo `[<id>]`.

### Limites

Cada chamada ao Lua (a carga do script e cada handler de evento) tem um
orçamento de instruções. O padrão é 10.000.000 de instruções e pode ser mudado
em `[Mods]`:

```ini
[Mods]
LuaInstructionLimit = 10000000
LuaMemoryMB = 32
```

O limite de memória vale por mod: cada estado Lua tem um alocador próprio que
conta os bytes alocados. O padrão é 32 MiB. Estourar qualquer um dos limites
gera um erro Lua com traceback, desativa o mod e deixa o jogo continuar. O
`lua_close` do próprio estado roda sem o hook de instruções e sem o teto de
memória, para a desativação não falhar no meio da limpeza.

### Eventos

Os nomes são os mesmos de `src/mod/events.h` (`kModEventNames`):

```lua
zelda.on("RoomEnter", function(ev)
  zelda.log(string.format("sala %d, antes %d", ev.room, ev.prev))
end)
```

Todo evento tem `ev.a`, `ev.b` e `ev.c`. Além disso:

| Evento | Aliases |
|---|---|
| `RoomEnter` | `ev.room`, `ev.prev` |
| `AreaEnter` | `ev.area`, `ev.prev` |
| `ItemReceive` | `ev.item`, `ev.chest`, `ev.method` |
| `ChestOpen` | `ev.room`, `ev.chest` |
| `DialogueOpen`, `DialogueClose` | `ev.msg` |
| `LinkDamage` | `ev.damage`, `ev.health_before` |
| `SpriteSpawn`, `SpriteKilled` | `ev.slot`, `ev.type` |
| `ModuleChange` | `ev.module`, `ev.prev` |
| `FileLoad`, `FileCreate` | `ev.slot` |
| `DungeonEnter` | `ev.palace`, `ev.entrance` |
| `DungeonExit` | `ev.palace` |
| `Quit` | `ev.saved` |
| `OverworldTransition` | `ev.area`, `ev.direction` |
| `DropSpawned` | `ev.item`, `ev.slot` |
| `PlayerDeath` | `ev.a` = 1 ou 2 (jogador) |
| `PlayerRespawn` | `ev.a` = 1 ou 2, `ev.b` = vida depois do respawn |
| `FrameStart`, `FrameEnd` | — (rodam uma vez por frame; `FrameStart` aceita `state_write`) |
| `FrameDraw` | `ev.a` = metade (0/1), `ev.b` = número de metades |

Os eventos de save usam o slot **0-based** (0, 1 e 2); internamente o jogo
representa o mesmo slot em `srm_var1` como `slot * 2 + 2`. `FileLoad` dispara
na entrada de `Module05_LoadFile`, com
o save já copiado para a RAM; por isso ele também roda no "save and continue"
depois de uma morte, como o `OnFileLoad` do `z3randomizer`. `FileCreate`
dispara quando o arquivo novo terminou de ser escrito na tela de nome.
`DungeonEnter` traz `cur_palace_index_x2` e o índice de entrada em
`which_entrance`. `DungeonExit` traz o palácio antes de o jogo limpar
`cur_palace_index_x2` para `0xff` (esse valor aparece quando a rotina é
chamada sem um palácio ativo, por exemplo ao voltar de uma área especial).
`Quit` dispara no ramo de "salvar e sair" do menu de morte e do menu de pausa;
`ev.saved` é 1 quando `SaveGameFile` rodou e 0 quando não havia progresso
para salvar. `OverworldTransition` dispara na troca de tela por borda, depois
do filtro `OverworldTransitionTarget`; `ev.direction` são os bits de direção
usados pelo jogo.

`DropSpawned` dispara no instante em que um drop de inimigo, arbusto ou vaso
é criado, antes de o sprite ser desenhado. `ev.item` é o tipo do item (por
exemplo `0xd8` para um coração pequeno) e `ev.slot` é o slot de sprite que
recebeu o drop; um handler pode zerar `sprite_state` desse slot para remover
o item sem que ele apareça por um frame. Drops de inimigo usam
`PrepareEnemyDrop`, que preserva a marcação de inimigo morto mesmo quando o
handler cancela o item; arbustos, potes e demais segredos de terreno usam
`Sprite_SpawnSecret`.

`ItemReceive` é um evento-filtro: se o handler retornar um inteiro, ele vira o
item recebido (0 a 255). Os handlers seguintes e o `--mod-trace` veem o valor
já trocado. Retornar `nil` mantém o item original.

### Filtros e decisões

Além dos eventos, um script pode trocar valores que o jogo calcula e responder
perguntas que ele faz antes de agir. Os nomes são os mesmos de
`src/mod/hooks.h` (tabela `kModHooks`):

```lua
zelda.filter("SwordBeamDamage", function(valor, arg)
  return 9
end)

zelda.decide("MirrorAllowed", function(padrao, arg)
  if arg == 0x40 and zelda.state.get("player_is_indoors") == 0 then
    return false
  end
  return padrao
end)
```

`filter` recebe o valor atual e o argumento e devolve o novo valor. `decide`
recebe o padrão (`true`/`false`) e o argumento e devolve a decisão. Retornar
`nil` mantém o valor recebido. Um filtro precisa devolver inteiro dentro da
faixa da tabela; uma decisão precisa devolver booleano. Tipo errado, valor
fora da faixa, erro de script ou estouro do limite de instruções desativam o
mod, como qualquer outro handler.

| Nome | Tipo | Valor/padrão | Argumento | O que muda |
|---|---|---|---|---|
| `SwordBeamDamage` | filtro | classe de dano do beam (1 = L1) | slot do sprite atingido | classe de dano aplicada pelo sword beam (2 = espada mestra/L2) |
| `FairyReviveHealth` | filtro | 56 | vida máxima | quanto a fada enche ao reviver |
| `MaxBombs` | filtro | tabela por nível | nível de bomba | máximo de bombas |
| `MaxArrows` | filtro | tabela por nível | nível de flecha | máximo de flechas |
| `ReceiveItemAlternate` | filtro | item alternativo (255 = nenhum) | id do item no baú | troca a túnica pelo item alternativo |
| `BirdTravelDestination` | filtro | 0..7 | byte reservado de `birdtravel_var1` | destino do pássaro/flauta |
| `OverworldTransitionTarget` | filtro | tela calculada | direção (bits de direção) | tela de destino da transição do overworld |
| `LinkDamage` | filtro | dano calculado (8 no buraco) | fonte: 0 sprite, 1 ancilla, 2 buraco, 3 ambiente | dano aplicado à vida do Link |
| `PendantDropSpecialCase` | decisão | `true` | índice do item que cai | usa o caso especial do palácio 20 |
| `SwordTurnWhileCharging` | decisão | `false` | direção atual | permite virar durante o charge |
| `BlockAlreadyPushed` | decisão | `true` | índice do bloco | marca o bloco como já empurrado |
| `PikitStealShield` | decisão | `true` | slot do sprite | o Pikit pode roubar o escudo |
| `MirrorAllowed` | decisão | regra original do jogo | mundo atual (0 ou `0x40`) | permite usar o espelho |
| `SwordBeamSpawn` | decisão | `true` | direção atual | cria o raio da espada |
| `PotBreakSwordLevel` | decisão | `true`, exceto L1 | nível da espada (`link_sword_type`) | a espada atual quebra o vaso |

`OverworldTransitionTarget` pode **vetar** a transição: se o filtro devolver a
tela atual (a mesma de `overworld_screen_index` antes da troca), o jogo não
inicia a rolagem nem emite `OverworldTransition`/`AreaEnter`; o mod fica
responsável por reposicionar o Link para dentro da borda. Sem assinante do
filtro a condição nunca é tomada, então o jogo sem mods não muda. É o que o
quebra-cabeça da praia do A Link to the Islands usa para devolver o Link à
mesma tela.

`SwordBeamDamage` recebe a **classe de dano** do raio, não pontos de vida. O
raio é `ancilla_type == 0x0c` (criado por `AddSwordBeam`); o tiro da vara de
gelo (`0x0b`) não passa pelo filtro. A classe 1 é a da espada L1; a classe 2 é
a da espada mestra (L2) no golpe normal, que é o valor usado pelo Redux.

`LinkDamage` é aplicado no ponto em que o jogo grava `link_health_current`,
depois de o dano de sprite, ancilla ou ambiente já ter passado por armadura e
recuo, e cobre também o dano direto de buraco. Devolver 0 deixa a vida intacta
(sem morte); devolver um valor maior que a vida atual mata normalmente. O
evento `LinkDamage` seguinte vê o dano já filtrado em `ev.damage`.

`PotBreakSwordLevel` só é consultado quando a opção `BreakPotsWithSword` está
ligada; o padrão reproduz o jogo original (bloqueia só L1). Um mod pode exigir
L4 devolvendo `level >= 4`.

**Ordem de prioridade:** os assinantes rodam do menor `priority` para o maior.
Empate mantém a ordem de carga dos mods (`priority` crescente e depois `id`
crescente). Vários handlers do mesmo mod rodam na ordem em que foram
registrados.

Sem nenhum assinante, o macro do jogo devolve o valor/padrão original e não
chama o Lua. Assinar qualquer filtro ou decisão liga
`g_mods_change_content`, então a comparação lado a lado com a ROM é desligada
mesmo antes de o gancho disparar.

Com `--mod-trace`, só as chamadas em que o resultado final mudou aparecem,
uma linha por chamada:

```
hook 236 Decide MirrorAllowed 1 -> 0 arg 64
hook 1010 Filter OverworldTransitionTarget 18 -> 17 arg 2
```

### Substituir rotinas de sprite

Um mod pode trocar a rotina de **um tipo de sprite** por uma função Lua. A
rotina do jogo continua disponível em `original(slot)` para o mod mudar só
uma parte do comportamento:

```lua
zelda.override("Sprite:4B-Knight", {
  tick = function(slot, original)
    local x = zelda.sprite.get(slot, "x")
    local y = zelda.sprite.get(slot, "y")
    zelda.sprite.set(slot, "x_vel", -12)
    zelda.sprite.set(slot, "y_vel", 0)
    original(slot)  -- desenha, anima, checa colisão e dano
  end
})
```

O `mod.json` precisa declarar a permissão:

```json
{
  "id": "inimigo-novo",
  "api": 1,
  "permissions": ["state_write"],
  "script": "main.lua"
}
```

Sem `state_write`, `zelda.override` e `zelda.sprite.set` geram erro Lua e o
mod é desativado. O `tick` roda no lugar da rotina do jogo para aquele tipo,
uma vez por sprite por frame, e recebe o índice do slot (`0..15`) e a função
`original`. `original(slot)` só é válida durante o `tick`, para o mesmo slot;
chamá-la depois ou para outro slot gera erro.

**Tipo:** a chave é `"Sprite:<tipo>"`. O tipo aceita hexadecimal (`0x4B`,
`$4B` ou `4B`) ou um nome da tabela de nomes de sprite, gerada de
`assets/tables.py:kSpriteNames` em `src/mod/sprite_names.c`:

```lua
zelda.override("Sprite:0x4B", { tick = fn })
zelda.override("Sprite:4B", { tick = fn })
zelda.override("Sprite:4B-Knight", { tick = fn })
zelda.override("Sprite:Knight", { tick = fn })  -- sufixo, se for único
```

Use o nome completo quando houver mais de um tipo com o mesmo sufixo
(`Person`, `Animal`, `BigSpikeBlock` e `???` não são únicos). O tipo é o
mesmo byte de `sprite_type[]`; a tabela tem 243 entradas (`0x00..0xF2`).

**Campos do slot:** `zelda.sprite.get(slot, nome)` lê e
`zelda.sprite.set(slot, nome, valor)` escreve. `get` é sempre permitido;
`set` exige `state_write` e, como `zelda.state.set`, falha durante
`FrameDraw`. Os nomes vêm das variáveis `sprite_*` de `src/variables.h`, sem
o prefixo `sprite_`:

| Grupo | Campos |
|---|---|
| posição | `x`, `y` (16 bits, 0..65535), `x_lo`, `x_hi`, `y_lo`, `y_hi` |
| movimento | `x_vel`, `y_vel`, `z_vel` (com sinal, -128..127), `x_subpixel`, `y_subpixel`, `x_recoil`, `y_recoil`, `z`, `z_subpos`, `wallcoll`, `floor` |
| estado | `state`, `ai_state`, `subtype`, `subtype2`, `A`, `B`, `C`, `D`, `E`, `F`, `G`, `type`, `room` |
| timers/vida | `delay_main`, `delay_aux1`..`delay_aux4`, `hit_timer`, `anim_clock`, `pause`, `stunned`, `health` |
| gráficos/direção | `graphics` (alias `gfx`), `head_dir` (alias `direction`), `oam_flags`, `obj_prio` |
| flags/dano | `flags`, `flags2`..`flags5`, `defl_bits`, `die_action`, `ignore_projectile`, `bump_damage`, `give_damage` |
| auxiliares | `unk1`..`unk5` |

O valor de um campo de 1 byte precisa caber em `0..255` (ou `-128..127`
quando o campo tem sinal). `x`/`y` escrevem `x_lo`/`x_hi` e `y_lo`/`y_hi`
juntos. Nome desconhecido, slot fora de `0..15` e valor fora da faixa geram
erro Lua. O exemplo `mods-examples/inimigo-novo/` usa `x`, `y`, `x_vel`,
`y_vel`, `ai_state`, `delay_main`, `head_dir` e `D`.

**Despacho sem custo:** `SpriteActive_Main` consulta
`g_mod_sprite_override_mask[tipo]`. Sem nenhum override para aquele tipo, a
máscara é zero e o jogo chama `kSpriteActiveRoutines[tipo]` exatamente como
antes, sem chamar o módulo de mods. Com override, a rotina escolhida é a do
mod; se ela não chamar `original()`, a rotina do jogo não roda naquele frame.

**Vários mods no mesmo tipo:** vence o de maior `priority` (empate: o que
carregou depois, como nos assets). `original()` sempre chama a rotina do
jogo, não o override anterior. Se o mod vencedor for desativado, o anterior
volta a valer no frame seguinte.

**Erro e limite:** erro de script, tipo errado em `set`, estouro do limite de
instruções ou memória desativam o mod e removem todos os overrides dele. No
mesmo frame, `SpriteActive_Main` cai na rotina original do tipo, então o
sprite não fica sem `tick`. Com `--mod-trace`, a primeira chamada de cada
slot e uma amostra a cada 32 frames aparecem como:

```
override 2 Sprite 4B-Knight 2 x 368 y 2848
override 96 Sprite 4B-Knight 1 x 384 y 2837
```

Assinar um override liga `g_mods_change_content`, desligando a comparação
lado a lado com a ROM. O replay de feature
`tests/replays-features/f4e-sprite-override.sav` usa o exemplo e confere o
trace e o hash final.

### Estado do Link

```lua
zelda.link.health = 24
zelda.link.max_health = 48
zelda.link.rupees = 100
zelda.link.bombs = 10
zelda.link.arrows = 30
zelda.link.magic = 128
print(zelda.link.x, zelda.link.y)
```

Campos disponíveis: `health`, `max_health`, `rupees`, `x`, `y`, `bombs`,
`arrows` e `magic`. As escritas são validadas: `health` não pode passar do
`max_health` atual, `magic` vai até 128 e `rupees` até 9999. Diminuir
`max_health` abaixo do health atual ajusta o health junto. Nome desconhecido
gera erro Lua.

### Itens

```lua
if zelda.item.get("bow") == 0 then
  zelda.item.set("bow", 1)
end
zelda.item.set("arrows", 30)
```

Nomes aceitos: `bow`, `boomerang`, `hookshot`, `bombs`, `mushroom`/`powder`,
`fire_rod`, `ice_rod`, `bombos`, `ether`, `quake`, `lamp`, `hammer`, `flute`,
`net`/`bug_net`, `book`/`book_of_mudora`, `bottle1`..`bottle4`, `somaria`/
`cane_somaria`, `byrna`/`cane_byrna`, `cape`, `mirror`, `gloves`, `boots`,
`flippers`, `moon_pearl`, `sword`, `shield`, `armor`, `heart_pieces`, `arrows`
e `keys`.

Cada nome mapeia para a variável correspondente em `src/variables.h`. Valores
fora da faixa segura do item geram erro Lua em vez de escrever lixo no save.
Nome desconhecido também é erro.

### Itens novos

Um mod Lua pode registrar itens que não existem no jogo original. O item ganha
um id fora da faixa original (`0x80`..`0x9f`), um espaço no inventário e estado
próprio salvo no arquivo do mod ao lado do save:

```lua
local pena = zelda.item.register {
  name = "pena",
  icon = "pena.png",                 -- 16x16, PNG cinza de 8 bits
  receive_gfx = "pena-recebimento.png",
  text = "PENA",                     -- nome no menu
  message = 0x51,                    -- id de diálogo do recebimento
  max = 1,
  on_receive = function(ev)
    -- ev.item, ev.count, ev.chest, ev.method
  end,
  on_use = function(ev)
    -- ev.item, ev.count; use zelda.link/zelda.state para o efeito
  end,
}
```

`register` devolve o id do item, que pode ser devolvido pelo filtro/evento
`ItemReceive` para trocar o item de um baú ou NPC:

```lua
zelda.on("ItemReceive", function(ev)
  if ev.item == 0x12 and zelda.state.get("dungeon_room_index") == 260 then
    return pena
  end
end)
```

`name` é único, usa 1..32 letras, dígitos, `_` ou `-`, e não pode sombrear um
nome original de `zelda.item.get`. `max` é a quantidade máxima (1..99).
`text` é o nome curto mostrado no menu (até 16 caracteres). `message` é um id
de diálogo existente para o texto de recebimento; para uma mensagem inédita,
use o modkit (`content/dialogue.txt`) para substituir uma mensagem e passe o id
dela. Sem `message`, o item é recebido sem caixa de texto.

**Formato dos PNGs:** `icon` e `receive_gfx` são PNGs de 16×16, 8 bits por
pixel, em escala de cinza. O valor de cada pixel é o índice de cor SNES: `0` é
transparente e `1..15` escolhem a cor na paleta. O ícone usa a paleta do ícone
original cujo espaço foi emprestado; o recebimento usa a paleta 3 dos sprites,
como os recebimentos originais.

**Receber:** o filtro `ItemReceive` troca o item antes de o jogo usá-lo. O
engine toca a animação de recebimento, carrega o gráfico do PNG e mostra a
mensagem configurada. O `on_receive` roda depois de a quantidade ser
incrementada (limitada por `max`).

**Usar:** depois de recebido, o item aparece no menu e pode ser equipado no Y
(ou em X/L/R com `ItemSwitchLR`). Ao apertar o botão, `on_use` é chamado com
acesso ao estado do Link por nome (`zelda.link`, `zelda.state`). O engine não
consome o item sozinho; o script decide se diminui a quantidade com
`zelda.item.set`.

**Menu:** no layout de 24 itens, o item usa o primeiro espaço livre entre os
itens originais. O ícone empresta os quatro tiles do ícone original daquele
espaço e os devolve todo frame, então o menu continua igual sem o mod. Com os
24 espaços ocupados, o item continua equipável pelo HUD, mas não aparece na
grade; o mesmo vale para mais de 24 itens registrados ao mesmo tempo. O layout
antigo de 20 espaços também aceita os itens nos espaços livres.

**Estado e save:** a quantidade fica no arquivo `saves/sram.mod.<slot>.<id>.sav`
do mod, na chave reservada `$item:<name>`. `zelda.save.set` recusa esse prefixo,
mas o script pode ler e escrever a quantidade com `zelda.item.get("pena")` e
`zelda.item.set("pena", 1)`. O bloco de SRAM do jogo não muda; um save sem o
mod, ou com o mod removido, abre normalmente.

**Limites:** o registro só existe com Lua (`make NO_LUA=1` ignora o script);
não há integração automática com prêmios de minigame ou tabelas de baús — o
mod troca o item pelo evento/filtro. Um item novo não tem ícone animado nem
variações de quantidade no menu.

### Lojas

Um mod define o que uma loja vende sem C com `zelda.shop.set(loja, itens)`.
Cada loja tem até três itens, nas três posições do balcão:

```lua
local pena = zelda.item.register { name = "pena", icon = "pena.png", ... }

zelda.shop.set(0x1f, {
  { item = "red_potion", price = 150 },
  { item = pena,         price = 10, count = 1 },
  { item = "bombs",      price = 50 },
})
```

`loja` é o byte baixo de `dungeon_room_index` usado por
`SpritePrep_Shopkeeper` (a mesma lista `kShopKeeperWhere` do jogo). A loja de
poções de Kakariko visitada no replay de referência é a sala `0x1f`; outras
lojas da lista vão de `0x0f` a `0x27` e podem ser consultadas no
`src/sprite_main.c`. A forma com um item só também é aceita:

```lua
zelda.shop.set(0x0f, { item = "heart", price = 10 })
```

Campos de cada item:

| Campo | Obrigatório | Significado |
|---|---|---|
| `item` | sim | Nome de item novo (`zelda.item.register`) ou id numérico `0..255`. Nomes originais aceitos: `red_potion`/`potion` (0x2e), `fighter_shield`/`shield` (4), `fire_shield` (5), `heart` (0x42), `arrows` (0x44), `bombs` (0x31) e `bee` (0x0e). |
| `price` | sim | Preço em rúpias, `0..999`. |
| `count` | não | Quantidade entregue de um **item novo**, `1..99` (padrão 1). Itens originais entregam a quantidade normal do recebimento (por exemplo, bombas dão 10). |

O engine usa a rotina genérica de compra: valida rúpias, garrafa vazia,
limite de bombas/flechas, escudo já obtido e vida cheia; mostra as mensagens
originais de falta de rúpias/garrafa/limite e chama `Link_ReceiveItem` com o
id configurado. O filtro/evento `ItemReceive` continua valendo, então outro
mod ainda pode trocar o item na hora da compra.

Um item novo aparece no balcão com o próprio ícone de 16×16 do registro (os
quatro tiles são instalados em VRAM de sprite), e o preço usa os dígitos do
HUD, então qualquer preço de 0 a 999 funciona. Até três itens novos na mesma
loja usam blocos de VRAM separados; os três primeiros slots do balcão são os
suportados. Sem `zelda.shop.set`, ou com o mod desativado, a loja volta à
tabela original; o registro não altera RAM, assets nem o save.

O exemplo `mods-examples/loja-nova/` registra a pena da F4e.5 e troca o
coração do meio da loja de Kakariko por ela. O replay de feature
`tests/replays-features/f4e3-shop.sav` entra na loja, compra a pena e confere
o `ItemReceive 128` no golden.

### RAM

```lua
local room = zelda.ram.read16(0x00A0)
local flags = zelda.ram.read8(0x0F3C5)
```

`read8` e `read16` são somente leitura e aceitam endereços abaixo de `0x20000`
(`read16` precisa de `addr <= 0x1FFFE`). `read16` lê little-endian.

### Estado por nome

`src/variables.h` dá nome a cada variável de `g_ram`. A tabela usada por Lua é
gerada a partir dele:

```sh
make state-table
```

O comando gera `src/mod/state_table.c` e `.h`, ordenados por nome para busca
binária. Ele não altera `variables.h` nem o layout da RAM. Entram os escalares
`uint8`/`uint16` e os arrays declarados como ponteiro para `uint8`/`uint16`.
Como o cabeçalho não declara o tamanho dos arrays, `count` é uma estimativa:
distância até o próximo offset definido dividida pelo tamanho do elemento,
limitada a 1..256. Tipos compostos (structs) e `srm_var1`, que mora em
`g_zenv.sram`, ficam de fora.

```lua
local x = zelda.state.get("link_x_coord")
local y = zelda.state.get("link_y_coord")
local room = zelda.state.get("dungeon_room_index")

-- Arrays usam índice 0-based.
local first = zelda.state.get("sprite_y_lo", 0)

-- info retorna { offset=, size=, count=, array= }.
local info = zelda.state.info("link_x_coord")
print(info.offset, info.size, info.count, info.array)

-- list aceita um prefixo opcional.
for _, name in ipairs(zelda.state.list("link_")) do
  print(name, zelda.state.get(name))
end
```

`get` lê 1 ou 2 bytes little-endian. Índice fora da faixa, nome desconhecido
ou índice em uma variável escalar geram erro Lua. `list` devolve os nomes em
ordem alfabética.

A escrita é opcional e precisa ser declarada no manifesto:

```json
{
  "id": "meu-mod",
  "api": 1,
  "permissions": ["state_write"],
  "script": "main.lua"
}
```

Sem `state_write`, `zelda.state.set` gera erro Lua e o mod é desativado, como
qualquer outro erro de handler. Com a permissão:

```lua
zelda.state.set("link_rupees_actual", 777)      -- escalar
zelda.state.set("sprite_y_lo", 3, 255)          -- array, índice 3
```

O valor precisa caber no tipo (`0..255` para `uint8`, `0..65535` para
`uint16`); fora disso também é erro Lua. Permissão desconhecida no `mod.json`
recusa o mod com uma mensagem, por exemplo:

```
mods: refused 'meu-mod': unknown permission 'fly' (supported: state_write)
```

No modo headless, `--dump-state` imprime depois da linha `final` uma linha
`state <nome> <valor>` para cada escalar, em ordem alfabética. Arrays ficam de
fora e a flag não altera o estado do jogo:

```sh
./zelda3 --config tests/regress.ini --headless --replay-ref 0 \
  --frames 3000 --dump-state zelda3.sfc
```

### Estado salvo do mod

`zelda.save` guarda valores do mod sem tocar no layout da SRAM. Os dados vão
para um arquivo ao lado do save do jogo, um por slot e por mod:

```text
saves/sram.mod.<slot>.<id>.sav
```

`<slot>` é 0, 1 ou 2 (o mesmo índice dos eventos `FileLoad`/`FileCreate`) e
`<id>` é o `id` do manifesto, com os caracteres fora de `A-Z a-z 0-9 - _`
codificados como `%XX` para virar um nome de arquivo seguro. O save do jogo
continua sendo `saves/sram.dat`; o arquivo do mod não é lido pelo jogo e não
entra na SRAM.

```lua
zelda.save.set("mortes", 3)
zelda.save.set("nome", "Link")
zelda.save.set("viu_intro", true)

local mortes = zelda.save.get("mortes")   -- nil se a chave não existe
zelda.save.set("mortes", nil)             -- apaga a chave
```

Aceita string, número (inteiro ou fracionário) e booleano. `get` devolve `nil`
para chave ausente. Limites por mod e por slot:

| Limite | Valor |
|---|---|
| chave | 1 a 64 bytes |
| string | até 4096 bytes |
| total do arquivo | 64 KiB |

Estourar qualquer limite gera erro Lua e desativa o mod, como os outros
handlers. Os valores ficam em memória e são gravados quando o jogo grava o
save (`SaveGameFile`); ao carregar um arquivo, o jogo lê o arquivo daquele
slot antes de emitir `FileLoad`. A gravação é atômica (arquivo temporário +
`rename`). Um save sem o arquivo do mod abre normalmente, com todas as chaves
ausentes; remover o mod deixa o arquivo órfão no disco, sem efeito no save.
Criar um arquivo novo zera os arquivos daquele slot para os mods ativos.

O exemplo `mods-examples/contador-mortes/` incrementa um contador no evento
`LinkDeath`, guarda em `zelda.save` e mostra o valor no HUD por `zelda.draw`.

### Desenho na tela

O evento `FrameDraw` é emitido logo depois que o PPU desenha o frame e antes
de a imagem ser apresentada. Handlers desse evento podem usar `zelda.draw`
para desenhar por cima do jogo sem tocar na simulação:

```lua
zelda.on("FrameDraw", function()
  local w = zelda.draw.width()
  local h = zelda.draw.height()
  zelda.draw.rect(4, 4, 120, 24, 0xA0000000)  -- preto semitransparente
  zelda.draw.pixel(w - 1, 0, 0xFF0000)
  local largura = zelda.draw.text(8, 8, "HUD", 0xFFFFFFFF)
end)
```

| Chamada | Significado |
|---|---|
| `zelda.draw.rect(x, y, w, h, cor)` | Retângulo preenchido. `w`/`h` zero não desenham nada. |
| `zelda.draw.pixel(x, y, cor)` | Um pixel. |
| `zelda.draw.text(x, y, texto, cor)` | Texto em uma linha, retorna a largura ocupada em pixels. |
| `zelda.draw.width()` | Largura da tela em pixels do SNES, incluindo as laterais do widescreen. |
| `zelda.draw.height()` | Altura da tela em pixels do SNES (224, ou 240 com `extend_y`). |

As coordenadas são em pixels do SNES, com `(0, 0)` no canto superior esquerdo
do framebuffer; com widescreen, as laterais também contam. Tudo é recortado
nas bordas. A cor pode ser `0xRRGGBB` (opaca) ou `0xAARRGGBB`, com alfa
`0x00`–`0xFF`; alfa diferente de 255 faz mistura linear simples com o pixel
que já está no buffer. Como `0xRRGGBB` não tem alfa, valores até `0xFFFFFF`
são tratados como opacos.

Com `[Coop] SharedWorld = 1` e a tela dividida, o `FrameDraw` roda **uma vez
por metade**: `ev.a` é o índice da metade (0 esquerda, 1 direita) e `ev.b` é 2.
As coordenadas continuam locais à metade (0..255) e `zelda.draw.width()` devolve
a largura de uma metade. Sem divisão o evento roda uma vez com `ev.a = 0` e
`ev.b = 1`.

A fonte é uma bitmap 5x7 própria do port, com os caracteres ASCII 32–126.
Cada caractere avança 6 pixels (5 do glifo mais 1 de espaço); `text` retorna
`6 * n - 1` para `n` caracteres (0 para texto vazio). Caracteres fora da
faixa ASCII imprimível viram `?`.

Cada chamada é um comando. O limite é de 4096 comandos por `FrameDraw`;
passar disso gera um erro Lua claro e desativa o mod, como qualquer outro
erro de handler. O buffer de texto do frame também tem limite de 64 KiB.

`zelda.draw` só é válido dentro de `FrameDraw`: fora dele, qualquer chamada
gera erro Lua. Durante o evento a simulação é somente leitura:
`zelda.state.set`, `zelda.item.set` e escritas em `zelda.link.*` falham com
`read-only during FrameDraw`; leituras continuam funcionando. O desenho mexe
apenas no buffer de pixels, depois do PPU e antes da apresentação, e não
altera RAM, VRAM nem o estado do PPU.

No modo headless não há apresentação, então `FrameDraw` normalmente não roda.
A flag `--screenshot <arquivo.ppm>` renderiza **um** frame depois de imprimir
o `final`, emite `FrameDraw`, aplica os comandos e grava um PPM binário (P6).
Como isso acontece no fim, não altera a simulação nem o hash já impresso:

```sh
./zelda3 --config tests/regress.ini --headless --replay-ref 0 \
  --frames 3000 --screenshot a.ppm zelda3.sfc
```

O exemplo `mods-examples/lua-hud/` usa a API para desenhar uma caixa
semitransparente no canto com rúpias, vida/vida máxima, sala/área e o
contador de frames.

### Erros

Erros de sintaxe são reportados pelo carregador e erros no topo do script ou
dentro de um handler usam `lua_pcall` com traceback completo no `stderr`.

- Erro ao carregar: o mod é recusado (`mods: refused ...`).
- Erro num handler: os handlers daquele mod são desinscritos, o script é
  desativado (`mods: script '<id>' disabled ...`) e o jogo segue normalmente.

Quando um script carrega com sucesso, `g_mods_change_content` é ligado e a
comparação lado a lado com a ROM fica desativada, como já acontece com
overrides e patches.

### Exemplos

- `mods-examples/lua-ola/`: imprime sala, diálogo e item recebido.
- `mods-examples/lua-flechas/`: ao receber o arco, dá 30 flechas.
- `mods-examples/lua-bau-aleatorio/`: troca o item de cada baú por outro de
  uma lista segura, com um LCG de seed fixa (determinístico).
- `mods-examples/lua-estado/`: ao entrar em sala, imprime `link_x_coord`,
  `link_y_coord` e `dungeon_room_index` via `zelda.state`.
- `mods-examples/lua-hud/`: desenha um HUD próprio no evento `FrameDraw` com
  `zelda.draw.rect` e `zelda.draw.text`.
- `mods-examples/contador-mortes/`: conta mortes por slot com `zelda.save` e
  mostra o total no HUD.
- `mods-examples/redux-ganchos/`: filtros e decisões com os valores do Redux
  (beam como L2, fada 64, bombas 60, flechas 99, Pikit sem roubo e blocos
  infinitos).
- `mods-examples/espelho-sombrio/`: proíbe o espelho ao ar livre no Mundo
  Sombrio, como no A Link to the Islands.
- `mods-examples/opcoes-islands/`: preset de `"features"` com as opções que o
  A Link to the Islands liga por assembly e o zelda3 já tem.
- `mods-examples/islands-ganchos/`: espelho no Mundo Sombrio, quebra-cabeça da
  praia (`OverworldTransitionTarget`) e recarga automática de magia
  (`FrameStart`).
- `mods-examples/inimigo-novo/`: substitui a rotina do cavaleiro verde
  (`Sprite:4B-Knight`) por uma versão que foge do Link e só se aproxima
  quando ele está de costas, usando `original()` para desenho e colisão.
- `mods-examples/item-novo/`: registra a pena, troca o item do baú da casa do
  Link pelo filtro `ItemReceive`, mostra o ícone no menu de 24 e usa um pulo
  curto no `on_use`.
- `mods-examples/loja-nova/`: registra a pena e usa `zelda.shop.set` para
  vender a pena no lugar do coração da loja de Kakariko, com preço próprio.

```sh
./zelda3 --config tests/regress.ini --headless --mods-dir mods-examples \
  --mod-trace --replay-ref 0 zelda3.sfc
```

## Conteúdo editável (modkit)

`tools/modkit.py` fecha o ciclo do conteúdo: o autor edita as fontes legíveis
(salas e overworld em YAML, diálogo em texto, sprites/fontes em PNG) e a
ferramenta compila essas fontes e grava somente os assets binários que mudaram.

O modkit guarda uma extração base da ROM em `.modkit/base/` (ignorado pelo
git). Na primeira execução ele extrai com `assets/restool.py`; a cache é
refeita se a ROM ou os scripts de extração mudarem. Não versione essa pasta
nem conteúdo derivado dela: no git ficam só o `mod.json` e um `content/`
parcial com o que o mod muda. A pasta `build/` é saída gerada.

### Fluxo

1. Crie o `mod.json` normalmente. Para consultar a fonte base, use `extract`
   (ele copia o arquivo inteiro para `content/`; deixe no mod só o que muda):

   ```sh
   python3 tools/modkit.py extract dungeon 260 mods-examples/bau-yaml
   python3 tools/modkit.py extract overworld 0 mods-examples/bau-yaml
   python3 tools/modkit.py extract dialogue us mods-examples/bau-yaml
   python3 tools/modkit.py extract dialogue pt mods-examples/bau-yaml
   ```

   O `id` de dungeon/overworld é o número da sala/área. Para diálogo, `us`
   gera `content/dialogue.txt`; `pt` gera `content/dialogue_pt.txt`, e assim
   por diante. Use `--rom <rom>` se o `zelda3.sfc` não estiver na raiz.

2. Edite os arquivos em `content/`. O layout é relativo à pasta `assets/`:

   ```
   content/
     dungeon/dungeon-260.yaml
     dungeon/default_rooms.yaml
     overworld/overworld-0.yaml
     dialogue.txt
     dialogue_pt.txt
     font_pt.png
     sprites/sprites_0.png
     linksprite.png
   ```

   **YAML de dungeon/overworld pode ser parcial:** use só as chaves de topo
   que o mod muda, por exemplo:

   ```yaml
   Chests: [2]
   ```

   O modkit carrega o YAML base da extração e sobrescreve essas chaves. Um
   arquivo com todas as chaves de topo do base continua funcionando e
   substitui o documento inteiro.

   **Diálogo também pode ser parcial:** `dialogue.txt` e `dialogue_<lang>.txt`
   usam o formato original `<id>: <texto>`, uma mensagem por linha. Inclua só
   as mensagens alteradas; o modkit mantém as demais iguais. Exemplo:

   ```text
   31: [Name], be careful out there!
   ```

   Os ids são os do arquivo base (1 a 397 no inglês). Não versione o arquivo
   completo extraído da ROM.

3. Gere os overrides antes de jogar/distribuir:

   ```sh
   python3 tools/modkit.py build mods-examples/bau-yaml
   python3 tools/modkit.py build mods-examples/bau-yaml --languages pt
   ```

O build compila a base e a versão do mod em diretórios temporários, pelo mesmo
caminho e com os mesmos idiomas, e compara os dois `zelda3_assets.dat` asset por
asset. Nunca usa o `zelda3_assets.dat` local como referência.

Para cada asset diferente, grava `<mod>/build/<kNome>.bin` e atualiza o
`asset_overrides` do `mod.json`. Se o tamanho não mudou, usa a forma curta
(`"kNome": "build/kNome.bin"`); se mudou, usa
`{"file": ..., "allow_resize": true}`. Os assets gerados ficam listados no
campo de topo `"generated_overrides"`; num build seguinte eles são
substituídos ou removidos quando não são mais necessários. Overrides e outros
campos escritos à mão são preservados (um override manual para o mesmo asset
que o modkit quer gerar é recusado com mensagem clara).

`build/*.bin` e `build/modkit.json` são saídas geradas e não entram no git
(`mods-examples/*/build`). O `mod.json` pode apontar para `build/...`; se o
arquivo ainda não existir, o loader recusa o mod com
`mods: refused '<id>': unable to read asset override '<caminho>'`.

O build também grava `<mod>/build/modkit.json` com a versão da ferramenta, os
idiomas usados, o SHA-1 do `.dat` base e o SHA-1/tamanho só dos assets base que
foram sobrescritos. Sem nenhuma diferença, a mensagem é
`modkit: no differences; zero overrides` e o `mod.json` fica sem overrides
gerados.

Se `content/` tiver algum `sprites/*.png`, o build passa
`--sprites-from-png` para as duas compilações; caso contrário os sprites vêm da
ROM. Isso evita gerar um override de `kSprGfx` sem querer só por incluir um PNG
na pasta errada.

### Idiomas e `--languages`

`kDialogue` e `kDialogueFont` são assets "packed" que contêm todos os idiomas
compilados de uma vez. **Quem joga com a tradução `pt` deve construir o mod com
`--languages pt`.** Um override de `kDialogue` gerado sem `pt` substituiria o
asset inteiro e apagaria a tradução.

Idiomas extras exigem a ROM traduzida correspondente. O modkit procura, por
exemplo, `portuguese.sfc` para `pt`, ou `zelda3_pt.sfc`/`pt.sfc`; também aceita
caminho explícito:

```sh
python3 tools/modkit.py build mods/my-mod --languages pt \
  --language-rom pt=portuguese.sfc
```

Se `content/dialogue_pt.txt` ou `content/font_pt.png` existir mas
`--languages` não incluir `pt`, o build avisa que o arquivo será ignorado.

### Exemplo: baú da casa do Link pelo YAML

`mods-examples/bau-yaml/` faz o mesmo que `bau-da-casa`, mas editando
`content/dungeon/dungeon-260.yaml`. O arquivo do exemplo tem só esta linha:

```yaml
Chests: [2]
```

O build gera `build/kDungeonRoomChests.bin` (504 bytes, o mesmo tamanho do
asset original) e o `mod.json` aponta para ele; esse binário é derivado da ROM
e não é versionado, então rode o build antes de testar. Para testar, use uma
pasta de mods que contenha só o `bau-yaml` (o `bau-da-casa` e o
`lua-bau-aleatorio` também mexem em baús):

```sh
python3 tools/modkit.py build mods-examples/bau-yaml
mkdir -p /tmp/mods-bau
cp -a mods-examples/bau-yaml /tmp/mods-bau/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-bau \
  --mod-trace --replay-ref 0 --frames 3400 zelda3.sfc
```

O trace deve mostrar `ChestOpen 260 ...` seguido de `ItemReceive 2 ...`.

### Importar gráficos, paletas, mapas, salas, texto e tabelas de uma ROM traduzida

`import-rom` leva para um mod os gráficos, tilemaps, paletas, mapas, salas de
masmorra e tabelas de uma ROM traduzida ou hack, sem recompilar o jogo. Ele lê
da própria ROM de origem as tabelas de ponteiros usadas por
`Decomp_spr`/`Decomp_bg` (SNES
`$00:CF80`/`$D05F`/`$D13E` para fundos e `$00:CFF3`/`$D0D2`/`$D1B1` para
sprites), então funciona também quando a tradução realocou ou recompactou os
gráficos:

```sh
python3 tools/modkit.py import-rom portuguese.sfc mods/ptbr-graficos
python3 tools/modkit.py import-rom altti.sfc mods/altti-graficos
```

O `import-rom` também monta um mod local completo a partir de um ROM de hack,
um preset de `features` e os scripts de outro mod:

```sh
mkdir -p mods/redux
printf '{"id":"redux","name":"Redux","version":"1.0.0","api":1}\n' \
  > mods/redux/mod.json
python3 tools/modkit.py import-rom redux.sfc mods/redux \
  --features-from mods-examples/opcoes-redux \
  --scripts-from mods-examples/redux-ganchos
```

`--features-from` pode ser repetido; os presets são aplicados na ordem e o
último vence em caso de chave repetida. `--scripts-from` copia o arquivo
apontado por `"script"` no `mod.json` do mod de scripts (mantendo o caminho
relativo) e as `permissions` declaradas, e atualiza o `"script"` do mod de
destino. O mod gerado em `mods/redux/` é local: `mod.json` e `content/` podem
ser versionados, mas `build/` e os binários derivados do ROM nunca entram no
git. Sem as duas opções, o comportamento do `import-rom` é o mesmo de antes.

A ROM **base** (`--rom`, por padrão `zelda3.sfc`) continua obrigatoriamente
verificada pelo SHA-1 da US; é dela que sai o `zelda3_assets.dat` de
referência. A ROM de **origem** (`translated_rom`, o primeiro argumento) pode
ser desconhecida: o `import-rom` avisa
`Warning: ROM with hash ... is not a known translation; reading it anyway.` e
segue. Esse relaxamento existe só no caminho do `import-rom`; `restool.py`,
`--extract-from-rom` e `modkit build` continuam recusando ROMs fora da lista
conhecida. Não há hashes de hacks na tabela de `assets/util.py`.

O primeiro argumento é a ROM traduzida/hack; o segundo, a pasta do mod (com
`mod.json`). `--rom <arquivo>` troca a ROM US usada como base e
`--assets k1,k2` escolhe exatamente quais assets importar. Sem `--assets`,
entram por padrão:

```text
kSprGfx, kBgGfx, kBgTilemap_0..5
kOverworldMapGfx, kLightOverworldTilemap, kDarkOverworldTilemap
kDungMap_FloorLayout, kDungMap_Tiles
kPalette_DungBgMain, kPalette_MainSpr, kPalette_ArmorAndGloves,
kPalette_Sword, kPalette_Shield, kPalette_SpriteAux3,
kPalette_MiscSprite_Indoors, kPalette_SpriteAux1,
kPalette_OverworldBgMain, kPalette_OverworldBgAux12,
kPalette_OverworldBgAux3, kPalette_PalaceMapBg,
kPalette_PalaceMapSpr, kHudPalData, kOverworldMapPaletteData
kDungeonRoom*, kDungeonRoomHeaders*, kDungeonRoomChests,
kDungeonRoomTeleMsg, kDungeonPitsHurtPlayer, kDungeonRoomDefault*,
kDungeonRoomOverlay*, kDungeonSecrets, kDungeonSprites, kDungeonSpriteOffs
kEntranceData_*, kStartingPoint_*
kEnding_Credits_Text, kEnding_Credits_Offs, kEnding0_Data, kEnding0_Offs
kDialogue, kDialogueFont, kLinkGraphics
```

`kBgTilemap_0` é a tela final; `1..4` são a seleção de arquivo; `5` é o mapa
de dungeon. As fontes desses seis tilemaps não são mais endereços fixos: o
`import-rom` lê as três tabelas paralelas que o NMI usa para montar o
ponteiro de 24 bits (rotina em `$80:8BCF`, tabelas em
`$80:937A`/`$80:9383`/`$80:938C`, índices 3, 5, 6, 7, 8 e 9). Na ROM US isso
dá exatamente `$0C:DD6D`, `$0C:E7BF`, `$0C:E2A8`, `$0C:E63C`, `$0C:E456` e
`$0E:DA9C`; no A Link to the Islands a tilemap 0 vem de `$28:8000` e as
tilemaps 2–4 de `$0C:E5D4`/`$0C:E76E`/`$0C:E685`. As tilemaps 2–4 do hack têm
os mesmos bytes da US, então não geram override.

`kEnding_Credits_Text`/`kEnding_Credits_Offs` entram por padrão e aceitam os
dois formatos de créditos suportados pelo renderer (veja "Créditos" abaixo).
O conteúdo de overworld entra por padrão desde o I3: o `import-rom` extrai as
áreas do próprio hack, compila os assets com o YAML extraído e importa os que
diferem da US. Salas de masmorra, cabeçalhos, baús, sprites, segredos,
`tele_msg`, pits, entradas e starting points também entram por padrão.

#### Diálogo, fonte e Link

Hacks (ROMs fora da tabela de traduções) e o Redux também têm o diálogo e o
sprite do Link extraídos da ROM de origem. A extração de texto usa o mesmo
decodificador do `--extract-dialogue`, no formato US
(`$9C:8000`/`$8E:DF40`), e escreve `dialogue.txt` e `font.png` no workspace
temporário como o slot `us`; o import compara e grava `kDialogue` e
`kDialogueFont` só quando diferem da base. O C já carrega esses assets por
`ZeldaSetLanguage`, então nenhum código de engine precisou mudar.

`kDialogue` e `kDialogueFont` são arrays packed com um blob por idioma, e o
`kDialogueMap` do `.dat` escolhe os índices. Por isso o `import-rom` lê o
`zelda3_assets.dat` do diretório do jogo (ou o caminho de `--base-dat`) e
mantém a mesma lista de idiomas: o hack entra no slot `us` e os outros slots
continuam com os blobs da base. O `kDialogueMap` não é sobrescrito. Assim um
`.dat` US+PT continua com os dois idiomas, e um `.dat` só US gera um asset só
US. Sem `.dat` de referência, o import usa o layout US-only.

Quando o diálogo do hack é importado, os overrides de `kDialogue` e
`kDialogueFont` são gravados com `allow_resize: true`. O C aceita tamanho
variável nesses assets porque `FindInAssetArray` passa o tamanho real para
`FindIndexInMemblk`, que lê a tabela de offsets e o último índice do próprio
blob; não há tamanho fixo por fonte. Isso também deixa um mod montado com um
`.dat` US+PT carregar num `.dat` só US (os slots extras são ignorados).

O sprite do Link é o bloco de 0x7000 bytes em `$10:8000`; o banco é lido da
instrução de DMA do `NMI_DoUpdates` em `$80:8A03`, para o caso de um hack
mover a folha. Ele é convertido para `linksprite.png`, compilado como
`kLinkGraphics` e importado quando difere. As paletas do Link
(`kPalette_ArmorAndGloves`, `kPalette_MainSpr`, etc.) já fazem parte da lista
padrão de paletas.

As traduções conhecidas (`pt`, `de`, `fr`, ...) continuam sem esses assets por
padrão, para o import PT-BR ficar byte a byte igual ao de antes. Num hack, a
importação pode ser forçada com
`--assets kDialogue,kDialogueFont,kLinkGraphics`.

Comandos de texto desconhecidos ou reservados não são inventados: a extração
registra o endereço e o byte no stderr e em `build/modkit-import.json`
(`dialogue_import.unknown`), mantém `dialogue.txt`/`font.png` da base e segue
com o resto do import. O mesmo vale para uma contagem de mensagens diferente
de 396/397. O Redux e a Retranslation usam o formato US e são importados
normalmente; não é preciso construir o mod com `--languages redux` só pelo
texto em inglês.

Exemplo de saída com um `.dat` US+PT:

```text
modkit: dialogue: 397 message(s) from us
modkit: Link graphics: 28672 bytes from 0x108000
modkit: dialogue: keeping 2 language slot(s) from zelda3_assets.dat: us, pt
  import [text     ] kDialogue                      73402 ->   72217 bytes
  import [interface] kLinkGraphics                  28672 ->   28672 bytes
```

No Redux, a fonte muda mas o blob US tem o mesmo tamanho; o asset continua
com 8424 bytes num `.dat` US+PT e 4201 num `.dat` só US.

#### Salas de masmorra

A extração das salas usa `assets/extract_resources.py` no ROM de origem. As
tabelas que o hack pode mover são localizadas pelo operando da instrução
original, com conferência dos bytes vizinhos e erro claro se o código não for
reconhecido; não há endereço fixo da US no caminho. Na US o resultado é
exatamente o mesmo de antes.

| tabela | instrução (US) | US | Islands | Redux |
|---|---|---|---|---|
| ponteiros de objetos (3 bytes) | `LDA $1F8000,X` em `$81:874B` | `$1F:8000` | `$1F:8000` | `$1F:8000` |
| ponteiros de cabeçalho | `LDA $04F502,X` em `$81:B5DC` | `$04:F502` | `$2A:8000` | `$22:8000` |
| sprites de dungeon | `LDA $D62E,Y` em `$89:C297` | `$89:D62E` | `$89:D2E1` | `$89:D62E` |
| segredos | `LDA $01DB69,X` em `$81:E6C1` | `$01:DB69` | `$01:DB69` | `$01:DB69` |
| baús | `LDA $01E96E,X` em `$81:EBFA` | `$01:E96E` | `$01:E96E` | `$01:E96E` |
| `tele_msg` | `LDA $F61D,Y` em `$87:B4FF` | `$87:F61D` | `$87:F61D` | `$87:F61D` |
| pits que machucam | `CMP $00990C,X` em `$87:94AA` | `$00:990C` | `$00:990C` | `$00:990C` |
| layouts padrão | `LDA $04EF2F,X` em `$81:882C` | `$04:EF2F` | `$0C:E2A5` | `$04:EF2F` |
| overlays de sala | `LDA $04ECC0,X` em `$81:B856` | `$04:ECC0` | `$04:ECC0` | `$04:ECC0` |
| entradas / starting points | `LDA $xxxx,X`/`LDA $xxxx,Y` em `$82:D9A3`… | `$82:C813`… | iguais à US | iguais à US |

O volume de sprites de dungeon do Islands, ao ler a tabela certa (`$09:D2E1`
em vez de `$09:D62E`), é **3.988 bytes únicos** (221 listas), não os 83,5 KB
que a leitura da tabela antiga sugeria. Cabe folgado em `kDungeonSpriteOffs`
(`uint16`); o formato do C não mudou.

Quando a tabela de cabeçalhos do hack tem menos de 320 entradas, as salas que
faltam são preenchidas com a sala correspondente da US. No Islands e no Redux
a tabela tem 296 entradas; as salas 296–319 vêm da US. No Islands a entrada 294
também aponta para um cabeçalho `0xFF` inválido e usa a sala da US. Ponteiros
0 significam “sem sprites”/“sem segredos”. Registros de baú/pit com sala
`0xFFFF`, quadrantes `0xFF` e tipos fora das tabelas são descartados ou usados
como valor seguro. O sprite `6B` (CannonTrooper) já é implementado pelo C como
no assembly (S6B), então não é mais pulado: as salas que o usam (Redux 176;
Islands 184 e 216) importam o sprite como qualquer outro. Cada substituição
aparece em `modkit: dungeon sanitize: ...`; o `build/modkit-import.json` guarda
o relatório completo em `dungeon_extraction`.

No Redux, com a tabela de cabeçalhos lida do código, o import marca **19
salas** diferentes da US (0, 5, 17, 18, 22, 60, 73, 91, 126, 134, 140, 158,
159, 176, 179, 190, 222, 260, 288). Isso inclui todas as salas nomeadas no
`REDUX-MAPA` (Ice Palace 158/190, Santuário 18, Skull Woods 73, Ganon's Tower
176). As “35 salas” da análise anterior vinham de comparar com a tabela de
cabeçalhos obsoleta da US e/ou de contar padding de objetos; a sala 184
(Eastern Palace) não muda no YAML — a estrela é gráfica e vem nos packs
importados.

#### Overworld

O extrator de overworld (`assets/extract_resources.py` com
`assets/overworld_tables.py`) escreve um YAML por área-cabeça, exatamente como
a extração US, e o compilador gera `kOverworldMapIsSmall`,
`kOverworldAuxTileThemeIndexes`, `kOverworldBgPalettes`, `kOverworld_SignText`,
`kOwMusicSets(2)`, `kBirdTravel_*`, `kWhirlpoolAreas`,
`kOverworld_Entrance_*`, `kFallHole_*`, `kExitData*`, `kSpExit*`,
`kOverworldSecrets_Offs`, `kOverworldSecrets`, `kOverworldSprite*`,
`kOverworld_Hi/Lobytes_Comp`, `kMap32ToMap16_0..3`, `kMap16ToMap8`,
`kMap8DataToTileAttr` e `kSomeTileAttr`. A tabela `kOverworldAreaHeads` (64
bytes) passou a ser uma tabela de mod (`MOD_TABLE_BYTES`); sem mod ela contém
os mesmos bytes da US. `kMap16ToMap8` é importado por padrão só quando a ROM de
origem é um hack (hash desconhecido); traduções conhecidas mantêm a base
curada, e a tabela pode ser forçada com `--assets kMap16ToMap8`.

Todas as tabelas são localizadas pelo operando da instrução original, não por
endereço fixo. Na US, no Islands e no Redux os endereços encontrados são os
mesmos da US:

| tabela | instrução (US) | US / Islands / Redux |
|---|---|---|
| áreas-cabeça | `LDA $02A5EC,X` em `$82:AA7D` | `$02:A5EC` |
| gfx por área | `LDA $00FC9C,X` em `$82:AB14` | `$00:FC9C` |
| paleta por área | `LDA $00FD1C,X` em `$82:AAFC` | `$00:FD1C` |
| placas | `LDA $F51D,Y` em `$87:B519` | `$07:F51D` |
| música / música 2 | `LDA $C303,Y` / `$C403,Y` | `$02:C303` / `$02:C403` |
| tamanho do mapa | `LDA $02F88D,X` em `$82:AC3C` | `$02:F88D` |
| sprites gfx / paleta | `LDA $FA41,Y` / `$FB41,Y` | `$00:FA41` / `$00:FB41` |
| ponteiros de sprites | `LDA $C881,Y` / `$C901,Y` / `$CA21,Y` | `$09:C881` / `$09:C901` / `$09:CA21` |
| hi / lo comprimidos | `LDA $02F94D,X` / `$02FB2D,X` | `$02:F94D` / `$02:FB2D` |
| map32→map16 | `LDA $038000,X` etc. em `$82:F691` | `$03:8000`, `$03:B400`, `$04:8000`, `$04:B400` |
| map16→map8 | `LDA $0F8000,X` em `$80:8864` | `$0F:8000` |
| atributos map8 / extra | `LDA $0E9459,X` / `$1BF110,X` | `$0E:9459` / `$1B:F110` |
| segredos | `LDA $1BC2F9,X` em `$9B:C8B8` | `$1B:C2F9` |
| exits (13 tabelas) | `LoadOverworldFromDungeon` em `$82:E4A3` | `$82:DD8A`…`$82:E405` |
| viagem de pássaro / redemoinho | rotina em `$82:EC34` | `$82:EAE5`…`$82:ECF8` |
| entradas do overworld | `$9B:BD10`/`$9B:BD19`/`$9B:BD5F` | `$1B:B96F`, `$1B:BA71`, `$1B:BB73` |
| buracos | `$9B:B893`/`$9B:B89B`/`$9B:B8B4` | `$1B:B800`, `$1B:B826`, `$1B:B84C` |

No Islands a instrução de `$82:AA7D` foi trocada por `JSL $24:8000`; o
extrator segue o gancho e lê a mesma tabela `$82:A5EC` dentro da rotina nova.
A tabela do hack tem 8 posições diferentes da US, criando as áreas-cabeça 25,
28, 32, 35 e 36 (e as equivalentes no Mundo Sombrio). O import gera override
de `kOverworldAreaHeads`; sem ele o C continuaria usando a tabela US.

Os assets de mapa `kMap32ToMap16_0..3` vêm de `map32_to_map16.txt`, que é
regerado da ROM de origem durante a extração. Sem esse passo o Redux
renderizava o mapa com tiles aleatórios porque seus índices map16 são
deslocados e exigem as tabelas novas.

Sanitização: os segredos do overworld usam nomes genéricos para bytes fora da
tabela US (o C trata qualquer valor ≥ `$80` como especial); a área 23 do
Islands preserva o tipo `$A6` e a 32 o `$E6`. Registros com posição ímpar ou
fora de 64×64 nas áreas-cabeça 117, 119, 122, 123, 124 e 127 descartam os
itens daquela área, com uma linha `modkit: overworld sanitize: ...`. O Islands
tem 21 exits com screen `$FF`, um buraco com área `$FF` e uma entrada com área
`$FFFF`; esses slots ficam com sentinelas (`$FFFF`/`$FF`) e são listados.
Nenhum dado do hack estourou o formato fixo do C.

O ponto de início do jogo continua sendo a casa do Link (sala 260) no port.
No Islands a tabela de exits importada manda a sala 260 para a área 16, não
para a área 10; o `kStartingPoint_*` do hack é importado, mas o caminho de
“jogo novo” do zelda3 não o consulta para escolher a área inicial. O atalho
opcional de Kakariko do Redux não está na ROM `redux.sfc`: a área 34 e as
vizinhas não mudam no YAML, então o import não tem o que aplicar.

O comando compila a base US e a ROM traduzida em diretórios temporários,
compara asset por asset e grava em `<mod>/build/` só os assets escolhidos que
diferem. O `asset_overrides` do `mod.json` é atualizado; assets que mudam de
tamanho usam `allow_resize`. A lista do que foi importado fica em
`imported_overrides`, separada de `generated_overrides` do `build`. Assim,
rodar `build` depois não apaga os gráficos importados; se o `build` tentar
gerar o mesmo asset, ele recusa como se fosse um override manual.

As tabelas de ponteiros do mapa de dungeon são lidas do operando da instrução
que as usa, não de um endereço fixo. `DungeonMap_DrawSingleRowOfRooms`
(`$8A:E5BC`, `src/messaging.c:1691`) carrega o floor layout com `LDA $F605,X`
(`$8A:E5E2` e `$8A:E624`) e os tiles com `LDA $FBE4,X` (`$8A:E63F`). O leitor
confere o opcode `BD` e as instruções vizinhas (`STA $0C`, `LDA ...`) e falha
com mensagem clara se não reconhecer.

Na US as duas tabelas ficam em `$8A:F605` e `$8A:FBE4`. No A Link to the
Islands o hack moveu só a segunda, para `$8A:FBB2`: a instrução virou
`C2 20 BD B2 FB 85 0C` e os ponteiros válidos são `$FBCE`, `$FBCF`, `$FBE5`,
`$FBF3`, ... Ler o endereço fixo `$8A:FBE4` no hack devolve outros dados
(`14 2D 2A 2D ...`), que não são ponteiros. `$0A:2D14` não é ROM: a metade
baixa dos bancos `$00-$3F`/`$80-$BF` é I/O. O `assert (ea & 0x8000)` voltou;
endereços na metade baixa dos bancos `$40-$6F`/`$C0-$EF` são rejeitados com
uma mensagem específica. Nenhum desses apareceu no ALTTI, Redux ou PT.

No ALTTI `kDungMap_Tiles` continua com 258 bytes, mas agora com os bytes
certos. O tamanho vem da quantidade de bytes diferentes de `0xF` no floor
layout do hack (230 bytes crus + 28 da tabela do asset packed), não da tabela
de ponteiros.

As demais leituras fixas que o import usa são blocos de dados diretos, não
tabelas de ponteiros: paletas, `kOverworldMapGfx`, as tilemaps claro/escuro,
`kPredefinedTileData`, `kMap16ToMap8` e as tabelas geradas. Conferi os
operandos que carregam esses dados no ALTTI e todos continuam nos endereços
da US. As tabelas de ponteiros que mudaram foram as de tiles do mapa de
dungeon, a de cabeçalhos de sala, a de sprites de dungeon e a de layouts
padrão; todas são lidas pelo operando da instrução.

Além dos assets, `import-rom` lê cada tabela direta registrada em
`src/mod/game_tables.def` no endereço SNES da ROM traduzida, compara com os
bytes da mesma tabela na ROM US e grava só as que mudaram em
`<mod>/build/<nome>.bin`. Tabelas `MOD_TABLE` são `uint16`; `MOD_TABLE_BYTES`
são `uint8`. O `mod.json` recebe `table_overrides` e a lista
`imported_tables` (separada de `imported_overrides`) para que uma nova
execução remova tabelas que deixaram de diferir. O parser do registry em
`tools/modkit.py` usa o mesmo `.def` incluído pelo C, então não há lista
duplicada. Na tradução PT-BR os endereços das tabelas do HUD continuam os
mesmos da ROM US (`$8DED09..$8DF959`); o código 65816 que as acessa também
aponta para os mesmos lugares.

As tabelas `MOD_TABLE_DERIVED` não são lidas em um endereço fixo. O
`import-rom` decodifica a própria ROM traduzida: no letreiro de fim de jogo
ele lê o operando do `CPX #` em `$88F523` (número de letras), o endereço da
Tab1 no `CMP abs,X` de `$88F514`, o alvo do `JML` em `$88F57E` e o banco do
`CMP abs,Y` da rotina nova (Tab2), e os alvos dos `JML` de `$88F5EF`/`$88F604`
e os operandos `LDA long,X` da rotina de tiles (tile/atributo de cima e de
baixo). Os operandos derivados (`N-1`, `N-2`, contagem final) também são
conferidos. Se a ROM traduzida usar os bytes da US, nada é gerado. Se o
formato não for reconhecido, o comando avisa e pula essas quatro tabelas, sem
inventar valores. Não há hash de ROM fixo no caminho.

Cada execução grava `<mod>/build/modkit-import.json` com o SHA-1 das ROMs e
do `.dat` base, os assets importados, os que diferiram mas ficaram de fora, as
tabelas importadas, as correções conhecidas que foram aplicadas
(`known_corrections`) e, para `kSprGfx`/`kBgGfx`, os índices dos packs que
realmente mudaram. Quando há conteúdo de dungeon importado ou alguma
sanitização, o arquivo também guarda `dungeon_extraction` com os endereços das
tabelas lidas e a lista de substituições. Exemplo com a tradução PT-BR
(Hyrule Legends/Trans-Center):

```text
modkit: 12 asset(s) differ from the US base:
  import [interface] kSprGfx                       133479 ->  132445 bytes
  import [interface] kBgGfx                        119899 ->  117758 bytes
  skip   [other    ] kMap16ToMap8                   30016 ->   30016 bytes
  import [text     ] kEnding_Credits_Text            1989 ->    2028 bytes
  import [text     ] kEnding_Credits_Offs             788 ->     788 bytes
  ...
modkit: 9 asset(s) imported -> build/
modkit: game-over letter tables: 9 letter(s) in translated ROM
modkit: 13 table(s) differ from the US base:
  import [table    ] kHudItemText                     640 bytes at $8df1c9
  ...
  import [table    ] kGameOverTextLetterCount           1 bytes (game-over code)
  import [table    ] kGameOverTextTab1                 10 bytes (game-over code)
  import [table    ] kGameOverTextTab2                 10 bytes (game-over code)
  import [table    ] kGameOverTextTiles                40 bytes (game-over code)
modkit: kSprGfx: 8 pack(s) changed: 5, 8, 45, 97, 104, 105, 106, 107
modkit: kBgGfx: 7 pack(s) changed: 22, 61, 65, 77, 83, 113, 114
```

Os assets marcados `other` não entram por padrão. Para incluí-los, nomeie-os
explicitamente com `--assets` (junto com os padrão que quiser manter), por
exemplo:

```sh
python3 tools/modkit.py import-rom portuguese.sfc mods/ptbr-graficos \
  --assets kSprGfx,kBgGfx,kBgTilemap_0,kBgTilemap_1,kBgTilemap_2,kBgTilemap_3,kBgTilemap_4,kEnding_MapData,kEnding0_Offs
```

`kMap16ToMap8` é uma tabela de conversão de tiles, não um gráfico de
interface; inclua só se souber que a tradução realmente precisa dela.

Os nomes de itens do menu não são um asset gráfico: são as tabelas
`kHudItemText` etc. em `src/hud.c`, agora cobertas por `table_overrides`.
O `-LIFE-` do HUD não muda porque o pack de sprites correspondente é igual ao
da ROM US.

Na tradução PT-BR, o nome do arco é especial. Rodando a rotina
`Hud_DrawSelectedYButtonItem` da própria ROM PT no emulador do projeto com
`link_item_bow` = 2, 3 e 4, o tilemap que ela escreve (`$122C`/`$126C`) é:

| arco | linha 0 | linha 1 |
|---|---|---|
| 2 | vazia | `Arco e` |
| 3 | `Flecha` | `Arco` |
| 4 | `Arco e` | `Flecha de Prata` |

A tabela mantém o layout US de 3 entradas × 8 colunas × 2 linhas, mas as duas
primeiras entradas estão deslocadas uma linha: `Flecha` deveria ser a segunda
linha do arco 2, e o arco 3 deveria mostrar só `Arco`. A decisão
(09/11/26) foi corrigir o texto na importação, sem mudar o C:

| arco | linha 0 | linha 1 |
|---|---|---|
| 2 | `Arco e` | `Flecha` |
| 3 | vazia | `Arco` |
| 4 | `Arco e` | `Flecha de Prata` |

O `import-rom` aplica essa correção só quando o SHA-1 da ROM de origem é
exatamente `d0d09ed41f9c373fe6afdccafbf0da8c88d3d90d` (a tradução PT de 2014).
A lista fica em `KNOWN_ROM_CORRECTIONS`, em `tools/modkit.py`, indexada pelo
SHA-1 e com a permutação das seis linhas de 8 words da `kHudBowItemText`
(`1, 2, 0, 3, 4, 5`). Outra ROM, inclusive um hack ou uma tradução alterada,
continua com os próprios bytes. O import imprime
`aplicando correção conhecida da tradução PT de 2014: texto do arco` e registra
a correção em `build/modkit-import.json`, no campo `known_corrections`. A
engine C só aplica os bytes de `table_overrides`; não há reconhecimento de
tradução no C. `PINGENTE` é o texto da própria tradução, não um corte.

#### Créditos

`kEnding_Credits_Text` é a tabela de linhas e `kEnding_Credits_Offs` a tabela
de 394 offsets. O `import-rom` localiza as duas pelo operando de
`Credits_AddNextAttribution` (`LDA abs,Y`), não por endereço fixo. Na US o
texto fica em `$8EB178` e os offsets em `$8EB93D`; na PT-BR o texto foi para
`$8EB038` e os offsets continuam em `$8EB93D`.

Cada entrada começa com dois bytes (deslocamento e contagem) e tem dois
layouts:

| formato | conteúdo da entrada |
|---|---|
| `map_index` (US) | `(count + 1) >> 1` bytes, cada um índice em `kEnding_MapData` |
| `raw_words` (PT-BR) | um byte alto e `(count + 1) >> 1` bytes baixos; a palavra de tilemap é `(alto << 8) | baixo` |

O `import-rom` escolhe o layout contando quantas entradas terminam exatamente
no próximo offset distinto; não usa heurística de valor de byte. O tamanho do
texto também sai dessa comparação: 1989 bytes na US e 2028 na PT-BR (a
extração antiga lia 2041, incluindo bytes depois da última entrada).
`kEnding_MapData` e `kEnding0_Offs` continuam fora do import padrão; o
formato `raw_words` não consulta `kEnding_MapData`.

O renderer em `src/ending.c` consulta `EndingCredits_UsesRawWords()`
(`src/mod/ending_credits.c`). Sem mod, o texto da US é classificado como
`map_index` e o caminho é o mesmo de antes. Com o mod PT, cada palavra sai
direto de `(alto << 8) | baixo`, sem passar por `kEnding_MapData`.

A prova visual usou o replay `ref12`: os créditos começam por volta do frame
101000 e foram capturados em 101000, 104000, 107000, 110000, 113000, 116000,
119000, 122000, 125000 e 128000, com e sem o mod; o texto PT aparece legível
e alinhado. Na US as mesmas telas mostram os títulos em inglês. Os screenshots
ficam em `.agents/shots/` (fora do git).

O conteúdo gerado em `mods/<id>/build/` vem da ROM traduzida local e não
deve ser versionado. O `mod.json` e o `content/` é que podem ser
compartilhados; o `build/` é sempre gerado localmente.

#### Epílogo

As cenas do final usam `kEnding0_Data` (os blocos de texto) e
`kEnding0_Offs` (17 offsets, um para cada cena). O `import-rom` localiza a
rotina `Credits_AddEndingSequenceText` pelo prefixo de bytes comum às ROMs US
e PT e lê os operandos do próprio código:

- a tabela de offsets vem dos dois `LDA abs,Y` que carregam início e fim da
  cena;
- na US o texto é lido no lugar por `LDA abs,Y`;
- na PT a rotina salta para um carregador em `$A0:80AC`, que aponta para
  `$A0:80C8`; o import segue esse salto e lê o ponteiro de lá.

Cada cena é uma sequência de blocos com cabeçalho de 4 bytes (destino de
VRAM e tamanho/controle). O cabeçalho é igual nos dois formatos; o payload
muda:

| formato | payload de cada bloco |
|---|---|
| `map_index` (US) | um byte por palavra, índice em `kEnding_MapData` |
| `raw_words` (PT-BR) | um byte alto e um byte baixo por palavra; a palavra é `(alto << 8) \| baixo` |

O import escolhe o formato contando quantas cenas terminam exatamente no
próximo offset distinto em cada layout. Na US o `kEnding0_Data` tem 917 bytes
e 16 cenas fecham em `map_index`; na PT tem 1074 bytes e 16 fecham em
`raw_words`. `kEnding_MapData` continua fora do import padrão: o formato
`raw_words` não a consulta. O `record` de `build/modkit-import.json` ganha
`ending_epilogue` com os endereços e o formato detectado.

O renderer em `src/ending.c` consulta `EndingEpilogue_UsesRawWords()`
(`src/mod/ending_credits.c`). Sem mod, os bytes da US são classificados como
`map_index` e o caminho é exatamente o de antes. Com o mod PT, cada palavra é
montada a partir do byte alto e dos bytes baixos, sem passar por
`kEnding_MapData`. A prova visual usou o replay `ref12`, com e sem o mod, nos
frames 90000, 95000, 98000 e 101000 (a frase da espada); o texto PT aparece
legível e alinhado. Os PNGs ficam em `.agents/shots/` (fora do git).

## Configuração

Seção no `zelda3.ini`:

```ini
[Mods]
Enabled = 1
Directory = mods
LuaInstructionLimit = 10000000
LuaMemoryMB = 32
```

Flags de linha de comando:

- `--no-mods`: desliga todos os mods nesta execução (vence as outras opções).
- `--mods-dir <dir>`: usa outro diretório de mods e liga os mods, mesmo que o
  ini tenha `Enabled = 0`.

`tests/regress.ini` desliga mods e `tools/regress.sh` passa `--no-mods`, para a
bancada nunca depender da pasta do usuário.

## Comparação com a ROM

Se algum mod aplicar override, patch, sprite ou carregar um script, o
carregador liga a flag global `g_mods_change_content`. Com ela ligada,
`ZeldaRunFrame` não roda a comparação lado a lado com o assembly original e o
modo headless imprime `compare: off`. Sem mods a flag continua falsa e o
comportamento não muda.

## Exemplo: baú da casa do Link

`mods-examples/bau-da-casa/` troca o item do baú da sala 260 (`0x104`) por um
bumerangue. O `mod.json` aponta para `patches/bau.json`:

```json
[
  {
    "asset": "kDungeonRoomChests",
    "offset": 411,
    "expect": "04 01 12",
    "bytes": "04 01 02"
  }
]
```

O asset `kDungeonRoomChests` é um array de registros de 3 bytes:

```
uint16 room;   // little-endian; bit 0x8000 marca baú grande (big key)
uint8 item;    // id passado para Link_ReceiveItem
```

Os registros de uma sala são consumidos na ordem em que aparecem; o baú da
sala 260 é o registro 137. O item original é `0x12` (18 = lâmpada) e o novo é
`0x02` (2 = bumerangue). O `expect` confere o registro inteiro antes de
trocar.

Para testar (a pasta `mods-examples` não é carregada por padrão):

```sh
./zelda3 --config tests/regress.ini --headless --mods-dir mods-examples \
  --mod-trace --replay-ref 0 zelda3.sfc
```

O trace deve mostrar `ChestOpen 260 ...` seguido de `ItemReceive 2 ...` no
lugar de `ItemReceive 18 ...`.
