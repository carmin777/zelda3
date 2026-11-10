# Tarefa R0-redux-mapa — o que o hack Redux exige do zelda3

Leia `AGENTS.md`, `docs/engine/PLANO.md`, `docs/engine/F4-PLANO.md`,
`docs/engine/MODS.md` e `docs/engine/HOOKS-MAPA.md`. Esta tarefa é **só
análise e documentação**: não mude código do jogo nem da engine.

## Material (fora do git, só leitura)

- Fonte do hack "A Link to the Past Redux" (GPL-3, ShadowOne333), commit
  `37c3098`: `.agents/redux-src/`.
  `code/main.asm` inclui tudo; `code/loz3-dx/` é o hack DX (Conn e
  qwertymodo) que o Redux usa como base; `code/optional/` e
  `patches/Optional/*.ips` são as opções; o `ReadMe.md` tem o changelog.
- ROMs já montadas a partir da ROM US do zelda3 (CRCs do BPS conferidos):
  `redux.sfc` (Redux) e
  `redux-retrans.sfc` (Retranslation).
  2 MB, LoROM, FastROM (bancos `$80+`).
- **Licença**: o zelda3 é MIT, o Redux é GPL-3. **Não copie código, tabelas
  nem texto do Redux para o repositório.** Descreva com suas palavras e cite
  o arquivo do Redux (`code/gameplay/misc.asm:12`) e o endereço.
- **Não rode** os binários de `redux-src/bin/` (asar, flips, scompress).

## O que fazer

1. Para **cada mudança** do Redux (cada arquivo `.asm` incluído por
   `main.asm`, cada patch de dados — mapas, salas, gráficos, texto — e cada
   patch opcional), descubra **o que ela faz de fato** lendo o assembly
   (não confie só no changelog) e qual rotina do zelda3 ela altera: ache a
   função C pelo comentário de endereço (`// 87a006`) e cite
   `arquivo.c:linha`.
2. Classifique cada uma em:
   - **A — já existe no zelda3** como opção (`zelda3.ini` `[Features]`,
     `DimFlashes`, MSU…). Compare o comportamento: igual, parecido (diga a
     diferença) ou diferente.
   - **B — dados importáveis**: gráficos, paletas, textos, tabelas, salas,
     mapas. Rode de verdade `python3 tools/modkit.py import-rom
     redux.sfc <pasta temporária fora de mods/>`
     (e o mesmo para a Retranslation) e diga o que já é importado, o que
     falha e por quê. Para salas e mapas: o zelda3 tem esses dados como
     assets (`kDungeonRoom*`, `kOverworld*`, ver `assets/restool.py` e
     `src/mod/asset_names.c`); diga se dá para importá-los da ROM Redux
     (ponteiros movidos? compressão?) e o que faltaria no modkit.
   - **C — script Lua** com a API que existe hoje: diga quais eventos e
     campos de `zelda.state`/`zelda.link`/`zelda.item` bastam e esboce o
     script em poucas linhas.
   - **D — precisa de um gancho novo** na engine (evento ou filtro em um
     ponto específico): diga qual função C e qual seria o gancho.
   - **E — comportamento novo** (F4e): menu de 24 itens, HUD novo, pá que
     desenterra itens etc. Descreva o que a engine teria de oferecer.
3. Escreva `docs/engine/REDUX-MAPA.md`:
   - uma tabela com uma linha por mudança (nome, arquivo do Redux,
     o que faz, rotina zelda3, classe A–E, notas);
   - um resumo com a contagem por classe e a ordem sugerida para um mod
     "redux" gerado localmente (como o `mods/ptbr-graficos`);
   - a lista de ganchos novos (classe D) e o que a classe E pede da F4e.
4. Prova: anexe ao relatório a saída real do `import-rom` (resumida) e, se
   der, screenshots headless (`--screenshot`, PNGs em `.agents/shots/`, fora
   do git) da tela de título e do menu com o mod importado temporário.
5. Commit só do `REDUX-MAPA.md` (inglês na mensagem; o documento pode ser em
   português como os outros de `docs/engine/`). Não commite ROMs, mods
   gerados nem imagens. `make -j2` e `tools/regress.sh` não são
   necessários se você não mudou código; diga se mudou algo.

## Relatório

Além do pedido no AGENTS.md: contagem por classe, as 5 mudanças mais
difíceis e por quê, e tudo o que você não conseguiu confirmar no assembly.
