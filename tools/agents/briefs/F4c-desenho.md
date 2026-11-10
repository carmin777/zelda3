# Tarefa F4c-desenho — mods desenham na tela

Leia `docs/engine/F4-PLANO.md` (F4c) e `docs/engine/MODS.md` (API Lua). Os
scripts passam a poder desenhar por cima da imagem do jogo (HUD próprio,
contador de randomizer, overlay de debug) **sem tocar na simulação**.

## Fatos (verificados)

- `src/main.c` `DrawPpuFrameWithPerf()` chama
  `ZeldaDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags)`
  (`src/zelda_rtl.c`), que roda o PPU linha a linha no buffer. Descubra o
  formato do pixel (bytes por pixel, ordem) e as dimensões reais
  (`g_snes_width`, `g_snes_height`, `extraLeftRight`, altura 224/240).
- **`ZeldaDrawPpuFrame` escreve estado** (`irq_flag = 0`, registradores
  do PPU, HDMA). O headless hoje nunca desenha. Não mude isso.

## O que fazer

1. **Evento `FrameDraw`** (novo em `kModEv_*`, depois dos existentes —
   não reordene os ids antigos) emitido logo depois de `ZeldaDrawPpuFrame`,
   no caminho com janela. Os handlers Lua desse evento podem chamar
   `zelda.draw.*`; as chamadas viram uma lista de comandos aplicada no
   buffer depois do PPU e antes da apresentação.
2. **API Lua `zelda.draw`** (só válida dentro de `FrameDraw`; fora dele →
   erro Lua):
   - `zelda.draw.rect(x, y, w, h, cor)` — preenchido; `cor` = `0xRRGGBB`
     ou `0xAARRGGBB` (alfa 0–255, mistura simples).
   - `zelda.draw.pixel(x, y, cor)`
   - `zelda.draw.text(x, y, texto, cor)` — fonte bitmap própria, pequena
     (ex.: 5×7 ou 8×8, ASCII 32–126), **escrita por você** no código (não
     use a fonte do jogo nem baixe fontes). Retorna a largura em pixels.
   - `zelda.draw.width()`, `zelda.draw.height()` — tamanho da tela em
     pixels do SNES (com widescreen, inclui as laterais).
   - Coordenadas em pixels do SNES; recorte nas bordas; limite de
     comandos por frame (ex.: 4096) com erro claro se passar.
3. **Sem mexer na simulação**: dentro de `FrameDraw`, `zelda.state.set`,
   `zelda.item.set` e `zelda.link.*` para escrita devem dar erro Lua
   ("read-only during FrameDraw"). Leituras continuam liberadas.
4. **Screenshot no headless**: `--screenshot <arquivo.ppm>` — depois do
   último frame (depois de imprimir o `final`), renderiza **um** frame no
   buffer, emite `FrameDraw`, aplica os comandos e grava PPM (P6).
   Documente que é só no fim para não alterar a simulação.
5. **Mod de exemplo** `mods-examples/lua-hud/`: no `FrameDraw`, uma caixa
   semitransparente no canto com rúpias, vida/vida máxima, sala/área e o
   contador de frames.
6. `docs/engine/MODS.md`: seção "Desenho na tela".
7. Arquivos `.c` do jogo são CRLF: preserve (AGENTS.md, regra 7).

## Verificação a relatar

1. `make -j2` e `make -j2 NO_LUA=1` sem warnings.
2. `tools/regress.sh`: todas as linhas `ok` (15 casos) e a linha final.
3. `--replay-ref 0 --frames 3000 --screenshot a.ppm` sem mods e com
   `--mods-dir` só com o `lua-hud`: o `final` (hash) é **idêntico** nos dois
   e igual ao golden no frame 3000; os dois PPM diferem só na área do HUD
   (conte os pixels diferentes e dê o retângulo que os contém). Converta
   para PNG (`python3` sem dependências novas, ou `magick`/`convert` se
   existir) e salve em `.agents/shots/lua-hud.png` e `.agents/shots/sem-mod.png`
   (fora do git: imagem do jogo é conteúdo da ROM — **não commite**
   screenshots).
4. `zelda.state.set` dentro de `FrameDraw` → erro Lua, mod desativado.
   `zelda.draw.rect` fora de `FrameDraw` → erro Lua. Cole as saídas.
5. Diga se testou o caminho com janela ou só o headless (sem display aqui,
   provavelmente só o headless — tudo bem, mas diga).
