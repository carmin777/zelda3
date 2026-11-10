# Tarefa B290-cavar — upstream #290

"Heart container from digging game not showing up when a second character
is made" — no jogo de cavar (Dark World, sul), o pedaço de coração não
aparece para o segundo personagem criado; para o primeiro aparece. Siga
`docs/engine/BUGS-PROTOCOLO.md`.

Pistas: o jogo de cavar sorteia o prêmio; "segundo personagem" sugere
estado que deveria ser por-save mas está global (ou o contrário), ou RNG.
Leia o código do jogo de cavar (procure o sprite/NPC do digging game e a
rotina que decide o prêmio; `src/sprite_main.c`) e a criação de save
(`src/select_file.c`). Compare com o comportamento do assembly: o oráculo
mostra divergência se o C escolher prêmio diferente.

Reproduzir de verdade exige jogar o mini-jogo com dois saves; se for
inviável com roteiro, faça a análise de código, aponte a diferença C×asm
com evidência (endereços, bytes da ROM) e só corrija se o oráculo puder
confirmar em algum cenário. Se não achar diferença, relate que não
reproduz e por quê — não invente correção.
