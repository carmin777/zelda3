# Co-op pela rede (ALttPO clássico)

Implementação das etapas **C1 e C2** do `COOP-DESENHO.md`: cada jogador roda
o próprio jogo, o progresso declarado da SRAM é compartilhado e os outros
jogadores aparecem como fantasmas. O protocolo no fio é o **ALttPO clássico**
(`0x02` + serialização `0x13`), descrito em `COOP-ALTTPO.md`. A C1 usa a rede;
a C2 usa o mesmo código com transporte em memória e duas instâncias na mesma
janela (seção "Local (tela dividida)").

Com `[Coop] Enabled = 0` (o padrão) o módulo não abre socket, não lê nem
escreve memória do jogo e não desenha nada. O regress e o oráculo continuam
iguais.

## Como ligar

No `zelda3.ini`:

```ini
[Coop]
Enabled = 1
Server = 127.0.0.1
Port = 4590
Group = minha-sala
PlayerName = Link
PlayerColor = 0x085C
ShowNames = 1
RaceMode = 0
```

| Chave | Padrão | Significado |
|---|---|---|
| `Enabled` | `0` | Liga o cliente. Desligado é no-op total. |
| `Server` | `alttp.online` | Endereço do servidor/relay. Use `127.0.0.1` nos testes. |
| `Port` | `4590` | Porta UDP do ALttPO clássico. |
| `Group` | `zelda3` | Nome do grupo; até 20 caracteres, sem diferenciar maiúsculas. |
| `PlayerName` | `Link` | Nome mostrado sobre o fantasma; até 20 caracteres. |
| `PlayerColor` | `0x085C` | Cor do rótulo em BGR555 (`0xBBBBBGGGGGRRRRR`). |
| `ShowNames` | `1` | Desenha o nome sobre o fantasma. |
| `RaceMode` | `0` | Modo corrida: só posição/nome/sprites, sem progresso. |

Também é possível usar um arquivo separado com `--config arquivo.ini`.
O grupo e o nome são preenchidos com espaços até 20 bytes, como o cliente
clássico faz.

> **Nunca aponte os testes para `alttp.online`.** O relay local
> (`tools/coop_relay.py`) escuta só em `127.0.0.1` por padrão.

## O que é compartilhado

A fonte é a cópia de trabalho da SRAM (`g_ram + 0xF000`, equivalente a
`$7EF000`), não o arquivo salvo. Salvar o jogo continua sendo
responsabilidade do jogador.

Enviado a cada 16 frames (`frame & 15 == 0`) e mesclado no outro cliente:

| Offset | Conteúdo | Regra |
|---:|---|---|
| `0x340..0x35F` | itens (arco, boomerang, hookshot, varas, medalhões, martelo, flauta, redes, espelho, luvas, botas, flippers, pérola, espada, escudo, armadura, bottles) | maior valor; bottles só `0 -> não-zero`; espada ignora `0xFF`; `0x343` (bombas) e `0x34F` (bottle selecionada) ficam de fora |
| `0x354`, `0x35A`, `0x35B` | luvas, escudo, armadura | maior valor + atualização de paleta/gráficos nativos |
| `0x364..0x369` | bússolas, big keys, mapas | OR de bits |
| `0x36B/0x36C` | pedaços de coração e capacidade | combinados e pego o maior |
| `0x370/0x371` | capacidade de bombas/flechas | maior valor |
| `0x374/0x375` | pingentes | OR de bits |
| `0x379/0x37A` | flags de habilidade | OR de bits |
| `0x37A/0x37B` | cristais | OR de bits |
| `0x37B` | consumo de magia | maior valor |
| `0x3C5` | estado do mundo/chuva | maior valor; ao sair da chuva remove o overlay e ajusta o ambiente |
| `0x3C6`, `0x3C9` | flags de progresso 1/2 | OR + limpeza de followers obsoletos |
| `0x3C7` | ícones do mapa | maior valor |
| `0x000..0x24F` | salas de dungeon (2 bytes cada) | OR com as máscaras do estudo (`0x035`, `0x10B`, `0x028`) |
| `0x280..0x33F` | eventos/overlays do overworld | OR; máscara `0xDF` nas áreas `0x3B/0x7B`; caso do buraco da pirâmide (`0x5B`) |

Enviado a cada 32 frames, alternando: salas de dungeon em `frame & 31 == 0` e
overworld em `frame & 31 == 16`. O pacote de itens leva as duas faixas
`0x340..0x37C` e `0x3C5..0x3CA`, como o cliente clássico.

