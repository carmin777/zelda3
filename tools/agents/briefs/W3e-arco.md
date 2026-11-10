# Tarefa W3e-arco — corrigir o texto deslocado do arco da tradução PT

Leia `AGENTS.md`, `docs/engine/MODS.md` (tabelas do jogo, importação de
ROM traduzida) e `src/mod/game_tables.def` (`kHudBowItemText`, 48 words em
`$8DF5C9`).

## Decisão (09/11/26)

A tradução PT de 2014 (`portuguese.sfc`,
SHA-1 `d0d09ed41f9c373fe6afdccafbf0da8c88d3d90d`) tem o texto do arco no
menu deslocado uma linha: o import fiel mostra bow2 = "Arco e",
bow3 = "Flecha/Arco", bow4 = "Arco e/Flecha de Prata" (este certo). Ficou decidido **corrigir**: bow2 = "Arco e / Flecha", bow3 = "Arco",
bow4 = "Arco e / Flecha de Prata". A W3b descobriu isso rodando a ROM PT
no emulador; confira você mesmo os bytes de `kHudBowItemText` importados.

## O que fazer

1. Em `tools/modkit.py`, uma **correção por dados, documentada**, aplicada
   só quando a ROM de origem tem exatamente esse SHA-1: reordena as linhas
   de `kHudBowItemText` para o resultado acima. O C continua genérico.
   Mensagem clara no import ("aplicando correção conhecida da tradução PT
   de 2014: texto do arco") e registro em `modkit-import.json`.
2. Deixe o mecanismo reutilizável (uma lista de correções conhecidas por
   SHA-1 de ROM), sem generalizar além disso.
3. `docs/engine/MODS.md`: documente a correção e por que existe.

## Verificação a relatar

`make -j2`; `tools/regress.sh` verde; `import-rom portuguese.sfc` numa pasta
temporária antes e depois (`diff -r`: só `kHudBowItemText.bin` e o json
mudam). **Prova visual**: screenshots headless do menu com o arco 2, 3 e 4
(mesma técnica da W3b: partir de um replay com o arco e escrever a
variável do arco; PNGs em `.agents/shots/`, fora do git), mostrando o texto
certo nos três.
