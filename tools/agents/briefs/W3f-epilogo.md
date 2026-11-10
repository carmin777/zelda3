# Tarefa W3f-epilogo — o texto do epílogo em PT

Leia `AGENTS.md`, `docs/engine/MODS.md` (importação de ROM traduzida,
créditos da W3d) e `src/ending.c`.

## O problema (visto na prova da W3d)

Com o mod `ptbr-graficos`, os créditos aparecem em PT, mas a frase do
final antes deles ("AND THE MASTER SWORD SLEEPS AGAIN… FOREVER!") e
possivelmente os outros textos do epílogo (as cenas de cada lugar do final)
continuam em inglês. Na ROM PT (`portuguese.sfc`,
fora do git) esses textos estão traduzidos.

## O que fazer

1. Descubra **de onde vêm** esses textos no zelda3 (tilemap, tabela, asset;
   `src/ending.c`, `assets/`), e onde a ROM PT os guarda, **seguindo o
   operando do código** da ROM PT (nunca endereço fixo da US). Se o
   formato da PT for diferente (como foi nos créditos), descreva e adapte.
2. Faça o `import-rom` importá-los por padrão para traduções, com o mesmo
   cuidado do W3d: sem mod, nada muda (o `ref12` passa pelo final com o
   oráculo ligado).
3. Prova visual: `--replay-ref 12` com o mod gerado numa pasta temporária,
   screenshots das cenas do epílogo e da frase da espada, com e sem o mod
   (PNGs em `.agents/shots/`, fora do git). O texto PT tem de aparecer
   legível e alinhado.

## Verificação a relatar

`make -j2`; `tools/regress.sh` verde com todas as linhas; `import-rom
portuguese.sfc` antes/depois (`diff -r`, só os arquivos novos); se mexer em
`assets/`, base reconstruída numa cópia isolada com SHA-1
`188bfc8630e55345dc4dfea0ef16bb9bff2cd483`.
