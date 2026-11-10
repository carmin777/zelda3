# ALttPO — estudo do protocolo para o co-op

Estudo de 09/11/26 para a etapa **C0** do `COOP-DESENHO.md`. É só leitura e
documento: nenhum código do jogo foi alterado.

Fontes principais (fora do git, em `.agents/refs/alttpo/`):

- cliente em AngelScript: `GameState.as`, `LocalGameState.as`,
  `SyncableItem.as`, `SyncableByte.as`, `ROMMapping.as`, `pre_frame.as`,
  `post_frame.as`, `pre_nmi.as`, `receive.as`, `Projectile.as`,
  `GameAncilla.as`, `TilemapChanges.as`, `TilemapRun.as`, `torches.as`,
  `SettingsWindow.as`, `Sprite.as`, `GameSprite.as`, `LocalFrameState.as`;
- servidor em Go: `alttp-server/main.go`, `protocol01.go`, `protocol02.go`;
- `README.md`, `faq.md` e `o2-README.md` (este último só tem uma linha).

O `o2-README.md` local é um stub. Para responder sobre o ALttPO 2.0 consultei
o upstream `github.com/alttpo/o2` (clone raso no commit
`165a553d`, de 27/09/2026, tag `v0.4.5`) e `github.com/alttpo/o2-server`
(commit `a3e0b24e`, de 24/11/2022). Onde a conclusão vem desses repositórios
e não do material local, está marcado.

**Licença:** nem o cliente/servidor clássico nem o o2 declaram licença nos
arquivos estudados. Este documento descreve o funcionamento com palavras
próprias; nada de código foi copiado. A recomendação da seção 7 leva o risco
de licença em conta.

## Resumo para decisão

1. O ALttPO clássico é **UDP puro, porta 4590**, com um servidor que só faz
   **relay** (não guarda progresso, itens nem sprites). Grupos são salas
   criadas sob demanda, sem senha; o nome do grupo é o único "segredo".
2. O estado do jogo viaja **entre os clientes**: cada cliente manda seu
   progresso, posição e sprites; o servidor apenas repassa. O protocolo
   externo é o `0x02`; dentro dele vai uma serialização própria, a `0x13`.
3. O que é sincronizado é uma **lista declarada de offsets da SRAM**
   (`$7EF000+`) com regras de mescla: maior valor, OR de bits ou uma função
   de mutação. Posição, OAM, CHR, paleta, sons, tilemap, ancillas e PvP vão
   por mensagens separadas.
4. Os "fantasmas" são o **OAM do Link remoto** (até 12 sprites) mais os
   tiles de CHR e a paleta usados por ele, desenhados por cima; não são
   sprites da simulação. O bsnes-as tem uma camada extra de PPU para isso.
   Além de aparecer, o cliente remoto pode tocar sons, instanciar ancillas,
   aplicar mudanças de tilemap e, se ligado, participar de PvP.
5. O **modo corrida** não é um modo do servidor: é o preset "Multiworld"
   do cliente, que desliga item/progresso e deixa só os sprites. O README
   chama isso de Race Mode; no código atual é uma combinação de checkboxes.
6. O **o2** é um aplicativo Go separado, com interface web, que fala com
   SNES real/emulador por SNI, qusb2snes, RetroArch ou FX Pak Pro. Ele usa
   o protocolo **0x03** (protobuf sobre o mesmo UDP 4590), uma serialização
   **0x14** e injeta código 65816 na SRAM/ROM para aplicar mudanças no
   console. É a direção atual do projeto, mas **não desenha fantasmas** no
   código estudado e não implementa tilemap, ancillas, objetos ou PvP.
7. **Recomendação:** para a C1, começar com **protocolo próprio, versionado
   e documentado, inspirado no ALttPO** (mesma ideia de envelope, lista de
   progresso e fantasmas), não com compatibilidade total. Se o objetivo for
   jogar com a comunidade que já usa bsnes+ALttPO, o único caminho com
   fantasmas hoje é o **ALttPO clássico (0x02 + 0x13)**, como adaptador
   posterior; o o2 não entrega fantasmas e é um alvo móvel. Ver seção 7.6.

---

## 1. Transporte e servidor

### 1.1 Rede

- **UDP**, não TCP. O cliente abre um socket UDP e faz `connect` no
  endereço do servidor; o servidor responde para o endereço de origem.
- Porta padrão **4590** nos dois lados (`alttp.online` é o servidor público
  padrão do cliente clássico).
- Cabeçalho comum: dois bytes little-endian `0x651F` (25887, "ALTTP" em
  T9), seguidos do byte de protocolo do servidor.
- Protocolos de servidor aceitos: `0x01` (legado), `0x02` (clássico) e
  `0x03` (o2). O `alttp-server` copiado em `.agents/refs/` só implementa
  `0x01`/`0x02`; o `o2-server`, que é o servidor atual do `alttp.online`,
  aceita os três na mesma porta.
- Não há TCP, TLS, autenticação, senha, chat ou persistência. O grupo é
  identificado por um nome; quem souber o nome entra.
- O cliente clássico limita pacotes a **1452 bytes** e divide listas
  grandes (sprites, tilemap) em vários pacotes.

### 1.2 Grupos, índices e setores

- O grupo do protocolo `0x02` é um campo fixo de **20 bytes**, preenchido
  com espaços. O servidor calcula uma chave: tira espaços e uma lista de
  pontuação nas pontas, converte para minúsculas e usa essa chave para
  achar/criar o grupo. Ou seja, o nome não diferencia maiúsculas e o
  cliente deve usar algo simples (o README fala em até 20 caracteres).
- Não há lista de lobbies: o grupo é criado no primeiro pacote que chega.
- O servidor atribui um **índice de cliente** (`uint16`) por grupo. O
  cliente começa com índice `-1` e pede o índice; o servidor responde com
  o índice e o cliente passa a usá-lo. O índice é a posição do cliente no
  array do servidor e é reutilizado quando alguém sai.
- **Setor**: cada cliente tem um `Sector` (`uint32`) no servidor. O cliente
  manda o setor nos pacotes de "broadcast para setor"; o servidor guarda o
  último setor visto e repassa só para clientes do mesmo setor. O setor é
  a localização agregada do jogador (sala/área), não um lobby separado.
  **Detalhe do código clássico:** o cliente declara o setor como `uint16`
  antes de escrever os 4 bytes, então, na prática, só os 16 bits baixos
  (sala/área) chegam ao servidor; os bits de dungeon/Light-Dark se perdem.
  O comentário no código sugere limpar o bit 17 em dungeon, mas o tipo
  torna isso inócuo. A checagem fina de "mesma sala" é feita no cliente,
  não no servidor.
