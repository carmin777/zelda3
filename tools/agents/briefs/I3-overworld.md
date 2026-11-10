# Tarefa I3-overworld — importar o overworld de uma ROM de hack

Leia `AGENTS.md`, `docs/engine/MODS.md` (ROM de hack, I1 e I2),
`docs/engine/ALTTI-MAPA.md` (com as correções) e, em
`assets/extract_resources.py` / `assets/compile_resources.py`, tudo o que
extrai e compila o overworld (`print_overworld`, `print_overworld_tables`,
áreas, map16/map32, entradas e saídas do overworld, buracos, segredos,
sprites, placas, cabeçalhos de área, gfx/paleta por área).

Esta tarefa é o **passo 4** do plano do ALTTI-MAPA. ROMs fora do git:
`altti.sfc` (Islands: Mundo da Luz novo,
Mundo Sombrio com só 3 áreas) e `redux.sfc` (Redux: 4 áreas). Não commite
ROMs, YAML extraído delas, mods gerados nem imagens.

## Regra que já pegou duas vezes (I1 e I2)

Toda tabela que o hack pode mover é achada pelo **operando da instrução
original que a lê**, com conferência dos bytes vizinhos e erro claro se
não reconhecer. Nunca o endereço fixo da US. Um ponteiro fora de janela de
ROM, ou um volume de dados absurdo, quer dizer **tabela errada**: ache a
instrução certa; não afrouxe verificações. Na ROM US o resultado tem de ser
exatamente o de hoje.

## O que fazer

1. Faça para o overworld o mesmo que o I2 fez para as masmorras: tabelas
   pelo código, extração do YAML do hack numa pasta temporária, compilação
   dos assets de overworld e import dos que diferirem da US. Liste no
   relatório cada tabela: endereço US, Islands, Redux.
2. Formato e limites: se algum dado do hack não couber no formato que o C
   carrega (tamanho fixo, número de áreas, tipos fora das tabelas US),
   **pare nesse ponto, não mude o C** e relate o tamanho real.
3. Dados inválidos (o ALTTI-MAPA cita segredos com tipo `0xA6`, posições
   ímpares, áreas 117–127 fora da tabela): confirme com a tabela certa se
   são reais; se forem, trate de forma explícita e liste o que foi trocado.
4. O ponto de início do jogo: o Islands começa na área 10. Diga se o
   starting point do hack passa a valer com o mod.

## Prova

1. `make -j2`; `tools/regress.sh` com todas as linhas `ok` e a linha final.
2. **Base idêntica, numa cópia isolada** (não no worktree, onde o
   `zelda3_assets.dat` é link para o checkout principal): `git archive` da
   sua branch, `restool.py -e` com a ROM US (o `git status` não pode mudar
   nenhum arquivo versionado) e build com `--languages pt` (copie
   `assets/dialogue_pt.txt` e `assets/font_pt.png` do checkout principal);
   o SHA-1 do `.dat` tem de ser `188bfc8630e55345dc4dfea0ef16bb9bff2cd483`.
3. `import-rom` do PT idêntico (`diff -r`); Redux e Islands: cole a saída.
4. **Visual** (headless, PNGs em `.agents/shots/`, fora do git): com o mod
   do Islands, um **jogo novo** começando no lugar do hack e duas áreas do
   Mundo da Luz; o mapa-múndi; com o Redux, o atalho para Kakariko. Cada
   uma com a imagem equivalente sem mod.
5. Diga o que não conseguiu provar. `docs/engine/MODS.md` e
   `docs/engine/ALTTI-MAPA.md` atualizados. Commit em inglês.
