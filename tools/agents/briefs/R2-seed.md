# Tarefa R2-seed — ler uma seed do alttpr.com e testar rodá-la no emulador

Leia `AGENTS.md` e **`docs/engine/RANDOMIZER-COMPAT.md` inteiro** (inclusive
as descobertas: base JP 1.0, ALTTP-DX roda seeds no emulador embutido).
Esta tarefa é **investigação + protótipo**, sem decidir o produto final.

## Material (fora do git, só leitura)

- `japanese.sfc` — ROM JP 1.0 local (fora do git)
  (CRC32 `3322EFFC`).
- `.agents/alttpr/download/seed-xvJo08oxyJ.json` — seed diária de
  09/11/26 (`patch`, `spoiler`, `hash`, `size: 2`).
- `.agents/alttpr/download/base-79bbb4ad512838ab01fc06b7971f656c.bps` —
  patch-base (build `2023-09-22`).
- Referências do `z3randomizer` (MIT): `.agents/refs/z3r/z3r-tables.asm`,
  `z3r-ram.asm`, `z3r-sram.asm`, `z3r-hooks.asm`.
- **Não commite** ROMs, a ROM da seed, o JSON da seed nem dados extraídos.

## O que fazer

1. **Montar a ROM da seed** com uma ferramenta nossa (sem rodar nada
   baixado): `tools/alttpr_seed.py build --jp-rom ... --base ... --seed
   ... -o seed.sfc`, com BPS (CRCs conferidos) e a lista da seed, e
   conferência do MD5 do patch-base contra o `md5` informado. Pode
   reaproveitar a lógica de BPS/UPS que já existe em scripts temporários;
   escreva a sua no repositório.
2. **Ler a seed**: `tools/alttpr_seed.py info seed.sfc` imprime nome, os 5
   ícones de hash (a tabela `SeedHash` do `z3randomizer`), modo, goal,
   cristais, e onde estão os itens principais, lendo as tabelas
   documentadas (`tables.asm`) e o spoiler do JSON quando houver. Confira
   uma dúzia de posições contra o spoiler.
3. **Viabilidade de rodar a ROM da seed no emulador embutido** (o
   `snes/` que o oráculo usa): carregue a `seed.sfc` como cartucho
   completo (sem o `PatchRom` do oráculo, que é só para a ROM US) e rode
   N frames headless com entrada roteirizada até a seleção de arquivo e o
   começo do jogo; tire screenshots. O que falta para isso ser jogável
   (APU/som, SRAM, entrada, saída de vídeo do emulador)? Meça a
   velocidade (frames por segundo). Se precisar de código novo em C,
   ponha atrás de uma opção de linha de comando de teste (`--emu-rom
   <arquivo>`) e deixe o caminho normal intocado.
4. Escreva `docs/engine/RANDOMIZER-R2.md` com o que funcionou, as medidas
   e uma recomendação concreta (emulador primeiro? nativo depois?).

## Verificação a relatar

`make -j2` sem warnings; `tools/regress.sh` com todas as linhas `ok` e a
linha final; saída do `info` (sem colar o spoiler inteiro); screenshots em
`.agents/shots/` (fora do git) com o que a ROM da seed mostrou; fps medido.
