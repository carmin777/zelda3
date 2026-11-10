# Tarefa R1-ponte-sni — o zelda3 fala o protocolo EmuNWA

Leia `AGENTS.md` e `docs/engine/RANDOMIZER-COMPAT.md` (seções 2 e 4).

## Por quê

Trackers da comunidade (EmoTracker, PopTracker) e o cliente do Archipelago
falam com "consoles" através do **SNI**. O SNI detecta emuladores que
implementam o protocolo **EmuNWA** (Emulator Network Access) em
`localhost:48879..48888`. Se o zelda3 falar EmuNWA, o SNI o enxerga como um
SNES, sem mudança nenhuma do lado da comunidade. A RAM do zelda3 é a
mesma do SNES (o portão prova isso), então trackers de itens já funcionam
no jogo normal.

## Referências (fora do git, só leitura)

- Especificação: `.agents/refs/emunwa/emunwa-spec.md`
  e `.../emunwa/SNES.md` (nomes das memórias: `WRAM`, `SRAM`, `CARTROM`
  obrigatórias; `VRAM`, `OAM`, `CGRAM` opcionais).
- Cliente real do SNI (Go, MIT): `.agents/refs/sni/client.go`
  e `driver.go`. **O formato que importa é o que esse código envia e
  parseia** (detecção com `EMULATOR_INFO`, `GAME_INFO`,
  `EMULATION_STATUS`, `CORE_READ` com várias regiões, `bCORE_WRITE`).
  Se a especificação e o cliente divergirem, siga o cliente e diga onde.
- Não baixe nem rode binários (SNI, trackers). Teste com um cliente seu.

## O que fazer

1. Servidor EmuNWA em `src/mod/` (ex.: `nwa_server.{c,h}`), **desligado
   por padrão**: seção nova no `zelda3.ini` (ex.: `[Network]`
   `EmuNwa = 0`, `EmuNwaPort = 48879`, `EmuNwaAllowWrite = 0`). Escuta
   **só em 127.0.0.1**. Desligado = nenhum socket aberto, nenhum custo.
2. Sockets não bloqueantes, atendidos **uma vez por frame**, entre frames
   (nunca no meio da simulação), no laço com janela e no headless.
   Código de socket isolado para compilar em Linux e com `#ifdef` para
   Windows (Winsock); diga o que você testou de verdade.
3. Comandos: no mínimo os que o `client.go`/`driver.go` usam, mais
   `MY_NAME_IS` e `CORE_MEMORIES`. Memórias: `WRAM` (`g_ram`, 128 KB),
   `SRAM` (a SRAM do jogo), `CARTROM` (a ROM carregada; se não houver ROM,
   tamanho 0 ou erro conforme a especificação), e `VRAM`/`CGRAM`/`OAM` se
   for simples.
4. **Escrita** (`bCORE_WRITE`) só com `EmuNwaAllowWrite = 1`; senão, erro
   conforme a especificação. Escrita aplicada entre frames; ao escrever em
   `WRAM`/`SRAM`, a comparação lado a lado tem de desligar (como os mods
   que mudam conteúdo). Diga como a escrita interage com replay/gravação.
5. `tools/nwa_probe.py` (Python sem dependências novas): conecta, faz o
   mesmo diálogo de detecção do SNI, lê regiões e imprime. É o teste.
6. `docs/engine/MODS.md` (ou um `docs/engine/SNI.md`): como ligar, como o
   SNI acha o zelda3, segurança (só localhost, escrita desligada por
   padrão).

## Verificação a relatar

1. `make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com
   todas as linhas `ok` e a linha final (com o servidor desligado nada
   muda).
2. Headless com o servidor ligado num ponto conhecido de um replay (ex.:
   `--replay-ref 1 --stop-replay-at N` e um modo de ficar parado
   esperando o cliente, que você cria só para teste, se precisar): o
   `nwa_probe.py` lê a vida, rúpias e itens do Link na `WRAM` e os valores
   batem com `--dump-state` no mesmo frame. Cole as saídas.
3. Leitura com várias regiões num só `CORE_READ`, como o SNI faz.
4. Escrita: recusada com a opção desligada; com ela ligada, muda as
   rúpias e o jogo mostra o valor novo (cole a prova) e o `compare` fica
   off.
5. Diga explicitamente o que não testou (por exemplo, o SNI real, Windows).
