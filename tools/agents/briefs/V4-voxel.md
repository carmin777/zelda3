# Tarefa V4-voxel — R-4: plugin voxel com altura pela colisão

Leia `AGENTS.md`, **`docs/engine/RENDER-DESENHO.md`** (aprovado; esta é a
etapa **R-4**, seção 3.3; decisões: altura **derivada da colisão** por
padrão, perfil só como ajuste, **OpenGL primeiro**) e **`docs/engine/RENDER.md`**
(a `FrameDesc` com a camada semântica, a API de plugin v2, os plugins
`ppu-ref` e `hd-tiles` como exemplo de estrutura).

## O que fazer

1. **Plugin `plugins/voxel/`** (Makefile próprio) que monta uma cena 3D a
   partir da `FrameDesc`:
   - cada tile 8×8 visível de BG1/BG2 vira uma coluna de voxels com a
     **altura vinda do atributo de colisão** (tabela padrão por tipo:
     parede sobe, chão 0, buraco desce, água rebaixada, borda/degrau meio
     bloco, grama baixa…) e a **cor dos pixels do próprio tile** (o tile
     decodificado da VRAM com a paleta, como o renderizador faz);
   - sprites viram cartões (quads) em pé, posicionados com o `z` da
     descrição (o Link pula de verdade);
   - camadas sem semântica (HUD/BG3, texto, modo 7) ficam como hoje: o
     HUD desenhado por cima em 2D, e o modo 7/telas especiais caem no
     renderizador de referência (`host->render_frame_desc`).
2. **Render em OpenGL** (a máquina tem GL e EGL), num framebuffer fora da
   tela, lido de volta para o buffer de `present`. Funcione também em
   `--headless` (EGL sem janela, ou outro contexto oculto); se o contexto
   GL falhar, o plugin cai no renderizador de referência com mensagem
   clara, sem travar.
3. **Câmera**: perspectiva inclinada acima do Link (ângulo e distância por
   `[Graphics]` ou por variável do plugin), enquadrando a mesma área da
   tela do SNES (não revelar o que o jogo esconde).
4. **Perfil opcional** (arquivo de dados, não código): altura por tipo de
   atributo e exceções por sala/área/tile. Um perfil padrão no repositório.
5. Documente no `RENDER.md`: tabela de altura padrão, câmera, perfil, o
   que ainda fica 2D, custo.

## Prova

- Screenshots em `.agents/shots/` (fora do git): uma área do overworld e
  uma sala de masmorra, cada uma lado a lado com o render normal; e uma
  sequência curta (frames numerados ou GIF) do Link andando e pulando
  numa delas.
- Sem plugin, nada muda: `tools/regress.sh` com todas as linhas `ok` e a
  linha final. O `ppu-ref` continua com 0 diferenças (spot de 500 frames).
- Custo por frame do plugin.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; build do plugin;
`tools/regress.sh`; as provas acima. Não commite `.so`/`.dll` nem
screenshots.
