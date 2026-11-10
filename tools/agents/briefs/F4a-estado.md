# Tarefa F4a-estado — registro de estado por nome

Leia `docs/engine/F4-PLANO.md` (contexto) e `docs/engine/MODS.md` (API Lua
atual). Os scripts Lua passam a poder ler — e, com permissão, escrever —
qualquer uma das ~1.417 variáveis nomeadas de `src/variables.h`.

## Fatos (verificados)

- `src/variables.h`: cada linha é
  `#define nome (*(uint8*)(g_ram+0xNN))` (escalar u8, 493 casos),
  `#define nome (*(uint16*)(g_ram+0xNN))` (escalar u16, 350 casos) ou
  `#define nome ((uint8*)(g_ram+0xNN))` / `((uint16*)(...))` (ponteiro =
  array, tamanho não declarado). Pode haver outras formas: trate-as e liste
  no relatório as que você ignorou.
- **Não mude `variables.h` nem o layout da RAM.** Isto é só uma tabela de
  consulta por cima.

## O que fazer

1. `tools/gen_state_table.py`: lê `src/variables.h` e gera
   `src/mod/state_table.c` + `.h` com
   `{ name, offset, elem_size (1|2), count, is_array }`, ordenado por nome
   (busca binária). Para arrays, `count` = distância até o próximo offset
   definido dividida por `elem_size`, com teto de 256 e mínimo 1 —
   documente que é uma estimativa. Alvo `make state-table` para regerar;
   commite o `.c` gerado.
2. Lua (em `src/mod/lua_api.c`):
   - `zelda.state.get(nome [, índice])` → inteiro (u8/u16, índice 0-based
     para arrays; fora da faixa → erro Lua).
   - `zelda.state.set(nome, [índice,] valor)` → só se o mod tiver a
     permissão `state_write`; valor fora de 0..255 / 0..65535 → erro.
   - `zelda.state.info(nome)` → tabela `{offset=, size=, count=, array=}`.
   - `zelda.state.list([prefixo])` → lista de nomes.
   - Nome desconhecido → erro Lua claro.
3. Permissões no `mod.json`: `"permissions": ["state_write"]`. Permissão
   desconhecida → mod recusado com mensagem. Sem o campo = nenhuma.
4. Headless: `--dump-state` imprime no fim, depois do `final`, uma linha
   `state <nome> <valor>` por escalar (ordem por nome). Não altera nada.
5. Mod de exemplo `mods-examples/lua-estado/`: ao entrar em sala imprime
   `link_x_coord`, `link_y_coord` e `dungeon_room_index` via `zelda.state`.
6. Documente em `docs/engine/MODS.md` (seção "Estado por nome").

## Verificação a relatar

1. `make -j2` e `make -j2 NO_LUA=1` sem warnings.
2. `tools/regress.sh` verde.
3. Replay 0 com o `lua-estado`: linhas reais impressas.
4. `zelda.state.set` sem permissão → erro Lua, mod desativado; com
   permissão → efeito visível (ex.: setar rupias e ler de volta). Cole.
5. `--dump-state` rodado duas vezes no replay 0 com `--frames 3000`: saída
   idêntica; diga quantas linhas.
6. Quantas entradas a tabela tem, por tipo, e quais linhas de
   `variables.h` não foram reconhecidas.

## Fora do escopo

Desenho na tela (F4c), modkit (F4b, outro agente trabalha nele em paralelo
em `tools/modkit.py` — não toque nesse arquivo), renomear acessos crus.
