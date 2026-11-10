# Tarefa V3-hd-tiles — R-3: plugin de substituição de tiles (HD pack)

Leia `AGENTS.md`, **`docs/engine/RENDER-DESENHO.md`** (aprovado; esta é a
etapa **R-3**) e **`docs/engine/RENDER.md`** (descrição do frame da R-1 e a
API de plugin da R-2, com o plugin `plugins/ppu-ref/` de referência).
Voxel (R-4) não faz parte desta tarefa.

## O que fazer

1. **Plugin `plugins/hd-tiles/`** (Makefile próprio, como o `ppu-ref`) que
   desenha o frame a partir da `FrameDesc` trocando tiles por imagens
   maiores, no estilo dos HD packs do Mesen:
   - um tile 8×8 (BG ou sprite) é identificado pelo **conteúdo** (hash dos
     bytes do tile na VRAM) **+ paleta**; se o pack tiver uma imagem para
     ele, desenha a imagem na escala do pack (2×, 4×); senão, desenha o
     tile original ampliado;
   - prioridades, camadas, janelas, matemática de cor e brilho continuam
     corretos (reaproveite a lógica do renderizador de referência onde der);
   - **condições** simples para tiles repetidos (por exemplo `room`/`area`,
     ou atributo de colisão da camada semântica), como o `tileNearby` do
     Mesen, mas só o necessário para o exemplo.
2. **Formato do pack**: pasta com um arquivo de texto/JSON que mapeia
   `hash do tile + paleta (+ condição)` → `png, x, y` e a escala. Documente
   o formato em `docs/engine/RENDER.md`.
3. **Ferramenta de extração**: um modo (`--frame-desc-dump` estendido ou
   um script em `tools/`) que, a partir de um frame, lista os tiles visíveis
   com hash e paleta e exporta uma folha PNG desses tiles, para quem for
   desenhar o pack saber o que trocar. **Essas folhas saem da ROM: ficam em
   `.agents/` ou numa pasta gitignored, nunca no git.**
4. **Pack de exemplo** feito por você, **sem nenhum pixel tirado da ROM**:
   por exemplo, três ou quatro tiles redesenhados à mão em 2× (grama,
   arbusto, um bloco de masmorra) como formas geométricas simples, só para
   provar o caminho. Ele pode ir para o git em `plugins/hd-tiles/example-pack/`
   porque é original.
5. Sem plugin, nada muda (portão).

## Prova

- Com o pack **vazio**, o `hd-tiles` em 1× dá **os mesmos pixels** do
  renderizador atual (`--frame-check` com o plugin; relate frames e
  configurações, como na V1/V2).
- Com o pack de exemplo, screenshots (antes/depois, overworld e masmorra) em
  `.agents/shots/`, fora do git, mostrando os tiles trocados no lugar certo
  e o resto igual.
- Custo por frame com o plugin.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; build do plugin;
`tools/regress.sh` com todas as linhas `ok` e a linha final. Não commite
`.so`/`.dll` nem nada extraído da ROM.
