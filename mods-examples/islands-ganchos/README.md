# Ganchos do A Link to the Islands

Ganchos de comportamento do hack A Link to the Islands que o zelda3 não tinha
como opção. O código Lua descreve o efeito observável documentado em
`docs/engine/ALTTI-MAPA.md` (seção 3) e no "hex edits" do autor; não é uma
cópia do assembly.

## Espelho no Mundo Sombrio (`MirrorAllowed`)

O jogo original permite usar o espelho ao ar livre no Mundo Sombrio. O hack
proíbe: toca o som de erro e não faz nada. Em dungeons e interiores o espelho
continua funcionando, porque é assim que o hack devolve o Link à entrada.

```lua
zelda.decide("MirrorAllowed", function(allowed, world)
  if world == 0x40 and zelda.state.get("player_is_indoors") == 0 then
    return false
  end
  return allowed
end)
```

## Quebra-cabeça da praia (`OverworldTransitionTarget`)

Na área `$3F` (canto inferior direito do mapa claro), sair por baixo ou pela
direita cai fora da grade 8x8. O hack intercepta essas transições e guarda uma
combinação de quatro telas. A tabela do binário v2.5 é **cima, baixo, direita,
cima** (`$37, $47, $40, $37`); o FAQ do autor fala "Up, Down, Left, Right", mas
o binário é a fonte que vale. Enquanto a sequência não fecha, a tentativa é
desviada de volta para `$3F`; na quarta direção certa o destino vira `$37`
(Beach Temple). Sair pela esquerda (`$3E`) é sempre permitido e zera o
contador.

O contador da combinação fica no script. O filtro devolve a tela atual para
vetar a transição e coloca o Link na borda oposta da mesma tela, como o efeito
visível do hack.

**Mudança no C:** para o veto funcionar, `OverworldHandleTransitions`
(`src/overworld.c`) passou a tratar "destino igual à tela atual" como
cancelamento da transição quando há um assinante de
`OverworldTransitionTarget`. Sem mod, ou se o filtro devolver outra tela, o
caminho é exatamente o de antes. O replay de feature
`tests/replays-features/ax-beach-veto.sav` cobre o caso.

## Recarga automática de magia (`FrameStart`)

O hack reabastece a magia continuamente no jogo normal: um ponto a cada 256
frames, saturando em 128, e mais rápido quando o livro está equipado. O evento
`FrameStart` roda a cada frame com permissão de `state_write`, então o efeito
cabe nos ganchos atuais. O acumulador de 8 bits fica no script, como o
contador que o hack mantém na RAM. Com o livro equipado a taxa é 17/256 por
frame.

## Como testar

Espelho (o replay do F4e.1 equipa o espelho e aperta X no Mundo Sombrio):

```sh
mkdir -p /tmp/mods-islands-ganchos
cp -a mods-examples/islands-ganchos /tmp/mods-islands-ganchos/
./zelda3 --config tests/regress.ini --headless \
  --mods-dir /tmp/mods-islands-ganchos \
  --replay tests/replays-features/f4e-mirror.sav --frames 399 \
  --mod-trace zelda3.sfc | grep -E '^(hook|ev WorldChange|final)'
```

Esperado: `hook ... Decide MirrorAllowed 1 -> 0 arg 64` e **nenhum**
`WorldChange`; sem o mod o replay faz `WorldChange 0 1`.

Praia: o replay de feature `ax-beach-veto.sav` mostra o veto:

```sh
./zelda3 --config tests/replays-features/ax-beach-veto.ini --headless \
  --mods-dir tests/replays-features/mods \
  --replay tests/replays-features/ax-beach-veto.sav --frames 500 \
  --mod-trace zelda3.sfc | grep -E '^(hook|final)'
```

Esperado: `hook ... Filter OverworldTransitionTarget 48 -> 63 arg 1` e o Link
continua na área `$3F` (sem `AreaEnter`).

Magia: a partir de um estado com magia baixa, o valor sobe um ponto a cada 256
frames (ou ~17 pontos por 256 frames com o livro). O teste usado no AX está
descrito no relatório da tarefa; o gancho é o `FrameStart` acima.
