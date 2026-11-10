# Tarefa R3c-oraculo-seed-2 — avançar o oráculo da seed

Leia `AGENTS.md` e **`docs/engine/RANDOMIZER-ORACULO.md`** (escrito no
R3b: método, `--oracle-rom`, `--oracle-ignore-jp`, `PatchRomSeed`, a lista
ordenada das divergências). Material fora do git: `japanese.sfc`, a seed em
`.agents/alttpr/download/`, referências do `z3randomizer` em
`.agents/refs/z3r/`.

## Onde paramos

Com `--oracle-ignore-jp`, o oráculo passa 8 frames e diverge no frame 9 na
paleta da seleção de arquivo (`SetFileSelectPalette`, `org $8CCDA5`). No
modo estrito, para no frame 1 numa diferença JP 1.0 × US em
`LoadFileSelectGraphics`.

## O que fazer

1. Implemente em C, no modo seed (só com a ROM da seed carregada), os
   próximos comportamentos do `z3randomizer` da lista, **um de cada vez**,
   cada um com a divergência antes e depois: `SetFileSelectPalette`, a
   tabela em `$7F5B02..` (ache a origem) e o que vier depois, até criar um
   arquivo novo na seed e dar os primeiros passos no jogo.
2. Comece o **modo JP 1.0** pelas diferenças que aparecerem no caminho
   (como `LoadFileSelectGraphics`): no C, atrás do mesmo modo seed,
   reproduza o que a JP 1.0 faz de diferente da US, com o endereço da
   rotina JP no comentário. Atualize a lista das 58 operações do
   `PatchRom` que dependem disso.
3. **Torne o progresso permanente**: um replay da seed para o oráculo
   (gravado com `--oracle-rom`, como os outros replays) e uma entrada no
   portão que rode só quando `japanese.sfc` e a seed existirem (pule com
   aviso claro se não existirem, para o portão continuar funcionando sem
   elas).
4. Atualize o `RANDOMIZER-ORACULO.md`: até onde o oráculo vai agora, a lista
   restante e a estimativa.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final (o caminho normal não muda); saídas reais
das divergências antes/depois de cada passo; quantos frames o oráculo da
seed passa no fim.
