# Replays de features

Replays que só fazem sentido com uma feature do zelda3 ligada. Com feature
ligada a comparação byte a byte contra a ROM fica desligada, então estes
casos são conferidos pelo trace de eventos e pelo hash final, contra
`tests/golden-features/<nome>.txt`. O `tools/regress.sh` roda esta pasta
depois dos replays normais (`--update` regrava os golden daqui também).

Cada replay usa `tests/replays-features/<nome>.ini` se existir; senão, `tests/regress-switchlr.ini` (`ItemSwitchLR = 1`). Os
próprios `.sav` já carregam `kFeatures0_SwitchLR` no snapshot inicial, então a
feature fica ligada durante o replay independentemente do `.ini`; o `.ini` só
serve para deixar a intenção explícita e evitar surpresas se o snapshot for
regravado.

Se existir `tests/replays-features/<nome>.mod`, o regress copia o diretório de
mod ali indicado para uma pasta temporária e roda o replay com `--mods-dir`
em vez de `--no-mods`. O arquivo tem só o caminho, por exemplo
`mods-examples/espelho-sombrio`. É assim que os casos de filtros e decisões
(uma linha `hook ...` no golden) rodam sem depender da pasta `mods/` do
usuário. O grep do golden inclui `compare`, `ev `, `hook `, `override ` e
`final`.

## #236 — Book of Mudora / Magic Mirror fora do Y

Os dois replays usam o menu para equipar o item no X e depois apertam X. No
livro, o Y também é trocado (fica a rede no Y e o livro só no X); no espelho, o
Y já estava com bombas. O snapshot inicial de cada um foi extraído de um replay
já existente e o bit da feature foi gravado nele, então o `.sav` é
autossuficiente.

### Livro de Mudora na placa do deserto

Snapshot: `tests/replays/issue242.sav` no frame 500 (Link diante da placa do
Desert Palace, livro no Y). O roteiro equipa o livro no X, troca o Y para a
rede e aperta X.

```sh
./zelda3 --config tests/regress-switchlr.ini --headless --no-mods \
  --replay tests/replays-features/issue236-book.sav --frames 399 --mod-trace \
  zelda3.sfc | grep -E '^(compare|ev |final)'
```

Esperado (`tests/golden-features/issue236-book.txt`): `DialogueOpen 189`
(mensagem 0xbd, a placa do deserto) no frame 275. Sem a correção não há
`DialogueOpen`.

### Magic Mirror no Dark World como coelho

Snapshot: `saves/ref/Chapter 4 - After Tower of Hera.sav` no frame 69477
(Link coelho no Dark World sem Moon Pearl, espelho no inventário). O roteiro
equipa o espelho no X e aperta X.

```sh
./zelda3 --config tests/regress-switchlr.ini --headless --no-mods \
  --replay tests/replays-features/issue236-mirror.sav --frames 399 --mod-trace \
  zelda3.sfc | grep -E '^(compare|ev |final)'
```

Esperado (`tests/golden-features/issue236-mirror.txt`): `WorldChange 0 1`
(Dark World → Light World) no frame 237. Sem a correção o espelho não é usado
e o Link continua coelho no Dark World.

Para comparar com o golden:

```sh
diff -u tests/golden-features/issue236-book.txt <(
  ./zelda3 --config tests/regress-switchlr.ini --headless --no-mods \
    --replay tests/replays-features/issue236-book.sav --frames 399 --mod-trace \
    zelda3.sfc 2>/dev/null | grep -E '^(compare|ev |final)')
```

## #246 — Barreira do Agahnim no widescreen

O snapshot inicial foi extraído de `tests/issues/issue245-start.sav` no frame
33030 (Link entrando na sala 0x60 do Hyrule Castle, com a barreira ainda não
carregada). O roteiro sai para o exterior do castelo com a câmera em 0x6ae,
onde a barreira fica na borda direita da tela estendida; depois desce e anda
para a direita para trazê-la para dentro da tela. O `.sav` já carrega
`kFeatures0_ExtendScreen64`/`kFeatures0_WidescreenVisualFixes` no snapshot, e
`tests/replays-features/issue246.ini` liga o widescreen (`16:9`).

```sh
./zelda3 --config tests/replays-features/issue246.ini --headless --no-mods \
  --replay tests/replays-features/issue246.sav --frames 500 --mod-trace \
  zelda3.sfc | grep -E '^(compare|ev |final)'
```

Esperado (`tests/golden-features/issue246.txt`): `SpriteSpawn 13 64` (tipo
0x40, a barreira do Agahnim) no frame 45. Sem a correção a varredura inicial
com widescreen não carrega a barreira e ela não aparece no resto do replay.

## Menu de 24 itens (NewStyleInventory)

Snapshot: `saves/ref/Chapter 10 - After Ice Palace.sav` no frame 77699 (fim
do capítulo, com todos os itens e garrafas). O snapshot base já carrega
`kFeatures0_NewStyleInventory`. O roteiro abre o menu, desce duas linhas,
escolhe a pá, equipa e fecha. O `.ini` deixa a opção explícita.

