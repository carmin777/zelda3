# Tarefa C3d-coop-acabamento — acabamento do co-op no mesmo mundo

Leia `AGENTS.md`, **`docs/engine/COOP-MODELO-B.md`** (C3a–C3c, em especial
"Vida e magia correntes" e a seção de provas) e `docs/engine/COOP-DESENHO.md`.

## O que fazer

1. **HUD do jogador 2**: hoje o HUD mostra só a vida/magia do P1 e a do P2
   só aparece em `--dump-state`. Mostre a vida (corações) e a magia do P2
   com `SharedWorld` ligado. Escolha o jeito mais simples que não quebre o
   HUD normal nem o `ReduxHud` (por exemplo, uma linha de corações do P2
   com cor diferente, ou o HUD de cada metade quando a tela divide) e
   documente. Sem `SharedWorld`, o HUD não muda.
2. **Provas que faltam** (listadas como pendentes no `COOP-MODELO-B.md`):
   - um cenário **determinístico** em que a espada do P2 acerta e machuca
     um inimigo de verdade (evento/log com o dano, e vida do inimigo
     caindo), virando replay de feature com golden;
   - os dois jogadores usando **itens diferentes no mesmo frame** (por
     exemplo, P1 com arco e P2 com bumerangue) com screenshot mostrando
     cada gráfico no Link certo.
3. Atualize o `COOP-MODELO-B.md` (tire das limitações o que foi resolvido).

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final; o replay novo com golden; screenshots do HUD
do P2 (junto e dividido) e dos dois itens em `.agents/shots/`, fora do git.
