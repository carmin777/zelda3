# Tarefa B241-som — upstream #241

"Dark Palace Missing Sound Effect": no Dark Palace, ao acertar a estátua
com flecha para a parede se mover, falta o som do tremor da parede
(relato em Linux; mantenedor não reproduziu no Windows). Leia
`docs/engine/BUGS-PROTOCOLO.md` e a memória do projeto sobre UB: vários
bugs "só no Linux" eram leitura fora de tabela (já corrigidos pela
varredura ASan — veja `git log`), então pode já estar resolvido.

1. Ache o momento num replay de referência (o 5, "After Dark Palace", ou
   o 4) com `--mod-trace` (sala do Dark Palace com a estátua; descubra o
   id da sala no código/YAML), e ache no C a rotina da parede/estátua e
   onde ela pede o som (variáveis `sound_effect_1/2`,
   `sound_effect_ambient`, `music_control` em `src/variables.h`).
2. Verifique se o pedido de som acontece: no headless, com `zelda.state`
   num mod Lua de teste (fora de `mods-examples`, não commitado) ou com
   `--dump-state` por frame, registre o valor das variáveis de som no
   trecho. Como essas variáveis estão na RAM comparada, se o C não
   escrevesse o som o oráculo já acusaria — então investigue o lado do
   tocador: `src/audio.c` / `src/spc_player.c` (o zelda3 tem tocador
   próprio de SPC). O som chega a ser enfileirado e tocado?
3. Se achar a causa, corrija e prove (mesmo trecho: som pedido e
   processado). Se tudo bater, relate que não reproduz, com a evidência
   (frame, valor das variáveis, caminho no tocador). Não invente correção.
