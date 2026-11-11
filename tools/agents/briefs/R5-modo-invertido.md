# Tarefa R5-modo-invertido — o modo invertido do ALttPR no motor em C

Leia `AGENTS.md`, `docs/engine/RANDOMIZER-COMPAT.md` (o C lê a seed como
dados; a meta é a mesma seed dar o mesmo resultado), `docs/engine/RANDOMIZER-IMPORT.md`
(importador e `Z3rRules`) e as referências do z3r em `.agents/refs/z3r/`
(fora do git; procure o tratamento de `InvertedMode`: `inverted.asm` e os
hooks que leem `$18004A`).

No modo invertido o Link começa no Mundo das Trevas, a casa e a Sanctuary
mudam de lugar, a Pérola da Lua vale para o Mundo da Luz, e várias
passagens e entradas do mapa mudam.

## O que fazer

1. **Desenho** em `docs/engine/RANDOMIZER-INVERTIDO.md`: liste tudo o que o
   z3r muda no invertido (início, pérola/coelho, entradas e saídas, tiles e
   passagens do overworld, espelho, Aga, tower, textos), com o endereço de
   cada hook e como cada coisa vira dado ou regra no motor US (o que dá
   para importar da ROM da seed e o que é comportamento novo). Estime o
   tamanho de cada parte.
2. **Primeira fatia**, só com `Z3rRules` e `InvertedMode = 1`: o jogo
   novo começa no lugar certo do Mundo das Trevas, e a regra da pérola/coelho
   é invertida. Prova com `z3r_settings` forjado num passo do teste (sem
   dados de seed no git) e screenshot em `.agents/shots/` (fora do git).
3. Sem `InvertedMode`, nada muda (portão).

Docs e comentários impessoais. Verificação: `make -j2`, `make -j2 NO_LUA=1`
sem warnings; `tools/regress.sh` com todas as linhas `ok` e a linha final.
Não baixe nada: se precisar de uma seed invertida real para comparar, diga
no relatório (o download precisa de aprovação).