- O servidor não tem estado de jogo: só a tabela de grupos/clientes, o
  índice, o setor, `IsAlive` e `LastSeen`. Cliente inativo por **15 s** é
  removido; grupo sem clientes é apagado. Há métricas opcionais para
  InfluxDB, mas isso é observabilidade, não estado do jogo.

### 1.3 Protocolo `0x02` (clássico)

Envelope cliente → servidor:

| Campo | Tamanho | Observação |
|---|---:|---|
| header | 2 | `0x651F` little-endian |
| protocolo | 1 | `0x02` |
| grupo | 20 | fixo, com espaços |
| tipo | 1 | `0x00` pede índice; `0x01` broadcast a todos; `0x02` broadcast ao setor |
| índice | 2 | o que o cliente pensa que é; `0`/`-1` antes de saber |
| setor | 4 | só no tipo `0x02` (cliente → servidor) |
| carga | variável | serialização `0x13` (seção 2) |

O servidor responde com o bit alto ligado no tipo:

| Tipo de resposta | Significado |
|---|---|
| `0x80` | resposta ao pedido de índice: vem o índice atribuído |
| `0x81` | broadcast de outro cliente (inclusive o que era "para setor") |
| `0x82` | broadcast para setor; o cliente clássico aceita, mas o servidor atual reenvia setor como `0x81` |

O protocolo `0x01` é histórico: grupo e nome com comprimento de 1 byte,
índice, tipo de cliente e carga; o servidor repassa para os outros, mas não
responde ao pedido de índice. Não é necessário para o co-op novo.

### 1.4 Protocolo `0x03` (o2)

Mesmo header `0x651F` e mesma porta, mas depois do byte `0x03` vem uma
mensagem **protobuf** `GroupMessage`:

| Campo | Tipo | Papel |
|---|---|---|
| `group` | string | grupo/lobby |
| `playerTime` | int64 | relógio do cliente (ns) |
| `serverTime` | int64 | preenchido pelo servidor (ns) |
| `playerIndex` | uint32 | preenchido pelo servidor; índice do remetente |
| `playerInSector` | uint64 | setor do jogador (a localização) |
| `oneof` | — | `joinGroup`, `broadcastAll`, `broadcastSector`, `echo` |

- `joinGroup` responde com o índice do cliente.
- `broadcastAll` repassa a todos os outros clientes do grupo.
- `broadcastSector` repassa só a quem está no `targetSector`.
- `echo` devolve a mensagem ao remetente; o o2 usa isso para medir RTT e
  estimar o relógio do servidor (`ServerNow`, e uma versão quantizada no
  ritmo de frame do SNES).
- O servidor continua sendo só relay: guarda grupo, cliente, índice, setor
  e `LastSeen`; não guarda progresso. O timeout também é 15 s.
- O o2 usa `broadcastAll` para o jogo (o `broadcastSector` está comentado no
  `net.go` do jogo, embora o servidor o suporte). Ou seja, o protocolo 3
  tem setor, mas o cliente atual não o usa para separar sprites.

---

## 2. Formato das mensagens

### 2.1 Serialização interna `0x13`

Depois do envelope do servidor, o cliente clássico escreve:

```
protocolo de script (1 byte) = 0x13
team (1 byte)
frame (1 byte, contador de 8 bits)
TLVs...
```

Cada TLV começa por um byte de tipo. Os tipos conhecidos:

| Tipo | Nome | Conteúdo | Quando é enviado |
|---:|---|---|---|
| `0x01` | localização | módulo, submódulo, sub-submódulo; localização 24 bits; x/y; dungeon/entrada; última posição de overworld; offsets de scroll; cor do jogador; flag de Super Metroid | todo pacote principal (até 60 Hz) |
| `0x02` | sons | `sfx1`, `sfx2` | todo pacote principal |
| `0x03` | sprites (início) | quantidade + entradas de OAM; opcionalmente CHR e paleta | se `SyncSprites` |
| `0x04` | sprites (continuação) | índice inicial + quantidade + entradas | quando os sprites não cabem em 1452 bytes |
| `0x05` | WRAM | contagem, offset e registros de 6 bytes (timestamp 4 + valor 2) | cristal de dungeon e chaves pequenas |
| `0x06` | SRAM | flag de "começa em 0", flag de itens de Super Metroid, início, contagem, bytes | itens/progresso a cada 16 frames; salas/overworld a cada 32 |
| `0x07` | tilemap | timestamp 4, localização 24, início, quantidade, runs | se `SyncTilemap` |
| `0x08` | objetos | bloco bruto de `0x2A0` bytes | se `enableObjectSync` (desligado por padrão) |
| `0x09` | ancillas | quantidade + entradas de ancilla | se `SyncTilemap` |
| `0x0A` | tochas | quantidade + pares (tocha, timer) | a cada 2 frames, se `SyncTilemap` |
| `0x0B` | PvP | hitbox ativa, caixa, tempo de espada, item, tipo de espada, nível da sala, ataques | se `EnablePvP` |
| `0x0C` | nome | 20 bytes com espaços | todo pacote principal |
| `0x0D` | eventos SM | 0x54 bytes + flags de clear | SMZ3, a cada 32 frames |
| `0x0E` | buffer SRAM | início, contagem, bytes | SMZ3, a cada 32 frames |
| `0x0F` | localização SM | área, x/y, sub-x/sub-y, sala, pose | SMZ3 |
| `0x10` | sprite SM | offsets e paleta de Samus | SMZ3 |

Detalhes que importam para a compatibilidade:

- A localização de 24 bits é `bit17 = Dark World`, `bit16 = dungeon`,
  `bits 0..15 = sala/área`. Em dungeon, o bit de Light/Dark é ignorado
  para decidir "mesma sala".
- O nome vai **sempre** com 20 bytes; o cliente remoto remove espaços.
- O `frame` é de 8 bits e o cliente tem uma checagem de "dado velho"
  incompleta (o próprio código marca um TODO para o wrap de `0xFF`).
- Cada sprite remoto leva os 4 bytes principais do OAM, o quinto byte
  estendido, e opcionalmente 16 palavras de CHR (8x8) ou 64 palavras
  (16x16, quatro tiles) e 16 palavras de paleta CGRAM.
