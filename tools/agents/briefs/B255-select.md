# Tarefa B255-select — upstream #255

"Ram mismatch when pressing select while in inventory menu." Sem anexo.
Siga **`docs/engine/BUGS-PROTOCOLO.md`**.

Comece de um ponto em que o Link anda livre no overworld com vários
itens, por exemplo `--replay-ref 1 --stop-replay-at 91000` (o arco já foi
recebido no frame 87028; confira com `--mod-trace` que o módulo é 9 e não
há diálogo aberto). No roteiro: `Start` para abrir o menu, espere ele
abrir (módulo 14 no trace), aperte `Select` (varie o momento e a
duração; tente também com o cursor em itens diferentes e com o menu
ainda animando). Tente também dentro de uma dungeon (outro replay) — o
Select no menu muda de comportamento conforme o lugar.

Se não reproduzir com método, pare e relate o que tentou.
