# Ganchos do Redux (exemplo F4e.1)

Exemplo dos filtros e decisões com os valores do Redux. Nenhum destes ganchos
muda o jogo sem o mod.

## Sword beam (dano de espada L2)

O Redux tem a mudança "Sword beam equal in damage to Master Sword"
(`docs/engine/REDUX-MAPA.md`, item 17): o patch `bugfixes.asm:43-44`
(`org $06EC8A`) troca a classe de dano do raio da espada de `1` (L1) para `2`
(L2). Em `src/ancilla.c`, `kAncilla_Damage[0x0c] = 1` é o valor original; o
tipo `0x0c` é o raio criado por `AddSwordBeam` (`// 8ff67b`). O tipo `0x0b` é
o tiro da vara de gelo e não passa por esse filtro.

O valor do gancho é uma **classe de dano**, não pontos de vida. A classe da
espada mestra no golpe normal é `2`: `Sprite_CalculateSwordDamage` usa
`link_sword_type - 1 == 1` e `kSprite_Func14_Damage[1] == 2` em `src/sprite.c`.
Por isso o mod devolve `2`, e não `9`: o valor antigo `9` não é a classe da
espada mestra. Confirmei no `redux.sfc` que o byte em `$06EC8A`
(`kAncilla_Damage[0x0c]`) é `02`.

## Outros ganchos

- `FairyReviveHealth` = 64 (o original enche 56).
- `MaxBombs` = 60 e `MaxArrows` = 99.
- `PikitStealShield` = `false` (o Pikit não rouba o escudo).
- `BlockAlreadyPushed` = `false` (blocos reutilizáveis).

## Como testar

```sh
mkdir -p /tmp/mods-redux
cp -a mods-examples/redux-ganchos /tmp/mods-redux/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-redux \
  --replay tests/replays-features/sword-beam-hit.sav --frames 180 \
  --mod-trace zelda3.sfc | grep -E '^(hook .*SwordBeamDamage|ev )'
```

O trace deve mostrar `hook 139 Filter SwordBeamDamage 1 -> 2 arg 13` no frame
em que o raio acerta o inimigo. O replay é o mesmo caso do
`tests/golden-features/sword-beam-hit.txt`.
