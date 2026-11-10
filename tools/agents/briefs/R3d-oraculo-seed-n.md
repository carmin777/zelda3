# Tarefa R3d-oraculo-seed-n — próxima rodada do oráculo da seed

Leia `AGENTS.md` e **`docs/engine/RANDOMIZER-ORACULO.md`** (método, lista
restante, estado atual). Decisão (09/11/26): o randomizer nativo
**continua em paralelo**, rodada a rodada. Material fora do git:
`japanese.sfc`, a seed em `.agents/alttpr/download/`, referências do
`z3randomizer` em `.agents/refs/z3r/`.

## Regras desta rodada

1. **Trocar o andaime por C.** A rodada anterior (R3c) usou
   `EmuRunSeedRoutine` para rodar rotinas JP da própria seed contra a RAM
   do C (tabelas de larguras, ponteiros de texto, paletas). Nesse trecho o
   oráculo compara o asm com ele mesmo e não prova nada. **Reimplemente
   essas rotinas em C** (modo JP, atrás do modo seed, com o endereço da
   rotina JP no comentário) e remova as chamadas de `EmuRunSeedRoutine`
   que elas substituírem. O oráculo tem de continuar passando os 9 frames.
   Se alguma não der, diga qual e por quê.
2. **Avançar**: depois disso, siga a lista a partir do frame 10 (tilemap
   JP da seleção, `$8E:F6A8`), um comportamento por vez, com a divergência
   antes e depois de cada um, até onde der nesta rodada.
3. Atualize o replay/golden da seed no portão para o novo número de
   frames, e o `RANDOMIZER-ORACULO.md` (até onde vai, lista restante).
4. Nada muda no caminho normal.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` (inclusive `seed-oracle`) e a linha final; quantas chamadas
de `EmuRunSeedRoutine` sobraram; quantos frames o oráculo da seed passa
agora; as divergências reais antes/depois.
