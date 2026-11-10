# No Sword Beam

Nega a criação do sword beam no gancho de decisão `SwordBeamSpawn`. O golpe de
espada continua normal, mas a âncora do beam (`ancilla_type == 0x0c`, criada
por `AddSwordBeam`) nunca existe: não há projétil, som de beam nem dano.

## Pedido

Hack "No Sword Beam" (romhacking.net id 4486), listado entre os hacks pequenos
da onda 4.

## Como testar

Com um save de espada L2+ e vida cheia, golpeie com B. O `--mod-trace` mostra
a negação no instante da criação:

```sh
mkdir -p /tmp/mods-beam
cp -a mods-examples/no-sword-beam /tmp/mods-beam/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-beam \
  --load "saves/ref/Chapter 10 - After Ice Palace.sav" \
  --input-script beam.txt --frames 120 --mod-trace zelda3.sfc
```

Um roteiro mínimo (`beam.txt`, aperta B e solta) é `10 B`, `12 -`, `30 B`,
`32 -`. Esperado: `hook ... Decide SwordBeamSpawn 1 -> 0 arg <direção>` e
nenhuma âncora `0x0c` na RAM (sem o mod, o slot 4 fica com `ancilla_type =
0x0c` depois do golpe). O `stderr` deve ter
`mods: loaded 'no-sword-beam'`.
