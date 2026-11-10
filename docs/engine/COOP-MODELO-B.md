# Co-op Modelo B — dois Links no mesmo mundo (C3a + C3b + C3c)

Implementação das fatias C3a, C3b e C3c do `COOP-DESENHO.md`: **um mundo, dois
Links**, com **pose/CHR próprios do jogador 2**, **câmera compartilhada que se
divide sozinha** quando os dois se afastam, **vida/magia e reaparecimento por
jogador**, **transições disparadas por qualquer um dos dois** e **buffers de
item e desenho de mods por metade**. O porte é o `[Coop] SharedWorld`,
desligado por padrão. Com ele em `0`, `SharedWorld_*` é no-op e o jogo continua
idêntico ao original.

```ini
[Coop]
SharedWorld = 1

[KeyMap2]
Controls = i, m, j, n, u, e, h, y, b, g, 7, 8

[GamepadMap2]
Controls = DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, B, A, Y, X, Lb, Rb
```

`SharedWorld = 1` não combina com `Local = 1` (o programa recusa). A rede da
C1/C2 pode ficar ligada, mas essa combinação não foi testada. O jogador 2 usa
as mesmas teclas/controles da C2.

## O que é "do Link" e o que é "do mundo"

A simulação inteira usa `g_ram` por macro. Para rodar dois Links sem duplicar o
mundo, o módulo guarda **duas cópias apenas das regiões do Link** (128 KB
virtuais cada, mas só os intervalos abaixo são copiados). A cópia do jogador 2
fica fora de `g_ram`; antes do passo dele as regiões são trocadas para a RAM
viva e depois devolvidas. As regiões estão em `src/mod/shared_world.c`
(`kPlayerRegions`).

| Faixa | O que é | Por que é do Link |
|---|---|---|
| `0x0020..0x006E` | posição X/Y/Z, subpixels, velocidades, direção, `link_direction_facing`, `link_player_handler_state`, animação, colisão de tile, capa, bunny, item na mão, espada/escudo em uso, `link_direction_last`, safe-return | é o corpo e o estado de movimento/ataque de cada Link |
| `0x0071..0x0079` | scratch de interação com tile, `index_of_interacting_tile`, spin attack | temporários do passo do Link |
| `0x00A9..0x00AA` | quadrante do Link | muda quando o Link atravessa a sala |
| `0x00EE..0x00EF` | andar/`link_is_on_lower_level`, bits de transição usados pelo Link | o Link pode estar em andar diferente do outro |
| `0x00F0..0x00FB` | `joypad1*_last`, `filtered_joypad_*`, históricos | cada Link tem o seu controle e o seu edge detector |
| `0x0100..0x0109` | `link_dma_*` | seleção de gráficos/animação do Link |
| `0x0114..0x0115` | tile abaixo do Link | colisão do Link |
| `0x0280..0x02B1` | ancillas: `objprio`, `U`, `z_vel`, `z`, `z_subpixel` | projéteis/efeitos pertencem a quem os criou |
| `0x02C6..0x02F8` | recoil, natação, recebimento de item, bunny, timers de espada, `flag_is_link_immobilized`, baú, `link_pose_for_item` | estado de ação do Link |
| `0x0300..0x037F` | `link_item_in_hand`, `link_state_bits`, dash/spin, capa, hookshot, timers, `link_give_damage`, posição/prioridade do OAM | estado de ação e dano do Link |
| `0x0380..0x03C3` | ancillas `K/L/A/B/G`, boomerang/hookshot scratch | ancillas do Link |
| `0x03C4..0x03FC` | ancillas `H/T/R/floor/attr`, flute, sword-up, timers | ancillas e ações do Link |
| `0x0728..0x075A` | ancillas de porta/entulho/22/25/26 | ancillas do Link |
| `0x0AC0..0x0ADB` | fontes de DMA do CHR do Link (corpo, espada, escudo, itens) | cada Link escolhe a própria animação; `0x0ADC..0x0AE3` (tile animado e DMA 9/14) ficam no mundo |
| `0x0BF0..0x0C99` | arrays de ancilla: posição, velocidade, tipo, step, timer, OAM | projéteis do Link |
| `0x0FC2..0x0FC5` | `link_x_coord_prev`, `link_y_coord_prev` | transições e recoil |
| `0xF36D..0xF36E` | `link_health_current`, `link_magic_power` | vida e magia correntes de cada Link |

Ficam **no mundo** (não são trocados): `main_module_index`,
`submodule_index`, sala/área (`dungeon_room_index`, `overworld_screen_index`,
`current_area_of_player`), câmera/scroll (`BG*HOFS/VOFS`), mapa, tilemap,
sprites (`sprite_*`), overlord/garnish, HUD/menu, paletas, música, SRAM,
progresso (`0xF340..`), textos e cutscenes. Também ficam no mundo:

