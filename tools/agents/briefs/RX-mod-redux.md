# Tarefa RX-mod-redux — montar o mod "redux" local de ponta a ponta

Leia `AGENTS.md`, **`docs/engine/REDUX-MAPA.md`** (seções 5–8),
`docs/engine/MODS.md` (inteiro) e os exemplos `mods-examples/opcoes-redux`,
`mods-examples/redux-ganchos`. ROM: `redux.sfc`
(fora do git). Fonte GPL do Redux em `.agents/redux-src/` (só leitura; não
copie código, tabelas nem texto dele para o repositório).

## O que fazer

1. `tools/modkit.py` ganha um jeito de **combinar** num único mod gerado
   localmente: o `import-rom` da ROM do hack + um preset de `"features"` +
   scripts Lua de ganchos (por exemplo `--features-from
   mods-examples/opcoes-redux --scripts-from mods-examples/redux-ganchos`).
   Documente. O mod gerado vai para `mods/redux/` (fora do git).
2. **Menu de 24 itens com os gráficos do Redux**: com `NewStyleInventory`
   ligado e os `kSprGfx`/`kBgGfx` + tabelas de HUD importados do Redux, o
   menu tem de ficar certo (o REDUX-MAPA mostrou que só os gráficos, sem o
   layout, quebram o menu). Corrija o que faltar no layout de 24 do
   `src/hud.c` para casar com os gráficos do Redux, **sem mudar nada com a
   opção desligada**.
3. **Barra de magia horizontal** e **cronômetro no menu** do Redux: como
   opções novas de `[Features]` (padrão desligado), aceitas no `"features"`
   de um mod; implemente lendo o asm do Redux só como referência de
   comportamento (com suas palavras, sem copiar).
4. Prova visual (PNGs em `.agents/shots/`, fora do git): HUD e menu do jogo
   com o mod `redux` completo, num ponto com vários itens (por exemplo
   `--replay-ref 9 --stop-replay-at <fim>` + `Start`), e o mesmo sem mod.
   Compare com a ROM do Redux rodando no emulador embutido
   (`./zelda3 --emu-rom redux.sfc --headless ...`, o modo do R2/R3a) no
   mesmo tipo de tela, se der.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final; um replay de feature para as opções novas
(barra horizontal, cronômetro) com golden; o que ainda difere do Redux.
