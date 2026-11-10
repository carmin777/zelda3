# Tarefa F3-lua — scripting em Lua 5.4 para mods

Mods passam a poder ter código em Lua, reagindo aos eventos da F1
(`src/mod/events.h`) e carregados pelo mod loader da F2 (`src/mod/`,
`docs/engine/MODS.md`). Leia os dois antes de começar.

## Lua

- Use o Lua 5.4 do sistema: `pkg-config --cflags --libs lua5.4` (headers em
  `/usr/include/lua5.4`). **Não baixe nada da internet** e não vendorize o
  Lua nesta tarefa.
- `make` normal compila com Lua; `make NO_LUA=1` compila sem (stubs que só
  avisam "scripting desativado" se um mod tiver script). Toda a cola com o
  Lua fica em `src/mod/lua_*.c` para ser trocável depois por uma cópia
  vendorizada.
- Os projetos do Visual Studio: adicione os arquivos novos sob um define
  (`ZELDA3_LUA`) desligado por padrão, para não quebrar o build do Windows.

## Modelo

- Campo novo no `mod.json`: `"script": "main.lua"`.
- **Um `lua_State` por mod** (isolamento). Bibliotecas abertas: base, string,
  table, math, utf8. **Sem** io, os, package, debug, e sem `dofile`,
  `loadfile`, `load` com bytecode (`load` só com modo `"t"`). `require`
  ausente. `print` vai para o stderr com prefixo `[<id>]`.
- Script com mod ativo → desliga a comparação com a ROM (mesma flag da F2).
- Erro em script (ao carregar ou num handler): `lua_pcall` com traceback no
  stderr, o mod é desativado (desinscreve os handlers dele) e o jogo segue.

## API (tabela global `zelda`)

```lua
zelda.on("RoomEnter", function(ev) ... end)   -- nomes = kModEventNames
-- ev.a, ev.b, ev.c sempre; aliases por evento:
--   RoomEnter: ev.room, ev.prev   AreaEnter: ev.area, ev.prev
--   ItemReceive: ev.item, ev.chest, ev.method   ChestOpen: ev.room, ev.chest
--   DialogueOpen/Close: ev.msg   LinkDamage: ev.damage, ev.health_before
--   SpriteSpawn/Killed: ev.slot, ev.type   ModuleChange: ev.module, ev.prev
-- ItemReceive é filtro: se o handler retornar um inteiro, ele vira o item.

zelda.frame()             -- contador de frames desde o início
zelda.log(...)            -- igual print

zelda.link.health / max_health / rupees / x / y / bombs / arrows / magic
zelda.item.get("bow")     zelda.item.set("bow", 2)
-- leitura e escrita por NOME, mapeadas para variables.h
-- (inclua pelo menos: bow, boomerang, hookshot, bombs, mushroom/powder,
--  fire_rod, ice_rod, bombos, ether, quake, lamp, hammer, flute, net, book,
--  bottles 1-4, somaria, byrna, cape, mirror, gloves, boots, flippers,
--  moon_pearl, sword, shield, armor, heart_pieces, arrows, keys).
-- Nome desconhecido → erro Lua claro, não crash.

zelda.ram.read8(addr) / read16(addr)   -- só leitura, addr < 0x20000
```

Escrita de estado só pelos nomes (`zelda.link.*`, `zelda.item.set`). Valide
faixas (ex.: health ≤ max_health) para um mod não corromper o save.

## Mods de exemplo (`mods-examples/`)

1. `lua-ola/`: imprime ao entrar em sala, ao abrir diálogo e ao pegar item.
2. `lua-flechas/`: ao receber o arco, dá 30 flechas.
3. `lua-bau-aleatorio/`: mini-randomizer — troca o item de cada baú por outro
   de uma lista segura, usando um gerador próprio com seed fixa definida no
   script (não `math.random` sem seed), para ser determinístico.

Documente a API e os exemplos em `docs/engine/MODS.md` (seção "Scripts Lua").

## Verificação que você deve fazer e relatar

1. `make -j2` e `make -j2 NO_LUA=1` sem warnings novos.
2. `tools/regress.sh` verde (sem mods).
3. Headless, replay 0, `--mods-dir mods-examples --mod-trace`, mostrando as
   linhas reais do `lua-ola` e o `ItemReceive` trocado pelo
   `lua-bau-aleatorio` no baú da sala 260. Rode duas vezes e confirme que o
   hash final é igual nas duas (determinismo).
4. Script com erro de sintaxe e script com erro dentro de handler: mensagem
   + mod desativado + jogo continua. Cole a saída.
5. Sandbox: um script que tenta `os.execute`, `io.open` e `require` falha
   com erro Lua (não com crash). Cole a saída.

## Fora do escopo

Hooks novos na engine, APIs de sprites/salas/desenho, hot reload, mod
manager — fica para depois.
