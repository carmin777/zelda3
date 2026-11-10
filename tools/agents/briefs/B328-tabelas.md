# Tarefa B328-tabelas — upstream #328 e PR #329

Leitura fora da tabela em `src/player.c`: `LinkItem_Bow` (`kBowDelays`),
`LinkItem_Rod` (`kRodAnimDelays`), `LinkItem_Hammer` (`kHammerAnimDelays`)
e `LinkItem_CaneOfSomaria` (`kRodAnimDelays`) indexam com
`player_handler_timer` depois de incrementar; quando ele vale 3, lê o 4º
elemento de uma tabela de 3. Em alguns builds (Linux, #328) isso trava a
animação. O PR #329 do upstream "resolve" pulando a escrita quando vale 3 —
**não faça assim**: o assembly original lê o byte que vem depois da
tabela na ROM, e esse valor vai para `link_delay_timer_spin_attack`.

Siga `docs/engine/BUGS-PROTOCOLO.md`, com estas diferenças:
1. Descubra, para cada tabela, onde ela está na ROM (procure o padrão de
   bytes da tabela no banco da rotina — a rotina `LinkItem_Bow` é
   `// 87a006`) e qual byte vem logo depois. Complete cada array C com
   esse byte real (comentário dizendo de onde veio). Se duas rotinas
   compartilham a tabela, confira que o byte seguinte é o mesmo.
2. Prove com o oráculo: os replays de referência usam arco, varas, martelo
   e Somaria (o arco é recebido no replay 1, frame 87028). `tools/regress.sh`
   tem que continuar verde.
3. Prove a ausência de leitura fora dos limites: compile uma vez com
   `make -j2 CFLAGS="-O1 -g -fsanitize=address,undefined
   -fno-sanitize-recover=all -I . $(sdl2-config --cflags)"` (ajuste ao
   Makefile; não commite essa mudança) e rode o trecho do replay em que
   cada item é usado, antes e depois da correção. Cole a saída do sanitizer
   de antes e a ausência dela depois. Procure outros arrays em `player.c`
   com o mesmo padrão "incrementa e indexa" e relate-os (corrija só os que
   o sanitizer ou o oráculo confirmarem).

Não precisa adicionar replay novo se o regress já cobre os itens; diga em
qual replay/frame cada item aparece.
