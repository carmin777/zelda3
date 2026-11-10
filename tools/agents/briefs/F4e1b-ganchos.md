# Tarefa F4e1b-ganchos — ganchos pedidos pelos mods da onda 4

Leia `AGENTS.md`, `docs/engine/F4E-DESENHO.md`, `docs/engine/MODS.md`
(seção "Filtros e decisões"), `docs/engine/HOOKS-MAPA.md` (seção 15) e
`src/mod/hooks.{c,h}` (o mecanismo da F4e.1). Os mods da onda 4 estão em
`mods-examples/` e cada `README.md` diz o que faltou.

## O que fazer

Mesmo padrão da F4e.1 (`MOD_FILTER`/`MOD_DECIDE`, tabela única de nomes,
custo zero sem mod, endereço da rotina original no comentário, linha no
`HOOKS-MAPA.md`). Confira cada ponto no código antes; se não bater, diga e
pule.

| Nome | Tipo | Onde | Quem pediu |
|---|---|---|---|
| `LinkDamage` | filtro | onde o dano ao Link é aplicado à vida (todas as fontes: sprite, ancilla, buraco, ambiente); arg = fonte | `god-mode`, `hero-mode` (hoje usam reposição a cada frame) |
| `SwordBeamSpawn` | decisão | onde o raio da espada é criado | `no-sword-beam` |
| `DropSpawned` | evento (via `MOD_EMIT`) | onde um drop de inimigo/arbusto/vaso é criado; a = tipo do item, b = slot | `hero-mode` (remover corações sem aparecer 1 frame) |
| `PotBreakSwordLevel` | decisão | onde `BreakPotsWithSword` decide se a espada atual quebra o vaso; arg = nível da espada | `espada-quebra-vaso` (#262) |

Depois **atualize os quatro mods** para usarem os ganchos novos no lugar
dos contornos, e os `README.md` deles.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final. Para cada gancho, uma execução headless
mostrando a linha `hook` no `--mod-trace` e o efeito; um replay de feature
novo (padrão `tests/replays-features/<nome>.{sav,ini,mod}` + golden) para
o `LinkDamage`.
