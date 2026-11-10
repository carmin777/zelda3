# Tarefa C3c-modelo-b — pendências do co-op no mesmo mundo

Leia `AGENTS.md`, **`docs/engine/COOP-MODELO-B.md`** (C3a e C3b: regiões
do Link por jogador, câmera que se divide, limitações listadas) e
`docs/engine/COOP-DESENHO.md`.

## O que fazer

1. **Vida e reaparecimento**: vida e magia de cada jogador separadas (hoje
   são compartilhadas). Quando o P2 morre, ele reaparece ao lado do P1
   depois de um tempo (sem game over); quando o P1 morre com o P2 vivo,
   defina e documente a regra (por exemplo, o P1 reaparece ao lado do P2;
   game over só se os dois caírem).
2. **Transições pelo P2**: o P2 também pode passar por portas/bordas;
   defina a regra mais simples que não quebre o jogo (por exemplo, quem
   passa leva o outro) e documente.
3. **Mais IAs mirando o mais próximo**: liste as IAs de inimigos comuns que
   ainda leem `link_x_coord`/`link_y_coord` direto e troque pelas funções
   centrais da C3a, começando pelos mais comuns (soldados, octoroks, etc.).
4. **Buffer de itens compartilhado** (`gfx_4bpp_buffer`): quando os dois
   usam itens diferentes no mesmo frame, o gráfico de um aparece no outro;
   resolva com um buffer por jogador.
5. **Mods desenhando na tela dividida**: o `FrameDraw` hoje só desenha na
   metade esquerda; faça cada metade receber o seu desenho (ou documente a
   API, por exemplo um parâmetro com a metade).
6. Sem `SharedWorld`, nada muda (portão).

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final. Headless com dois roteiros: P2 morre e
reaparece; P2 passa por uma porta; um soldado mira o P2 quando ele está
mais perto; os dois usam itens diferentes no mesmo frame. Screenshots em
`.agents/shots/` (fora do git) e um replay de feature com golden para o
reaparecimento. Atualize o `COOP-MODELO-B.md`.
