# Opções do A Link to the Islands

Preset de `"features"` para o hack A Link to the Islands. Só entram as opções
que o hack liga por assembly e que o zelda3 já implementa com o mesmo
comportamento (classe A da seção 3 do `docs/engine/ALTTI-MAPA.md`):

| Opção | Desvio do hack |
|---|---|
| `ItemSwitchLR` | troca de item com L/R (`$82:88F9`, `$82:A55A`) |
| `BreakPotsWithSword` | quebrar vasos com espada Lv2+ (`$81:DABD`) |
| `CollectItemsWithSword` | pegar itens com a espada (`$81:DAFC`) |
| `TurnWhileDashing` | virar correndo com os Pegasus Boots (`$82:8B13`) |
| `CancelBirdTravel` | cancelar o transporte de pássaro/flauta (`$82:857D`) |

Ficam de fora:

- `MirrorToDarkworld`, porque o hack faz o **oposto** (proíbe o espelho no
  Mundo Sombrio); isso é tratado em `mods-examples/islands-ganchos/`.
- `ItemSwitchLRLimit`, porque o limite de itens do zelda3 não é o temporizador
  que o assembly do hack usa.
- `MiscBugFixes`/`GameChangingBugFixes`, porque a seção 3 do mapa não os
  classifica como equivalentes aos patches do hack; ligá-los mudaria outras
  regras do port.
- as opções de HUD/inventário do Redux, que o Islands não usa.

## Como testar

```sh
mkdir -p /tmp/mods-islands-opcoes
cp -a mods-examples/opcoes-islands /tmp/mods-islands-opcoes/
./zelda3 --config tests/regress.ini --headless \
  --mods-dir /tmp/mods-islands-opcoes --replay-ref 0 --frames 200 \
  zelda3.sfc 2>&1 | grep "mods: feature"
```

Esperado: uma linha `mods: feature '<nome>' = on` para cada uma das cinco
opções.
