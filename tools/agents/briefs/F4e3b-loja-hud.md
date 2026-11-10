# Tarefa F4e3b-loja-hud — loja substituível e HUD de jogo do Redux

Leia `AGENTS.md`, `docs/engine/F4E-DESENHO.md` (F4e.3: substituição de
rotina), `docs/engine/MODS.md` (override de sprite da F4e.3a, itens novos da
F4e.5, `"features"`) e `docs/engine/REDUX-MAPA.md` (seções 5–8: HUD). Fonte
do Redux (GPL, só leitura, não copie) em `.agents/redux-src/`; ROM em
`redux.sfc` (fora do git).

## O que fazer

1. **Lojas**: um mod consegue definir o que uma loja vende (item, preço,
   quantidade) sem C, usando o override de sprite da F4e.3a nos lojistas
   ou um registro próprio (`zelda.shop.set(loja, {...})`), incluindo itens
   novos da F4e.5. Escolha o caminho mais simples e documente. Mod de
   exemplo `mods-examples/loja-nova/` (por exemplo, a loja de Kakariko
   vendendo a pena da F4e.5).
2. **HUD de jogo do Redux**: o RX deixou só a barra de magia horizontal;
   corações, contadores e caixa de item continuam nas posições antigas.
   Implemente o layout de HUD do Redux como opção de `[Features]`
   (por exemplo `ReduxHud`), lendo o asm do Redux só como referência, e
   ligue no preset `mods-examples/opcoes-redux`. Compare lado a lado com a
   ROM do Redux rodando no emulador (`--emu-rom redux.sfc --headless`).
3. Sem mod ou opção, nada muda (portão).

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final; replay de feature para a loja (comprar o
item novo) e para o HUD, com golden; screenshots lado a lado (nosso HUD com
o mod do Redux × ROM do Redux no emulador) em `.agents/shots/`, fora do git.
