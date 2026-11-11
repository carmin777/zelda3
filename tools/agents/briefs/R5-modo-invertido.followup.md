# R5 — rodada 7: as duas últimas peças do invertido

Revisei e mergeei `b8aba40` (espelho de verdade, morte/save, textos do HC, os
4 NPCs). Traga a branch para o estado atual (`git merge engine/modloader`);
`git status` limpo. Commite a cada item.

1. **`TurtleRockPegSolved`**: o que o invertido muda no quebra-cabeça das
   estacas da Turtle Rock, com prova (estado forjado, emulador × motor).
2. **Fonte invertida** (`newfont_inverted.bin` do z3r): se a seed carrega uma
   fonte diferente no invertido, importe-a da ROM da seed pelo mecanismo de
   fonte do modkit e prove com um texto na tela; se não carrega, documente.
3. **Provas que faltaram dos NPCs**: Old Man, Sick Kid e Catfish recebendo o
   item da seed (estado forjado), como você fez com o Sahasrahla.
4. **Uma rota longa na seed invertida**: use o `tools/seed_route.py` em
   C-only com o mod da seed invertida e leve o Link do início até a primeira
   masmorra, abrindo pelo menos um baú; conte o que funcionou e o que
   travou. Isso é o mais perto de "jogar de ponta a ponta" que dá sem o
   usuário.

Atualize o `RANDOMIZER-INVERTIDO.md`. Docs e comentários impessoais.
Verificação: `make -j2`, `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh`
com todas as linhas `ok` e a linha final.
