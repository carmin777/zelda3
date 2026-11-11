# V6 — gauntlet, oitava leva (agente novo)

Você está assumindo uma tarefa que outro agente levou de r1 a r34. Não há
histórico de conversa: o estado está no código e nestes arquivos.

- `plugins/voxel/README.md` e a seção do voxel em `docs/engine/RENDER.md`:
  como o renderizador funciona (modos de arte, perfil, faces, câmera).
- `docs/engine/ESTADO.md`, linha 43: o que cada leva fez.
- `tools/voxel_gauntlet.sh <tag>` renderiza 15 cenas (2D à esquerda, voxel à
  direita, 16:9) em `.agents/shots/gauntlet/<tag>/`; métricas em
  `metrics.txt`. Referência atual: `.agents/shots/gauntlet/r34-final/`.

Primeiro traga a branch para o estado atual (`git merge engine/modloader`).
**Commite a cada ciclo.** Siga a seção "Economia de contexto" do
`AGENTS.md`: `voxel.c` tem mais de 3 mil linhas, leia por trechos.

## O que fazer

1. **Revisão própria, de olhos novos**: rode o gauntlet, olhe as 15 cenas
   lado a lado e liste os 3 piores defeitos visuais que você vê (o que um
   jogador estranharia), com a cena e a causa no código.
2. Conserte esses 3. Já sabidos pelo revisor: copas da Lost Woods (`13`) e
   da borda direita do campo (`03`) ainda recortadas em degraus quadrados;
   escadinhas serrilhadas nas bordas de barranco.
3. Nada pode regredir nas cenas boas (`02`, `04`, `05`, `07`, `08`, `09`,
   `10`, `11`, `12`, `14`). Para cada cena que mudar, compare o PNG com o
   `r34-final`, não só a métrica.

No fim: lista dos 3 defeitos e o que fez, tabela de métricas, caminho do
`grid.png` final, `tools/regress.sh` com todas as linhas `ok` e a linha
final, `ppu-ref` com 0 diferenças, builds sem warnings. Nada de código do
DramaticShape/PotatoVoxel.
