# Protocolo para corrigir bugs do upstream

A verdade é o **assembly 65816 original**. Com a ROM carregada e sem
features/mods, cada frame roda em C e no assembly e a RAM é comparada; na
primeira divergência o headless imprime o diff (`0xENDEREÇO: meu != deles`)
e sai com código 3. Corrigir um bug de lógica = fazer o C fazer o que o
assembly faz.

## Ferramentas (headless)

```
./zelda3 --config tests/regress.ini --headless --no-mods [início] \
  [--input-script roteiro.txt] [--frames N] [--hash-every K] \
  [--mod-trace] [--save-replay saida.sav] zelda3.sfc
```

Início (um só):
- `--replay-ref <0..12>`: replay de referência desde o começo (lista em
  `tools/regress.sh`; ~1,07 M frames cobrindo o jogo inteiro).
- `--replay <arquivo.sav>` / `--load <arquivo.sav>`: tocar um `.sav` desde
  o começo / carregar o estado final dele.
- `--stop-replay-at N` (com `--replay-ref`/`--replay`): sai do replay no
  frame N e continua com o roteiro. Use para começar de qualquer ponto do
  jogo.

Roteiro (`--input-script`): uma linha por mudança, `<frame> <botões>`, os
botões ficam apertados até a próxima linha. Botões: `B Y Select Start Up
Down Left Right A X L R`, `-` = nada. Frames contam a partir de 1 no
headless (mesma contagem de `--frames` e do `--stop-replay-at`).

`--mod-trace` mostra eventos (sala, módulo, diálogo, item, dano) — útil para
saber onde o Link está. `zelda.state` (Lua) ainda não existe nesta branch.

## Passos

1. **Reproduzir** com o oráculo: achar início + roteiro que dê exit 3. Se
   depois de tentar com método (varrer o frame de um botão, variar a
   duração) não reproduzir, pare e relate o que tentou — não invente
   correção.
2. **Localizar**: os endereços do diff → nomes em `src/variables.h` →
   função C que escreve ali (o comentário `// 87a006` na assinatura é o
   endereço da rotina na ROM). Para ver o que o assembly faz, o emulador
   está em `snes/` e `src/zelda_cpu_infra.c` (`RunEmulatedSnesFrame`); dá
   para ler bytes da ROM pelo endereço.
3. **Corrigir** o C para igualar o assembly. Quando o C lê fora de uma
   tabela, o assembly lê o byte seguinte da ROM: o certo é reproduzir esse
   valor (por exemplo, completando a tabela com os bytes reais da ROM), não
   pular a leitura.
4. **Provar**: o mesmo comando agora termina com exit 0 e `compare: on`.
5. **Tornar permanente**: rode de novo com `--save-replay
   tests/replays/issue<NNN>.sav` (nome sem espaços) e confirme que
   `./zelda3 ... --replay tests/replays/issue<NNN>.sav --frames <total>` dá
   o mesmo hash final. Com `--load <save> --frames <N> --save-replay ...`
   o replay gravado começa no estado carregado e tem `N` frames; com
   `--replay <save> --stop-replay-at <total do save>` + roteiro ele mantém o
   histórico e o total soma os frames. Gere o golden só desse caso
   (`tools/regress.sh --update` regrava todos; confira com `git diff` que
   **os 13 golden antigos não mudaram** e só o novo apareceu). Se o arquivo
   passar de 1 MB, comece de um ponto mais perto do bug.
6. `tools/regress.sh` verde, `make -j2` sem warnings, commit.

## Relatório

Além do pedido no AGENTS.md: o comando exato de reprodução, o diff de RAM
antes da correção, a explicação do que o assembly faz e o C não fazia, e o
tamanho do replay adicionado.