```sh
./zelda3 --config tests/replays-features/newstyle-menu.ini --headless --no-mods \
  --replay tests/replays-features/newstyle-menu.sav --frames 220 --mod-trace \
  zelda3.sfc | grep -E '^(compare|ev |final)'
```

Esperado (`tests/golden-features/newstyle-menu.txt`): `compare: off` e o
`ModuleChange 14`/`9` do menu. Sem a opção o layout de 20 espaços é o mesmo do
jogo original e a comparação lado a lado continua ativa.

## HUD e cronômetro do Redux

`redux-hud-timer` usa o mesmo snapshot do menu de 24 itens, mas roda com
`mods-examples/opcoes-redux`, que liga `NewStyleInventory`,
`HorizontalMagicBar` e `MenuTimer`. O roteiro abre e fecha o menu, desenha a
barra horizontal e o relógio, e o golden confere o trace e o hash final. O
`.ini` repete as três opções para deixar a intenção explícita; o `.mod` é a
fonte efetiva das features.

```sh
./zelda3 --config tests/replays-features/redux-hud-timer.ini --headless \
  --mods-dir <pasta-com-opcoes-redux> \
  --replay tests/replays-features/redux-hud-timer.sav --frames 220 \
  --mod-trace zelda3.sfc | grep -E '^(compare|ev |final)'
```

`redux-game-hud` usa um snapshot curto no fim do capítulo 10 e o mesmo
`opcoes-redux` para exercitar só o HUD de jogo com `ReduxHud` (corações no
canto superior esquerdo, barra horizontal embaixo, contadores deslocados e
caixa de item no canto superior direito). O golden tem o `compare: off` e o
hash final do estado do HUD.

## F4e.3b — loja definida por mod

`f4e3-shop.sav` começa na entrada da loja de Kakariko (sala 0x1f) e repete os
inputs do replay de referência que compram o coração do meio. O `.mod` aponta
para `mods-examples/loja-nova`, que registra a pena da F4e.5 e usa
`zelda.shop.set` para colocar a pena no lugar do coração. O golden tem
`ItemReceive 128` (0x80, o id do item novo) no frame 150.

```sh
./zelda3 --config tests/replays-features/f4e3-shop.ini --headless \
  --mods-dir <pasta-com-loja-nova> \
  --replay tests/replays-features/f4e3-shop.sav --frames 250 \
  --mod-trace zelda3.sfc | grep -E '^(compare|ev |final)'
```

## C3b — câmera que se divide sozinha

`camera-split.sav` parte do replay `issue242.sav` (mesmo snapshot base), para
no frame 250 e continua com o jogador 1 andando para a direita. O `.script2`
leva o jogador 2 para a direita até o frame 251 e depois para a esquerda/cima.
O golden mostra a divisão e a junção pelo evento `CameraSplit`:

```
compare: off
ev 152 CameraSplit 1 209 33
ev 367 CameraSplit 0 143 90
final 550 ...
```

```sh
./zelda3 --config tests/replays-features/camera-split.ini --headless \
  --no-mods --replay tests/replays-features/camera-split.sav \
  --input-script2 tests/replays-features/camera-split.script2 \
  --frames 550 --mod-trace zelda3.sfc
```

O `.sav` foi gerado com `--stop-replay-at 250 --save-replay`; os primeiros
250 frames são os inputs do `issue242` e o resto é o roteiro do jogador 1.

## C3c — vida separada e respawn do jogador 2

`respawn-p2.sav` é uma cópia de `cannon-trooper.sav`. O mod de teste
`mods/p2-low-health` baixa a vida do jogador 1 uma vez no frame 2, antes do
primeiro seed do jogador 2; os dois começam com um coração. O `.script2` leva o
jogador 2 para a esquerda, na frente do canhão; ele toma dano nos frames 54 e
114, morre e reaparece no frame 234, sem game over. O jogador 1 continua com a
vida dele (e toma um dano separado no frame 166).

```
compare: off
ev 54 LinkDamage 4 8 0
ev 114 LinkDamage 4 4 0
ev 114 LinkDeath 0 0 0
ev 114 PlayerDeath 2 0 0
ev 148 SpriteSpawn 12 107 0
ev 166 LinkDamage 4 8 0
ev 234 PlayerRespawn 2 24 0
ev 270 SpriteKilled 13 107 0
ev 270 DropSpawned 216 13 0
final 450 ...
```

```sh
./zelda3 --config tests/replays-features/respawn-p2.ini --headless \
  --mods-dir <pasta-temporaria-com-p2-low-health> \
  --replay tests/replays-features/respawn-p2.sav \
  --input-script2 tests/replays-features/respawn-p2.script2 \
  --frames 450 --mod-trace zelda3.sfc
```

`PlayerDeath`/`PlayerRespawn` são os eventos novos da C3c (`a` = jogador,
`b` = vida depois do respawn).