- **rupias, flechas, bombas, chaves** e todo o inventário/progresso;
- **capacidade de vida** (`link_health_capacity`): é progresso, não vida atual;
- **follower/tagalong**: segue o jogador 1 nesta fatia.

Vida e magia **correntes** são de cada jogador (`0xF36D..0xF36E`). O HUD
mostra as duas: a barra do jogador 1 continua no lugar original e a do
jogador 2 é desenhada numa faixa livre do mesmo BG3 (ver "HUD do jogador 2"
abaixo). `--dump-state` continua imprimindo `state p2 ...`.

Antes do passo do jogador 2, `kControlRegions` salva os campos de controle do
mundo (módulo, submódulo, sala, câmera de transição, menu). Se o jogador 2 não
disparou uma transição, esses campos são restaurados: ele não abre menu, não
muda a câmera de transição e não mexe no módulo global. A exceção é uma
transição liderada pelo jogador 2 (abaixo): nesse caso o controle novo é
mantido e o jogador 2 vira o Link vivo da transição.

## Vida, morte e reaparecimento

Cada Link tem sua própria vida e magia. O dano de sprite no jogador 2 não
desce mais a barra do jogador 1, e o uso de magia do jogador 2 só gasta a
magia dele.

- **Jogador 2 morre**: o módulo 18 (game over) do passo dele é cancelado, o
  controle do mundo volta para o jogador 1 e o jogador 2 fica invisível e
  imóvel por **120 frames**. Depois ele reaparece ao lado do jogador 1 com a
  vida de `kHealthAfterDeath` (a mesma fórmula do respawn original), 60 frames
  de invulnerabilidade e a magia que tinha. **Não há game over** enquanto o
  jogador 1 estiver vivo.
- **Jogador 1 morre com o jogador 2 vivo**: a regra é simétrica. O módulo 18
  é cancelado, o jogador 1 fica invisível/imóvel por 120 frames e reaparece ao
  lado do jogador 2 com a vida de respawn. A câmera acompanha o jogador 2
  enquanto o 1 está caído.
- **Os dois morrem**: aí sim o fluxo original de game over roda (o módulo 18
  não é restaurado). Se o jogador 1 cai e o jogador 2 já estava morto, o
  game over é imediato; se o jogador 2 cai com o jogador 1 já morto, o passo
  do jogador 2 deixa o módulo 18 vivo.
- Link morto nunca é escolhido como alvo por inimigos: se só o outro está
  vivo, os helpers de alvo devolvem o outro mesmo que o morto esteja mais
  perto.

Eventos novos para mods/golden: `PlayerDeath` (`a` = 1 ou 2) e `PlayerRespawn`
(`a` = 1 ou 2, `b` = vida depois do respawn). O `LinkDeath` original continua
sendo emitido quando o dano letal acontece. `Player2SpriteDamage`
(`a` = slot, `b` = tipo do sprite, `c` = dano) é emitido quando o dano
pendente aplicado pelo P2 realmente desconta a vida do sprite no passo do
mundo seguinte; é a prova usada pelo replay `p2-sword-hit`.

## Como roda o frame

`ZeldaRunFrameInternal()` roda o jogo uma vez (mundo + jogador 1) e, antes do
NMI, chama `SharedWorld_FrameStep()`:

1. confere morte do jogador 1 (inclusive vinda de uma transição/buraco) e
   aplica os timers de respawn;
2. copia as regiões do jogador 1 para a cópia 0 e as do jogador 2 para `g_ram`;
3. lê o controle 2 com `NMI_ReadJoypads()` (mesmo edge detector do jogo);
4. roda `Link_Main()` do jogador 2 (movimento, colisão, itens, espada, dano);
5. roda `Ancilla_Main()` do jogador 2 (projéteis/efeitos dele);
6. limpa a janela secundária de OAM (`0xe0`), roda `LinkOam_Main()` do jogador
   2 e guarda o OAM real dele; calcula as fontes de DMA do jogador 2 e carrega
   os tiles dele em `Ppu.linkChr` (ver abaixo);
7. faz uma varredura mínima de interação nos 16 sprites ativos:
   `Sprite_CheckDamageToLink()` (inimigo -> jogador 2) e
   `Sprite_CheckDamageFromLink()` (jogador 2 -> inimigo);
8. devolve as regiões do jogador 1 para `g_ram`; se o jogador 2 disparou
   uma transição, ele continua vivo até a transição terminar e o jogador 1
   volta a ser o Link do mundo logo depois (ver "Transições").

