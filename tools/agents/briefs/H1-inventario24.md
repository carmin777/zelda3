# Tarefa H1-inventario24 — inventário de 24 itens como opção

Leia `AGENTS.md`, `docs/engine/MODS.md` (seção `"features"`),
`docs/engine/REDUX-MAPA.md` (seções 5, 6 e 8: menu de 24 itens).

## Fatos (confira)

- `src/hud.c:8` tem `kNewStyleInventory = 0` num `enum`: um layout de 24
  itens (um espaço por item, pá e flauta separadas, garrafas…) que o
  upstream deixou desligado em tempo de compilação. São ~28 usos, todos em
  `src/hud.c`. `src/hud.c` é **CRLF**.
- As opções de jogo vivem em `g_config.features0` / `enhanced_features0`
  (`src/features.h`), e a tabela única de nomes das opções está em
  `src/mod/mod_features.c` (serve ao `zelda3.ini` e ao `"features"` do
  `mod.json`).

## Antes de mexer (relate primeiro no relatório)

1. **Save**: o layout de 24 itens muda o que vai para a SRAM/save
   (inventário, slot equipado `hud_cur_item`, garrafas, pá/flauta)? Um save
   feito com a opção ligada abre certo com ela desligada, e vice-versa?
   Descubra lendo o código e testando no headless. Se houver incompatibilidade,
   **não invente conversão**: documente exatamente o que acontece.
2. Ligue `kNewStyleInventory = 1` numa cópia local só para ver se compila e
   se o menu abre (screenshot headless). Diga o que funciona e o que está
   incompleto (gráficos dos espaços extras? magic meter? textos?).

## O que fazer

1. Troque a constante de compilação por uma opção de jogo:
   `kFeatures0_NewStyleInventory` (próximo bit livre em `src/features.h`),
   nome `NewStyleInventory` no `[Features]` do `zelda3.ini` (padrão 0,
   comentário explicando) e na tabela de `mod_features.c`, para um mod
   ligar com `"features"`. `kHudItemCount` e as tabelas que dependem dele
   viram valores de execução; tabelas de tamanho fixo ficam no tamanho
   máximo.
2. **Sem a opção, nada muda**: o jogo tem que fazer exatamente o que faz
   hoje (portão verde). Com a opção ligada, a comparação lado a lado fica
   desligada como nas outras opções (confira que isso acontece sozinho).
3. Teste de feature: grave um replay curto com a opção ligada (protocolo
   em `docs/engine/BUGS-PROTOCOLO.md`, passo 5; replays de feature em
   `tests/replays-features/` com `.ini` próprio, golden em
   `tests/golden-features/`): abrir o menu, mover o cursor por vários
   espaços, equipar um item, fechar.
4. Screenshots headless (PNGs em `.agents/shots/`, fora do git) do menu com
   e sem a opção, num ponto do jogo com vários itens (por exemplo
   `--replay-ref 9 --stop-replay-at <fim>` + `Start`).
5. `docs/engine/MODS.md` e o comentário no `zelda3.ini`: o que a opção faz,
   o que ainda falta (por exemplo, gráficos do Redux para os espaços novos)
   e o efeito no save.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final (incluindo o caso novo); as respostas da
seção "Antes de mexer".