## C3d — espada do P2 e itens diferentes no mesmo frame

`p2-sword-hit.sav` parte do snapshot do `sword-beam-hit`, com o P1 parado. O
mod de teste `mods/p2-sword-target` aproxima o Hopping Bulb Plant (slot 13,
tipo 0x22) já carregado, e o `.script2` leva o P2 até ele e golpeia com a
espada normal. O log de depuração mostra `hp 8 -> 4` e o golden tem:

```
compare: off
ev 40 Player2SpriteDamage 13 34 4
final 60 ...
```

`Player2SpriteDamage` é o evento novo da C3d (`a` = slot, `b` = tipo,
`c` = dano); ele só dispara quando o dano pendente do P2 realmente desconta a
vida do sprite. O mesmo evento aparece no `shared-world-two-links` antigo
(`ev 186 Player2SpriteDamage 13 107 2`).

`p2-two-items.sav` usa o mesmo snapshot. O mod `p2-two-items` deixa o P2
nascer com o bumerangue e depois seleciona o arco só para o P1; os dois
apertam Y no frame 20. O golden guarda o hash final; para a prova visual, o
frame 24 mostra o arco no Link 1 e o bumerangue no Link 2 (os índices de DMA
dos dois Links diferem).

## F4e.1 — filtros e decisões

Os três casos abaixo exercitam ganchos de `src/mod/hooks.c` com os mods de
exemplo. O golden mostra a linha `hook ...` da chamada que mudou o resultado e
o hash final.

- `f4e-mirror.sav` + `f4e-mirror.mod` (`mods-examples/espelho-sombrio`):
  o mesmo snapshot do caso #236 do espelho, mas o mod nega o espelho no Mundo
  Sombrio. O golden tem `hook ... Decide MirrorAllowed 1 -> 0 arg 64` e **não**
  tem `WorldChange`.
- `f4e-max-items.sav` + `f4e-max-items.mod` (`mods-examples/redux-ganchos`):
  menu de 24 itens com o mod que sobe bombas para 60 e flechas para 99. O
  golden tem as linhas `hook ... Filter MaxBombs 50 -> 60` e
  `MaxArrows 70 -> 99`.
- `f4e-pikit.sav` + `f4e-pikit.mod` (`tests/replays-features/mods/pikit-setup`):
  põe Link perto de um Pikit já carregado e nega o roubo de escudo. O golden
  tem `hook ... Decide PikitStealShield 1 -> 0 arg 13`.

## F4e.1 — correção do dano do sword beam

`sword-beam-hit.sav` parte do snapshot de `f4e-pikit.sav` no frame 100 (Link
com espada L2, vida cheia e um inimigo à direita). O roteiro vira para a
direita e golpeia com B. O raio nasce como `ancilla_type 0x0c` e acerta o
sprite no slot 13. Com `redux-ganchos`, o golden tem:

```
hook 139 Filter SwordBeamDamage 1 -> 2 arg 13
```

A classe 1 é a da espada L1; a 2 é a da espada mestra (L2) no golpe normal.
O `0x0b` é o tiro da vara de gelo e **não** aparece nesse filtro.

## F4e.1b — ganchos da onda 4

- `f4e-linkdamage.sav` + `f4e-linkdamage.mod` (`mods-examples/hero-mode`):
  cópia do replay `cannon-trooper.sav`, com dano de sprite no frame 149 e um
  drop no frame 258. O golden tem
  `hook 149 Filter LinkDamage 4 -> 8 arg 0`, a vida final menor que no jogo
  base e o evento `DropSpawned 216 13` (coração); o `sprite_state` do slot é
  zerado no mesmo instante em que o drop nasce, para o coração não aparecer
  por um frame.
  A fonte `0` no argumento é sprite (`1` ancilla, `2` buraco, `3` ambiente).

## F4e.3 — substituir a rotina de um sprite

`f4e-sprite-override.sav` começa na sala 80 (Hyrule Castle) com dois
cavaleiros verdes (`0x4B`) já carregados. O `.mod` aponta para
`mods-examples/inimigo-novo`, que substitui a rotina desse tipo por uma
versão que foge do Link e só se aproxima quando ele está de costas. O golden
tem as linhas `override ... Sprite 4B-Knight ...` (primeira chamada de cada
slot e amostras a cada 32 frames) e o hash final.

```sh
./zelda3 --config tests/replays-features/f4e-sprite-override.ini \
  --headless --mods-dir <pasta-com-inimigo-novo> \
  --replay tests/replays-features/f4e-sprite-override.sav --frames 300 \
  --mod-trace zelda3.sfc | grep -E '^(compare|ev |hook |override |final)'
```

Esperado (`tests/golden-features/f4e-sprite-override.txt`): `compare: off`,
as linhas de `override` com as posições mudando e `final 300 ...`. Sem o mod,
os cavaleiros usam o movimento aleatório original; a rotina do jogo continua
sendo chamada por `original(slot)`.