O mundo e os sprites andam **uma vez por frame**; só a lógica do Link é
duplicada. O custo é a cópia das regiões do Link duas vezes por frame (ida e
volta do jogador 2), mais o passo extra de `Link_Main`/`Ancilla_Main` e as duas
cópias de 12 KB do buffer 4bpp.

## Interação mínima

- **Inimigos**: cada Link tem seu próprio hitbox de espada/item; a varredura do
  jogador 2 usa o mesmo `Sprite_CheckDamageFromLink()` do jogo. Sprites podem
  acertar qualquer um dos dois.
- **Alvo mais próximo**: `Sprite_IsRightOfLink()`, `Sprite_IsBelowLink()`,
  `Sprite_DirectionToFaceLink()`, `Sprite_ProjectSpeedTowardsLink()` e
  `Sprite_CheckDamageToLink()` usam `SharedWorld_OtherLinkIsTarget()`. A posição
  do outro Link vem da cópia dele, então a escolha é feita sem trocar a RAM
  viva. Empates ficam com o jogador ativo; Link morto não é alvo.
- **Itens do chão**: quem pega é quem executa o `Link_Main()` naquele frame; o
  item/progresso vai para a SRAM compartilhada e desaparece para o outro.
- **Progresso**: é um só (SRAM/`g_ram+0xF000`).
- **Dano**: `link_give_damage`, `link_health_current` e `link_magic_power` são
  de cada Link. Dano no jogador 2 não reduz a barra do jogador 1 e vice-versa.
  Morte e respawn seguem a seção "Vida, morte e reaparecimento".

### IAs de inimigos e alvo mais próximo

As funções centrais da C3a já faziam soldados, octoroks, morcegos, moldorms,
moblins, hardhats, cucos, poes e vários outros perseguirem o Link mais próximo
por `Sprite_ProjectSpeedTowardsLink()`/`Sprite_DirectionToFaceLink()`. A C3c
trocou também as leituras diretas de `link_x_coord`/`link_y_coord` que ainda
escolhiam o alvo por conta própria:

| IA / rotina | O que passou a usar |
|---|---|
| `SoldierThrowing_Common` (soldado que arremessa) | `SharedWorld_LinkPosFor` |
| `Probe` (projétil do soldado) | `SharedWorld_LinkPosFor` |
| `Sprite_D0_Lynel` | `SharedWorld_LinkPosFor` |
| `Sprite_Raven` | `SharedWorld_LinkPosFor` |
| `Stalfos_Skellington` | `SharedWorld_LinkPosFor` |
| `Sprite_4C_Geldman` | `SharedWorld_LinkPosFor` |
| `Sprite_51_ArmosStatue` | `SharedWorld_LinkPosFor` |
| `Sprite_ArmosCrusher` | `SharedWorld_LinkPosFor` |
| `Sprite_95_LaserEyeLeft` | `SharedWorld_LinkPosFor` |
| `Sprite_MadBatterBolt` | `SharedWorld_LinkPosFor` |
| `Bee_Main` | `SharedWorld_LinkPosFor` |
| `Sprite_PinkBall` | `SharedWorld_LinkPosFor` |
| `GenerateIceball` | `SharedWorld_LinkPosFor` |

Continuam lendo o Link vivo de propósito: rotinas que **movem, roubam ou
curam** o Link no passo do mundo (Pikit, Ladrão, Wallmaster, plataforma de
Somaria, canos, pull switches, lojas, diálogos, chefes com cutscene) e as
rotinas de escudo/colisão que precisam da direção do Link ativo. O Octorok
comum não lê as coordenadas: ele anda e cospe na direção em que está virado.
Onde a IA só decide movimento/ataque, ela agora mira o mais próximo; onde ela
altera o estado do Link, a interação continua sendo do Link ativo e o jogador 2
é atendido pela varredura de dano dele.


## Desenho do segundo Link (C3b: pose e CHR próprios)

A VRAM do SNES carrega **um** conjunto de gráficos de Link por frame. Para o
jogador 2 ter a própria animação sem tocar na VRAM do jogo, o `Ppu` ganhou um
**espaço de CHR extra em RAM**: `linkChr[0x400]`, um espelho dos words de VRAM
`0x4000..0x43ff`, e `oamIsPlayer2[128]`, uma marca por sprite de OAM. Nenhum
tile de VRAM é realocado; o jogo continua com o conjunto do jogador 1.

O passo do jogador 2 agora:

- guarda o OAM do jogador 1;
- limpa a janela secundária de OAM (`0xe0`, 28 slots) e roda `LinkOam_Main()`
  com `sort_sprites_setting = 1`, produzindo o corpo, espada, escudo, sombra e
  item do jogador 2;
