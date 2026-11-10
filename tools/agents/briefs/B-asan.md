# Tarefa B-asan — varredura de leituras fora de tabela

Na tarefa B328 (já mergeada: veja `git log` e `src/player.c`), o
AddressSanitizer achou leituras além do fim de tabelas `static const` —
o C lê fora do array onde o assembly lê o byte seguinte da ROM. A
correção certa é completar o array com os bytes reais da ROM (com
comentário do endereço), não guardar o índice. Também apareceu um caso
ainda aberto em `src/sprite.c:1514`
(`Sprite_PrepAndDrawSingleLargeNoPrep`, `kSprite_PrepAndDrawSingleLarge_Tab2`).
Leia `docs/engine/BUGS-PROTOCOLO.md`.

## O que fazer

1. Build de prova (não commitado) com
   `-O1 -g -fsanitize=address,undefined -fsanitize-recover=address
   -fno-omit-frame-pointer` e `ASAN_OPTIONS=halt_on_error=0`, de modo
   que **todas** as ocorrências sejam registradas numa passada. Use
   `-fno-sanitize=alignment,shift` (avisos de alinhamento e shift são
   esperados nesse código e fora do escopo). Faça o build num diretório
   próprio ou restaure o binário normal antes de rodar o regress.
2. Rode os 15 casos do `tools/regress.sh` (13 refs + `tests/replays/*.sav`)
   nesse build, no máximo 2 em paralelo, e junte todos os relatórios de
   `global-buffer-overflow` e `index out of bounds` (deduplicados por
   linha de código). Liste-os no relatório com contagem e o replay/frame
   da primeira ocorrência.
3. Para cada um: ache a tabela na ROM (padrão de bytes no banco da rotina
   indicada no comentário `// 86dc13` etc.), descubra quantos bytes além
   do fim são lidos de verdade (o maior índice visto) e complete o array
   com os bytes reais. Se for leitura de RAM/estrutura e não de tabela,
   **não corrija** — só relate.
4. Rode a varredura de novo e mostre que as ocorrências tratadas sumiram.
5. Build normal: `make -j2` sem warnings, `tools/regress.sh` com 15 `ok`.
6. Commite só as tabelas corrigidas.
