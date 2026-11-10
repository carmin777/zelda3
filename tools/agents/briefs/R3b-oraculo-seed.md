# Tarefa R3b-oraculo-seed — o oráculo contra a ROM de uma seed

Leia `AGENTS.md`, `docs/engine/RANDOMIZER-COMPAT.md` (seções 3 e 5:
oráculo, base JP 1.0), `docs/engine/RANDOMIZER-R2.md`,
`docs/engine/RANDOMIZER.md`, `docs/engine/F4E-DESENHO.md` (F4e.4) e
`src/zelda_cpu_infra.c` (como o oráculo carrega a ROM, aplica `PatchRom`
e compara RAM/VRAM).

Material fora do git: `japanese.sfc`, a seed em `.agents/alttpr/download/`,
`tools/alttpr_seed.py build`, referências do `z3randomizer` em
`.agents/refs/z3r/`.

## Objetivo

A F4e.4 quer o comportamento do `z3randomizer` em C, **provado pelo
oráculo contra a ROM da seed**. Esta tarefa mede o tamanho disso e dá o
primeiro passo.

## O que fazer

1. **Rodar o oráculo com a ROM da seed** (opção de teste, ex.
   `--oracle-rom seed.sfc`): o C nativo roda e o emulador executa o asm da
   seed lado a lado, comparando a RAM como hoje. O `PatchRom` do oráculo foi
   feito para a ROM US: veja quais dos 56 patches se aplicam à ROM da seed
   (que é JP 1.0 + z3randomizer), quais conflitam e trate (o RANDOMIZER-COMPAT
   estimou 1 sobreposição com os desvios do `hooks.asm`).
2. **Medir a primeira divergência** a partir de um ponto comum (boot até a
   seleção de arquivo, criar arquivo, primeiros passos): em que frame, que
   endereços, e **por quê** (diferença JP 1.0 × US? desvio do z3randomizer?
   SRAM estendida?). Repita para avançar além de cada divergência
   (por exemplo, anotando e pulando) e monte uma **lista ordenada** das
   causas, com o desvio do `hooks.asm` correspondente quando houver.
3. Implemente em C **a primeira causa** da lista que for comportamento do
   `z3randomizer` (não diferença JP), atrás de um modo "seed" que só liga
   com a ROM da seed carregada, e mostre o oráculo indo mais longe.
4. `docs/engine/RANDOMIZER-ORACULO.md`: método, a lista ordenada, quanto
   falta (estimativa honesta), e o que é diferença JP 1.0 × US (que exige
   um "modo JP" no C).

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final (o caminho normal não pode mudar); as saídas
reais das divergências (frame, endereço, valores) antes e depois do passo 3.
