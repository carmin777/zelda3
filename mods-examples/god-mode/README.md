# God Mode (vida não cai)

Mantém a capacidade em 20 corações (160 unidades) ao carregar/criar um arquivo
e usa o filtro `LinkDamage` para zerar **todo** dano antes de o jogo gravar a
vida: sprite, ancilla, buraco e ambiente. O resultado é vida sempre cheia e
nenhuma morte por dano normal.

O filtro recebe `(damage, source)`, com a fonte em `source`: `0` sprite,
`1` ancilla, `2` buraco e `3` ambiente. O valor devolvido é o dano aplicado à
vida.

## Pedido

Hack "Zelda3 God Mode" (romhacking.net id 2366) e o pedido "God Mode (vida
não cai)" da onda 4.

## Como testar

`tests/replays-features/f4e-linkdamage.sav` tem dano de sprite no frame 149.
Com o mod:

```sh
mkdir -p /tmp/mods-god
cp -a mods-examples/god-mode /tmp/mods-god/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-god \
  --replay tests/replays/cannon-trooper.sav --frames 450 --mod-trace \
  --dump-state zelda3.sfc \
  | grep -E '^(hook|state link_health_(current|capacity))'
```

Esperado: `hook 149 Filter LinkDamage 4 -> 0 arg 0` e a vida sem cair (o
snapshot do replay começa com 24 unidades; a capacidade de 160 só é aplicada
por `FileLoad`/`FileCreate`). O `stderr` deve ter `mods: loaded 'god-mode'`.
