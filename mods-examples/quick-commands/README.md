# Quick Commands (atalhos fixos)

Aproximação em Lua do hack "A Link to the Past Quick Commands" (romhacking.net
id 7575). Com `L` ou `R` segurado, os botões de face equipam um item direto no
Y, sem abrir o menu:

| Combo | Item |
|---|---|
| L + A | arco |
| L + B | bumerangue |
| L + X | hookshot |
| L + Y | martelo |
| R + A | bombas |
| R + B | fire rod |
| R + X | ice rod |
| R + Y | cane of Somaria |

Itens não obtidos são ignorados (mesma ideia do `Hud_DoWeHaveThisItem`).

## Pedido

Hack "A Link to the Past Quick Commands" (romhacking.net id 7575), listado
entre os hacks pequenos da onda 4.

## Limitação

O hack original permite escolher até oito itens no menu, gravando a combinação
com o item destacado. A API de mods atual não tem evento de menu/tecla
persistente nem UI para isso; este mod usa um mapa fixo. Para a versão fiel
faltaria um gancho de menu/entrada ou uma API de configuração do mod. Com
`ItemSwitchLR` ligado, L/R sem botão de face continuam trocando o item como
sempre; os combos com face ainda funcionam, mas podem competir com a troca.

## Como testar

`--load` carrega um estado e o `--input-script` consegue apertar botões (em
`--replay` os botões do roteiro são ignorados). O exemplo abaixo usa o fim do
capítulo 10, que tem o inventário cheio, aperta L+A e mostra o item equipado:

```sh
printf '2 L A\n4 -\n' > /tmp/quick.txt
mkdir -p /tmp/mods-quick
cp -a mods-examples/quick-commands /tmp/mods-quick/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-quick \
  --load 'saves/ref/Chapter 10 - After Ice Palace.sav' --input-script /tmp/quick.txt \
  --frames 10 --dump-state zelda3.sfc \
  | grep -E '^state (hud_cur_item|current_item_y)'
```

Esperado: `hud_cur_item 1` e `current_item_y 3` (arco), com
`mods: loaded 'quick-commands'` no `stderr`.
