# Tarefa B242-livro — upstream #242

"Ram mismatch when pressing y (using book) during desert prayer cutscene."
Ou seja: usar o Book of Mudora (equipado no Y) diante da placa que abre o
Desert Palace e apertar Y de novo durante a cena da "reza". Siga
**`docs/engine/BUGS-PROTOCOLO.md`**.

Pontos de partida:
- `tests/issues/issue242-start.sav` (anexo da issue; `--load`): é o estado
  em que o autor estava. Tocado desde o começo passa no oráculo.
- Melhor: algum replay de referência usa o livro na placa do deserto.
  Ache o frame com `--mod-trace` (procure diálogo/módulo da cena perto da
  entrada do Desert Palace; os replays 1 e 2 são candidatos), comece um
  pouco antes com `--stop-replay-at`, e no roteiro repita o que o replay
  faria até a cena, apertando Y durante ela. Varra o frame do Y.

Se não reproduzir com método, pare e relate o que tentou.
