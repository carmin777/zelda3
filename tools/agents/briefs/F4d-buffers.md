# Tarefa F4d-buffers — nomes para os acessos crus a g_ram

Leia `AGENTS.md`, `docs/engine/F4-PLANO.md` (F4d) e `src/variables.h`.

## O que fazer

1. Liste todos os acessos crus a `g_ram[...]` / `&g_ram[0x....]` /
   `WORD(g_ram[...])` fora de `src/variables.h` e de `src/mod/` (conte
   antes; o plano estimou ~250).
2. Agrupe por região de memória (por exemplo buffers de descompressão,
   tilemaps de transferência, tabelas temporárias) e dê **nomes** em
   `src/variables.h` no mesmo estilo dos existentes (macro sobre
   `g_ram+offset`, com o tipo certo). Use os nomes do mapa de RAM da
   comunidade quando souber; senão um nome descritivo e um comentário.
3. Troque os acessos pelos nomes. **Mudança mecânica**: nenhum offset,
   tamanho ou tipo efetivo pode mudar.
4. Regenere a tabela de estado por nome (`tools/gen_state_table.py`) se
   ela pegar os nomes novos, e confira que nada sumiu dela.
5. Preserve CRLF arquivo por arquivo (`git ls-files --eol`); diffs de
   arquivo inteiro estão errados.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` verde
com todas as linhas (é a prova de que nada mudou); contagem antes/depois
de acessos crus; a lista dos nomes novos com offset e tamanho.