Não são sincronizados: inimigos/drops, vida e magia atuais, flechas, bombas e
rúpias atuais, conteúdo de bottles existentes, chaves atuais, potes/star
tiles, morte, follower, ponto inicial, diálogos e cutscenes.

Quando uma mudança vinda da rede é escrita, `g_coop_changed_content` é ligado
e a comparação lado a lado contra a ROM de referência é desligada, do mesmo
jeito que um mod que altera conteúdo.

## Fantasmas

O cliente envia até 12 entradas de OAM do Link local, lidas de
`ppu->oam` a partir de `sort_sprites_offset_into_oam_buffer`:

- OAM bruto (os 4 bytes + o quinto byte estendido), com flip, paleta,
  prioridade e tamanho;
- os tiles de CHR usados (16 palavras por tile 8x8; quatro tiles no caso
  16x16), quando o CHR é menor que `0x80`;
- as 16 cores de CGRAM da paleta usada.

O adaptador não envia sprites de efeito (folhas, cortes de grama, boomerang,
projéteis etc.). Um cliente clássico pode enviá-los; o zelda3 recebe e
desenha o que couber no formato, mas o primeiro escopo é o Link.

O fantasma só é desenhado quando o remoto está na mesma localização de 24
bits (sala/área + dungeon + Light/Dark World; em dungeon o bit de mundo é
ignorado, como no ALttPO), durante transições aceita a localização antiga e a
nova, e o pacote tem TTL de 255 frames. O desenho é feito **depois do PPU**,
direto no buffer de vídeo, sem tocar em OAM, VRAM, CGRAM ou na simulação. O
nome usa a fonte do `FrameDraw` (`ModDraw_AddText`).

## Modo corrida

`RaceMode = 1` desliga o envio e a aplicação de progresso. Posição, nome,
sons (ainda não enviados) e sprites continuam, então o jogador vê os outros e
é visto, mas itens não passam. É o mesmo conceito do preset "Multiworld" do
cliente clássico.

## Protocolo

Envelope cliente → servidor:

```
u16 0x651F | u8 0x02 | grupo[20] | u8 tipo | u16 índice | [u32 setor] | carga
```

Tipos usados: `0x00` pede índice, `0x01` broadcast geral, `0x02` broadcast
ao setor (usado para sprites; o setor é a sala/área de 16 bits). O relay
responde `0x80` com o índice; broadcasts chegam como `0x81` e broadcasts de
setor como `0x81` ou `0x82` (os dois são aceitos).

Carga (serialização `0x13`):

```
u8 0x13 | u8 team | u8 frame | TLVs...
```

TLVs enviados: `0x01` localização (26 bytes), `0x02` sons (2 bytes, hoje
sempre zero), `0x03/0x04` sprites, `0x06` SRAM, `0x0C` nome (20 bytes). O
receptor também sabe pular `0x05`, `0x07`, `0x08`, `0x09`, `0x0A`, `0x0B`,
`0x0D`, `0x0E`, `0x0F` e `0x10` para não perder o alinhamento ao jogar com
um cliente clássico.

Pacotes são limitados a 1452 bytes; sprites grandes são divididos em
`0x03` + continuações `0x04`.

## Servidor local de teste

`tools/coop_relay.py` é um relay mínimo em Python puro (sem dependências).
Ele mantém grupos, índice por cliente, setor e timeout de 15 s, e só repassa
pacotes. Não guarda progresso.

```sh
python3 tools/coop_relay.py --port 4590 --verbose --dump /tmp/coop.hex
```

`--dump` grava uma linha por pacote recebido do cliente, em hexadecimal, para
conferir o formato.

### Dois zelda3 headless

Crie dois `.ini` iguais aos de `tests/regress.ini`, mudando só `[Coop]`
(nome/cor) e aponte `Server = 127.0.0.1`. Exemplo de teste de progresso com
um replay real que pega espada/escudo:

```sh
python3 tools/coop_relay.py --port 45901 &

./zelda3 --config coopA.ini --headless --replay-ref 0 \
  --frames 5600 --dump-state zelda3.sfc > A.out 2> A.err &

./zelda3 --config coopB.ini --headless --replay-ref 0 \
  --frames 5600 --stop-replay-at 3000 --dump-state zelda3.sfc > B.out 2> B.err &

wait
grep -E 'link_sword_type|link_shield_type' A.out B.out
```

O jogador A pega espada/escudo por volta do frame 5450; B para o replay no
frame 3000 (ainda sem espada) e recebe o progresso. `B.err` mostra
`coop: received progress; compare: off`.

Para a screenshot do fantasma, use um save em que os dois ficam na mesma
sala, um roteiro de input para um andar e `--screenshot` no outro:

