# SNI / EmuNWA

O zelda3 pode se apresentar ao **SNI** como um SNES que fala o protocolo
**EmuNWA** (Emulator Network Access). Com isso, EmoTracker, PopTracker e o
cliente do Archipelago enxergam o jogo sem nenhuma mudança do lado deles.
A RAM exposta é a mesma que o portão compara com a ROM: `g_ram` (WRAM) e a
SRAM do jogo.

O servidor fica em `src/mod/nwa_server.c`. Ele é desligado por padrão e, nesse
estado, não abre socket nenhum.

## Como ligar

No `zelda3.ini` (ou `zelda3.user.ini`):

```ini
[Network]
EmuNwa = 1
EmuNwaPort = 48879
EmuNwaAllowWrite = 0
```

- `EmuNwa` liga o servidor.
- `EmuNwaPort` é a porta base (padrão 48879, `0xbeef`). Se ela estiver ocupada,
  o servidor tenta as 9 seguintes. O SNI varre `48879..48888`; a faixa antiga
  `65400..65409` que o cliente também testa não é aberta por nós.
- `EmuNwaAllowWrite` libera `bCORE_WRITE`. Desligado, o comando responde
  `error:not_allowed`.

O `--config arquivo.ini` continua valendo; o servidor usa a seção `[Network]`
do arquivo carregado.

## Como o SNI acha o zelda3

O detector do SNI abre conexão TCP em `localhost:48879..48888` e envia
`EMULATOR_INFO`. Nossa resposta segue o que o `client.go` parseia:

```
\n
name:zelda3\n
version:1.0\n
nwa_version:1.0\n
id:zelda3-48879\n
commands:...\n
\n
```

Depois do detectado, o cliente pode pedir `GAME_INFO`, `CORE_CURRENT_INFO`,
`EMULATION_STATUS`, `CORE_MEMORIES`, `CORE_READ` e `bCORE_WRITE`.

## Memórias

| Nome | Tamanho | Acesso | Origem |
|---|---:|---|---|
| `WRAM` | 131072 | `rw` com escrita ligada, `r` sem | `g_ram` |
| `SRAM` | 8192 | idem | `g_zenv.sram` |
| `CARTROM` | tamanho da ROM carregada (0 sem ROM) | `r` | `g_snes->cart->rom` |
| `VRAM` | 65536 | `r` | `g_zenv.vram` |
| `OAM` | 544 | `r` | `g_zenv.ppu->oam` |
| `CGRAM` | 512 | `r` | `g_zenv.ppu->cgram` |

`CARTROM` é a ROM que o emulador está rodando, depois dos patches de
compatibilidade do zelda3 e da expansão para potência de dois feita por
`snes_loadRom`. Os patches mexem em código e em alguns bytes de dados; um
leitor que compare a ROM byte a byte com o arquivo original vai ver
diferenças. O cabeçalho usado para identificar o jogo não é alterado.

### Modo seed (`--emu-rom`)

Quando o jogo é uma ROM completa rodando no emulador (`RANDOMIZER.md`), as
mesmas memórias apontam para o emulador em vez do motor nativo:

| Nome | Origem |
|---|---|
| `WRAM` | `snes->ram` |
| `SRAM` | `snes->cart->ram` (o buffer gravado em `seed.srm`) |
| `CARTROM` | ROM da seed carregada no emulador |
| `VRAM`, `OAM`, `CGRAM` | PPU do emulador |

`GAME_INFO` informa o caminho da seed e `EMULATION_RESET` reseta o console
preservando a SRAM. Fora do `--emu-rom`, nada muda em relação à tabela
anterior.

`CORE_READ` aceita várias regiões numa única chamada, como o SNI usa:

```
CORE_READ WRAM;$f340;$18;$f359;$3;$f360;$8;$f36b;$10;$f377;$5
```

A resposta é um único bloco binário (`\0`, tamanho big-endian de 4 bytes,
dados), na ordem pedida. Região fora dos limites na última faixa é encurtada;
nas faixas anteriores é `invalid_argument`.

## Escrita

`bCORE_WRITE` só altera `WRAM` e `SRAM`, e só com `EmuNwaAllowWrite = 1`.
O bloco binário tem de bater exatamente com a soma dos tamanhos pedidos.
`CARTROM`, `VRAM`, `OAM` e `CGRAM` são somente leitura.

