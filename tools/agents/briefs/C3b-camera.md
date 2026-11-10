# Tarefa C3b-camera — câmera que se divide sozinha e pose própria do P2

Leia `AGENTS.md`, `docs/engine/COOP-DESENHO.md` (decisão:
**câmera compartilhada que se divide sozinha** quando os jogadores se
afastam), **`docs/engine/COOP-MODELO-B.md`** (a C3a: regiões do Link por
jogador, limitações) e `docs/engine/COOP.md` (a tela dividida da C2).

## O que fazer

1. **Pose e CHR próprios do jogador 2**: hoje o P2 é desenhado com a
   pose/tiles do P1. Faça o P2 ter a própria animação (o jogo carrega os
   tiles do Link por frame; carregue também os do P2 num espaço de CHR
   extra que não estrague a VRAM do jogo).
2. **Câmera compartilhada**: com os dois perto, a câmera segue o ponto
   médio (com os limites de rolagem da sala/área). Quando se afastam além
   do que cabe na tela, a tela **se divide sozinha** (cada metade com a sua
   câmera, como a C2 faz com duas instâncias, mas aqui com um mundo só:
   renderize o mesmo estado duas vezes com rolagens diferentes) e volta a
   juntar quando se aproximam. Defina e documente a regra (distância,
   histerese, dentro de masmorras vs overworld).
3. **Transições**: qualquer um dos dois pode passar por uma porta/borda?
   Escolha a regra mais simples que não quebre o jogo e documente (por
   exemplo: a transição acontece quando um passa, e o outro vai junto).
4. Sem `SharedWorld`, nada muda (portão).

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final. Headless com dois roteiros: os dois perto
(câmera no meio), se afastando (a tela divide), se aproximando (junta),
com screenshots em `.agents/shots/` (fora do git) e um replay de feature
com golden. Janela testada no Xvfb. O que ainda fica estranho.
