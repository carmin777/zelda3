# Tarefa W3b-tabelas-hud — nomes de itens do menu em PT-BR (#281, parte 2)

A W3 (mergeada; leia `tools/modkit.py import-rom` e a seção
"Importar gráficos de uma ROM traduzida" em `docs/engine/MODS.md`)
trouxe título e seleção de arquivo em PT-BR, mas o **menu de itens**
continua em inglês ("ITEM", "BOW & ARROWS", "PENDANTS"): os nomes são
tabelas de índices de tiles que o zelda3 tem fixas em `src/hud.c`
(região SNES `$8DED09..$8DF9xx`, `kHudItemText` em `$8DF1C9`, segundo a
W3). A ROM PT (`./portuguese.sfc`) tem essas tabelas traduzidas.

## O que fazer

1. **Tabelas substituíveis (engine)**: um registro em `src/mod/`
   (`ModTable { name, void *ptr, size, rom_addr }`) das tabelas do jogo
   que um mod pode trocar. Comece pelas tabelas de texto/tilemap do HUD e
   do menu de itens em `src/hud.c` (descubra quais são; inclua só as que a
   ROM PT muda). Para isso elas deixam de ser `static const` (vire
   `static` sem `const`, ou exponha um ponteiro) — mudança mínima, sem
   alterar valores. Preserve CRLF.
2. **mod.json**: `"table_overrides": { "kHudItemText": "build/x.bin" }`,
   tamanho exato obrigatório (sem resize), caminho via `ModResolvePath`,
   aplicado na carga como os assets, e liga `g_mods_change_content`
   (compare: off). Nome desconhecido ou tamanho errado → mod recusado com
   mensagem clara.
3. **modkit `import-rom`**: além dos assets, lê cada tabela registrada no
   `rom_addr` da ROM traduzida (confira se a tradução moveu alguma: os
   ponteiros/código que acessam a tabela na ROM PT apontam para onde?),
   compara com os bytes da tabela na ROM US e escreve `table_overrides` só
   das que mudaram. Exporte o registro (nome, tamanho, endereço) para o
   Python sem duplicar à mão (ex.: gere um JSON a partir do C, ou o
   inverso — escolha e documente).
4. Também importe por padrão `kEnding_Credits_Text`/`kEnding_Credits_Offs`
   se forem o texto dos créditos traduzido (verifique; se forem, entram
   como categoria "texto"; os demais "other" continuam fora).
5. Prova visual (headless `--screenshot`, PNGs em `.agents/shots/`, fora
   do git): menu de itens com e sem o mod — mesmo roteiro da W3
   (`--replay-ref 1 --stop-replay-at 102482`, `Start`, ~60 frames). Se der,
   uma tela dos créditos (fim do replay 12?).
6. `make -j2` sem warnings; `tools/regress.sh` com todas as linhas `ok`
   (tabelas sem override = valores idênticos → hashes idênticos).
   `docs/engine/MODS.md` atualizado. Não commite nada de `mods/`.