- roda `NMI_UpdateLinkDmaSources()` para o jogador 2 (as fontes de DMA do Link,
  `0x0AC0..0x0ADB`, passaram a ser regiões "do Link") e
  `NMI_LoadPlayer2LinkChr()`, que copia os mesmos slices que o NMI carregaria
  na VRAM para `Ppu.linkChr`. As fontes de DMA de NPC/follower
  (`dma_source_addr_16..21`, `dma_var6/7`) continuam no mundo e são copiadas
  como estão;
- restaura o OAM do jogador 1, copia os efeitos do jogador 2 para slots livres
  (CHR normal de sprite) e copia as 28 entradas do Link do jogador 2 para
  slots livres, marcando-as em `oamIsPlayer2`;
- o renderizador, ao ver a marca, busca o tile em `linkChr` em vez de `vram`.
  Entradas com tile `>= 0x40` ou banco de sprite 1 (sombras e efeitos comuns)
  continuam usando a VRAM normal.

Assim a pose desenhada, a espada/escudo e os efeitos do jogador 2 são os dele.

### Buffer 4bpp por jogador

`gfx_4bpp_buffer` (`g_ram+0x9000`, 0x3000 bytes) é compartilhado pelos gráficos
de item/animação do Link. Para os dois não sobrescreverem o item um do outro, o
módulo mantém **duas cópias**: a do jogador 1/do mundo e a do jogador 2. Antes
do passo do jogador 2, a cópia 0 recebe o buffer vivo e o buffer vivo recebe a
cópia do jogador 2 (mais os 0x80 bytes do item na mão, `0x2d40..0x2dbf`). O
passo do jogador 2 escreve no buffer vivo; `NMI_LoadPlayer2LinkChr()` copia o
item dele para `Ppu.linkChr`; depois o buffer do jogador 1 é restaurado para o
NMI real. Assim o item do jogador 1 vai para a VRAM normal e o do jogador 2
para o espelho `linkChr`, mesmo no mesmo frame. As cópias são só RAM; nenhum
tile de VRAM é realocado. O CHR comum de sprites (inimigos, efeitos) continua
compartilhado.

### HUD do jogador 2

A vida e a magia do P2 são desenhadas no mesmo tilemap BG3 do HUD, em células
que ficam vazias tanto no HUD original quanto no `ReduxHud`:

| O que | Células (coluna, linha) | Desenho |
|---|---|---|
| corações do P2 | 13..24 nas linhas 4 e 3 (12 por linha) | corações vazios/meio/cheios com a paleta 0 (laranja/dourada), diferente dos corações vermelhos do P1; a linha de baixo enche primeiro e a de cima recebe o excedente, até 20 corações |
| magia do P2 | 27..30 na linha 4 | quatro segmentos cheios/vazios, ~32 unidades de magia cada, com os mesmos tiles da barra horizontal |

As colunas 25..26 das linhas 3..4 são do indicador de andar da masmorra e não
são tocadas. A faixa existe nas duas metades quando a tela divide (BG3 é fixo
nas duas), então o HUD do P2 aparece junto e dividido sem precisar de um
tilemap por metade. `SharedWorld_UpdateHud()` roda depois do passo do mundo e
do P2 e antes do NMI; com `SharedWorld = 0` ela não escreve nada, e menus,
transições e cutscenes continuam com o HUD original. O inventário, rupias,
flechas, bombas e chaves continuam compartilhados; não há contadores
separados por jogador.


## Câmera compartilhada e tela dividida automática

Com os dois Links no mesmo mundo, a câmera deixa de ser a do jogador 1:

- **Juntos**: antes do passo do mundo, a câmera vai para o ponto médio dos dois
  Links, deslocado para deixá-los no centro (`mid_x - 120`, `mid_y - 100`), e é
  limitada aos mesmos limites de rolagem do jogo: `room_bounds_x/y` do
  quadrante atual dentro de masmorras, `ow_scroll_vars0` na área do overworld.
  Os limites `camera_*_coord_scroll_low/hi` são deslocados junto para a lógica
  de câmera do jogo continuar coerente.
- **Afastados**: a tela se divide sozinha em **duas metades horizontais**, cada
  uma com 256x224. A metade 1 centraliza o jogador 1; a metade 2 centraliza o
  jogador 2; as duas usam os mesmos limites de rolagem. O mesmo estado é
  renderizado duas vezes: BG1/BG2, OAM e (se for o caso) mode 7 são deslocados
  pela diferença entre a câmera da simulação e a câmera da metade. BG3/HUD fica
  fixo nas duas metades.

### Regra exata (distância, histerese, masmorra x overworld)

