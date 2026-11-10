# Tarefa W5-prs-gles-dpi — trazer os PRs #270 (GLES) e #301 (DPI)

Leia `AGENTS.md` e `docs/engine/UPSTREAM-TRIAGEM.md`. Os diffs dos dois PRs
do upstream `snesrev/zelda3` estão em `.agents/refs/pr270.diff` (GLES, para
Raspberry Pi/Android) e `.agents/refs/pr301.diff` (DPI alto). Ficou decidido trazê-los para a nossa branch em 10/11/26.

## O que fazer

1. Aplique cada PR num commit próprio (com o autor original no
   `Co-authored-by:` e o número do PR na mensagem), resolvendo conflitos com
   o que mudou na nossa branch (renderizador, plugins de render, `opengl.c`,
   `glsl_shader.c`, `main.c`, `config.c`).
2. **#270 (GLES)**: deve compilar no Linux com o caminho GL normal e com o
   caminho GLES ligado (opção de build ou `OutputMethod`); confira que o
   plugin voxel (EGL/GL próprio) continua funcionando com o host em GL e em
   GLES, ou documente a limitação.
3. **#301 (DPI)**: janela com escala certa em tela de DPI alto; o tamanho
   padrão não muda em DPI normal.
4. Atualize `UPSTREAM-TRIAGEM.md` (os dois PRs como trazidos).

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings, e o build com GLES; o
`tools/regress.sh` com todas as linhas `ok` e a linha final; o voxel
carregando (`--render-plugin plugins/voxel/voxel.so --headless`); o que não
deu para testar sem o hardware (Raspberry/Android, monitor HiDPI) fica dito
no relatório como teste manual.
