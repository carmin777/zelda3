# Tarefa B236-livro-botao — upstream #236

"Book of Mudora doesn't work when equipped to anything but Y" (comentário:
o espelho também). Com `ItemSwitchLR` ligado o jogo permite usar itens em
outros botões (extensão do zelda3: veja `kFeatures0_SwitchLR` e o
`zelda3.ini`). Com features desligadas o oráculo compara com a ROM — então
primeiro descubra **se isso é sequer possível sem a feature**. Siga
`docs/engine/BUGS-PROTOCOLO.md`.

- Se o bug só existe com `ItemSwitchLR` ligado, o oráculo não ajuda
  (compare: off). Nesse caso: leia o código da extensão (onde ele mapeia
  X/L/R para itens) e compare com o caminho do Y para o livro (diante da
  placa do deserto — o replay de referência 1/2 tem esse momento; veja a
  tarefa B242 já mergeada em `src/sprite_main.c`
  `Sprite_B3_PedestalPlaque`) e para o espelho. Corrija a extensão para o
  livro/espelho funcionarem igual ao Y, e prove no headless com
  `--mod-trace` (a cena da placa acontece: diálogo/módulo) usando uma config
  com `ItemSwitchLR = 1` (crie `tests/regress-switchlr.ini` só para isso) e
  um roteiro que usa o livro pelo botão X/L/R.
- Grave o caso como replay em `tests/replays-features/` (pasta nova; o
  `regress.sh` atual só roda `tests/replays/` com compare: on — **não**
  misture). Documente no relatório como rodar.
