# API de renderização e voxel — desenho para decisão

Rascunho de 09/11/26, feito com autorização para planejar sem
ele. **Aprovado em 10/11/26** (as cinco recomendações da seção
5); a R-1 começou como tarefa `V1-descricao-frame`.

## 1. Referências estudadas

| Referência | Como funciona | O que ensina |
|---|---|---|
| **3dSen** (Geod Studio, NES, comercial, fechado; 1.0 em 2025, Unity) | lê o estado gráfico de cada frame e monta "dioramas" de voxels; **cada jogo precisa de um perfil feito à mão** (amostragem de frame, regras de profundidade, Lua) | o resultado chama muita atenção, mas o custo é o perfil por jogo |
| **HD packs do Mesen** (NES) | troca cada tile por uma imagem maior identificada pelo conteúdo do tile + paleta; **condições** (`tileNearby`, `tileAtPosition`) resolvem tiles repetidos | substituição por tile é simples e extensível; ambiguidade se resolve com contexto |
| **Seamless World** (`mstan/ZeldaAlttPSNESRecomp`) | mundo expandido ao vivo, zoom, andares de masmorra conectados | dá para ir além da tela do SNES quando se conhece o jogo |
| **ALTTP-DX** | filtros CRT por shader, widescreen com câmera e HUD nas bordas | o "acabamento" que um port moderno oferece |

## 2. A vantagem do zelda3

Um emulador só vê tiles e paletas. O zelda3 **sabe o que cada tile é**:

- colisão de cada tile 8×8: `dung_bg1_attr_table` e `dung_bg2_attr_table`
  (masmorras, por camada) e `overworld_tileattr` (overworld): parede,
  chão, buraco, água, borda, escada, grama, etc.;
- altura do Link e dos sprites: `link_z_coord`, `sprite_z` (pulos,
  quedas, voo);
- camada de cada coisa (andar de cima/baixo nas masmorras), sala e área,
  tipo de cada sprite.

Com isso a **altura sai da própria lógica do jogo**: paredes sobem, buracos
descem, água fica rebaixada, bordas viram degraus, o Link pula de verdade.
Um "perfil" passa a ser só **ajuste fino** (por tile ou por tipo de
sprite), não o trabalho todo.

## 3. Proposta de mecanismo

### 3.1 Descrição do frame (a API)

Depois da simulação de cada frame, o motor monta uma **descrição do frame**
em vez de (ou além de) pixels:

- camadas de fundo: tilemap, rolagem, prioridade, paleta, gráficos dos
  tiles, janelas e matemática de cor do PPU;
- sprites (OAM): posição, tile, paleta, prioridade, tamanho, e quando
  der, **de quem é** (Link, tipo de sprite, ancilla) e a altura `z`;
- a camada semântica: atributo de colisão de cada tile visível, sala/área,
  andar;
- modo 7 (mapa-múndi) à parte.

Quem consome: o renderizador atual (pixels do PPU, como hoje), um
renderizador de **substituição de tiles** (HD pack) e um renderizador
**3D/voxel**. A simulação não muda; o oráculo continua valendo.

### 3.2 Plugin de renderização

Renderizadores alternativos são **plugins nativos** (decisão da F4e: fora
do Lua, fase própria): uma interface pequena e versionada (`begin_frame`,
`frame_description`, `present`), com o renderizador atual como
implementação de referência.

### 3.3 Voxel

- Cada célula 8×8 vira uma peça de diorama classificada pelo desenho: `flat`
  (chão/água, um quad), `top` (telhado/borda, arte na face de cima) ou
  `upright` (parede/árvore/fachada, arte reconstruída na face sul). A altura
  vem da colisão e do número de linhas desenhadas; adereços transparentes são
  extrudados por pixel, então arbustos e vãos de cerca não viram pilares.
  A cor continua sendo a do tile do jogo, como no 3dSen.
- Sprites viram "cartões" 3D ou voxels de 1 de espessura, posicionados
  com `z`.
- Câmera: perspectiva inclinada acima do Link, com níveis (off/15°/35°/50°) e
  transição suave. A câmera inclinada enxerga um trapézio maior que a tela do
  SNES; o plugin desenha **a área ou sala atual inteira que o jogo já carregou**
  (map16 do overworld, tilemap da sala, até 2×2 telas numa masmorra), sem
  revelar telas ou salas vizinhas. O que fica fora dessa janela é backdrop, e o
  HUD continua em 2D nítido.
- Perfil opcional por arquivo (dados, não código): altura por tile ou por
  tipo de sprite, exceções por sala.

## 4. Etapas

| Etapa | Entrega | Prova |
|---|---|---|
| **R-1** | **descrição do frame** extraída do PPU + camada semântica, com um visualizador de depuração (por exemplo, um dump por frame) | o renderizador atual reconstruído **a partir da descrição** dá os mesmos pixels que hoje |
| **R-2** | interface de plugin + o renderizador atual como plugin de referência | portão e screenshots iguais |
| **R-3** | plugin **HD/substituição de tiles** (estilo Mesen) | um pack de exemplo |
| **R-4** | plugin **voxel** com altura pela colisão | screenshots/vídeo de uma masmorra e de uma área |
| **R-5** | perfis de ajuste, câmera livre, sombras/iluminação | — |

A R-1 já se paga sozinha (HD pack, widescreen melhor, ferramentas de
depuração), antes de qualquer 3D.

## 5. Decisões em aberto

1. A API passa por uma **descrição do frame** (e não por pintar em cima
   dos pixels)? *Recomendo sim; é o que permite 3D e HD.*
2. Renderizadores alternativos como **plugins nativos** com interface
   versionada? *Recomendo sim.*
3. Altura do voxel **derivada da colisão** por padrão, perfil só como
   ajuste? *Recomendo sim; é a nossa vantagem sobre o 3dSen.*
4. Motor gráfico do plugin voxel: OpenGL (já usado pelo zelda3) primeiro,
   Vulkan/WebGPU depois? *Recomendo OpenGL primeiro.*
5. Ordem: R-1 antes do co-op ou depois? *São independentes; a R-1 é menor
   e dá visibilidade ao projeto mais cedo.*