A decisão usa a posição dos dois Links e a câmera de ponto médio já limitada:

| | Masmorra | Overworld |
|---|---|---|
| Separar quando `|dx|` > | 192 px | 208 px |
| Separar quando `|dy|` > | 144 px | 152 px |
| Juntar quando `|dx|` < | 128 px | 144 px |
| Juntar quando `|dy|` < | 96 px | 104 px |

Além disso, a tela separa se qualquer corpo (16x24 px) não couber na vista com
8 px de margem sob a câmera de ponto médio, e só junta quando os dois corpos
couberem com 40 px de margem horizontal e 32 px vertical. Isso cobre o caso em
que a câmera bate na parede e um Link sai da tela antes da distância-limite.
Entre o limiar de separar e o de juntar vale o estado anterior (histerese).
Numa sala que cabe inteira numa tela (limites de câmera iguais nos dois eixos)
nunca há divisão, porque as duas metades mostrariam a mesma coisa.

Os números de masmorra são um pouco menores porque as salas são pequenas e a
câmera bate nas paredes; no overworld a folga é maior para não dividir a tela a
cada obstáculo.

A divisão é sempre horizontal, mesmo quando a separação é vertical (as duas
metades ainda mostram scrolls verticais diferentes). A janela e a textura
crescem para 512x224 quando divide e voltam a 256x224 quando junta.

## Transições

A regra da C3c é **quem passa leva o outro**:

- **Jogador 1 e jogador 2 podem disparar** portas, escadas, buracos e bordas
  de tela/área. Se o jogador 1 dispara, o fluxo original continua e o jogador 2
  é colocado ao lado dele quando a sala/área termina de carregar.
- **Jogador 2 dispara**: quando o passo dele muda `main_module_index`,
  `submodule_index`, `player_is_indoors` ou a sala/área (`CurrentRoomId()`), e
  o módulo novo é de transição (5, 7, 8, 9, 11, 15, 17 ou 25), o controle do
  mundo **não** é restaurado. O jogador 2 vira o Link vivo da transição e o
  jogador 1 fica guardado na cópia dele. Assim que o jogo volta ao gameplay
  normal (`shared_world.c: FinishPlayer2Leader()`), o jogador 1 volta a ser o
  Link do mundo e é colocado ao lado do jogador 2 na sala/andar novos. Os dois
  terminam juntos, com a vida/magia de cada um preservada.
- O posicionamento tenta 24 px à direita, depois 24 px à esquerda; se os dois
  lados estiverem em parede/fora dos limites da sala, o Link é colocado na
  mesma posição do outro. Isso evita o jogador 2 nascer preso dentro de parede
  em salas pequenas (e é o que permite ele mesmo atravessar a porta/escada).
- **Continuam só do jogador 1**: menus, mapas, diálogos, save/load, cutscenes e
  o game over. Se o passo do jogador 2 abrir um menu (módulo 14), o controle do
  mundo é restaurado e a transição é descartada.
- A divisão de tela é desfeita em qualquer transição; ela volta a ser calculada
  quando os dois estão de novo em gameplay normal.

## Depuração e testes

`--dump-state` com `SharedWorld = 1` imprime `state p1 ...` e `state p2 ...`
(as variáveis do mundo aparecem iguais nos dois, porque são lidas da RAM viva).
`--input-script2` funciona no headless e é liberado com `SharedWorld = 1`.

```sh
# dois Links andando na mesma sala (snapshot da C1)
./zelda3 --config shared.ini --headless --no-mods \
  --load "saves/ref/Chapter 1 - Zelda's Rescue.sav" \
  --input-script p1_right.txt --input-script2 p2_left.txt \
  --frames 120 --dump-state --screenshot dois-links.ppm zelda3.sfc

# transição de sala: o jogador 2 termina na mesma tela/andar do jogador 1
./zelda3 --config shared.ini --headless --no-mods \
  --replay tests/replays/issue243.sav --input-script2 p2_parado.txt \
  --frames 5000 --dump-state zelda3.sfc
```

O caso `tests/replays-features/shared-world-two-links.sav` usa
`cannon-trooper.sav` (mesmos 450 frames) com um `.script2` que repete os
movimentos do replay no controle 2. O golden `tests/golden-features/` confere
`compare: off`, os eventos de dano/drop e o hash final. O `.ini` do caso liga
`SharedWorld = 1`.

O caso `tests/replays-features/camera-split.sav` é o teste da C3b: parte do
replay `issue242.sav`, para no frame 250 e continua com o jogador 1 andando
para a direita. O `.script2` leva o jogador 2 para a direita até o frame 251 e
depois para a esquerda/cima. O golden mostra a divisão e a junção:

```
compare: off
ev 152 CameraSplit 1 209 33
ev 367 CameraSplit 0 143 90
final 550 ...
```

`CameraSplit 1/0` é o evento novo `kModEv_CameraSplit` (`a` = 1 dividiu, 0
juntou; `b`/`c` = `|dx|`/`|dy|`).

```sh
# câmera no meio com os dois perto, depois dividida, depois junta de novo
# p1_down_up.txt:
#   1 Down
#   201 Up
#   800 -
./zelda3 --config shared.ini --headless --no-mods \
  --load "saves/ref/Chapter 2 - After Eastern Palace.sav" \
  --input-script p1_down_up.txt --input-script2 p2_parado.txt \
  --frames 200 --dump-state --screenshot divide.ppm zelda3.sfc

# replay de feature (golden tests/golden-features/camera-split.txt)
./zelda3 --config tests/replays-features/camera-split.ini --headless \
  --no-mods --replay tests/replays-features/camera-split.sav \
  --input-script2 tests/replays-features/camera-split.script2 \
  --frames 550 --mod-trace zelda3.sfc
```

### C3c — vida/respawn, transições pelo P2, itens e FrameDraw

O caso `tests/replays-features/respawn-p2.sav` é uma cópia de
`cannon-trooper.sav`. O mod de teste
`tests/replays-features/mods/p2-low-health` baixa a vida uma vez no frame 2,
antes do primeiro seed do jogador 2; assim os dois começam com um coração e o
`.script2` leva o P2 para a esquerda, na frente do canhão. O golden mostra o
dano, a morte só do P2 e o reaparecimento 120 frames depois:

```
compare: off
ev 54 LinkDamage 4 8 0
ev 114 LinkDamage 4 4 0
ev 114 LinkDeath 0 0 0
ev 114 PlayerDeath 2 0 0
ev 166 LinkDamage 4 8 0
ev 234 PlayerRespawn 2 24 0
final 450 ...
```

```sh
./zelda3 --config tests/replays-features/respawn-p2.ini --headless \
  --mods-dir <pasta-temporaria-com-p2-low-health> \
  --replay tests/replays-features/respawn-p2.sav \
  --input-script2 tests/replays-features/respawn-p2.script2 \
  --frames 450 --mod-trace zelda3.sfc
```

O `tools/regress.sh` já copia o mod para uma pasta temporária e roda esse caso.

Transição liderada pelo P2 (o jogador 1 fica parado; o P2 sobe a escada da
sala inicial e troca a sala 18 -> 2; o jogador 1 vai junto):

```sh
# p1_parado.txt: 1 -
# p2_sobe.txt:   1 Up
#                500 -
./zelda3 --config shared.ini --headless --no-mods \
  --load "saves/ref/Chapter 1 - Zelda's Rescue.sav" \
  --input-script p1_parado.txt --input-script2 p2_sobe.txt \
  --frames 60 --dump-state zelda3.sfc
# estado: os dois terminam com dungeon_room_index 2; o log de depuração mostra
# "player 2 leads transition" e "player 2 transition done in room 00010002".
```

Vida do jogador 1 separada: no mesmo save, `p1_desce_longo.txt` (`1 Down`,
`2500 -`) faz o P1 cair no buraco/borda e chegar ao módulo 18. Com o P2 vivo,
o log mostra `player 1 died` e `player 1 respawns` 120 frames depois, sem game
over; o jogador 2 continua com a vida dele.

O buffer de item por jogador é exercitado pelo mesmo caminho do `linkChr`:
`shared world: frame N item buffers differ p1=... p2=...` aparece no log de
depuração (`ZELDA3_SHARED_WORLD_DEBUG=1`) quando os dois buffers de item não
são idênticos. Para conferir visualmente, use o replay de canhão com
`--screenshot`.

`FrameDraw` na tela dividida: o evento roda **uma vez por metade**, com
`ev.a` = índice da metade (0 esquerda, 1 direita) e `ev.b` = 2. As coordenadas
de `zelda.draw` são locais à metade (0..255) e `zelda.draw.width()` devolve 256
em cada uma. Sem divisão, o evento continua único (`ev.a = 0`, `ev.b = 1`).
Exemplo com o HUD de exemplo:

```sh
./zelda3 --config tests/replays-features/camera-split.ini --headless \
  --mods-dir <pasta-com-lua-hud> \
  --replay tests/replays-features/camera-split.sav \
  --input-script2 tests/replays-features/camera-split.script2 \
  --frames 200 --screenshot hud-dividido.ppm zelda3.sfc
```

### C3d — HUD do P2 e provas que faltavam

