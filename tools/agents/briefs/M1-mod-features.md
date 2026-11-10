# Tarefa M1-mod-features — um mod liga opções de `[Features]`

Leia `AGENTS.md`, `docs/engine/MODS.md`, `docs/engine/REDUX-MAPA.md`
(seção 6, item 1) e `docs/engine/ALTTI-MAPA.md` (seção 3).

## Por quê

O mapa do Redux achou 33 mudanças que o zelda3 já tem como opção do
`zelda3.ini` (`[Features]`: `TurnWhileDashing`, `ItemSwitchLR`,
`MiscBugFixes`, `DimFlashes`…), e o A Link to the Islands também usa
várias. Hoje um mod não consegue ligá-las; o jogador teria de editar o ini
à mão.

## Fatos (confira)

- `src/config.c` lê `[Features]` em `g_config.features0` (bits
  `kFeatures0_*` em `src/features.h`; `DimFlashes` fica em outra seção mas é
  bit do mesmo campo). Em jogo o valor vive em `enhanced_features0`
  (`g_ram+0x64c`, `src/features.h:51`). Descubra onde um vira o outro, se
  `enhanced_features0` entra em save/replay, e como as opções ligadas
  desligam a comparação lado a lado (`src/main.c:1058`,
  `src/zelda_rtl.c:767`).

## O que fazer

1. **`mod.json`**: campo opcional `"features": { "TurnWhileDashing": true,
   "ItemSwitchLR": true, ... }` com os **mesmos nomes** das chaves do
   `zelda3.ini` (uma só tabela de nomes no C, sem duplicar strings entre o
   parser do ini e o do mod). Valores booleanos; `false` desliga.
   Nome desconhecido ou valor não booleano → mod recusado com mensagem
   clara. Inclua só as opções de jogabilidade/correção de `[Features]` e
   `DimFlashes`; **não** inclua `ExtendedAspectRatio`, vídeo, áudio, teclas
   nem caminhos (diga quais ficaram de fora e por quê).
2. **Regra decidida**: enquanto o mod estiver ativo, as
   opções dele **valem por cima** do `zelda3.ini`. Se dois mods discordarem
   numa opção, vence o de **maior prioridade** (mesma regra de prioridade
   que o carregador já usa; confira qual é a ordem). Sempre registre no log
   (stderr, como o resto do carregador) cada opção que um mod mudou e cada
   conflito, com os nomes dos mods.
3. **Sem mod, nada muda**: sem mods (ou com mods sem `"features"`) o valor
   final tem que ser exatamente o do ini. Mods desligados por erro não
   aplicam nada (a carga é transacional; siga o padrão do `ModTransaction`).
4. **Headless e `--mod-trace`**: o resultado tem que valer também no
   headless, para dar para testar com replay.
5. **Mod de exemplo** `mods-examples/opcoes-redux/`: só um `mod.json` que
   liga o conjunto que o REDUX-MAPA lista como classe A (seção 6, item 1).
6. `docs/engine/MODS.md`: seção sobre `"features"`, com a regra de
   precedência e a lista de nomes aceitos.

## Verificação a relatar

1. `make -j2` e `make -j2 NO_LUA=1` sem warnings.
2. `tools/regress.sh` com todas as linhas `ok` e a linha final.
3. Teste com replay: rode `tests/replays-features/issue236-book.sav` com
   `--config tests/regress.ini` (opções desligadas) **e** um mod de teste
   (fora do repositório) que liga `ItemSwitchLR`; o trace e o hash final
   têm que ser iguais aos do golden `tests/golden-features/issue236-book.txt`
   (que foi gravado com a opção ligada pelo ini). Cole as saídas.
4. Conflito: dois mods de teste discordando em `TurnWhileDashing`; cole o
   log mostrando quem venceu. Nome inválido e valor não booleano: cole o
   erro.
5. Mods de teste ficam fora do repositório; não commite `mods/`.

## Relatório

Além do pedido no AGENTS.md: onde `g_config.features0` vira
`enhanced_features0`, se isso vai para save/replay, e a ordem de
prioridade que você confirmou no carregador.
