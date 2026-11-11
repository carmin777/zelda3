# Tarefa V6-voxel-diorama — refazer o visual do voxel como diorama

Leia `AGENTS.md`, `docs/engine/RENDER-DESENHO.md`, a seção do voxel em
`docs/engine/RENDER.md` e o código atual em `plugins/voxel/`.

## Por que refazer

Jogado numa janela, o voxel atual ficou estranho (screenshots locais em
`.agents/shots/voxel-user-voxel.webp` e `voxel-user-normal.webp`): colunas listradas com a arte do
tile esticada nas laterais, cercas e arbustos virando pilares, a fachada da
casa como uma caixa, o Link marrom e o soldado cinza (paleta de sprite
errada), e o topo da imagem cortado.

## Referência (estude, **não copie**)

O "DramaticShapeVoxelMod" para o Gen1Recomp é a referência visual. Cópia
pública: `github.com/scottcandy34/DramaticShapeVoxelMod-latest`. O README dele
diz que **redistribuir o código é proibido sem permissão**: leia as ideias nos
comentários e docs (`lib/TileShape.lua`, `lib/Voxel3D.lua`,
`lib/VoxelScene.lua`, `lib/TerrainAtlas.lua`, `lib/TiltShift.lua`,
`lib/Shadows.lua`, `assets/docs/buidling_to_voxel/sprite_to_voxel_methodology.md`),
e escreva tudo do zero, em C, no nosso plugin. Nada de código, tabelas ou
dados dele no repositório; cite a referência no doc.

Os princípios dele que importam aqui:

1. **Modo de arte por tile, não só altura.** `flat` (chão, água: um quad,
   sem caixa), `top` (bordas, telhados: arte na face de cima, porque o 2D
   mostra visto de cima), `upright` (paredes, árvores, cercas, placas,
   fachadas: a face sul da caixa reconstrói a arte 2D em pé, porque o 2D
   mostra de frente — a "regra de dobrar para cima"). No ALttP (visão 3/4)
   isto é central: o telhado da casa é `top`/inclinado e a fachada é
   `upright`.
2. **Classificar por célula de colisão, não por tile.** Tile decorativo em
   célula andável é chão (flores e tufos não viram pilares; o vão entre os
   postes da cerca não vira parede). Use a camada semântica da `FrameDesc`.
3. **Estruturas medidas pelo desenho.** Flood fill de cada objeto desenhado:
   adereços pequenos voxelizados por pixel na altura real desenhada, com
   transparência (um arbusto de 2 linhas é uma silhueta de 16 px, não uma
   caixa); coisas grandes sobem como volume com a altura medida da arte,
   ciente de repetição (uma fileira de árvores não vira um monólito).
4. **Profundidade resolve a oclusão** (depth buffer, sem ordenação por y);
   personagens como cartões inclinados para trás, com a paleta certa.
5. **Luz**: escurecer cada face pelo ângulo com o sol (por vértice) e sombra
   projetada de verdade (shadow map), desligável; tilt-shift opcional como
   pós-processamento só do mundo (o HUD fica nítido).
6. **Enquadramento**: a câmera inclinada enxerga um trapézio maior que a tela
   do SNES. Desenhe o que a câmera enquadra usando o mapa inteiro **da área
   ou sala atual** que o jogo já tem carregado (map16 do overworld, tilemap
   da sala), sem revelar áreas ou salas vizinhas. Documente essa mudança na
   regra do `RENDER-DESENHO.md`.

## O que fazer

1. Corrija primeiro a **paleta dos sprites** (metade de sprites da CGRAM) e
   prove com screenshot do Link verde e do soldado azul.
2. Reescreva a classificação e a malha do `plugins/voxel/` com os princípios
   acima (perfil de dados com exceções por tile/sala, como o nosso
   `default-profile.json`).
3. Câmera com níveis (por exemplo off / 15° / 35° / 50°) e transição suave;
   as teclas atuais continuam valendo.
4. Screenshots lado a lado (normal × voxel) em `.agents/shots/` (fora do git):
   a casa do Link com o soldado (mesmo enquadramento da queixa), o castelo
   por fora, uma sala de masmorra, Kakariko; e uma sequência curta andando.
5. Atualize `RENDER.md` e o `plugins/voxel/README.md`.

## Verificação

`make -j2`, `make -j2 NO_LUA=1`, `make -C plugins/voxel` sem warnings;
`tools/regress.sh` com todas as linhas `ok` e a linha final; `ppu-ref` com 0
diferenças (spot de 500 frames); custo por frame. Docs e comentários
impessoais.
