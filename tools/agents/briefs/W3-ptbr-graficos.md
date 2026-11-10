# Tarefa W3-ptbr-graficos — menus e itens em PT-BR (upstream #281)

O alvo é a tradução PT-BR (`portuguese.sfc`, patch Hyrule
Legends/Trans-Center v1.0 BETA). O zelda3 só aproveita **diálogo e fonte**
de ROMs traduzidas; menus, nomes de itens e gráficos de interface continuam
em inglês porque vêm da ROM US. Objetivo: levar os gráficos traduzidos para
o jogo **por um mod**, gerado localmente a partir da ROM local.

Leia `docs/engine/MODS.md` (mod loader, overrides por nome, modkit) e
`tools/modkit.py`.

## Fatos (medidos)

- `zelda3.sfc` 1 MB (US). `portuguese.sfc` 2 MB (a tradução expandiu a
  ROM). `cmp` por banco de 0x8000: diferenças grandes em 0x0E (7.182 bytes),
  0x11–0x18 e 0x1C (quase inteiros), pequenas em 0x00, 0x08, 0x0A, 0x0C,
  0x0D, 0x0F. Ou seja: gráficos provavelmente traduzidos, possivelmente
  **realocados** e com código alterado para apontar para eles.
- `assets/restool.py` / `extract_resources.py` / `compile_resources.py`
  extraem da ROM US para PNG/YAML e compilam os 165 assets
  (`src/mod/asset_names.c` tem os nomes). Gráficos: `kSprGfx`, `kBgGfx`,
  HUD, etc. — descubra quais e de onde cada um é lido na ROM.
- O jogo C não executa código da ROM traduzida; só os **dados** importam.

## O que fazer

1. Descubra quais assets de gráfico/tilemap contêm texto em inglês da
   interface (menu de itens, nomes de itens, "-LIFE-", tela de seleção de
   arquivo, mapa, etc.) e onde esses dados ficam na ROM PT. Se a tradução
   realocou, descubra os ponteiros novos (o código 65816 da ROM PT que
   carrega aquele conjunto aponta para outro lugar) — pode usar o emulador
   do projeto (`snes/`) para rodar a ROM PT e ver o que é carregado.
2. Implemente `tools/modkit.py import-rom <rom-traduzida> <pasta-do-mod>
   [--assets k1,k2]`: extrai os assets da ROM traduzida pelo caminho certo
   (com os ponteiros novos quando houver), compila, compara com a base US e
   escreve os overrides no mod (mesmo esquema `build/` + `mod.json` do
   `build`). Relate todo asset que diferir e **separe**: gráficos/tilemaps de
   interface (entram) × qualquer outra coisa (salas, lógica, tabelas — não
   entram por padrão; liste para eu decidir).
3. Gere o mod em `mods/ptbr-graficos/` **no checkout do worktree, sem
   commitar** (é conteúdo da ROM local; `mods/` não é versionado —
   confira). Commite só o código do modkit e a documentação em
   `docs/engine/MODS.md` ("Importar gráficos de uma ROM traduzida").
4. Prova visual sem display: use `--screenshot` (headless, renderiza o
   último frame) com um roteiro que abre o menu de itens (`Start`) num
   ponto com itens (ex.: `--replay-ref 1 --stop-replay-at 102482`, depois
   `Start`, espere ~60 frames). Gere PNG (python puro) **sem** e **com** o
   mod em `.agents/shots/w3-menu-us.png` e `w3-menu-pt.png` (fora do git).
   Faça o mesmo para a tela de seleção de arquivo se conseguir chegar nela.
   Eu vou olhar as imagens; você não precisa avaliar o texto visualmente,
   mas diga quantos pixels mudaram e em que região.
5. `tools/regress.sh` com todas as linhas `ok` (o C não deve mudar; se
   precisar mudar, explique).

Se a realocação for complexa demais para resolver com método, pare e
relate o que achou (bancos, ponteiros, assets) — isso já tem valor.
