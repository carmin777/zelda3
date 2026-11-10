# Tarefa B245-zelda — upstream #245

"Ram mismatch when dying/continuing indoors with zelda follower" (com o
velho seguindo não acontece). Siga **`docs/engine/BUGS-PROTOCOLO.md`**.

Ponto de partida conferido: `--load tests/issues/issue245-start.sav` e
`--mod-trace` mostram o Link morrendo no frame 86 (`LinkDamage`,
`LinkDeath`, `ModuleChange 18 7`) e o diálogo 3 abrindo no frame 405 — é a
tela de continuar. Escolha as opções (continuar / salvar e continuar) com
o roteiro, varrendo o momento do botão, até o oráculo acusar.

Se não reproduzir com método, pare e relate o que tentou.