- `0x05` não é um bloco arbitrário de WRAM: o cliente só entende o cristal
  de dungeon e o array de chaves pequenas; outros offsets são descartados
  (mas o cursor avança).
- `0x07` usa runs horizontais/verticais de tiles de 16 bits mais atributo
  de 8 bits; o timestamp é o relógio local do cliente, em milissegundos.

### 2.2 Frequência real (cliente clássico)

O cliente limita o envio principal a **16 ms** (aproximadamente 60 Hz).
Dentro do `send()`:

| Conteúdo | Frequência no código |
|---|---|
| localização, nome, sons, PvP (se ligado) | a cada envio principal (16 ms) |
| sprites (com CHR/paleta quando necessário) | a cada envio, se `SyncSprites` |
| tilemap comprimido | a cada envio, se `SyncTilemap` |
| ancillas, objetos, cristal, chaves pequenas | no pacote principal, conforme opções |
| tochas | `frame & 1 == 0` (a cada 2 frames) |
| itens/progresso da SRAM | `frame & 15 == 0` (a cada 16 frames) |
| salas de dungeon (`0x000..0x24F`) | `frame & 31 == 0` (a cada 32) |
| eventos de overworld (`0x280..0x33F`) | `frame & 31 == 16` (a cada 32, defasado) |
| extras de randomizer (potes/sprites) | a cada envio, se existirem |
| eventos/buffer de SMZ3 | a cada 32 frames |

O pacote principal é `Broadcast` (todos); sprites, tilemap e tochas são
`BroadcastToSector` (só o setor). Na prática o setor é a sala/área de 16
bits (ver 1.2); a decisão final de desenhar ou não o fantasma usa a
localização completa de 24 bits no cliente. É por isso que o mapa/lista de
jogadores mostra todo mundo, mas o fantasma só aparece para quem passa no
`can_see`.

### 2.3 O que muda no `0x14` do o2

O o2 mantém a mesma ideia de TLVs, mas muda a versão para **`0x14`** e
acrescenta:

- `playerTime`/`serverTime` no protocolo 3 e um pacote de `echo` para RTT;
- horário de início e fim da partida dentro da mensagem de localização e
  da mensagem de nome (para o cronômetro de corrida);
- um cliente que valida a versão e **rejeita** `0x13` (e o clássico rejeita
  `0x14`). Portanto, os dois clientes não se entendem no nível de jogo,
  mesmo usando o mesmo servidor/grupo. O servidor repassa, mas cada lado
  descarta a serialização do outro.
- o cliente o2 **não envia** sprites/tilemap/ancillas/PvP; ele até analisa
  mensagens de sprite/tilemap para pular o conteúdo, mas não as desenha.
  `DeserializeObjects` e `DeserializePvP` estão "not implemented" e podem
  derrubar o handler se um cliente clássico as enviar no mesmo grupo.

---

## 3. O que é sincronizado e com que regra de mescla

### 3.1 A SRAM "viva" e a SRAM do save

No jogo original, `$7EF000..$7EF4FF` é a **cópia de trabalho** do arquivo
selecionado. O save de verdade fica em `g_zenv.sram`, em três slots de
`0x500` bytes (`0x0000`, `0x0500`, `0x0A00`), com cópias em
`+0x0F00`. O ALttPO lê e escreve a cópia de trabalho; o jogo só grava no
slot quando o jogador salva. No zelda3 isso corresponde exatamente a
`save_dung_info` (`g_ram + 0xF000`) e `g_zenv.sram`.

Isso é importante para a C1: sincronizar item é escrever na cópia viva;
persistir é responsabilidade do save do jogador. Se quisermos garantia
contra crash, teremos de decidir se escrevemos também no slot, o que foge
do comportamento original.

### 3.2 Lista de offsets (vanilla, US/JP/EUR)

Offsets relativos a `$7EF000` (a mesma tabela que o zelda3 usa em
`variables.h`):

| Offset | Conteúdo | Regra no ALttPO |
|---:|---|---|
| `0x000..0x24F` | estado das salas de dungeon (2 bytes por sala) | OR com máscara; 0x128 salas |
| `0x280..0x33F` | eventos/overlays do overworld | OR com máscara; 0x82 áreas mescladas |
| `0x340` | arco (normal/prata) | máximo (randomizer troca por zero → não-zero) |
| `0x341` | boomerang | máximo (randomizer troca por zero → não-zero) |
| `0x342` | hookshot | máximo |
| `0x343` | **bombas (contagem)** | **não sincroniza** |
| `0x344` | cogumelo/pó | máximo (randomizer remove este syncable) |
| `0x345` | fire rod | máximo |
| `0x346` | ice rod | máximo |
| `0x347` | Bombos | máximo |
| `0x348` | Ether | máximo |
| `0x349` | Quake | máximo |
| `0x34A` | lamp/lanterna | máximo |
| `0x34B` | martelo | máximo |
| `0x34C` | pá/flauta | máximo (randomizer troca por pá → flauta → ativa) |
| `0x34D` | bug net | máximo |
| `0x34E` | livro de Mudora | máximo |
| `0x34F` | **seleção de bottle** | **não sincroniza** (trava o seletor) |
| `0x350` | cane of Somaria | máximo |
| `0x351` | cane of Byrna | máximo |
| `0x352` | magic cape | máximo |
| `0x353` | magic mirror | máximo |
| `0x354` | luvas | máximo + atualiza paleta |
| `0x355` | botas | máximo |
| `0x356` | flippers | máximo |
| `0x357` | moon pearl | máximo |
| `0x358` | não usado | — |
| `0x359` | espada (1..4) | máximo; ignora `0xFF` do ferreiro |
| `0x35A` | escudo (0..3) | máximo + atualiza gráficos/paleta |
| `0x35B` | armadura (0..2) | máximo + atualiza paleta |
| `0x35C..0x35F` | quatro bottles | custom: só 0 → não-zero (não troca conteúdo) |
| `0x360..0x363` | **rupias** | **não sincroniza** |
| `0x364/0x365` | bússolas (2 bytes cada) | OR de bits |
| `0x366/0x367` | big keys | OR de bits |
| `0x368/0x369` | mapas | OR de bits |
| `0x36A` | rupias no lago | não sincroniza |
| `0x36B` | pedaços de coração | combinado com `0x36C`, máximo |
| `0x36C` | capacidade de coração | combinado com `0x36B`, máximo |
| `0x36D` | **vida atual** | **não sincroniza** |
| `0x36E` | **magia atual** | **não sincroniza** |
| `0x36F` | **chaves atuais** | fora do sync de itens; há sync opcional de chaves |
| `0x370` | capacidade de bombas | máximo |
| `0x371` | capacidade de flechas | máximo |
| `0x374` | pingentes | OR de bits |
| `0x377` | **contagem de flechas** | **não sincroniza** |
| `0x379` | flags de habilidade | OR de bits |
| `0x37A` | cristais | OR de bits |
| `0x37B` | uso de magia (1/2, 1/4) | máximo |
| `0x37C..0x38B` | chaves pequenas por dungeon | timestamp; maior vence; tie → maior valor |
| `0x3C5` | estado do mundo/intro | custom: maior; dispara gráficos ao sair da chuva |
| `0x3C6` | flags de progresso 1 | OR + casos de follower |
| `0x3C7` | ícones do mapa | máximo |
| `0x3C8` | ponto inicial | **não sincroniza** (desligado; causa bugs) |
| `0x3C9` | flags de progresso 2 | OR + casos de follower |
| `0x3CA` | save "está no Dark World" | não faz parte da lista base |
| `0x3CC` | follower atual | não é sincronizado diretamente; há limpezas condicionais |
| `0x3E7`, `0x403` | contadores de morte | não sincronizam |
| `0x410/0x411` | NPC flags (randomizer) | progresso/OR |
| `0x417` | espada progressiva (randomizer) | máximo |
| `0x418` | pedaços da Triforce (randomizer) | máximo |
| `0x41A` | "coat" (randomizer) | máximo |
| `0x422` | escudo progressivo (randomizer) | máximo |
| `0x429` | contador de pingentes (randomizer) | máximo |
| `0x434..0x439` | contadores de baús por dungeon (randomizer) | máximo |
| `0x46E` | mail/armadura progressiva (randomizer) | máximo |
| `0x472` | dungeons completas (randomizer) | máximo |
| `0x476` | contador de cristais (randomizer) | máximo |
| `0x38C/0x38E` | bits de inventory swap (VT) | OR + casos de cogumelo/pó/arco |
| `0x302..0x33F` | contadores de compras de loja (randomizer) | máximo; ficam na faixa de overworld |
| `0x390..0x3C4` | contadores de limites de itens (randomizer) | máximo |
| `0x3FF..0x429` | progresso do randomizer (VT) | faixa serializada |
| `0x4C0..0x4CD` | baús do door randomizer | máximo |
| `0x4E0..0x4ED` | chest-keys do door randomizer | máximo; o `DoorRandomizerMapping` serializa, o o2 comenta o envio |

