# Jogar uma seed do alttpr.com no zelda3

Guia do modo `--emu-rom`: a ROM completa da seed roda no emulador SNES
embutido, com a janela, o áudio, o controle e a SRAM do zelda3. O motor C do
zelda3 não é usado nesse modo; quem executa o jogo é a própria ROM gerada pelo
alttpr.com. A investigação que provou que a ROM roda está em
`RANDOMIZER-R2.md`.

## 1. Montar a ROM da seed

São necessários a ROM japonesa v1.0 (CRC32 `3322EFFC`), o BPS do patch-base
informado pela seed e o JSON da seed. Nada disso entra no git; os arquivos
ficam em `.agents/alttpr/download/`.

```sh
python3 tools/alttpr_seed.py build \
  --jp-rom japanese.sfc \
  --base .agents/alttpr/download/base-79bbb4ad512838ab01fc06b7971f656c.bps \
  --seed .agents/alttpr/download/seed-xvJo08oxyJ.json \
  -o .agents/alttpr/download/seed-xvJo08oxyJ.sfc
```

O comando confere a ROM JP, o BPS e o CRC do próprio patch, e imprime o MD5 e
o CRC32 da ROM final. Para ver o hash da seed, os ícones e as tabelas lidas da
ROM:

```sh
python3 tools/alttpr_seed.py info seed.sfc --json seed.json
```

## 2. Jogar

```sh
./zelda3 --emu-rom /caminho/seed.sfc
```

Sem `--headless`, abre a janela normal do zelda3. O que vale nesse modo:

- **Vídeo**: mesmo renderer do jogo (SDL, SDL-Software, OpenGL ou OpenGL ES),
  com a escala, a tela cheia, o filtro linear e o vsync do `zelda3.ini`. A
  imagem é 256x224 (4:3); widescreen e `extend_y` ficam desligados.
- **Áudio**: o DSP do SNES emulado é enviado para a mesma saída SDL do jogo
  (`EnableAudio`, `AudioFreq`, `AudioChannels`, `AudioSamples`).
- **Controle**: o mesmo `[KeyMap]`/`[GamepadMap]` do `zelda3.ini`. As teclas
  de controle (setas, Start, Select, A, B, X, Y, L, R) e o gamepad funcionam
  como no jogo normal.
- **Comandos úteis**: Alt+Enter (tela cheia), P/Shift+P (pausa), Ctrl+Up/Down
  (tamanho da janela), Shift+=/Shift+- (volume), Ctrl+R (reset com a SRAM
  preservada). Savestates, replays e cheats do motor nativo não são aplicados.
- **SRAM**: fica em `seed.srm`, ao lado da ROM da seed (mesmo nome-base). É
  carregada no início e gravada quando muda e ao sair. O jogo normal continua
  usando apenas `saves/sram.dat`; um modo nunca toca a SRAM do outro. Para
  esta seed o arquivo tem 32768 bytes, o tamanho anunciado no header.
- **Opções nativas desligadas**: features, mods, MSU, widescreen e assets do
  motor C não são carregados. O programa imprime uma linha no log:

  ```
  emu: native features, mods, MSU and widescreen disabled
  ```

Para começar do zero, apague o `seed.srm` (ou use outra cópia da ROM).

## 3. SNI / EmuNWA no modo seed

Com `[Network] EmuNwa = 1`, o servidor EmuNWA do R1 passa a expor as memórias
do **emulador**, não as do motor nativo:

| Memória | Origem no modo seed |
|---|---|
| `WRAM` | `snes->ram` (128 KB) |
| `SRAM` | `snes->cart->ram` (o mesmo buffer de `seed.srm`) |
| `CARTROM` | ROM carregada no emulador (2 MB nesta seed) |
| `VRAM`, `OAM`, `CGRAM` | PPU do emulador |

`GAME_INFO` mostra o nome e o caminho da seed. `EMULATION_RESET` faz um reset
sem apagar a SRAM. O protocolo e os comandos são os mesmos de `SNI.md`.

```sh
./zelda3 --config nwa.ini --emu-rom seed.sfc
python3 tools/nwa_probe.py --port 48879
```

## 4. Diagnóstico headless

Os comandos de teste exigem `--headless`:

```sh
./zelda3 --headless --emu-rom seed.sfc \
  --frames 3800 \
  --input-script roteiro.txt \
  --screenshot teste.ppm
```

- `--input-script` usa o mesmo formato do resto do projeto (frame e botões).
- `--screenshot` grava um PPM do último frame.
- `--audio-dump arquivo.wav` grava a saída do DSP (PCM 16-bit estéreo,
  32040 Hz) para conferir se há som.
- `--nwa-hold-at N --nwa-hold S` pausa a simulação depois do frame N e atende
  o servidor por S segundos, como no modo nativo.

Exemplo de decodificação de rúpias/vida no modo seed:

```sh
./zelda3 --config nwa.ini --headless --emu-rom seed.sfc \
  --frames 3000 --input-script load.txt \
  --nwa-hold-at 3000 --nwa-hold 10 --screenshot tela.ppm
python3 tools/nwa_probe.py
```

## 5. O que funciona e o que não

Funciona:

- a ROM inteira da seed (FastROM, 2 MB) roda no `snes/` sem `PatchRom`;
- título, seleção de arquivo com os cinco ícones do hash, criação/carregamento
  de arquivo e jogo normal;
- vídeo, áudio, teclado/gamepad e SRAM persistente no modo com janela;
- SNI/EmuNWA lendo WRAM, SRAM, ROM, VRAM, OAM e CGRAM do emulador;
- os asserts estritos do PPU continuam valendo no jogo nativo e no oráculo;
  só o `--emu-rom` liga a flag que os relaxa.

Não funciona / não foi testado:

- SNI real, EmoTracker, PopTracker e cliente do Archipelago não foram
  executados; o teste é o `tools/nwa_probe.py`;
- savestates, replays, cheats, mods, features, MSU e widescreen no modo seed;
- turbo/fast-forward no modo com janela (a tecla é ignorada);
- precisão de emulação frame a frame contra hardware: o runner foi validado
  visualmente e por hashes, não é um portão de regressão do SNES;
- o áudio foi conferido por WAV (RMS != 0), não por escuta em um dispositivo
  físico nesta máquina.

## 6. Verificação feita no R3a

- `make -j2` e `make -j2 NO_LUA=1`: compilam com `-Werror`, sem warnings.
- `tools/regress.sh`: `regress: all 19 replays and 7 feature replays passed`.
- SRAM: uma cópia limpa da seed criou `seed.srm` (32768 bytes) no primeiro
  run; o segundo run carregou o arquivo e a seleção de arquivo mostrou o save
  (`.agents/shots/r3a-sram-*`).
- Áudio: `--audio-dump` gerou 25 s de WAV estéreo a 32040 Hz na tela de
  título; RMS 1792,4 e pico 16348 (há som).
- SNI: `tools/nwa_probe.py` leu `WRAM` do emulador com vida 24/24 (3
  corações) e rúpias 0, batendo com o HUD do screenshot no mesmo frame.
- Janela: testada de verdade sob Xvfb (SDL-Software, áudio dummy) com
  `xdotool` enviando Start/Start/A; a seed carregou o save e apareceu em uma
  janela 512x448. Não foi testada em um monitor físico nem com o dispositivo
  de áudio real desta máquina.
