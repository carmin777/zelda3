# Tarefa I1-importador-base — import-rom aceita hacks e traz os gráficos

Leia `AGENTS.md`, `docs/engine/MODS.md` (importação de ROM traduzida),
`docs/engine/ALTTI-MAPA.md` (seções 4–6) e `docs/engine/REDUX-MAPA.md`
(seção 5). Esta tarefa são os **passos 1 e 2** do plano do ALTTI-MAPA.

## Material (fora do git, só leitura)

- `altti.sfc` — A Link to the Islands v2.5
  (3 MB, Hyrule Magic/ZScream).
- `redux.sfc` e `redux-retrans.sfc` —
  Redux (2 MB, FastROM).
- `portuguese.sfc` — tradução PT (já
  suportada).
- **Não commite** ROMs, dados extraídos delas, mods gerados nem imagens.
  `mods/` está no `.gitignore`.

## Passo 1 — destravar o import-rom

1. **ROM desconhecida**: hoje `import-rom` falha no portão de hash de
   `assets/util.py` (`ROM with hash ... not supported`). A ROM **base**
   (US) continua obrigatoriamente verificada pelo hash; a ROM **de origem**
   do `import-rom` (o hack) pode ser qualquer uma. Não adicione hashes de
   hacks a listas; faça o `import-rom` aceitar uma ROM de origem
   desconhecida (com um aviso claro, ou um flag explícito — escolha e
   documente). A extração normal do `zelda3_assets.dat` a partir da ROM US
   **não pode mudar**.
2. **`assert (ea & 0x8000)`** em `assets/util.py` (`get_byte`): o
   ALTTI-MAPA diz que o hack tem ponteiros como `$0A:2D14` na tabela de
   tiles do mapa de dungeon. Em LoROM, `$0A:0000-7FFF` **não é ROM**.
   Antes de mexer no mapeamento, descubra o que esses valores são de
   verdade: leia a rotina da ROM do hack que usa essa tabela (o código
   original está em `src/` com o comentário de endereço; compare os bytes
   da rotina no hack com os da US) e veja como ela forma o endereço (banco
   vindo de outro lugar, base somada, formato novo do ZScream…). Corrija o
   **leitor do import** para seguir o que o código do hack faz. Não remova
   o assert só para passar.
3. **YAML US no import**: o `import-rom` compila o hack com o YAML de
   salas/overworld da US e quebra quando a tabela de áreas do hack difere.
   Nesta tarefa salas e overworld **ficam de fora**: faça o `import-rom`
   pular essas categorias de forma explícita (com mensagem dizendo o que
   não foi importado e por quê) em vez de quebrar. Elas serão os passos
   3–4, outra tarefa.

## Passo 2 — gráficos, paletas e mapas

Faça o `import-rom` trazer do hack, quando diferirem da US:
`kSprGfx`, `kBgGfx`, `kOverworldMapGfx`, paletas (todas as que o
`asset_names.c` expõe), `kDungMap_*` e as tabelas de `game_tables.def`.
Os tilemaps fixos de `print_tilemaps` (o ALTTI-MAPA diz que o hack moveu
2–4 e trocou a 0): ache as fontes novas **pelo código do hack** (o que
carrega cada tilemap) e importe; se não conseguir provar, deixe de fora e
diga.

## Verificação a relatar

1. `make -j2` sem warnings; `tools/regress.sh` com todas as linhas `ok` e a
   linha final (não deve mudar nada no jogo).
2. **A base não mudou**: regenere o `zelda3_assets.dat` a partir da ROM US
   numa pasta temporária e compare o SHA-1 com o
   `zelda3_assets.dat` atual. Tem que ser
   idêntico.
3. **PT sem regressão**: `import-rom portuguese.sfc` numa pasta temporária,
   antes e depois da sua mudança; os arquivos gerados têm que ser
   idênticos (liste os SHA-1). Mesmo teste com `redux.sfc`, que agora deve
   funcionar sem editar `assets/util.py`.
4. **Islands**: `import-rom altti.sfc` numa pasta temporária fora de
   `mods/`: cole a saída (o que importou, o que pulou e por quê).
5. **Prova visual** (headless `--screenshot`, PNGs em `.agents/shots/`,
   fora do git) com o mod do Islands: tela de título, seleção de arquivo,
   mapa-múndi (abra o mapa no overworld) e o mapa de uma masmorra. Mostre
   também a mesma tela sem o mod. As salas continuam as da US, então o
   jogo em si vai parecer misturado: diga isso, não é bug desta tarefa.
6. `docs/engine/MODS.md`: documente o uso com ROM de hack, o que é
   importado e o que ainda não é. Commit em inglês.

## Relatório

Além do pedido no AGENTS.md: o que os ponteiros `$0A:2D14` eram de
verdade (com a prova no código do hack), as fontes dos tilemaps, e a
lista final de assets importados do Islands e do Redux.