Fora do `$7EF000`:

| Endereço | Conteúdo | Observação |
|---:|---|---|
| `$7F6000+` | SRAM estendida do door randomizer | potes em `$7F6018`, sprites em `$7F6268`, 0x250 bytes cada; alterna um por frame |
| `$7E09A2+` | SRAM de itens do Super Metroid | só SMZ3 |
| `$A17900+` | buffer de itens do SMZ3 | só SMZ3 |

### 3.3 Regras de mescla

O `SyncableItem` tem três estratégias:

1. **tipo 1 — maior vence**: `max(local, remoto)`. Usado em níveis de item
   e capacidades.
2. **tipo 2 — OR de bits**: `local | remoto`. Usado em flags, pingentes,
   cristais, mapas/bússolas/big keys, etc.
3. **tipo 0 — mutação customizada**: uma função decide. Exemplos:
   - zero → não-zero (bow, boomerang, mushroom, bottles);
   - pá → flauta → flauta ativa;
   - espada/escudo/armadura disparam a rotina de gráfico/paleta do jogo;
   - coração combina `0x36B` (pedaços) e `0x36C` (capacidade) num único
     valor, para não perder a conta quando um baú de coração inteiro é
     aberto no meio de uma animação;
   - progresso 1/2 tem casos para remover followers obsoletos.

Salas e overworld usam `SyncableUnderworldRoom`/`SyncableItem` com OR e uma
**máscara** por sala/área:

- `SyncChests = 0` tira os bits `0x03F0` das salas (baús/chaves);
- a água interna do Swamp Palace (`sala 0x035`) fica fora (`0xFF7F`);
- se o ROM não tem o `PersistentFloodgate`, os bits de água do Swamp e do
  watergate são mascarados (`0xFF7F`, `0xFEFF`) e os overlays `0x3B/0x7B`
  perdem `0x20`;
- o buraco da pirâmide (`área 0x5B`) tem um caso especial: ao abrir, o
  cliente local chama a rotina de criar o buraco se estiver na área.

Chaves pequenas e cristal de dungeon usam `SyncableByte`, que tem
**timestamp**: o maior timestamp vence; em empate, o maior valor. O
timestamp é o relógio local do cliente, não o do servidor; relógios
diferentes podem influenciar. O cristal só é capturado no módulo de
dungeon e é "resetado" ao entrar/sair, para não sincronizar transições.

### 3.4 O que NÃO é sincronizado

Do `faq.md` e do código:

- inimigos, drops de inimigos e itens no chão;
- contagem de flechas, bombas e rúpias;
- vida atual e magia atual (só capacidade/upgrade);
- conteúdo de bottles já existentes;
- chaves pequenas **atuais** por padrão (há opção separada, desligada no
  preset Default);
- mudanças temporárias de dungeon (potes, star tiles, trap floors);
- estado de save (morte, follower, ponto inicial);
- diálogos, cutscenes e progresso de história fora dos bits listados.

### 3.5 Dependência da versão da ROM

O `ROMMapping.as` detecta a ROM pelo título/região/versão no cabeçalho
(`$00FFC0`, `$00FFD9`, `$00FFDB`) e escolhe uma classe:

| ROM | Classe | Observação |
|---|---|---|
| US 1.0/1.1/1.2 | `USAROMMapping` | endereços de função/tabelas dos EUA |
| JP 1.0 | `JPROMMapping` | algumas tabelas deslocadas (ex.: +0x12 em tilemap/paleta) |
| EUR | `EURROMMapping` | endereços de função diferentes |
| GER-EUR | `GER_EURROMMapping` | idem, alguns "TODO" no próprio código |
| VT/AP/BM/BD/ER randomizer | `RandomizerMapping`/`MultiworldMapping`/`DoorRandomizerMapping` | lista extra de itens/progresso e SRAM estendida |
| SMZ3 | `SMZ3Mapping` | itens/eventos do Super Metroid e buffers próprios |