O caso `p2-sword-hit.sav` é uma cópia curta do `sword-beam-hit` com o P1
parado. O `.script2` move o P2 e golpeia; o mod de teste move o inimigo já
carregado para o alcance. O golden:

```
compare: off
ev 40 Player2SpriteDamage 13 34 4
final 60 ...
```

```sh
./zelda3 --config tests/replays-features/p2-sword-hit.ini --headless \
  --mods-dir <pasta-temporaria-com-p2-sword-target> \
  --replay tests/replays-features/p2-sword-hit.sav \
  --input-script2 tests/replays-features/p2-sword-hit.script2 \
  --frames 60 --mod-trace zelda3.sfc
```

O caso `p2-two-items.sav` usa o mesmo snapshot. O mod `p2-two-items` deixa o
P2 nascer com o bumerangue selecionado e troca o item do P1 para o arco; os
dois apertam Y no frame 20. O golden só guarda `compare: off` e o hash final,
porque não há evento de uso de item; a prova visual é o screenshot do frame
24.

```sh
./zelda3 --config tests/replays-features/p2-two-items.ini --headless \
  --mods-dir <pasta-temporaria-com-p2-two-items> \
  --replay tests/replays-features/p2-two-items.sav \
  --input-script2 tests/replays-features/p2-two-items.script2 \
  --frames 60 --mod-trace zelda3.sfc
```

Para os screenshots do HUD do P2:

```sh
# junto: P1 e P2 na mesma tela
./zelda3 --config tests/replays-features/p2-sword-hit.ini --headless \
  --mods-dir <pasta-temporaria-com-p2-sword-target> \
  --replay tests/replays-features/p2-sword-hit.sav \
  --input-script2 tests/replays-features/p2-sword-hit.script2 \
  --frames 24 --screenshot c3d-hud-p2-junto.ppm zelda3.sfc

# dividido: cada metade com a faixa do P2
./zelda3 --config tests/replays-features/camera-split.ini --headless \
  --no-mods --replay tests/replays-features/camera-split.sav \
  --input-script2 tests/replays-features/camera-split.script2 \
  --frames 200 --screenshot c3d-hud-p2-dividido.ppm zelda3.sfc
```

### O que foi verificado nesta fatia

- `make -j2` e `make -j2 NO_LUA=1` sem warnings.
- `tools/regress.sh` verde (todos os replays e os cinco casos de SharedWorld:
  `shared-world-two-links`, `camera-split`, `respawn-p2`, `p2-sword-hit` e
  `p2-two-items`).
- Dois Links na mesma sala, com `--dump-state` mostrando posições e animações
  diferentes para `p1` e `p2` e screenshot em `.agents/shots/`.
- Câmera compartilhada: no caso `camera-split`, os dois ficam perto (câmera no
  ponto médio), se afastam (a tela divide no frame 152) e voltam (junta no
  frame 367). Screenshots de 256x224 (junto) e 512x224 (dividido) em
  `.agents/shots/`.
- Pose própria: no `issue242` frame 180, `state p1 link_dma_graphics_index` =
  352 e `state p2 ...` = 400, com `link_animation_steps` diferentes; o corpo do
  jogador 2 é desenhado com os tiles de `Ppu.linkChr`.
- **Jogador 2 acertando inimigo de verdade**: o replay `p2-sword-hit` usa o
  snapshot do `sword-beam-hit`, com um Hopping Bulb Plant (slot 13, tipo 0x22)
  já carregado. O mod de teste `mods/p2-sword-target` coloca o inimigo ao
  alcance do P1 e o `.script2` leva o P2 até ele e golpeia. O log mostra
  `player 2 damaged sprite 13 type 22 hp 8 -> 4` e o golden tem
  `ev 40 Player2SpriteDamage 13 34 4`; o golpe é de espada normal, não de
  sword beam. É o mesmo caminho de `Sprite_CheckDamageFromLink()` usado pelo
  P1, agora com a vida do inimigo caindo de fato.
- **HUD do jogador 2, junto**: screenshot com a linha de corações laranja do
  P2 e a barra de magia de quatro segmentos ao lado da barra vermelha do P1,
  em `.agents/shots/c3d-hud-p2-junto.png`.
- **HUD do jogador 2, dividido**: no `camera-split` frame 200, a faixa do P2
  aparece nas duas metades de 256 px (saída 512x224), em
  `.agents/shots/c3d-hud-p2-dividido.png`.
- **HUD do jogador 2 com ReduxHud**: com `ReduxHud` ligado, os corações do P2
  ficam abaixo da barra horizontal de magia e os quatro segmentos de magia
  ficam na linha livre abaixo da caixa de item; o HUD do P1 e a caixa de item
  não mudam. Screenshot em `.agents/shots/c3d-hud-p2-redux.png`.
