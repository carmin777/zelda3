# Tarefa V2-plugin-render — R-2: interface de plugin de renderização

Leia `AGENTS.md`, **`docs/engine/RENDER-DESENHO.md`** (aprovado; esta é a
etapa **R-2**, seção 3.2) e **`docs/engine/RENDER.md`** (o que a V1 entregou
e a seção "O que a R-2 vai precisar"). R-3 (HD) e R-4 (voxel) não fazem
parte desta tarefa.

## O que fazer

1. **API pública e versionada** em `src/mod/render_plugin.h` (C puro, sem
   dependência de SDL nem de structs internas do PPU): a descrição do frame
   como estrutura pública (pode ser a `FrameDesc` atual exposta por um
   cabeçalho estável, com `kFrameDescVersion`), mais o ciclo de vida:
   `init(host, version)`, `begin_frame`, `frame_description`, `present`,
   `shutdown`. Documente quanto tempo cada ponteiro é válido e que o plugin
   copia o que quiser guardar.
2. **Plugins nativos carregados de arquivo** (`dlopen`/`LoadLibrary`) a
   partir de `[Graphics] RenderPlugin = <caminho>` ou `--render-plugin`.
   Checagem de versão na carga (recusa com mensagem clara se não bater).
   Sem plugin configurado, o caminho de hoje fica **idêntico** (portão).
3. **Renderizador atual como plugin de referência**: o mesmo caminho do
   `FrameDesc_Render`, empacotado pela interface. Ele pode ficar embutido no
   binário (registrado como `builtin:ppu`) e **também** existir como um
   plugin de exemplo compilado à parte (`plugins/ppu-ref/`, com o seu
   Makefile), para provar que a carga por arquivo funciona.
4. **Tela dividida**: decida e documente como o plugin recebe a tela
   dividida do `SharedWorld` (uma descrição por painel com a posição do
   painel é o mais simples). A V1 pulou esses frames na checagem; feche a
   checagem para eles também, se der.
5. Leve para a descrição o que ainda é só do renderizador e o plugin
   precisa: escala, espaço extra lateral atual e overlays de mods
   (`zelda.draw`), ou documente por que fica para depois.
6. Atualize o `RENDER.md`.

## Prova

- Com `--render-plugin plugins/ppu-ref/<arquivo>`, os replays de
  `tools/regress.sh` dão **os mesmos pixels** que o caminho embutido
  (estenda o `--frame-check` ou crie um modo equivalente; relate quantos
  frames e configurações, como na V1).
- Plugin com versão errada é recusado sem travar o jogo.
- Sem plugin, nada muda: `tools/regress.sh` com todas as linhas `ok` e a
  linha final.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; o build do plugin de exemplo;
`tools/regress.sh`; a checagem de pixels pelo plugin; custo por frame com e
sem plugin. Não commite binários do plugin (`.so`/`.dll`): adicione ao
`.gitignore`.