O **layout da SRAM vanilla é o mesmo** para US/JP/EUR; o que muda são
endereços de código/tabelas e os randomizers, que acrescentam offsets e
mudam a semântica de alguns bytes. O ALttPO detecta **FastROM** pelo byte
de banco do `JSL Sprite_Main` em `$00F82A+3` e soma `0x800000` aos
endereços de ROM quando presente. Endereços de função usados como gancho:

| Gancho | US (exemplo) | Uso |
|---|---:|---|
| `JSL MainRouting` | `$008056` | ponto de patch do script |
| `pre-main` | `$008053` | antes do main loop |
| tocha | `$01F3EC` / `$01F48D` / `$01F4A6` | acender/apagar |
| decompress sword/shield | `$00D2C8` / `$00D308` | ao ganhar espada/escudo |
| paleta sword/shield/armor | `$1BED03` / `$1BED29` / `$1BEDF9` | atualizar cor |
| mirror warp finish | `$02B260` | ao sair da chuva/mirror |
| sprite gfx properties | `$00FC62` | carregar gráficos |
| buraco da pirâmide | `$1BC2A7` | abrir o buraco |

O zelda3 nativo é baseado na ROM **US** (o README exige a ROM US com hash
específico), então a tabela US é a que interessa para a C1. Se um dia
rodarmos uma seed JP/randomizer em `--emu-rom`, a lista de syncables e os
endereços de função precisam da classe correspondente; não basta reusar a
tabela US.

---

## 4. Os "fantasmas"

### 4.1 O que é enviado

O cliente não manda uma "animação" semântica do Link remoto. Ele manda o
**OAM bruto**:

- 12 sprites do Link, a partir do offset de OAM do Link
  (`$7E0352 >> 2`), com os 4 bytes principais e o quinto byte estendido;
- quando o tile de CHR ainda não foi enviado naquele pacote, manda os 16
  words (8x8) ou 4×16 words (16x16) lidos da VRAM;
- quando a paleta ainda não foi enviada, manda as 16 cores de CGRAM;
- além do Link, filtra sprites de efeito por número de CHR: folhas de
  arbusto, grama cortada, caco de pote/pedra, boomerang, powder, cape,
  block push, pedra grande, item na mão, follower, flecha, fire rod, ice
  rod, lanterna, somaria, explosões, etc.;
- a posição vem da mensagem de localização: `x`, `y`, `xoffs`, `yoffs`,
  `location`, módulo/submódulo, dungeon/entrada, cor e `in_sm`;
- o nome e a cor do jogador são mensagens separadas (nome 20 bytes; cor
  entra na localização).

O `xoffs/yoffs` é o scroll do BG relativo ao canto superior esquerdo da
sala. Para desenhar o remoto, o cliente calcula
`rx = remote.xoffs - local.xoffs`, `ry = remote.yoffs - local.yoffs` e
desenha o OAM remoto deslocado por `(rx, ry)`.

### 4.2 Quando aparece

- Só em módulos de jogo: na prática dungeon (`0x07`), overworld
  (`0x09`/`0x0B`) e módulos de transição entre `0x06` e `0x1A`, exceto
  `0x14` (attract) e o mapa mode-7; fora disso não desenha.
- Só se `can_see(location)`: mesma localização 24 bits; durante transição
  de tela, aceita a localização real antiga e a nova.
- Sprites vão por setor, mas o setor que chega ao servidor é só a
  sala/área de 16 bits (ver 1.2); a decisão final de mesma sala/mundo é
  feita no cliente com a localização completa.
- O TTL local é 255 frames (aprox. 4,25 s); se o remoto para de mandar, o
  fantasma some. O servidor só remove o cliente após 15 s.

### 4.3 Como é desenhado no bsnes-as

Há dois caminhos, controlados por `enableRenderToExtra`:

1. **Camada extra do PPU (padrão, "Fast mode")**: o bsnes-as tem uma lista
   `ppu::extra` independente do OAM normal. O cliente coloca cada sprite
   remoto nessa lista, com prioridade um pouco abaixo dos sprites locais,
   fonte OBJ1/OBJ2, posição, flips, tamanho, prioridade e pixels; desenha
   os tiles de CHR com a paleta remota. O nome é um tile de texto com
   contorno, desenhado na mesma camada. Vantagem: não gasta slots de OAM
   nem sobrescreve a VRAM do jogo.
2. **Injeção no OAM normal (fallback)**: escolhe slots livres ao redor do
   índice original, copia os bytes de OAM, procura tiles livres em
   `0x100..0x1FF`, faz backup do tile de VRAM que será sobrescrito e
   restaura no frame seguinte. É mais frágil: pode faltar slot, o limite
   de 32 sprites/linha do SNES pode cortar sprites locais e a VRAM é
   temporariamente alterada.

A paleta do Link remoto vem da CGRAM capturada; se o remoto usa
`SyncTunic`, a cor da túnica dele é a cor escolhida no cliente dele. A
`player_color` separada é usada no rótulo e no mapa.

### 4.4 O que os fantasmas fazem além de aparecer

- **Som**: `sfx1/sfx2` remotos são tocados localmente, com o pan ajustado
  pela posição relativa; alguns sons de menu/beep são filtrados.
- **Ancillas**: bombas, flechas, fire/ice rod, somaria, lápide, explosões,
  etc. são escritas nos slots de ancilla locais (com um handshake de
  ownership). Isso faz o objeto existir na simulação local, podendo
  interagir com inimigos/cenário e, no caso de bomba, ferir o Link local
  indiretamente.
- **Tilemap**: mudanças de tiles/atributos (grama cortada, arbusto, parede
  bombardeada, porta, tocha) são aplicadas no WRAM e, quando seguro, na
  VRAM, com timestamp e checagem de sala.
- **Objetos**: o sync opcional copia objetos do tipo "being picked up"
  (`0xEC`) para mostrar o que o outro pegou; não é um item compartilhado.
- **PvP**: se ligado, o cliente calcula hitboxes de espada/martelo/bug net
  e projéteis, aplica dano/recoil e manda o ataque para o outro cliente;
  há opção de friendly fire. É a única forma de interação direta.
- **Mapa/lista**: a janela de mapa mostra marcadores coloridos por
  `player_color`; a lista mostra nome/time/localização.

---

## 5. Modo corrida e outras opções

- O README descreve **Race Mode**: não envia nem recebe estado de jogo,
  mas continua visível para os outros. No cliente atual não há um checkbox
  "Race"; o preset **Multiworld** faz isso: `syncSprites = true` e todos os
  outros syncs `false` (itens, progresso, salas, overworld, tilemap,
  corações, dungeon items, pingentes, cristais, chaves). O tooltip do
  preset diz que ele liga o RaceMode e a ponte qusb2snes.