```
# roteiro: segura Direita desde o primeiro frame
1 Right
520 -
```

```sh
./zelda3 --config coopA.ini --headless --load estado.sav \
  --frames 520 --input-script direita.txt zelda3.sfc &
./zelda3 --config coopB.ini --headless --load estado.sav \
  --frames 520 --screenshot fantasma.ppm zelda3.sfc &
```

O `--screenshot` grava PPM (P6); converta para PNG se quiser. O fantasma do
outro aparece por cima, com o nome na cor configurada.

### Modo corrida

Com `RaceMode = 1` no cliente A e `0` no B, um item ganho por A (por replay,
roteiro ou escrita de teste via EmuNWA em `WRAM:0xF350`) **não** aparece no
dump de B, mas o fantasma de A continua na screenshot de B.

## Local (tela dividida)

A etapa **C2** roda dois jogos completos no mesmo processo, lado a lado (ou um
acima do outro). Cada instância tem RAM, SRAM, PPU/VRAM, DMA e SPC/áudio
próprios; o progresso e os fantasmas passam pelo mesmo código desta C1, com um
**transporte em memória** no lugar do socket. O pacote continua sendo montado e
lido com a serialização `0x13`.

Com `Local = 0` (o padrão) o modo local é no-op: uma única instância, nenhuma
cópia extra e nenhum contexto de co-op a mais. A conferência frame a frame do
`tools/regress.sh` continua valendo.

### Como ligar

```ini
[Coop]
Local = 1
LocalLayout = Horizontal
PlayerName = Link
PlayerName2 = Link2
PlayerColor = 0x085C
PlayerColor2 = 0x0B7C
ShowNames = 1
RaceMode = 0

[KeyMap2]
Controls = i, m, j, n, u, e, h, y, b, g, 7, 8

[GamepadMap2]
Controls = DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, B, A, Y, X, Lb, Rb
```

| Chave | Padrão | Significado |
|---|---|---|
| `Local` | `0` | Liga a tela dividida. Tem precedência sobre `Enabled`; nenhum socket é aberto. |
| `LocalLayout` | `Horizontal` | `Horizontal` (lado a lado) ou `Vertical` (empilhado). |
| `PlayerName2` / `PlayerColor2` | `Link2` / `0x0B7C` | Nome e cor do jogador 2. `PlayerName`/`PlayerColor` são do jogador 1. |
| `[KeyMap2] Controls` | `i,m,j,n,u,e,h,y,b,g,7,8` | Teclas do jogador 2, na mesma ordem de `[KeyMap]`. |
| `[GamepadMap2] Controls` | igual a `[GamepadMap]` | Mapeamento do segundo controle. |

O primeiro controle aberto é o jogador 1; o segundo é o jogador 2. Teclas de
`[KeyMap2]` só valem com `Local = 1` e têm precedência sobre `[KeyMap]` quando
as duas usam a mesma tecla. Comandos de save/load, cheats e janela continuam
exclusivos do jogador 1.

### O que é trocado por frame

Sem `Local`, nada muda. Com `Local`, `LocalCoop_Select(i)` troca:

- **RAM (128 KB)**: copiada entre o buffer da instância e `g_ram` (a simulação
  inteira usa `g_ram` por macro, então o buffer vivo é um só);
- **SRAM, PPU/VRAM, DMA e SpcPlayer/DSP**: ponteiros para objetos alocados por
  instância, sem cópia;
- **gravador de replay/key log**: um `StateRecorder` por jogador;
- **contexto de co-op**: um conjunto de jogadores remotos, fila de pacotes e
  flags por jogador;
- **oráculo da ROM**: só o jogador 2 usa o emulador lado a lado. A instância 1
  roda só a implementação C; o estado do oráculo fica parado enquanto ela roda,
  então continua sincronizado com o jogador 2.

Custo medido do swap: **quatro `memcpy` de 128 KB por frame de vídeo** (salvar
e carregar em cada uma das duas trocas), 512 KB no total, **~13,5 µs por frame**
nesta máquina (memória a ~19 GB/s). Os ponteiros e o contexto custam menos de
1 µs. O que domina o modo local é rodar e desenhar dois jogos, não a troca.

Áudio: só o jogador 1 toca. O callback do mixer fica preso ao SpcPlayer da
instância 0, e as escritas de APU do jogador 2 são descartadas para não
contaminar a fila do jogador 1.

Mods/Lua: o estado dos mods e a VM Lua continuam globais e compartilhados; não
há duas VMs Lua. Mods que dependem do estado da instância vão ver a instância
que estiver ativa naquele frame.

