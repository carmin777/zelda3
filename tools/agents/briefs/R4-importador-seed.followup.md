# R4 — rodada 3: textos, dicas e o resto das regras

Revisei e mergeei `e1e848f` (248 de 248 locais, chaves pela masmorra de
destino, inventário inicial, opção `Z3rRules`). Traga a branch para o estado
atual (`git merge engine/modloader`).

1. **Limpeza**: tire o `fprintf(stderr, "seed-dungeon target=...")` de
   `src/misc.c` (saída de depuração no código do jogo); se algum teste
   precisar dessa informação, use um evento de mod ou o `--mod-trace`.
2. **Textos e dicas**: extraia da ROM da seed os textos do z3r (dicas dos
   telepatas e placas, textos dos itens novos, pedestal, Ganon) e aplique
   pelo mecanismo de diálogo do modkit (o mesmo que importa diálogo de
   hacks), gerando os dados no mod da seed (fora do git). Use a tabela
   `SeedOracle_JpMessageId` para alinhar IDs onde servir. Prova: um replay
   lendo uma dica (texto igual ao da ROM da seed).
3. **Lojas que dividem o byte baixo de sala** (`0x12`, `0x0f`): modele a
   porta de entrada (`PreviousOverworldDoor`, `$7F5099`) para cada loja ter
   o seu inventário.
4. **Configurações de `$B0:8000`**: liste quais flags esta seed usa e quais
   o motor ainda não aplica (modo do mundo, objetivo, etc.); aplique as que
   forem só dado e documente as que exigem comportamento novo (por exemplo
   inverted) como pendências.
5. **Uma seed jogável**: escreva no `RANDOMIZER-IMPORT.md` o passo a passo
   para jogar a seed no motor em C (gerar o mod, onde pôr, como
   rodar) e o que ainda difere da ROM da seed.

## Verificação

`make -j2`, `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final; a cobertura continua 248/248.