- **Team** (0..255): separa o sync de itens/progresso/salas; não separa
  totalmente sprites, tilemap de overworld, bombas ou PvP. Time 0 é o
  padrão "todos".
- **Sync toggles**: sprites, tilemap, underworld, overworld, items, chests,
  hearts, dungeon items, pendants, crystals, small keys, progress.
- **PvP** e **PvP friendly fire**.
- **Sync tunic**: muda a paleta do Link local para a cor do jogador; os
  outros veem a cor capturada.
- **Show player labels / Show my label / fonte**: rótulos na tela.
- **Bridge/qusb2snes** e **ConnectorLib**: ponte para SNES real e para
  CrowdControl/EmoTracker; não fazem parte do protocolo de co-op.
- **Discord**: atividade; não afeta a rede.
- Presets **Default** e **Multiworld**.
- Não há senha, autenticação, criptografia, chat, persistência de grupo ou
  modo servidor de corrida. "Corrida" é uma decisão local de cada cliente.

---

## 6. o2 — o que muda no ALttPO 2.0

> Esta seção usa o upstream `alttpo/o2` (v0.4.5, commit `165a553d`,
> 27/09/2026) e `alttpo/o2-server` (commit `a3e0b24e`), porque o
> `o2-README.md` local é só o título.

### 6.1 Arquitetura

- O o2 é um **aplicativo desktop em Go** com interface web local (porta
  padrão 27637), não um script dentro do emulador.
- Ele fala com o SNES por drivers: **SNI** (gRPC em `localhost:8191`),
  **qusb2snes**, **RetroArch** e **FX Pak Pro**; há também um driver mock.
- Para console, ele **patcha a ROM**: expande a SRAM para 32 KB e instala
  um gancho de init em `$00:802F` e um gancho antes do main loop em
  `$00:8056` (o `JSL Module_MainRouting`). As rotinas de update são
  copiadas para a SRAM em `$707D00..$708000` e chamadas pelo jogo; assim
  o o2 consegue aplicar mudanças no hardware real, não só na RAM do
  emulador.
- O o2 **não** tem a camada extra de PPU do bsnes-as. No código estudado
  ele analisa mensagens de sprite/tilemap para pular o conteúdo, mas não
  desenha fantasmas; a interface mostra lista de jogadores, localização,
  tempo de corrida e notificações.

### 6.2 Protocolo e serialização

- Protocolo de servidor **0x03**, protobuf `GroupMessage` (seção 1.4),
  mesma porta UDP 4590; o servidor ainda aceita 0x01/0x02.
- Serialização de jogo **0x14**: mesma família de TLVs, com horários de
  início/fim de partida e validação rígida de versão.
- O jogo envia localização (com hash/TTL de 60 frames quando não muda),
  chaves pequenas e estado da porta do supertile a cada frame, itens/
  progresso a cada 16 frames, salas a cada 32 e overworld a cada 32
  (defasado). Não envia sprites, tilemap, ancillas, objetos ou PvP.
- O servidor 0x03 não guarda estado de jogo; mede RTT via `echo` e oferece
  `serverTime` para o cliente estimar o relógio do servidor.

### 6.3 O que o o2 sincroniza hoje

Itens, dungeon items, progresso, corações, chaves pequenas, baús,
underworld/overworld, cor da túnica; o cronômetro de corrida usa o
primeiro início e o último fim entre os jogadores. Não há fantasmas,
tilemap, ancillas, objetos, PvP nem "ver o Link do outro" na tela do jogo.
A lista de jogadores e o mapa são a parte social.

### 6.4 É o protocolo do futuro?

- É a **direção atual** do projeto: o repositório o2 está ativo (commit de
  2026), o servidor já fala 0x03 e o protobuf é mais extensível que o
  envelope binário fixo.
- Mas, para o objetivo do co-op do zelda3 (Modelo A com fantasmas), o o2
  atual **não substitui** o ALttPO clássico: falta justamente a parte
  visual e a maior parte do estado de mundo. É um alvo móvel.
- O servidor 0x03 é compatível com o clássico no transporte (mesma porta e
  header), mas não há negociação de serialização; os clientes 0x13 e 0x14
  se ignoram.

---

## 7. Compatibilidade para o zelda3

### 7.1 O que o zelda3 já tem a favor

| Peça | Onde | Como ajuda |
|---|---|---|
| `g_ram` 128 KB | `src/zelda_rtl.c`, `variables.h` | mesmos offsets de WRAM do SNES; `save_dung_info` em `+0xF000` |
| SRAM 8 KB + slots | `g_zenv.sram` | mesmos offsets `$7EF000+`; save em `saves/sram.dat` |
| OAM/CGRAM/VRAM | `Ppu` (`oam[0x110]`, `cgram[0x100]`, `vram[0x8000]`) | dá para capturar e desenhar fantasmas |
| `oam_buf`/`extended_oam` | `g_ram+0x800`, `+0xA00`, `+0xA20` | mesma fonte que o ALttPO lê para o Link |
| `FrameDraw` | `src/main.c`, `src/mod/draw.*` | overlay depois do PPU; hoje só rect/text |
| `zelda.save` | `src/mod/lua_save.*` | configuração por mod/slot; não serve para progresso compartilhado |
| EmuNWA/SNI | `src/mod/nwa_server.c`, `docs/engine/SNI.md` | leitura/escrita de WRAM/SRAM/VRAM/OAM/CGRAM por ferramentas externas |
| F4d/nomes | `src/mod/state_table.c` | nomes como `link_item_bow`, `link_health_capacity`, `save_dung_info` |
| Eventos | `src/mod/events.h` | `FrameEnd`, `SramSave`, `SramLoad`, `RoomEnter`, `AreaEnter`, `ItemReceive`, etc. |

O zelda3 é US; a tabela de itens/SRAM do ALttPO US encaixa. O que não
existe hoje é a **camada extra de OAM** do bsnes-as; teríamos de criá-la no
`Ppu` ou desenhar por cima no `FrameDraw`.

### 7.2 Para conversar com o servidor clássico (0x02 + 0x13)

O que seria necessário:

1. **Socket UDP** para o servidor:4590, header `0x651F`, protocolo `0x02`,
   grupo de 20 bytes, pedido de índice e leitura de `0x80/0x81/0x82`.
