# Cheats do zelda3 ligados (#295)

Ao carregar um arquivo, aplica os efeitos dos quatro cheats de teclado que o
zelda3 já tem em `src/zelda_rtl.c` (`PatchCommand`):

- `CheatLife`: vida e magia cheias;
- `CheatEquipment`: +10 bombas, +10 flechas e +100 rupias;
- `CheatKeys`: uma chave;
- `CheatWalkThroughWalls`: atravessar paredes.

O efeito é aplicado uma vez por carga de arquivo, como se o jogador tivesse
apertado as teclas de cheat. As teclas originais continuam funcionando.

**Limitação:** `CheatWalkThroughWalls` muda a colisão e pode levar a estados
estranhos (inclusive o assert de objeto de masmorra em alguns replays). É o
cheat mais invasivo; se quiser os outros sem ele, comente a linha
`zelda.state.set("cheatWalkThroughWalls", 1)`.

## Pedido

Issue #295 do `snesrev/zelda3`, "cheats shouldn't be active by default". A
solução limpa é deixar o jogo base sem cheats e oferecer os cheats como mod
opt-in; é isso que este exemplo faz. Nenhum comportamento muda sem o mod.

## Como testar

O replay `tests/replays/gameover.sav` dispara `FileCreate` no frame 1253 e
`FileLoad` no frame 1280; o mod reage ao `FileLoad`:

```sh
mkdir -p /tmp/mods-cheats
cp -a mods-examples/cheats-ligados /tmp/mods-cheats/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-cheats \
  --replay tests/replays/gameover.sav --frames 1300 --dump-state zelda3.sfc \
  | grep -E '^state (link_health_current|link_magic_power|link_rupees_goal|link_num_keys|cheatWalkThroughWalls)'
```

Esperado: vida igual à capacidade, magia 128, rupias 100 e
`cheatWalkThroughWalls 1`. O `stderr` deve ter
`mods: loaded 'cheats-ligados'`. Fora de masmorra o jogo troca
`link_num_keys` por `255`, então a chave só aparece em dungeon; para ver a
chave use um save carregado dentro de uma masmorra.
