# Tarefa C3a-dois-links — modelo B: dois Links no mesmo mundo (1ª fatia)

Leia `AGENTS.md`, **`docs/engine/COOP-DESENHO.md`** (modelo B; decisão: câmera compartilhada que se divide sozinha — isso fica para a
C3b), `docs/engine/COOP.md` (C1/C2) e `src/variables.h` (com os nomes da
F4d). Referência conceitual: o fork `rail5/zelda3-ml` faz dois Links
trocando as regiões de RAM "do jogador" na RAM viva antes de rodar cada
Link (`Link_MainForAllPlayers`, sprites mirando o jogador mais próximo).
**Não copie código do fork** (licença não declarada); implemente a ideia.

## O que fazer (só esta fatia)

1. **Estado do Link por jogador**: liste as regiões de RAM que são "do
   Link" (posição, direção, estado, animação, velocidade, item na mão,
   espada/escudo em uso, invencibilidade, ancillas dele, vida?) e as que
   são do mundo (sala, sprites, mapa, progresso). Documente a lista e por
   quê em `docs/engine/COOP-MODELO-B.md`.
2. **Dois Links**: com `[Coop] SharedWorld = 1` (desligado por padrão),
   o jogo roda a lógica do Link duas vezes por frame, trocando o estado do
   jogador antes de cada uma, com a entrada de cada controle (reaproveite o
   `[KeyMap2]`/`[GamepadMap2]` da C2). O segundo Link é desenhado (OAM/CHR
   extra, sem estragar o layout de VRAM do jogo).
3. **Interação mínima**: os dois acertam inimigos; inimigos podem acertar
   os dois (o alvo é o Link mais próximo); itens do chão são de quem pega;
   progresso é um só (o mundo é um só).
4. **Transições**: nesta fatia, a câmera segue o jogador 1; quando o
   jogador 1 troca de sala/área, o jogador 2 é levado junto para perto dele.
   Diga o que fica estranho (para a C3b).
5. **Sem a opção, o jogo é idêntico** (portão); com ela, a comparação lado a
   lado desliga.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final. Headless com `--input-script` e
`--input-script2`: os dois Links andando na mesma sala; o jogador 2
acertando um inimigo; um inimigo acertando o jogador 2; transição de sala
levando o jogador 2 (screenshots em `.agents/shots/`, fora do git, e
`--dump-state` mostrando o estado dos dois). Um replay de feature com
golden. Liste tudo o que ainda não funciona.