Exemplo de escrita nas rúpias (goal e actual):

```
bCORE_WRITE WRAM;$f360;$4
<binário: 09 03 09 03>
```

A escrita é aplicada entre frames, nunca no meio da simulação. Na primeira
escrita aceita o servidor imprime no `stderr`:

```
nwa: write to WRAM; compare: off
```

e a comparação lado a lado com a ROM é desligada (`g_nwa_changed_content`).
Isso é necessário porque a partir daí a RAM não corresponde mais à execução
do assembly.

### Replay e gravação

A escrita muda a RAM/SRAM viva diretamente. Ela **não** é registrada como
comando no log de replay (`StateRecorder`), então um replay salvo depois não
reproduz a escrita a partir do log. Um savestate ou `saves/sram.dat` gravado
depois da escrita contém a memória já modificada. Em outras palavras: a
escrita é uma intervenção externa, não uma entrada de jogo.

## Segurança

- O servidor escuta **somente** em `127.0.0.1`; não há opção de expor na rede.
- Não há autenticação. Qualquer processo local pode ler a RAM (e escrever, se
  `EmuNwaAllowWrite = 1`).
- A escrita fica desligada por padrão. Ligue só enquanto estiver usando uma
  ferramenta que precise entregar itens.
- Com `EmuNwa = 0` nenhum socket é criado e `NwaServer_Poll` não faz nada.

## Teste

`tools/nwa_probe.py` usa só a biblioteca padrão do Python. Ele faz o mesmo
diálogo de detecção do SNI, lê as memórias e decodifica vida, rúpias, itens,
bombas e flechas:

```sh
python3 tools/nwa_probe.py --port 48879
```

Para testar sem janela, use o modo de espera (só para teste):

```sh
./zelda3 --config nwa.ini --headless --no-mods \
  --replay-ref 1 --stop-replay-at 300 --frames 300 \
  --nwa-hold-at 300 --nwa-hold 10 --dump-state zelda3.sfc
```

`--nwa-hold-at N` pausa a simulação depois do frame N e atende o servidor por
`--nwa-hold` segundos; `--dump-state` sai no mesmo frame. Para provar escrita
com o jogo voltando a rodar, use `--frames N+5` e escreva durante a espera.

Escrita recusada:

```sh
python3 tools/nwa_probe.py --write-rupees 777 --expect-write-refused
```

Escrita aceita (com `EmuNwaAllowWrite = 1`):

```sh
python3 tools/nwa_probe.py --write-rupees 777
```

## O que não foi implementado/testado

- O SNI real, EmoTracker, PopTracker e Archipelago não foram executados; o
  teste é o `nwa_probe.py`, que reproduz o diálogo do `client.go`.
- Windows: o código tem a camada Winsock (`#ifdef _WIN32`, `ws2_32`), mas só
  foi compilado e testado no Linux.
- Sinais (`!GAME_LOADED`, etc.), savestates por rede (`LOAD_STATE`,
  `SAVE_STATE`, `bLOAD_STATE_FROM_NETWORK`) e `EMULATION_STOP/RELOAD` não
  existem; respondem `invalid_command`.
- `CORE_WRITE` em `VRAM`/`CGRAM`/`OAM` não é permitido.
- A variável de ambiente `NWA_PORT_RANGE` não é lida pelo servidor; a porta
  vem de `EmuNwaPort`.
- O cliente do SNI também varre `65400..65409`; não abrimos essa faixa.

## Divergências entre a especificação e o cliente

- A especificação escreve `nwa_verion` no exemplo do `EMULATOR_INFO`; usamos
  `nwa_version`, que é o nome citado no texto e o que o cliente efetivamente
  ignora/consome como `name` e `version`.
- O `CORE_READ` do cliente sempre manda `offset;size` em hexadecimal com `$`;
  aceitamos também decimal e a forma "só offset" da especificação.
- A especificação manda erro quando qualquer endereço de um multiread estoura;
  o cliente trata a última região como leitura parcial. Implementamos leitura
  parcial só na última região e erro nas anteriores.
- O cliente abre uma conexão por dispositivo e manda um `CORE_READ` por tipo
  de memória, lendo todas as respostas em ordem. O servidor mantém a ordem por
  conexão e responde em streaming.
