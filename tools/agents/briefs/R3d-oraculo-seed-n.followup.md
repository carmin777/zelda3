# Próxima rodada do oráculo da seed

Rodada anterior revisada e mergeada em `engine/modloader` (11200 frames no estrito pela rota de Kakariko, sem máscaras, 0 chamadas de `EmuRunSeedRoutine`). Antes de começar, traga a sua
branch para o estado atual: `git merge engine/modloader` (resolva conflitos
de documentação mantendo os dois lados).

Siga as mesmas regras do brief original e a lista do
`RANDOMIZER-ORACULO.md`:
1. **Uma tradução de IDs de mensagem, não um ajuste por NPC**: já há quatro
   casos de ID de mensagem diferente entre US e JP tratados um a um
   (Priest `0x16`→`0x17`, game over `0x184/0x185`→`0x182`, bottle vendor e
   snitch lady `-2`). Os deslocamentos não são constantes, então eles vêm
   da diferença entre as duas listas de mensagens. Monte **uma** tabela de
   tradução US→JP (alinhando as duas listas: as mensagens que o US
   acrescentou ou tirou), aplique num ponto central (onde o C mostra uma
   mensagem / grava `dialogue_message_index`, só com `EmuSeedRomActive()`)
   e retire os quatro ajustes locais. O estrito de 11200 tem que continuar
   passando.
2. Depois, **frame 10023** (com `A` no 10000 fechando a mensagem da snitch
   lady): o buffer `uvram` (`$1006/$1008/$100E`...) diverge no fechamento /
   upload da mensagem. Ponha o `A` no replay e porte.
3. Avance o quanto der, um comportamento por vez, com a divergência antes e
   depois de cada um; atualize o replay/golden da seed e o documento.
4. Nada de andaime: nenhuma chamada nova a `EmuRunSeedRoutine`, e calcule os bytes de rascunho em vez de fixá-los pelo estado final (inclusive os de `$A2:CACA`, se der).

Mesma verificação: `make -j2`, `make -j2 NO_LUA=1`, `tools/regress.sh` verde
(com `seed-oracle`), quantos frames passam agora (normal e estrito).

Mantenha cada comando abaixo de ~10 minutos (oráculo com `--frames`
pequeno; o `tools/regress.sh` inteiro no máximo uma vez por rodada).
