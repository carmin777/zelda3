# V5 — revisão, rodada 2

Revisei `6e881bf`. Câmera, sombras, perfis, BG1 do overworld, HDMA por faixa
e tela dividida estão bons. Um problema antes do merge:

## Conflito de teclas

O `zelda3.ini` do repositório já liga **`WindowBigger = Ctrl+Up`** e
**`WindowSmaller = Ctrl+Down`** (linhas ~254–255). Os padrões novos
`VoxelCameraUp/Down = Ctrl+Up/Down` colidem com eles: quem usa o ini padrão
perde uma das duas funções. `Ctrl+0`, `Ctrl+-` e `Ctrl+=` também aparecem
no exemplo comentado de `ReplayRef`.

1. Troque os padrões da câmera do voxel para uma combinação livre (por
   exemplo **Alt+setas** para girar/inclinar e **Alt+=/Alt+-/Alt+0** para
   zoom/volta), conferindo contra **todas** as teclas já usadas no
   `kDefaultKbdControls` e no `zelda3.ini`.
2. Acrescente as teclas novas ao `[KeyMap]` do `zelda3.ini` (com
   comentário), como as outras, para o usuário ver e mudar.
3. Documente as teclas no `RENDER.md`/`plugins/voxel/README.md`.

## Verificação

`make -j2`, `make -j2 NO_LUA=1` e os plugins sem warnings;
`tools/regress.sh` com todas as linhas `ok` e a linha final. Commit novo na
mesma branch.
