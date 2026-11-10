# F4e3b — revisão, rodada 2

Revisei `2d45253`. A loja (`shop.c`, `zelda.shop.set`, `ShopItem_Custom`,
exemplo `loja-nova`, replay `f4e3-shop`) está boa e fica como está.

## Bloqueio: dado do Redux copiado

`kHudTilemapRedux[5][32]` em `src/hud.c` é a tabela do Redux em `$0DFE77`
(`code/menus/24items_menu_withgfx.asm`) copiada para o repositório. O
Redux é GPL e este repositório é MIT: a regra do projeto é **descrever, não
copiar** código, dados ou texto do Redux (veja `REDUX-MAPA.md`).

Troque a tabela por código nosso que **monta** o fundo do HUD a partir de
uma descrição do layout (moldura da barra de magia em x 3..12 nas linhas
2–3, moldura da caixa de item em x 27..30 nas linhas 0–3, ícones fixos dos
contadores, resto vazio), usando os ids de tile do HUD do próprio zelda3
(os mesmos que o HUD vanilla já usa: `0x207f` vazio, `0x2850`/`0x3c4f` da
barra, `0x285b`/`0x285c`/`0x285d` da caixa, etc.). Tire o comentário que
aponta o endereço/arquivo do Redux como fonte da tabela; pode citar o
`REDUX-MAPA.md` como referência do layout. O resultado na tela pode (e
deve) ser o mesmo layout; o que não pode é a tabela copiada. Confira que
nenhuma outra parte do diff copiou tabelas ou texto do Redux.

## Comparação

O lado a lado atual mostra cenas diferentes (o nosso numa sala do castelo,
o Redux numa área de praia), o que não prova muito. Refaça com as duas
imagens no mesmo tipo de cena (por exemplo, os dois logo depois de entrar
no jogo, ou a mesma sala), mesma vida e magia se der, e descreva no
relatório cada diferença que sobrar (cor da barra, número de linhas de
corações, posição dos contadores).

## Verificação

`make -j2`, `make -j2 NO_LUA=1` sem warnings, `tools/regress.sh` com todas
as linhas `ok` e a linha final. Se o golden `redux-game-hud` mudar, diga
por quê. Commit novo na mesma branch.