2. **Serializador 0x13** exato: `team`, `frame`, TLVs `0x01..0x10`.
   Localização/nome/sons a 60 Hz; SRAM a cada 16/32 frames; sprites e
   tilemap quando aplicável.
3. **Tabela de progresso** com as regras da seção 3, lendo/escrevendo
   `g_ram+0xF000+off` e persistindo no save do jogo. As funções nativas
   equivalentes existem: `DecompressSwordGraphics`, `DecompressShieldGraphics`,
   `Palette_Load_Sword`, `Palette_Load_Shield`,
   `Palette_Load_LinkArmorAndGloves`, `Dungeon_LightTorch`,
   `CreatePyramidHole`, `MirrorWarp_FinalizeAndLoadDestination`.
4. **Fantasmas**: capturar `oam_buf`/`extended_oam`/CGRAM/VRAM do Link,
   mandar os pacotes `0x03/0x04`, e desenhar os remotos. Para "aparecer
   para quem usa bsnes", o pacote tem de ser byte a byte o 0x13; não basta
   mandar localização e nome.
5. **Regras de visibilidade/setor**: setor de 16 bits (sala/área) no
   servidor e checagem de localização completa de 24 bits no cliente,
   TTL de 255 frames, `can_see` com transição.
6. **Sons, ancillas, tilemap e PvP** são opcionais para um primeiro
   adaptador, mas sem eles o co-op é mais pobre que o ALttPO clássico.
7. **Teste real** com bsnes-as + cliente clássico e com um servidor
   alttpo/o2; sem isso não há como provar a compatibilidade.

Custo: alto. A parte de itens é a mais simples; a de sprites/CHR/paleta e
a de tilemap/ancillas/PvP é onde mora o trabalho e os detalhes de versão
de ROM.

### 7.3 Para conversar com o o2 (0x03 + 0x14)

1. **Protobuf 0x03** (ou escrever o wire format à mão): `GroupMessage`,
   `joinGroup`, `broadcastAll`, `broadcastSector`, `echo`.
2. **Serialização 0x14**: localização/nome com timestamps de início/fim,
   itens/progresso; validar a versão e não mandar TLVs que o o2 não
   implementa (`0x08` objetos e `0x0B` PvP derrubam o handler).
3. **Relógio**: usar `serverTime`/`echo` para o cronômetro e para os
   timestamps; o o2 espera isso.
4. **Fantasmas**: o o2 atual não desenha; mandar sprites não fará o outro
   jogador ver o Link. No máximo ele aparece na lista/mapa.
5. **Teste** com o2 + SNI/qusb2snes/RetroArch ou com o driver mock.

Custo: médio para itens/progresso, mas **não entrega o objetivo visual**.
O o2 também muda com frequência (0x14 é a versão atual), então um adaptador
precisa ser versionado e testado a cada release.

### 7.4 Quem vê quem (situação real hoje)

| Combinação | Progresso | Fantasma | Observação |
|---|---|---|---|
| clássico ↔ clássico | sim | sim | ecossistema maduro |
| o2 ↔ o2 | sim | não | só lista/mapa/notificações |
| clássico ↔ o2 | não | não | versões 0x13/0x14 se rejeitam; o2 pode até panicar com TLVs não implementados |
| zelda3 ↔ clássico | só depois do adaptador 0x13 | só depois do adaptador 0x13 | precisa implementar o protocolo inteiro |
| zelda3 ↔ o2 | só depois do adaptador 0x14 | não (o2 não desenha) | item/progresso é viável; fantasma não |
| zelda3 ↔ zelda3 (protocolo próprio) | sim | sim | caminho recomendado para a C1 |

### 7.5 Riscos

- **Licença**: nenhum dos repositórios declara licença. Não podemos copiar
  código, nomes de tabelas extensas ou assets; a implementação tem de ser
  independente, a partir de uma especificação observada. O risco jurídico
  de interoperar por engenharia reversa varia por jurisdição; não é um
  parecer legal.
- **Versão**: o clássico está congelado em 0x13, mas é legado; o o2 está
  em 0x14 e muda. O servidor aceita três protocolos, mas não há negociação
  entre clientes.
- **Teste**: não há suíte pública de conformidade; a única prova real é
  rodar bsnes-as+ALttPO e o2 contra o zelda3. O regress do zelda3 não cobre
  rede.
- **Segurança**: UDP sem autenticação/criptografia; o nome do grupo é o
  único controle de acesso. Qualquer um que descubra o grupo entra, e o
  servidor pode ser alvo de spoofing/flood. Não expor um servidor próprio
  sem firewall.
- **NAT/rede**: o servidor é relay, então só é preciso saída UDP para o
  servidor; não há hole punching entre clientes. Perda de pacote é
  absorvida pela repetição de itens/progresso, mas sprites/PvP são
  transitórios.
- **Banda**: CHR/paleta de sprites e tilemap comprimido podem gerar
  rajadas; o limite de 1452 bytes força vários pacotes. Em LAN é tranquilo;
  na internet depende do servidor e do link.
- **Relógios**: timestamps de chaves/cristal/tilemap são locais; relógios
  diferentes podem favorecer um cliente. O o2 tem `serverTime`, o clássico
  não.

### 7.6 Recomendação

**Para a C1: protocolo próprio, inspirado no ALttPO, com a porta aberta
para um adaptador depois.**

Prós:

- controlamos versão, documentação, testes e licença;
- reaproveitamos o que o ALttPO já provou: envelope UDP pequeno, lista de
  offsets da SRAM, regras max/OR/custom, fantasmas como OAM/CHR/paleta;
- dois zelda3 em loopback são testáveis no regress/CI, sem bsnes;
- o mesmo modelo interno serve para o adaptador 0x13 e para o 0x14;
- não ficamos reféns de um formato sem licença nem de um alvo móvel.

Contras:

- não jogamos com a comunidade existente no dia 1;
- precisamos manter o nosso servidor/relay (ou usar um relay simples);
- o adaptador de compatibilidade continua sendo um projeto à parte.

**Se a prioridade for jogar com quem já usa bsnes+ALttPO:** a única opção
com fantasmas hoje é o **clássico 0x02+0x13**. O o2 não desenha fantasmas
e não implementa tilemap/ancillas/PvP; escolhê-lo agora daria menos
funcionalidade e mais risco de mudança. O custo do clássico é alto, mas o
escopo pode começar pequeno: localização, nome, itens/progresso, salas/
overworld e sprites; tilemap/ancillas/PvP ficam para depois.

