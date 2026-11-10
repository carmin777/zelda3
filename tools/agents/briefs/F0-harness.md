# Tarefa F0 — bancada de regressão headless

Você vai criar a ferramenta que garante que nenhuma mudança futura altere o
comportamento do jogo. É a base de todo o projeto: precisa ser determinística
e confiável.

## Contexto (já verificado no código)

- `src/main.c` `main()`: lê `--config <arquivo>` (só se for o 1º argumento),
  carrega assets, `ZeldaInitialize()`, cria janela SDL/áudio, e se houver mais
  um argumento chama `LoadRom(argv[0])`. Com a ROM carregada,
  `g_emu_runframe` fica setado.
- `src/zelda_rtl.c` `ZeldaRunFrame(inputs)`: se `g_emu_runframe != NULL`,
  `enhanced_features0 == 0` e `g_zenv.dialogue_flags == 0`, cada frame roda o
  C **e** o assembly original e compara a RAM
  (`EmuRunFrameWithCompare` em `src/zelda_cpu_infra.c`; em divergência ele
  imprime o diff e chama `assert(0)` ou tenta de novo — leia o código).
- Replays de referência: `saves/ref/*.sav` (lista em `kReferenceSaves`).
  `SaveLoadSlot(kSaveLoad_Replay, 256 + i)` carrega o i-ésimo e entra em
  modo replay (`state_recorder.replay_mode`); `ZeldaRunFrame` retorna `true`
  enquanto ainda está tocando o replay.
- A ROM US fica em `./zelda3.sfc` e os assets em `./zelda3_assets.dat`
  (links simbólicos neste worktree; não adicione ao git).
- O `zelda3.ini` / `zelda3.user.ini` locais ligam features e idioma `pt`,
  o que **desliga** a comparação. A bancada precisa de uma config própria.

## O que fazer

1. **Modo headless em `src/main.c`**, ativado por flags na linha de comando
   (aceite-as depois do `--config`, antes da ROM):
   - `--headless`: não cria janela, renderer nem dispositivo de áudio (não
     chame `SDL_Init` de vídeo/áudio). Não use o relógio para nada.
   - `--replay-ref <n>`: toca o replay de referência `n` (índice em
     `kReferenceSaves`, a partir de 0).
   - `--frames <N>`: para depois de N frames (ou antes, quando o replay
     terminar — `ZeldaRunFrame` passa a retornar `false`). Não continue
     rodando depois que o replay acabou.
   - `--hash-every <K>`: a cada K frames imprime uma linha
     `frame <num> <hash>`, onde hash é FNV-1a 32 bits (hex, 8 dígitos) de
     `g_zenv.ram` (0x20000 bytes) + `g_zenv.sram` (0x2000) + VRAM do PPU.
     No fim imprime sempre `final <num> <hash>`.
   - Exit code 0 se terminou normal; diferente de 0 se a comparação com a ROM
     falhar. **Garanta que uma divergência termine o processo com código ≠ 0**
     em vez de entrar no laço de "tentar de novo" (no headless, divergência =
     imprimir o diff e `exit(3)`).
   - Imprima uma linha `compare: on` ou `compare: off` no início, dizendo se a
     comparação com o assembly original está ativa.
   - O modo normal (com janela) deve continuar exatamente como está.
2. **`tests/regress.ini`**: config mínima com todas as features desligadas,
   idioma inglês, sem MSU, sem áudio — o necessário para a comparação ficar
   ativa.
3. **`tools/regress.sh`**:
   - Roda `./zelda3 --config tests/regress.ini --headless --replay-ref <n>
     --frames <N> --hash-every <K> zelda3.sfc` para cada replay de referência.
   - `N` e quais replays: por padrão todos, com `N` configurável por variável
     de ambiente (`REGRESS_FRAMES`, padrão: o replay inteiro). Meça quanto
     tempo cada replay leva e coloque o resultado no relatório.
   - Compara a saída com `tests/golden/ref<n>.txt`. Mostra diff resumido e
     sai com ≠ 0 se algo divergir ou se `compare: off`.
   - `tools/regress.sh --update` regrava os golden.
   - Rode em paralelo com no máximo 2 processos.
4. Gere os golden com o código atual (antes de qualquer mudança sua de lógica
   — você só mexeu em main.c, que não altera a simulação) e commite-os.
5. Rode `tools/regress.sh` (sem `--update`) e confirme verde.
6. Teste negativo: mude temporariamente 1 byte da simulação (por exemplo,
   some 1 em alguma variável num ponto que o replay certamente passa), rode
   `tools/regress.sh`, confirme que ele FALHA, e desfaça. Diga no relatório
   o que você mudou e a saída.

## Fora do escopo

Não mexa em nada além de `src/main.c`, `tests/`, `tools/regress.sh` e, se
indispensável, `src/zelda_cpu_infra.c` (só para o exit em divergência no
headless). Nada de eventos, mods, refactor.
