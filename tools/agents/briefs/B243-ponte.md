# Tarefa B243-ponte — upstream #243

"Ram mismatch when throwing bombs off death mountain bridge." Sem anexo.
Siga **`docs/engine/BUGS-PROTOCOLO.md`**.

A ponte fica na Death Mountain, no caminho para a Tower of Hera: o replay
de referência 3 ("After Tower of Hera") passa por lá, e outros podem
passar também. Ache o trecho com `--mod-trace` (`AreaEnter` das áreas de
Death Mountain; descubra os ids no código/`assets`), pare o replay com o
Link na ponte (ou logo antes) e no roteiro: equipar bombas se preciso,
pôr uma bomba (`Y`), levantá-la (`A`) e arremessar (`A`) para fora da
ponte, em direções diferentes. Varie o momento e a posição.

Se não reproduzir com método, pare e relate o que tentou.
