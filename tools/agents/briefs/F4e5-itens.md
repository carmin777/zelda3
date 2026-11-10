# Tarefa F4e5-itens — registro de itens novos

Leia `AGENTS.md`, **`docs/engine/F4E-DESENHO.md`** (aprovado; seção 3.5 e
etapa F4e.5), `docs/engine/MODS.md` (filtros/decisões, override de sprite,
`zelda.save`, menu de 24 itens `NewStyleInventory`) e, no C, como um item
existe hoje: id de recebimento (`kReceiveItem*`, `Link_ReceiveItem`),
gráfico de recebimento, texto, espaço no menu (`src/hud.c`), efeito ao
usar (`Link_HandleYItem` e afins em `src/player.c`), máximo, save.

## O que fazer

1. **Registro** (`src/mod/items.{c,h}` + Lua): um mod declara um item novo
   com `zelda.item.register{ name=..., icon=..., receive_gfx=...,
   text=..., max=..., on_use=fn, on_receive=fn }` (ou um bloco no
   `mod.json`). O item ganha um id fora da faixa original, um espaço no menu
   de 24 itens (`NewStyleInventory`, que já tem espaços livres) e o estado
   dele (tem/não tem, quantidade) é salvo pelo `zelda.save` do mod. O
   gráfico vem de um PNG do mod (descreva o formato).
2. **Receber**: o item pode ser dado por um baú/NPC via o filtro
   `ItemReceive` (o mod troca o item original pelo novo) com a animação e o
   texto de recebimento próprios.
3. **Usar**: equipado no Y, `on_use` é chamado com acesso ao estado do
   Link por nome; o mod decide o efeito (por exemplo, criar um efeito com as
   ancillas existentes ou mudar estado).
4. **Mod de exemplo** `mods-examples/item-novo/`: um item simples e visível
   (por exemplo, uma "pena" que dá um pulo curto ou uma "lanterna" que acende
   tochas ao redor), dado pelo baú da casa do Link, com ícone, texto e uso.
5. Sem mod, nada muda (portão). Documente em `MODS.md` ("Itens novos").

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final; um replay de feature (abrir o baú, receber o
item, equipar e usar) com golden; screenshots do menu com o item e do uso
(`.agents/shots/`, fora do git); salvar e carregar mantendo o item. O que
ainda não é possível com itens novos.