**Não recomendo** adotar o protocolo 0x03/0x14 como principal nesta fase.
Ele é tecnicamente mais limpo e é o futuro do o2, mas o cliente atual não
entrega a parte visual do ALttPO e a serialização 0x14 ainda é um alvo
móvel. Se o o2 passar a desenhar fantasmas e documentar/estabilizar o
0x14, ele se torna o melhor candidato a segundo adaptador.

---

## 8. Como isso se encaixa no Modelo A (C1)

O ALttPO é praticamente a definição do **Modelo A**: cada jogador roda o
próprio jogo; o que é compartilhado é uma lista declarada de progresso e
os outros jogadores aparecem como fantasmas. Não precisamos trocar
`g_ram`/SRAM/PPU por instância (isso é a C2, tela dividida local).

### 8.1 Proposta de divisão da C1

| Etapa | Entrega | Prova |
|---|---|---|
| **C1a** | UDP próprio, versão no fio, handshake, heartbeat, grupo/setor, cliente e servidor simples | dois zelda3 em loopback trocam posição/nome |
| **C1b** | tabela de progresso (seção 3) + regras de mescla + persistência no save | um pega item, o outro vê; regress verde com co-op desligado |
| **C1c** | fantasmas: captura de OAM/CHR/CGRAM, envio e camada extra no `Ppu` | dois jogadores na mesma sala se veem; rótulos |
| **C1d** | testes de rede, perda de pacote, entrada/saída, versão; documentação | dois processos, cenários automáticos |
| **C3** | adaptador clássico 0x02+0x13 (e/ou o2 0x03+0x14) | um zelda3 visto por bsnes+ALttPO |

### 8.2 Onde encaixar no código

- **Rede**: `src/mod/coop/` (novo), com `coop_net.c/h` (UDP, framing,
  heartbeat), `coop_proto.c/h` (mensagens versionadas), sem bloquear o
  frame; poll não-bloqueante no `FrameEnd` ou uma thread com fila.
- **Progresso**: `coop_sync.c/h` com uma tabela `{offset, size, regra,
  nome, máscara}`; ler/escrever `g_ram+0xF000`; usar os nomes do F4d
  (`link_item_bow`, `link_health_capacity`, `save_dung_info`,
  `save_ow_event_info`) para legibilidade; chamar as funções nativas de
  atualização de gráfico/paleta.
- **Fantasmas**: `coop_ghost.c/h`; capturar `oam_buf`, `extended_oam`,
  `ppu->cgram`, `ppu->vram`; guardar os remotos; desenhar numa **camada
  extra** do `Ppu` (arrays separados, não hasheados) para não sujar
  `g_ram`, `g_zenv.sram` nem `g_zenv.vram`; rótulos podem usar o
  `FrameDraw`/fonte 5x7 ou um pequeno overlay nativo.
- **Configuração**: seção `[Coop]` no ini ou `"features"` de um mod;
  desligado por padrão, no-op total. O `zelda.save` pode guardar nome,
  cor, grupo e servidor por slot, mas **não** o progresso compartilhado.
- **Eventos**: `kModEv_FrameEnd` para o passo de rede; `kModEv_SramSave`
  para persistir; `kModEv_SramLoad`/`FileLoad` para reinicializar o
  estado remoto; `RoomEnter`/`AreaEnter` para o setor e a visibilidade.
- **SNI**: o EmuNWA do zelda3 continua sendo uma ferramenta de inspeção e
  uma possível ponte (via servidor SNI real) para o o2, mas não é o
  protocolo do co-op. O o2 fala gRPC com o SNI, não EmuNWA direto; e
  precisa de filesystem/BootFile que o nosso EmuNWA não expõe. Portanto,
  "rodar o o2 em cima do zelda3" não é um caminho pronto.

### 8.3 Regras de ouro

- Com o co-op desligado, **nenhum socket, nenhuma escrita, nenhum OAM
  extra**: o jogo tem de continuar byte a byte igual à ROM.
- A camada de fantasma não pode alterar `g_ram`, `g_zenv.sram` nem
  `g_zenv.vram`; arrays novos no `Ppu` e desenho depois do PPU resolvem
  isso. Se usarmos injeção no OAM normal, restaurar antes do hash.
- O progresso compartilhado só pode escrever nos offsets declarados e
  quando o co-op estiver ativo; o resto da RAM fica intocado.
- O adaptador de compatibilidade deve ser um módulo separado, com versão
  no fio e um teste que compara bytes com um cliente de referência.

---

## 9. O que não foi verificado / dúvidas

- Não rodei bsnes-as + ALttPO nem o o2; as conclusões de interação vêm da
  leitura do código. Não medi tráfego real, perda de pacote, NAT ou
  latência.
- O `o2-README.md` local é só o título; a seção 6 vem do upstream. O
  commit/tag está citado; se o upstream mudar, a análise envelhece.
- O código do o2 tem partes "not implemented" (objetos/PvP) e pode entrar
  em panic se receber certos TLVs do cliente clássico; não testei esse
  cenário ao vivo.
- O cliente clássico reenvia sprites/tilemap com mais frequência do que o
  necessário em alguns casos; não medi o impacto real de banda. A
  afirmação de frequência é a do código, não uma medição.
- Timestamps de chaves/cristal/tilemap usam o relógio local; não sei o
  quanto isso afeta partidas com relógios muito diferentes.
- A compatibilidade jurídica com protocolos sem licença não foi avaliada
  por advogado; a recomendação é implementação independente.
- O zelda3 é US; não verifiquei se uma ROM JP/EUR ou uma seed randomizer
  rodando em `--emu-rom` precisaria de ajustes além da tabela de syncables
  (provavelmente sim, principalmente nos ganchos de gráfico/paleta).

## 10. Comandos e arquivos consultados

- Leitura: `AGENTS.md`, `docs/engine/COOP-DESENHO.md`,
  `.agents/refs/alttpo/**` (cliente, servidor, README, faq, o2).
- Upstream: `github.com/alttpo/o2` (v0.4.5, `165a553d`) e
  `github.com/alttpo/o2-server` (`a3e0b24e`), consultados em 09/11/26.
- Código do zelda3: `src/variables.h`, `src/mod/state_table.c`,
  `src/mod/events.h`, `src/mod/draw.*`, `src/mod/lua_save.*`,
  `src/mod/nwa_server.c`, `snes/ppu.*`, `src/messaging.c`,
  `docs/engine/SNI.md`, `docs/engine/MODS.md`, `docs/engine/F4-PLANO.md`.
