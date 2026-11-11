# R4 — rodada 5: o que falta das regras do z3r, com prova

Revisei e mergeei `9a83e31` (flags do `$B0:8000`: GT, Ganon, goal items,
HUD, mapa, Byrna, menu, beep). Traga a branch para o estado atual
(`git merge engine/modloader`); `git status` limpo antes de começar.

O `InvertedMode` é outra tarefa (R5); não mexa nele.

1. **Modos de Ganon 6–8 e 10–11**: implemente os contadores que eles pedem
   (chefes derrotados, itens coletados, etc.) a partir do `goalitem.asm` e
   do `stats.asm` do z3r, só com `Z3rRules`.
2. **Flags que esta seed não liga**, para a próxima seed não quebrar:
   `FakeBoots`, `QuickSwap`, `GenericKeys`, `ItemCounterHUD`, `MapHUDMode`,
   `NoBGM`, `FastFanfare`. Cada uma lida do `z3r_settings`, com o hook z3r
   de origem, e uma prova com o `z3r_settings` forjado num passo do teste
   (como a prova da GT), sem dados da seed no git.
3. **Provas dinâmicas que faltam**: dano do Ganon bloqueado e liberado
   pelo modo da seed; Rupoor; um item progressivo recebido duas vezes
   (espada 1 → 2, por exemplo).
4. **Smoke test da seed jogada**: com o `tools/seed_route.py` em C-only
   (`--seed-rom` não, aqui é o motor US com o mod da seed), gere uma rota
   curta que abra baús de verdade numa masmorra (Hyrule Castle ou Eastern)
   e confira os itens recebidos contra a tabela da seed. Vira um passo do
   portão (pulado com aviso sem a ROM da seed).

Docs e comentários impessoais. Verificação: `make -j2`, `make -j2 NO_LUA=1`
sem warnings; `tools/regress.sh` com todas as linhas `ok` e a linha final;
cobertura 248/248; tabela de flags do `RANDOMIZER-IMPORT.md` sem pendentes
além do `InvertedMode`.
