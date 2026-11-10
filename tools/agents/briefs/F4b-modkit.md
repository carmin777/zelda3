# Tarefa F4b-modkit — mods com conteúdo em YAML/TXT/PNG

Leia `docs/engine/F4-PLANO.md` e `docs/engine/MODS.md`. Hoje um mod só
consegue trocar assets binários. Esta tarefa fecha o ciclo: o autor edita as
fontes legíveis (salas em YAML, diálogo em texto, sprites em PNG) e uma
ferramenta gera os overrides binários. **Só Python e documentação: não
altere nenhum `.c`/`.h`.** (Outro agente mexe no C em paralelo.)

## Fatos (verificados)

- `assets/restool.py` (`--help` funciona) extrai da ROM (`-e -r <rom>`) para
  `assets/dungeon/*.yaml` (322 salas: `Header`, `Sprites`, `Secrets`,
  `Chests`, `Entrances`, `Layer1..3`), `assets/overworld/*.yaml` (112),
  `assets/sprites/*.png`, `assets/dialogue.txt`, e compila com
  `assets/compile_resources.py` em `zelda3_assets.dat`.
  `--languages L1,L2` inclui diálogos traduzidos (ex.: `pt` usa
  `assets/dialogue_pt.txt` e `font_pt.png`).
- Neste worktree essas pastas extraídas **não existem** (não são
  versionadas). `./zelda3.sfc` é a ROM US (link). Extraia você mesmo aqui.
- O formato de `zelda3_assets.dat` é lido por `LoadAssets()` em
  `src/main.c` (cabeçalho de 88 bytes + tabela de tamanhos; assets
  alinhados a 4). Os nomes ficam em `src/mod/asset_names.c`.
- O mod loader aceita `"asset_overrides": {"kNome": "arq.bin"}` ou
  `{"file": ..., "allow_resize": true}`; tamanho diferente exige
  `allow_resize`.
- **Os assets locais são compilados com `--languages=pt`.** Um override de
  `kDialogue` gerado sem `pt` apagaria a tradução dele.

## O que fazer

1. `tools/modkit.py build <pasta-do-mod> [--languages L1,L2] [--rom zelda3.sfc]`:
   - Garante uma extração base (cache em `.modkit/base/`, ignorado pelo
     git; reextrai se não existir).
   - Copia a base para um diretório temporário e sobrepõe os arquivos de
     `<mod>/content/` nas mesmas posições relativas (`content/dungeon/
     dungeon-260.yaml`, `content/dialogue.txt`, `content/dialogue_pt.txt`,
     `content/sprites/...png`, ...).
   - Compila **a base e a versão do mod pelo mesmo caminho e com os mesmos
     `--languages`**, cada uma em seu diretório temporário, e compara as duas
     `.dat` asset por asset (nunca compare com o `.dat` do usuário).
   - Para cada asset diferente grava `<mod>/build/<kNome>.bin` e atualiza o
     `asset_overrides` do `mod.json` (preserva os outros campos e a
     formatação razoável); se o tamanho mudou, usa a forma objeto com
     `allow_resize: true`. Remove do `mod.json` overrides gerados antes que
     não são mais necessários (marque os gerados com um campo
     `"generated_by": "modkit"` no mod.json ou numa lista separada — você
     decide, documente).
   - Grava `<mod>/build/modkit.json` com idiomas usados, hash SHA-1 de cada
     asset base comparado e a versão da ferramenta.
   - Imprime um resumo: assets mudados, tamanho antes/depois.
   - Sem nenhuma diferença → zero overrides e mensagem dizendo isso.
2. `tools/modkit.py extract <dungeon|overworld|dialogue> <id> <pasta-do-mod>`:
   copia o arquivo base correspondente para `content/` para o autor editar.
3. Mod de exemplo `mods-examples/bau-yaml/`: muda o baú da casa do Link
   (sala 260, `dungeon-260.yaml`, chave `Chests`) para o bumerangue, mesmo
   efeito do `bau-da-casa`, mas pelo YAML. Commite `content/` e o `build/`
   gerado.
4. `docs/engine/MODS.md`: seção "Conteúdo editável (modkit)" com o fluxo
   completo, inclusive o aviso sobre `--languages` (quem joga com `pt` deve
   construir o mod com `--languages=pt`).

## Verificação a relatar

1. `tools/modkit.py build` num mod com `content/` vazio → zero overrides.
2. `bau-yaml`: quais assets mudaram (esperado: `kDungeonRoomChests`, talvez
   mais — explique cada um) e, no headless, `./zelda3 --config
   tests/regress.ini --headless --mods-dir mods-examples --mod-trace
   --replay-ref 0 --frames 3400 zelda3.sfc` mostra `ItemReceive 2` no baú
   da sala 260. (O `bau-da-casa` também está em `mods-examples`; desative-o
   para o teste com `"enabled": false` temporário, ou use outra pasta, e
   diga qual.)
3. Mudar uma fala em `content/dialogue.txt` → diga qual asset mudou e prove
   no jogo headless se possível (ex.: `--mod-trace` mostra `DialogueOpen`
   do mesmo id; não precisa ver a tela).
4. `--languages pt` com `dialogue_pt.txt`: se não houver
   `dialogue_pt.txt`/`portuguese.sfc` neste worktree, diga isso e teste o
   caminho com outro idioma disponível ou explique o que faltou.
5. `tools/regress.sh` verde (deve continuar, já que o C não muda).

## Fora do escopo

Qualquer código C, editor gráfico, empacotamento de mods para distribuição.