Renderização: cada pane usa o renderer normal em escala 1; `EnhancedMode7`
(4x4) fica desligado no modo local porque as duas metades precisam da mesma
escala. A escala da janela (`WindowScale`) vale para o conjunto; a janela fica
com o dobro da largura ou da altura.

### Testes headless

Progresso: o jogador 1 replaya `ref0` (pega espada/escudo por volta do frame
5450) e o jogador 2 recebe o mesmo ponto de partida sem replay, com um roteiro
de entrada que o leva até o jogo. O utilitário abaixo gera o roteiro a partir
do próprio replay; qualquer roteiro que ponha o jogador 2 em jogo serve.

```sh
# P1 replaya; P2 começa no mesmo snapshot e segue o roteiro até entrar em jogo.
./zelda3 --config local.ini --headless --replay-ref 0 \
  --input-script2 p2.txt --frames 5600 --dump-state zelda3.sfc
```

O `--dump-state` em modo local imprime as duas instâncias com prefixo
`state p1 ...` / `state p2 ...`. Resultado esperado depois do frame 5450:
`link_sword_type = 1` e `link_shield_type = 1` nos dois, e no stderr
`coop: received progress; compare: off`.

Conformidade da instância 2: `--local-player 1` carrega o replay na instância
2 e deixa o oráculo com ela. Os hashes de `compare: on`, `frame` e `final`
saem idênticos ao `tests/golden/ref0.txt` do modo de um jogador:

```sh
./zelda3 --config local.ini --headless --replay-ref 0 --local-player 1 \
  --frames 42998 --hash-every 1000 zelda3.sfc
```

Screenshot da tela dividida com fantasma (sem display, `--screenshot` compõe
os dois panes e o overlay):

```sh
# os dois carregam o mesmo save; P1 anda para a direita, P2 fica parado
./zelda3 --config local.ini --headless \
  --load "saves/ref/Chapter 1 - Zelda's Rescue.sav" \
  --input-script direita.txt --frames 120 \
  --screenshot local_coop.ppm zelda3.sfc
```

`direita.txt`:

```
1 Right
120 -
```

O PPM é P6; converta para PNG se quiser. A janela também foi testada em Xvfb,
nos layouts horizontal (1024x448 com escala 2) e vertical (512x896), com as
duas instâncias avançando juntas.

### Limitações do modo local

- Áudio do jogador 2 não é misturado (decisão desta etapa; o briefing permitia
  só o jogador 1).
- Mods/Lua não têm estado por instância; são globais.
- `--emu-rom`, `--oracle-rom`, NWA e MSU não são suportados junto com
  `Local = 1` (o programa recusa `--emu-rom`/`--oracle-rom`; NWA/MSU não foram
  testados e não têm instância própria).
- Save/load e autosave usam a instância ativa; no fim do frame a instância 0
  (jogador 1) fica selecionada.
- `--load` carrega o mesmo arquivo nas duas instâncias. `--replay`/`--replay-ref`
  replaya na instância escolhida por `--local-player` e carrega só o snapshot
  base na outra.
- O modo local não passa pelo relay e não é compatível com o servidor ALttPO;
  a compatibilidade de rede continua sendo a C1.

## O que ainda não existe

- sons remotos (`0x02` é enviado como zero e ignorado ao receber);
- ancillas/projéteis remotos (`0x09`), tilemap (`0x07`), objetos (`0x08`) e
  tochas (`0x0A`) são lidos e descartados;
- PvP (`0x0B`) é lido e descartado;
- sincronização de inventário do SMZ3 (`0x0D..0x10`);
- tabela de offsets para ROM JP/EUR e randomizers (o zelda3 é US vanilla);
- lista de jogadores/mapa, notificações de item e chat (não há chat no
  protocolo clássico);
- o2/0x03+0x14.

## Não testado / limitações

- Interoperabilidade real com **bsnes + ALttPO**: não há cliente clássico
  rodando neste ambiente. O que foi validado é o formato byte a byte e dois
  zelda3 falando pelo relay local.
- NAT, latência, perda de pacote e clocks diferentes. O servidor é relay
  (não precisa de hole punching), pacotes de progresso se repetem, mas
  sprites são transitórios.
- O relay local não tenta ser idêntico ao servidor oficial em métricas,
  limite de clientes ou casos de erro.
- A camada de fantasma desenha por cima do PPU; não reproduz a prioridade
  fina da camada extra do bsnes-as. Sprites remotos podem cobrir sprites
  locais quando se sobrepõem.
- O modo corrida não tem cronômetro nem “trava” o save; é só a decisão local
  de não compartilhar progresso.
