# Inimigo novo: soldado que foge

Exemplo de `zelda.override`: troca a rotina de **um tipo de sprite** por uma
função Lua. O alvo é o cavaleiro de armadura verde, tipo `0x4B`
(`4B-Knight` na tabela de nomes de sprite).

## Comportamento

O `tick` chama `original(slot)`, então o jogo continua desenhando, animando,
checando colisão com tiles e aplicando dano de contato. O que muda é a
decisão de movimento:

- com o Link de frente para o cavaleiro, ele recua (anda para longe);
- com o Link de costas, ele se aproxima e pode atacar;
- a cabeça (`head_dir`/`D`) vira para o Link nos dois casos.

`ai_state = 1` e `delay_main = 16` mantêm a rotina original no ramo de
movimento, mas os valores de `x_vel`/`y_vel` escolhidos pelo mod são
restaurados depois de `original(slot)` para valerem no frame seguinte.

## Arquivos

```text
mod.json   manifesto; permissions = ["state_write"]
main.lua   o override e a decisão de movimento
```

O `mod.json` precisa de `state_write`: `zelda.override` e
`zelda.sprite.set` recusam o mod sem essa permissão.

## API usada

```lua
zelda.override("Sprite:4B-Knight", { tick = function(slot, original)
  local x = zelda.sprite.get(slot, "x")
  local y = zelda.sprite.get(slot, "y")
  zelda.sprite.set(slot, "x_vel", -12)
  zelda.sprite.set(slot, "y_vel", 0)
  original(slot)
end })
```

O tipo também pode ser escrito como `"Sprite:0x4B"`, `"Sprite:4B"` ou
`"Sprite:Knight"` (sufixo único). Os campos são os de `src/variables.h` sem o
prefixo `sprite_`; a lista completa está em `docs/engine/MODS.md`.

## Como testar

Com uma pasta de mods contendo só este exemplo:

```sh
mkdir -p /tmp/mods-inimigo
cp -a mods-examples/inimigo-novo /tmp/mods-inimigo/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-inimigo \
  --replay tests/replays-features/f4e-sprite-override.sav --frames 300 \
  --mod-trace zelda3.sfc | grep -E '^(compare|override|final)'
```

O trace mostra `compare: off`, as linhas `override ... Sprite 4B-Knight ...`
com as posições dos dois cavaleiros e o hash final. O replay de regressão é
`tests/replays-features/f4e-sprite-override.sav`; o golden fica em
`tests/golden-features/f4e-sprite-override.txt`.

Para ver o jogo base no mesmo ponto, rode o mesmo comando com `--no-mods` (o
trace não terá as linhas `override`, e as posições dos cavaleiros seguem o
movimento aleatório original).

Erros no `tick` (por exemplo `error("...")`) desativam o mod e o sprite volta
à rotina original no mesmo frame; o stderr mostra o erro e
`mods: script '...' disabled after handler error`.
