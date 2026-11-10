# Vida cheia ao carregar (PowerPanda)

Enche a vida ao carregar um arquivo (`FileLoad`) e ao criar um arquivo novo
(`FileCreate`), usando `zelda.link.health = zelda.link.max_health`.

## Pedido

Pedido do PowerPanda (citado na triagem da onda 4): "começar com vida cheia ao
carregar". O evento `FileLoad` já existe e roda com o save copiado para a
WRAM, antes de `Link_Initialize`, que não altera a vida atual.

## Como testar

`tests/replays/gameover.sav` tem `FileCreate` no frame 1253 e `FileLoad` no
frame 1280. Para ver a diferença, use um mod auxiliar de teste que baixe a vida
no `FileLoad` e rode com prioridade menor, ou carregue um save com a vida
danificada no jogo normal. O comando abaixo confirma que o mod carrega e que a
vida termina cheia:

```sh
mkdir -p /tmp/mods-vida
cp -a mods-examples/vida-cheia-carregar /tmp/mods-vida/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-vida \
  --replay tests/replays/gameover.sav --frames 1300 --dump-state zelda3.sfc \
  | grep -E '^state link_health_(current|capacity)'
```

Esperado: `link_health_current` igual a `link_health_capacity` e
`mods: loaded 'vida-cheia-carregar'` no `stderr`.
