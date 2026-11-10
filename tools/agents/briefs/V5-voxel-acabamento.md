# Tarefa V5-voxel-acabamento — R-5: acabamento do voxel

Leia `AGENTS.md`, `docs/engine/RENDER-DESENHO.md` (aprovado; esta é a
etapa **R-5**: perfis, câmera livre, sombras/iluminação) e a seção R-4 do
**`docs/engine/RENDER.md`** (o plugin `plugins/voxel/` e as limitações que
ele listou).

## O que fazer

1. **Limitações da R-4**, na ordem:
   - BG1 do overworld sem camada semântica (hoje usa o atributo de BG2 na
     mesma célula): leve para a `FrameDesc` o que for preciso para BG1 ter
     a sua própria altura, ou documente por que o atributo de BG2 é o certo;
   - relevo deslocado em áreas com HDMA de rolagem: capture os atributos
     com a rolagem de cada linha (ou por faixa de linhas), não só a inicial;
   - tela dividida do `SharedWorld` com o voxel: um screenshot provando os
     dois painéis.
2. **Câmera livre**: teclas/atalhos para girar, inclinar e aproximar a
   câmera em jogo (com volta ao padrão), sem nunca mostrar tiles fora da
   `FrameDesc`. Ligar/desligar o voxel em jogo com uma tecla, voltando ao
   renderizador de referência.
3. **Sombras e luz simples**: sombra dos sprites (cartões) e das colunas no
   chão, e uma luz direcional; tudo ajustável no perfil e desligável.
4. **Perfis por hack**: mostre um perfil diferente do padrão funcionando
   (por exemplo, um perfil "plano" e um "exagerado"), e documente como um
   mod traz o seu perfil.
5. Atualize o `RENDER.md`.

## Prova

Screenshots em `.agents/shots/` (fora do git): tela dividida, câmera girada,
sombras ligadas e desligadas, os dois perfis; a área com HDMA de rolagem
antes e depois. `tools/regress.sh` verde; `ppu-ref` com 0 diferenças (spot
de 500 frames); custo por frame do voxel com sombras.

## Verificação a relatar

`make -j2`, `make -j2 NO_LUA=1` e o build dos plugins sem warnings;
`tools/regress.sh` com todas as linhas `ok` e a linha final. Não commite
`.so`/`.dll` nem screenshots.
