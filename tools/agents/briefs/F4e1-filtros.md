# Tarefa F4e1-filtros — ganchos de filtro e de decisão

Leia `AGENTS.md`, **`docs/engine/F4E-DESENHO.md` (aprovado)**,
`docs/engine/HOOKS-MAPA.md`, `docs/engine/MODS.md` (eventos, Lua),
`docs/engine/REDUX-MAPA.md` (seção 7) e `docs/engine/ALTTI-MAPA.md`
(seção 3). Esta tarefa é a **F4e.1** do desenho.

## Fatos (confira)

- `src/mod/events.{c,h}`: `MOD_EMIT` com `g_mod_event_mask` (custo zero
  sem assinante) e `ItemReceive` como filtro (`int *filter`).
- Mods que mudam conteúdo ligam `g_mods_change_content`, e isso desliga a
  comparação lado a lado.
- Arquivos `.c` do jogo podem ser CRLF: `git ls-files --eol`.

## O que fazer

1. **Mecanismo** em `src/mod/` (ex.: `hooks.{c,h}`), separado dos eventos:
   - `MOD_FILTER(kModFilter_X, valor, arg)` → devolve o valor, possivelmente
     trocado pelos mods, na ordem de prioridade;
   - `MOD_DECIDE(kModDecide_X, padrao, arg)` → devolve `bool`;
   - máscara por gancho: **sem assinante, o macro devolve o valor original
     sem chamar nada** (o portão tem de provar isso);
   - assinar qualquer filtro/decisão liga `g_mods_change_content`;
   - nomes por intenção numa tabela única (como `mod_features.c`), usados
     pelo Lua, pelo trace e pela documentação.
2. **Lua**: `zelda.filter(nome, fn)` e `zelda.decide(nome, fn)`; `fn`
   recebe o valor/padrão e o argumento e devolve o novo valor/decisão;
   erro, tipo errado ou limite de instruções → mod desativado como hoje.
   `--mod-trace` mostra cada filtro/decisão que mudou o resultado.
3. **Ganchos** (cada um no ponto exato da rotina, com o endereço da rotina
   original no comentário e uma linha nova no `HOOKS-MAPA.md`; confira
   cada local no código antes, os números de linha podem ter mudado):

   | Nome | Tipo | Onde (REDUX-MAPA/ALTTI-MAPA) |
   |---|---|---|
   | `SwordBeamDamage` | filtro | `src/ancilla.c` (`kAncilla_Damage` do beam) |
   | `FairyReviveHealth` | filtro | `src/messaging.c` (fada que revive) |
   | `MaxBombs`, `MaxArrows` | filtro | `src/hud.c` e `src/sprite_main.c`: **todos** os lugares que leem `kMaxBombsForLevel`/`kMaxArrowsForLevel` passam por um helper único com o filtro |
   | `ReceiveItemAlternate` | filtro | `src/player.c` (`kReceiveItemAlternates`, túnicas) |
   | `PendantDropSpecialCase` | decisão | `src/ancilla.c` (caso `cur_palace_index_x2 == 20`) |
   | `SwordTurnWhileCharging` | decisão | `src/player.c` (`Link_CheckForSwordSwing`) |
   | `BlockAlreadyPushed` | decisão | `src/dungeon.c` (`dung_flag_movable_block_was_pushed`) |
   | `PikitStealShield` | decisão | `src/sprite_main.c` (`Sprite_AA_Pikit`) |
   | `BirdTravelDestination` | filtro | `src/messaging.c` (`kBirdTravel_*`; primeiro warp) |
   | `MirrorAllowed` | decisão | `src/player.c` `LinkItem_Mirror` (`// 87a91a`), arg = mundo atual |
   | `OverworldTransitionTarget` | filtro | rotina da transição de área do overworld (Islands desvia em `$82:AA7D`); arg = direção; base do quebra-cabeça da praia |

   Se algum ponto não bater com o código, **diga** e pule; não invente.
4. **Mods de exemplo** em `mods-examples/` (só Lua): `redux-ganchos`
   (sword beam como L2, fada 64, bombas 60 e flechas 99, Pikit sem roubo,
   blocos infinitos) e `espelho-sombrio` (proíbe o espelho no Mundo
   Sombrio, como o Islands).
5. `docs/engine/MODS.md`: seção "Filtros e decisões" com a lista de nomes,
   os argumentos e a ordem de prioridade.

## Verificação a relatar

1. `make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com
   todas as linhas `ok` e a linha final (sem mods nada muda).
2. **Três replays de feature** (protocolo em `docs/engine/BUGS-PROTOCOLO.md`
   passo 5, `tests/replays-features/` com `.ini` e golden), escolha os
   ganchos mais fáceis de exercitar (por exemplo espelho proibido, máximo
   de bombas, Pikit); o golden tem de mostrar o efeito no trace/hash.
3. Para cada um dos outros ganchos: uma execução headless curta com o mod
   de exemplo mostrando o efeito (`--mod-trace` ou `--dump-state` antes e
   depois). Cole as saídas.
4. Erro de Lua e retorno de tipo errado num filtro → mod desativado; cole
   o log.
5. Diga quais ganchos não conseguiu exercitar e por quê.
