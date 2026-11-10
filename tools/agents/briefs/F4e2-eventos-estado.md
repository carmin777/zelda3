# Tarefa F4e2-eventos-estado — eventos que faltam e estado salvo do mod

Leia `AGENTS.md`, `docs/engine/F4E-DESENHO.md` (aprovado; seções 2, 3.4 e
4: F4e.2), `docs/engine/MODS.md` (eventos, Lua, filtros e decisões da
F4e.1) e `docs/engine/HOOKS-MAPA.md`.

## O que fazer

1. **Eventos novos** em `src/mod/events.h` (depois dos existentes; não
   reordene ids), cada um no ponto exato da rotina original, com o
   endereço no comentário e uma linha no `HOOKS-MAPA.md`:
   `FileLoad` (save carregado e jogo pronto; a = slot), `FileCreate` (save
   novo criado; a = slot), `DungeonEnter` (a = `cur_palace_index_x2`, b =
   entrada), `DungeonExit` (a = palácio), `Quit` (salvar e sair / sair sem
   salvar; a = 1 se salvou), `OverworldTransition` (a = área nova, b =
   direção). Confira nos `On*` do `z3randomizer`
   (`.agents/refs/z3r/z3r-hooks.asm`,
   só leitura) onde ele se pendura para os mesmos momentos, e diga se o
   nosso ponto é equivalente. Se algum não tiver um ponto claro, diga e
   pule.
2. **Estado salvo do mod** (decisão aprovada: arquivo ao lado do save, não
   mexe na SRAM): API Lua `zelda.save.get(chave)`, `zelda.save.set(chave,
   valor)` (string, número, booleano; limites de tamanho por mod), gravado
   quando o jogo grava o save e lido quando carrega, num arquivo por slot
   ao lado do save do jogo (descubra onde o zelda3 grava a SRAM e o nome
   do arquivo), separado por id de mod. Save sem esse arquivo abre normal;
   mod removido não quebra o save. Escrita atômica (arquivo temporário +
   rename).
3. `--mod-trace` mostra os eventos novos.
4. Mod de exemplo `mods-examples/contador-mortes/`: conta mortes por slot
   com `zelda.save` e mostra no HUD com `zelda.draw`.
5. `docs/engine/MODS.md`: eventos novos e `zelda.save`.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final (sem mod nada muda: os eventos novos usam
`MOD_EMIT`). Traces headless mostrando cada evento novo disparando (cole).
Teste do `zelda.save`: gravar, sair, carregar de novo e ler o valor (cole);
save sem o arquivo do mod abre igual.