- **Itens diferentes no mesmo frame**: o replay `p2-two-items` seleciona
  bumerangue para o P2 antes do seed e arco para o P1 depois; os dois apertam Y
  no frame 20. No frame 24, `state p1 link_dma_graphics_index` e
  `state p2 ...` diferem (234 e 330 no teste) e o screenshot
  `.agents/shots/c3d-dois-itens.png` mostra o arco no Link 1 (à esquerda) e o
  bumerangue no Link 2 (à direita).
- Jogador 2 levando dano: no replay `f4e-linkdamage` com o jogador 2 à
  esquerda do jogador 1, o log `player 2 takes 4 damage` apareceu duas vezes e
  o estado `p2 link_incapacitated_timer`/vida mudou.
- **C3c, vida separada**: no replay `respawn-p2`, a vida do P1 ficou em 8
  enquanto o P2 caiu de 8 para 4, depois para 0; o P2 ficou 120 frames morto e
  reapareceu com 24. Não houve game over. O golden tem `PlayerDeath 2` e
  `PlayerRespawn 2`.
- **C3c, P1 morrendo com P2 vivo**: no `Chapter 1` com P1 descendo até cair, o
  log mostrou `player 1 died` e `player 1 respawns` 120 frames depois; o jogo
  voltou ao módulo 7/9 normal e o P2 continuou vivo.
- **C3c, transição pelo P2**: no `Chapter 1`, com o P1 parado e o P2 subindo, o
  log mostrou `player 2 leads transition` e `player 2 transition done in room
  00010002`; os dois terminaram com `dungeon_room_index` 2. A transição de
  ida/volta pelo P1 continua funcionando (`issue243`, `shared-world-two-links`).
- **C3c, alvo mais próximo**: o `p2left` no replay de canhão leva o P2 para
  perto do soldado/canhão; o log `player 2 takes 4 damage` nos frames 53/113
  mostra que o inimigo mirou o P2 quando ele era o Link mais próximo (o P1
  estava mais longe).
- **C3c, buffer por jogador**: o módulo mantém uma cópia 0x3000 por jogador,
  captura o item do P2 para `linkChr` antes de restaurar a cópia do P1 e deixa
  o NMI real com o buffer do P1. O log de depuração imprime `item buffers
  differ` quando os dois slices de item não são iguais; screenshots com os dois
  usando ações/itens diferentes ficam em `.agents/shots/c3c-items.ppm`.
- **C3c, FrameDraw por metade**: com o `lua-hud` e a tela dividida, o HUD
  apareceu nas duas metades de 256 px; screenshot
  `.agents/shots/c3c-framedraw-split.ppm` (512x224), com `ev.a` 0/1 e
  `ev.b` = 2.
- Com `SharedWorld = 0`, o regress normal continua com `compare: on` e os
  hashes intactos.
- Janela testada no Xvfb: o modo SharedWorld abre, desenha e a janela cresce
  para 512x224 quando a divisão liga (teste com divisão forçada) e volta a
  256x224 quando desliga.

## O que ainda não funciona / próximos passos

- Menus, mapas, diálogos, save/load e cutscenes continuam sendo só do jogador
  1; quando ele faz isso, o jogador 2 vai junto e a tela volta a juntar.
- A divisão é sempre horizontal; não há layout vertical automático como o
  `LocalLayout = Vertical` da C2.
- A transição liderada pelo jogador 2 cobre portas/escadas/bordas/buracos nos
  módulos 5, 7, 8, 9, 11, 15, 17 e 25. Transições especiais que mudem o estado
  do mundo por outro caminho (por exemplo um chefe que troca de sala) não
  foram testadas com o P2 liderando.
- Inimigos miram o mais próximo nas funções centrais e nas IAs listadas acima;
  rotinas que alteram o Link vivo (roubo, arrasto, diálogo, curas, chefes com
  cutscene) continuam interagindo só com o Link ativo do passo. Um ataque
  especial dessas rotinas pode não acertar o jogador 2; o dano de contato e de
  projétil comum é coberto pela varredura do P2.
- Follower/tagalong continua único e do jogador 1.
- Os dois buffers 4bpp são cópias em RAM; o CHR comum de sprites (inimigos,
  efeitos) continua compartilhado. Itens que dependam de tiles fora de
  `linkChr` e do conjunto comum podem não ter um CHR próprio por jogador.
- Não há sincronização de rede do Modelo B; a C1 continua sendo o modo de rede.
- `--emu-rom`, `--oracle-rom` e NWA não foram testados com `SharedWorld = 1`.
  A comparação lado a lado fica desligada por definição quando ele está ativo.
