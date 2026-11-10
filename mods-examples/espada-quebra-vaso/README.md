# Espada quebra vaso (#262)

Liga a opção `BreakPotsWithSword` e usa a decisão `PotBreakSwordLevel` para
exigir **L4**. Com o mod, só a espada dourada quebra vasos ao golpear; L1, L2 e
L3 continuam sem quebrar. O argumento da decisão é `link_sword_type` (0 a 4).

## Pedido

Issue #262 do `snesrev/zelda3`: o pedido é escolher o **nível mínimo** de
espada que quebra vasos, sendo 4 o comportamento da versão GBA. O zelda3 só
tinha um booleano e o código tratava só `link_sword_type == 1` como bloqueio.
O gancho novo resolve o caso sem mudar o jogo sem mods.

## Como testar

```sh
mkdir -p /tmp/mods-espada
cp -a mods-examples/espada-quebra-vaso /tmp/mods-espada/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-espada \
  --frames 60 zelda3.sfc
```

O `stderr` deve mostrar `mods: loaded 'espada-quebra-vaso'` e
`mods: feature 'BreakPotsWithSword' = on`.

Para ver a decisão, golpeie um vaso com L2/L3: o trace mostra
`hook ... Decide PotBreakSwordLevel 1 -> 0 arg 2` (ou `arg 3`) e o vaso não
quebra. Com L4 a decisão devolve `true`; como o valor não mudou, o
`--mod-trace` não imprime linha e o vaso quebra normalmente (o evento
`DropSpawned` aparece se o vaso tiver item).
