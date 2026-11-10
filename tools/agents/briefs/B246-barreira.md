# Tarefa B246-barreira — upstream #246

"Agahnim barrier fails to spawn with widescreen enabled" — "use o
savestate do bug da Zelda": é `tests/issues/issue245-start.sav` (Link no
Hyrule Castle com a Zelda; a barreira mágica na entrada da torre do
Agahnim). Só acontece com widescreen (`ExtendedAspectRatio` no ini, ex.
16:9, e `kFeatures0_ExtendScreen64` / `extraLeftRight`). Com feature
ligada o oráculo desliga (compare: off), então a prova é por trace/estado
e screenshot. Leia `docs/engine/BUGS-PROTOCOLO.md` e
`tests/replays-features/README.md`.

1. Reproduza: config de teste com widescreen (crie
   `tests/replays-features/issue246.ini` a partir de
   `tests/regress.ini`, ligando só o widescreen). Leve o Link até a
   barreira (roteiro a partir do `--load`/`--replay`+`--stop-replay-at` do
   save) e mostre que ela não aparece com widescreen e aparece sem: use
   `--mod-trace` (`SpriteSpawn` do tipo da barreira — descubra o id) e/ou
   `zelda.state`/`--dump-state`, e `--screenshot` (PNGs em
   `.agents/shots/`, fora do git).
2. Causa provável: com tela mais larga, algum teste de "sprite fora da
   tela" ou de spawn usa a largura nova e descarta a barreira. Ache o
   ponto (procure `ExtendScreen64`, `extraLeftRight`, `kFeatures0_` perto
   de spawn/culling de sprites), corrija **só no caminho com widescreen**
   (sem widescreen o código deve ficar idêntico — o regress normal prova).
3. Grave o caso em `tests/replays-features/issue246.sav` + `.ini` e o
   golden em `tests/golden-features/` (`tools/regress.sh --update` regrava
   todos: confira com `git diff` que só o novo apareceu). Mostre o trace
   antes/depois.
4. `tools/regress.sh`: todas as linhas `ok`, inclusive features.
