# Hero Mode (dano dobrado, sem corações)

Implementa a versão "Hero Mode" do hack de romhacking.net id 8097: **dano
dobrado e sem drops de coração**, como descrito pelo autor (há também as
variantes ALBW Hero Mode e Superhero Mode, que não são este mod).

- Dano: o filtro `LinkDamage` dobra o dano antes de o jogo gravar
  `link_health_current`. O dano dobrado agora pode matar normalmente, como no
  hack.
- Corações: o evento `DropSpawned` recebe o tipo do item e o slot no instante
  em que o drop é criado. O mod zera o `sprite_state` do slot na mesma chamada,
  então o coração não chega a aparecer por um frame. Isso cobre drops de
  inimigos, potes e arbustos.

O filtro recebe `(damage, source)`, com a fonte em `source`: `0` sprite,
`1` ancilla, `2` buraco e `3` ambiente.

## Pedido

Hack "Zelda 3 Hero Mode" (romhacking.net id 8097, autor 8288/ThatEntityGirl),
citado na lista de hacks pequenos da onda 4.

## Como testar

`tests/replays-features/f4e-linkdamage.sav` é o replay de feature deste mod:
tem dano de sprite no frame 149 e um drop no frame 258.

```sh
mkdir -p /tmp/mods-hero
cp -a mods-examples/hero-mode /tmp/mods-hero/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-hero \
  --replay tests/replays-features/f4e-linkdamage.sav --frames 450 --mod-trace \
  --dump-state zelda3.sfc \
  | grep -E '^(hook|ev .*DropSpawned|state link_health_current)'
```

Esperado: `hook 149 Filter LinkDamage 4 -> 8 arg 0`, um evento `DropSpawned`
cujo slot é zerado no mesmo frame (o coração não chega a aparecer) e
`link_health_current` menor que no jogo sem o mod. O `stderr` deve ter
`mods: loaded 'hero-mode'`.
