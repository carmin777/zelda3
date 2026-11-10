# Tarefa R4-importador-seed — o motor em C joga uma seed do alttpr.com

Leia `AGENTS.md`, **`docs/engine/RANDOMIZER-COMPAT.md`** (em especial
"Mudança de rumo (10/11/26)": a meta mudou), `docs/engine/RANDOMIZER.md`,
`docs/engine/RANDOMIZER-ORACULO.md` (o que já se aprendeu sobre a ROM JP e o
z3r) e `docs/engine/MODS.md` (ganchos da F4e, `zelda.item.register`,
`features`). Referências locais, fora do git: `.agents/refs/z3r/`
(`z3r-tables.asm`, `z3r-hooks.asm`, `z3r-ram.asm`, `z3r-sram.asm`) e a seed
`.agents/alttpr/download/seed-xvJo08oxyJ.{json,sfc}` (o JSON desta seed não
tem spoiler; os locais vêm da ROM).

**Não** é para o C imitar a ROM JP. O motor continua o US (o oráculo US
continua valendo); a seed entra como **dados**.

## O que fazer

1. **Desenho curto** em `docs/engine/RANDOMIZER-IMPORT.md`: que dados a seed
   carrega e onde ficam na ROM (item de cada baú nas salas, itens soltos,
   itens de NPC/chefe/prêmio, inventário inicial, configurações do z3r,
   textos e dicas), lidos pelas tabelas do `z3r-tables.asm` e pelo formato
   das salas; os ids de item do z3r (espada progressiva, chaves por
   masmorra, etc.) e como cada um vira item no motor US (os que não
   existem no US viram itens da F4e.5 ou regra nova).
2. **Primeira fatia, ponta a ponta**:
   - `tools/` (ou `import-rom --seed`) gera, a partir da ROM da seed, um mod
     numa pasta gitignored (por exemplo `mods-seeds/<hash>/`) com a tabela
     local → item e as configurações;
   - o motor aplica: baús (incluindo o da casa do Link) e o item do tio,
     pelos ganchos da F4e (filtro no item recebido / no conteúdo do baú),
     sem mudar nada sem o mod (portão);
   - prova dinâmica: replay de feature (com o mod gerado por um passo do
     teste, já que a ROM da seed não vai para o git; pule com aviso se a
     seed não existir, como o `seed-oracle`) abrindo o baú da casa e
     recebendo o item do tio, conferindo que os itens são os que a ROM
     da seed tem nesses locais.
3. **Checagem estática**: um comando que lista todos os locais com o item
   da seed e o item que o mod gerado dá, e diz quantos o motor já cobre e
   quais faltam (por tipo de local). Esse número é a métrica de progresso
   daqui em diante.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final (o `seed-oracle` continua passando); a
cobertura de locais (N de M); o replay da casa/tio. Não commite ROM, nem o
mod gerado, nem nada extraído da seed.
