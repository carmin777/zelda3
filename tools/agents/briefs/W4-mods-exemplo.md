# Tarefa W4-mods-exemplo — onda 4: pedidos da comunidade viram mods

Leia `AGENTS.md`, `docs/engine/MODS.md` (inteiro: features, eventos,
filtros e decisões, estado, desenho, `zelda.save` se já existir) e
`docs/engine/UPSTREAM-TRIAGEM.md` (grupo D). Lista de hacks pequenos do
romhacking.net (só os nomes, para inspiração):
`.agents/rhdn-alttp-hacks.txt`.

## O que fazer

Mods **só de dados/Lua** em `mods-examples/<id>/`, cada um com `mod.json`,
script e um `README.md` curto (o que faz, como testar, de onde veio o
pedido). Sem mudar o C; se um pedido precisar de gancho que não existe,
**não crie** o gancho: anote no relatório qual faltou.

1. #262 nível de espada para quebrar vaso (usar `BreakPotsWithSword` +
   filtro/decisão se existir; se não der sem C, diga).
2. #295 cheats ligados por padrão: veja o que o zelda3 chama de cheats e
   faça um mod que os ligue, se for possível sem C.
3. "Começar com vida cheia ao carregar" (PowerPanda): evento de carga +
   `zelda.link`.
4. "God Mode" (vida não cai): filtro/evento de dano.
5. "Hero Mode" (dano dobrado / sem corações caindo, confira o que o hack
   faz pela descrição pública que você conseguir; se não souber, defina e
   documente a sua versão).
6. "Quick Commands" e "No Sword Beam": só se der com o que existe.
7. #126 "corrigir todos os bugs": um mod só com `"features"` que liga
   `MiscBugFixes` e `GameChangingBugFixes`.

## Verificação a relatar

Para cada mod: carregado sem erro (`mods: loaded`), uma execução headless
curta mostrando o efeito (`--mod-trace`, `--dump-state` ou screenshot em
`.agents/shots/`, fora do git), e o que não foi possível. `make -j2` e
`tools/regress.sh` verdes (não deve mudar nada no C).
