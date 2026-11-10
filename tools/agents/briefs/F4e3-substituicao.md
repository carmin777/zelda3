# Tarefa F4e3-substituicao — trocar a rotina de um tipo de sprite

Leia `AGENTS.md`, **`docs/engine/F4E-DESENHO.md`** (aprovado; seção 3.3 e
etapa F4e.3), `docs/engine/MODS.md` (Lua, estado por nome, filtros e
decisões, eventos) e `src/sprite_main.c` (tabela de rotinas por tipo,
`kSprite_Funcs` ou equivalente).

## O que fazer

1. **Mecanismo**: um mod pode substituir a rotina de **um tipo de sprite**
   (`zelda.override("Sprite:<tipo>", { tick = fn })`, tipo em hexa ou
   pelo nome da tabela de nomes de sprite). Dentro de `fn(slot)`, o mod
   lê e escreve os campos do slot por nome (`zelda.sprite.get(slot,
   "x")`, `set`, com a lista de campos documentada a partir de
   `variables.h`: posição, velocidade, estado, `ai_state`, timers, vida,
   gráficos, direção…) e pode chamar `original(slot)` para rodar a rotina
   do jogo. Exige `state_write` no `mod.json`.
   - Custo zero sem mod: o despacho só desvia quando há override para
     aquele tipo (máscara/array de 256 entradas).
   - Erro, limite de instruções ou tipo errado → mod desativado; o sprite
     volta à rotina original no mesmo frame.
   - Override liga `g_mods_change_content` (comparação desligada).
2. **Mod de exemplo** `mods-examples/inimigo-novo/`: um inimigo comum com
   comportamento diferente (por exemplo, um soldado verde que foge do
   Link e só ataca de costas), usando `original()` para a animação/desenho
   e trocando só a decisão de movimento. README explicando.
3. **Prova**: replay de feature (padrão `tests/replays-features/` com
   `.ini`, `.mod`, `.sav` e golden) numa sala com esse inimigo, mostrando
   no trace o override rodando e o comportamento novo (posições diferentes
   do jogo base). Screenshots fora do git.
4. Documente em `MODS.md` ("Substituir rotinas de sprite") e
   `HOOKS-MAPA.md`.

Fica **fora** desta tarefa (próximas): menu/HUD do Redux, loja, itens
novos.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final; o trace do replay novo; teste de erro
(override que lança erro → mod desativado, sprite segue com a rotina
original); custo sem mod (diga como garantiu que o despacho não muda).
