# Descrição do frame (R-1), plugin de renderização (R-2), HD tiles (R-3) e voxel (R-4/R-5)

Implementação das etapas **R-1** e **R-2** do `RENDER-DESENHO.md`: o PPU pode
descrever um frame inteiro (estado, mudanças por linha, sprites e camada
semântica) sem que a simulação leia nada disso, e renderizadores alternativos
podem ser carregados como **plugins nativos** por uma API versionada. O
renderizador de referência também sabe desenhar **a partir da descrição**, e um
modo de checagem compara os dois pixel a pixel — inclusive na tela dividida.

A R-5 completa o plugin voxel com câmera livre, sombras/iluminação simples,
perfis por hack e o acabamento das limitações da R-4. A simulação e o oráculo
continuam iguais: a captura é passiva e, quando desligada, `ZeldaDrawPpuFrame`
só testa uma flag. A checagem restaura PPU/DMA/`irq_flag` depois de desenhar,
então replays e hashes continuam iguais. Sem `RenderPlugin` configurado, o
caminho de desenho é o mesmo de antes da R-2.

## Arquivos

| Arquivo | Papel |
|---|---|
| `src/mod/frame_desc.h` | estrutura pública e versionada da descrição (`kFrameDescVersion`) |
| `src/mod/frame_desc_capture.h` | ganchos internos de captura (o único header que menciona `Ppu`) |
| `src/mod/frame_desc.c` | captura, renderizador a partir da descrição, checagem de pixels, donos de OAM |
| `src/mod/frame_desc_dump.c` | dump JSON + PNGs da camada semântica |
| `src/mod/render_plugin.h` | **API pública do plugin** (C puro, sem SDL/PPU interno) |
| `src/mod/render_plugin.c` | carga `dlopen`/`LoadLibrary`, checagem de versão, host e ciclo de vida |
| `src/mod/render_plugin_ref.c` | renderizador de referência compartilhado pelo `builtin:ppu` e pelo plugin de arquivo |
| `src/mod/hd_tile_hash.h` | hash canônico do conteúdo de um tile 2bpp/4bpp, usado pelo plugin e pelo dump |
| `plugins/ppu-ref/` | plugin de exemplo com Makefile próprio (`ppu-ref.so` não vai para o git) |
| `plugins/hd-tiles/` | plugin HD (R-3), pack de exemplo original e Makefile próprio |
| `plugins/voxel/` | plugin voxel (R-4/R-5), perfil padrão, perfis de exemplo e Makefile próprio |
| `src/mod/frame_desc_dump.c` | dump JSON/PNG da camada semântica e folha de tiles visíveis |
| `src/zelda_rtl.c` | captura montada em `ZeldaDrawPpuFrame`; HDMA e split da linha 128 registrados |
| `src/mod/shared_world.c` | seleciona a descrição de cada painel da tela dividida |
| `src/main.c` | opções `--frame-*`, `--render-plugin`, `--frame-check` e resumo |
| `src/config.c/.h` | `[Graphics] FrameDescription` e `[Graphics] RenderPlugin` (desligados por padrão) |
| `src/sprite.c`, `src/ancilla.c` | dicas de dono de OAM (só leitura, só quando a sonda está ligada) |

## Estrutura

`FrameDesc` (versão 3) contém:

- `ppu`: estado no começo do frame — modo, brilho, force blank, BGs (tilemap,
  tiles, tamanho, rolagem), telas principal/secundária, janelas, mosaico,
  matemática de cor, `extraLeftRight`/`extra*Cur`, modo 7 (matriz, flips,
  centro, perspectiva) e os latches de escrita (`scrollPrev`, ponteiros de
  VRAM/CGRAM/OAM).
- `display`: onde a descrição cai na imagem — `scale` (1 ou 4), índice e total
  de painéis, `pane_x`/`pane_y` (pixels SNES lógicos, antes da escala), tamanho
  da saída e o espaço lateral efetivo (`extra_left_right`, `extra*_cur`).
- `changes[]`: cada escrita de registrador do PPU com `line`, `reg`, `value` e
  origem (`HDMA6`, `HDMA7`, `IRQ`). No JSON, `source` é `0` = HDMA6,
  `1` = HDMA7 e `2` = IRQ. HDMA executado depois da linha N entra na
  linha N+1; as quatro escritas de `BG3HOFS/VOFS` do split da linha 128 entram
  na linha 128. A ordem da lista é a ordem de aplicação.
- `vram`, `cgram`, `link_chr`, `oam_is_player2`: **referências** para os arrays
  vivos do `Ppu` (sem cópia). São válidas até a próxima escrita no PPU. A
  prioridade/paleta por tile de BG vem do tilemap em `vram`; a descrição não
  duplica o tilemap.
- `oam`: ao contrário das outras referências, aponta para `oam_snapshot`, uma
  cópia feita na captura. A tela dividida reescreve a OAM entre os dois
  painéis; sem a cópia o primeiro painel veria os sprites do segundo.
- `sprites[]`: OAM decodificada (x/y, tile, paleta, prioridade, tamanho, flip),
  dono quando conhecido (Link / slot de sprite com `sprite_type` / slot de
  ancilla com `ancilla_type`), andar (`floor`) e `z` (`link_z_coord`,
  `sprite_z`, `ancilla_z`).
  No JSON, `owner_kind` é `0` = desconhecido, `1` = Link, `2` = sprite e
  `3` = ancilla; `owner_index` é o slot e `owner_type` o tipo do jogo.
- `semantics`: sala/área/andar, posição do Link e da câmera, e duas camadas de
  tiles 8×8 (BG1/BG2) com:
  - `attr`: atributo de colisão (mesma tabela que o jogo usa);
  - `raw`: byte bruto da masmorra ou palavra Map16 do overworld;
  - `valid`: se o tile cai na área realmente visível (respeita `extra*Cur`).

### Camada semântica

- **Masmorras**: BG1 usa `dung_bg1_attr_table` (andar de cima) e BG2 usa
  `dung_bg2_attr_table` (andar de baixo), indexadas como `GetTileAttribute`
  (`(x & 0x1f8) >> 3 + (y & 0x1f8) << 3`). A origem é a coordenada de tile
  relativa à sala (0..63).
- **Overworld**: BG2 usa `overworld_tileattr` (Map16) e a colisão é derivada
  com `Overworld_GetTileAttributeAtLocation`, exatamente como o jogo faz. A
  origem é a coordenada de tile do mundo. BG1 é o mapa de **overlay** separado
  (`dung_bg1`: árvores, pontes, chuva, etc.) e agora tem a sua própria camada
  (`source = 4`, `OverworldBg1`): `raw` vem do Map16 do overlay e `attr` da
  tabela `kSomeTileAttr`. A altura de BG1 não é mais emprestada do terreno de
  BG2 na mesma célula.
- **Rolagem por linha**: a captura dos atributos consulta
  `FrameDesc_GetBgScrollAtLine` a cada faixa de 8 linhas. Assim, áreas com
  HDMA de rolagem (por exemplo o *warp* do espelho, que mexe em BG1HOFS/VOFS
  por linha) ficam com o relevo alinhado ao tile desenhado, em vez de usar só a
  rolagem inicial do frame.
- **Modo 7** (mapa-múndi): `semantics.mode7 = true` e as camadas semânticas
  ficam vazias. O estado do modo 7 está descrito, mas o mapeamento
  matriz→tile→atributo não foi feito na R-1.
- **BG3/HUD** não tem camada semântica (não é chão/colisão).

### O que ainda não tem dono/semântica

- Dono de sprite é **melhor esforço**: a base de OAM é anotada em
  `Sprite_TimersAndOam` e em `Ancilla_AllocateOamFromRegion_A_or_D_or_F`.
  Sprites que realocam OAM no meio do desenho (por exemplo chefes com partes
  extras) podem ficar sem dono ou com um intervalo menor que o real. O Link usa
  o bloco fixo de `sort_sprites_offset_into_oam_buffer`.
- Sprites de garnish/effeitos que não passam por esses caminhos aparecem sem
  dono, mas com tile/paleta/prioridade/z quando o `z` é conhecido.
- O modo 7 não tem atributo por tile.
- A descrição referencia VRAM/CGRAM (e `link_chr`/`oam_is_player2`); um
  consumidor que queira guardar esses arrays depois de `present` precisa
  copiá-los. A OAM já é copiada por painel.

### Tela dividida (SharedWorld)

`SharedWorld_DrawFrame` continua desenhando os dois painéis com câmera, scroll
e OAM próprios. Para o plugin, cada painel vira uma `FrameDesc` completa com
`display.pane_index` (0/1), `display.pane_count = 2` e `display.pane_x` na
posição horizontal do painel (pixels SNES lógicos). O host emite um
`begin_frame` com `pane_count = 2`, dois `frame_description` e um único
`present`; o renderizador de referência desenha cada descrição no offset do
painel usando `display.scale`.

A checagem de pixels passou a incluir os frames divididos: ela compara os dois
painéis contra o desenho normal, em vez de pulá-los. A OAM é copiada para a
descrição justamente porque `SharedWorld_DrawFrame` a reescreve entre os
painéis.

## Plugin de renderização (R-2)

A API pública fica em `src/mod/render_plugin.h` e inclui `frame_desc.h`; o
plugin é C puro, não vê `Ppu`, SDL nem structs internas. O ciclo de vida é:

```c
ZeldaRenderPlugin *zelda3_render_plugin_get(uint32_t api_version);

init(plugin, host, kRenderPluginApiVersion)
begin_frame(plugin, &frame_info)
frame_description(plugin, desc)   // uma vez por painel
present(plugin, &target)         // desenha todos os painéis do frame
shutdown(plugin)
```

O host expõe `log()` e `render_frame_desc()`. O segundo é o mesmo caminho de
`FrameDesc_Render`, útil para um plugin que queira começar do visual original;
um plugin de voxel/HD pode ignorá-lo e desenhar só a partir da descrição.

Versões: `kRenderPluginApiVersion` (agora **3**) cobre assinaturas/layout da
API; `kFrameDescVersion` (agora **3**) cobre a descrição. A versão 3 acrescenta
o callback opcional `command` (câmera/sombras/iluminação) e a camada semântica
de BG1 do overworld. A carga recusa a biblioteca, com mensagem clara e sem
derrubar o jogo, quando qualquer uma não bate, quando falta o símbolo
`zelda3_render_plugin_get` ou quando `init()` retorna false. Nesse caso o
renderizador embutido continua sendo usado.

Ponteiros: `host` vale a vida toda do plugin; `frame_info` e `target` só durante
a chamada; `desc` (e VRAM/CGRAM referenciadas) vale de `frame_description` até
o próximo `begin_frame`/`shutdown`. O plugin copia o que quiser guardar. A
descrição é somente leitura para o plugin.

Overlays de mods (`zelda.draw`) **não** entram na `FrameDesc`: eles são
comandos de UI em coordenadas locais do painel, gerados pelo evento
`FrameDraw`, e o host os compõe depois de `present`. Assim o `builtin:ppu`, o
plugin de arquivo e o caminho sem plugin recebem o mesmo texto/alfa sem
reimplementar a fonte; a R-5 expôs os controles interativos pelo callback
opcional `command` do plugin, sem colocar UI na descrição.

## Como ligar

Configuração (desligada por padrão; captura passiva, sem checagem):

```ini
[Graphics]
FrameDescription = 1
# plugin nativo opcional; "builtin:ppu" seleciona o renderizador de referência
RenderPlugin = plugins/ppu-ref/ppu-ref.so
```

Linha de comando (os modos de depuração exigem `--headless`; o
`--render-plugin` também funciona na janela normal):

```sh
# compila o plugin de exemplo (o .so/.dll não é versionado)
make -C plugins/ppu-ref

# checa todos os frames: renderiza dos dois jeitos e compara pixel a pixel
./zelda3 --config tests/regress.ini --headless --no-mods \
  --replay-ref 2 --frames 1000 --frame-check zelda3.sfc

# a mesma checagem com o plugin carregado de arquivo
./zelda3 --config tests/regress.ini --headless --no-mods \
  --render-plugin plugins/ppu-ref/ppu-ref.so \
  --replay-ref 2 --frames 1000 --frame-check zelda3.sfc

# o renderizador de referência embutido, pela mesma interface
./zelda3 ... --render-plugin builtin:ppu --frame-check zelda3.sfc

# desenha cada frame com captura (mede o custo da captura)
./zelda3 ... --frames 5000 --frame-desc zelda3.sfc

# desenha cada frame sem captura (linha de base para a medição)
./zelda3 ... --frames 5000 --frame-draw zelda3.sfc

# dump de um frame em .agents/shots/
./zelda3 ... --frames 600 --frame-desc-dump 600 zelda3.sfc
```

O plugin de arquivo vive em `plugins/ppu-ref/ppu_ref.c`; ele inclui a mesma
implementação de `src/mod/render_plugin_ref.c` que o `builtin:ppu` usa, então
os dois não divergem. O Makefile do plugin gera `ppu-ref.so` no Linux/macOS e
`ppu-ref.dll` no Windows (via MSYS), ignorados pelo git.

O dump escreve `frame-desc-<frame>.json`, `...-bg1.png` e `...-bg2.png`
(só as camadas presentes). `.agents/` fica fora do git.

## Prova de pixels

O renderizador da descrição:

1. cria um `Ppu` de rascunho;
2. copia o estado inicial e as referências de VRAM/CGRAM/OAM;
3. chama `PpuBeginDrawing` com os mesmos `render_flags`;
4. aplica cada `change` antes de `ppu_runLine(line)`, na mesma ordem do frame;
5. usa o próprio `ppu_runLine`/`PpuDrawWholeLine`/`PpuDrawMode7Upsampled`.

A checagem sem plugin:

1. salva `Ppu`, `Dma` e `irq_flag`;
2. desenha o frame com `ZeldaDrawPpuFrame` (capturando a descrição) ou com
   `SharedWorld_DrawFrame` nos frames divididos (uma descrição por painel);
3. restaura o estado salvo (a simulação não vê o desenho);
4. desenha cada descrição em outro buffer e compara os 32 bits de cada pixel,
   painel por painel.

Com `--render-plugin`, o passo 4 vira: `begin_frame` + `frame_description`
por painel + `present` do plugin, e a comparação é contra o desenho normal. O
plugin de arquivo `ppu-ref` chama `host->render_frame_desc()`, que é o próprio
`FrameDesc_Render`; assim a prova cobre carga, versão, ciclo de vida e a
descrição, sem duplicar o renderizador.

Resultado da R-1 (máquina de desenvolvimento, `0` pixels diferentes):

| Configuração | Replays | Frames checados | Pixels | Diferenças |
|---|---|---:|---:|---:|
| 224, renderizador antigo (`tests/regress.ini`), spot | 3 | 19.820 | 1.136.558.080 | 0 |
| 224, renderizador novo + mode7 4×4 | 19 | 1.244.034 | 87.100.317.696 | 0 |
| 240, renderizador novo + mode7 4×4 | 19 | 1.244.034 | 93.321.768.960 | 0 |
| 224, widescreen 16:9, renderizador novo + mode7 4×4 | 19 | 1.244.034 | 135.413.775.168 | 0 |
| Configuração própria de cada replay de feature (224, majoritariamente renderizador antigo) | 16 | 9.757 | 575.409.408 | 0 |
| Features com o perfil de 240 linhas | 16 | 9.757 | 599.470.080 | 0 |
| Features com o perfil widescreen 16:9 | 16 | 9.757 | 869.856.064 | 0 |

O spot do renderizador antigo usou `cannon-trooper`, `issue242` e `issue243`
(este com 961 frames de modo 7 no caminho antigo). O perfil de 240 e o
widescreen também foram checados juntos num spot de 2.000 frames de
`issue243` (modo 7 4×4), com 0 diferenças.

Resultado da R-2 (plugin de arquivo carregado por `dlopen`, `kFrameDescVersion`
2, renderizador novo + mode7 4×4, 224 linhas):

| Configuração | Replays | Frames | Pixels | Diferenças |
|---|---|---:|---:|---:|
| `--render-plugin plugins/ppu-ref/ppu-ref.so --frame-check` | 19 | 1.244.034 | 87.100.317.696 | 0 |

Os 20 replays de feature também passaram pelo plugin com `--frame-check`
(10.387 frames, 624.725.248 pixels, incluindo `camera-split` e
`shared-world-two-links`), 0 diferenças; os detalhes estão na seção de tela
dividida. O spot de 240 e o widescreen 16:9
foram repetidos com o plugin de arquivo em `issue243` (2.000 frames, modo 7
4×4) e também fecharam com 0 diferenças. No perfil de 224, 18.410 frames de
modo 7 (18.410 no caminho 4×4) passaram pelo plugin.

Um plugin de teste com `api_version = 99` foi recusado na carga
(`plugin API version 99, host expects 1`), o jogo continuou no renderizador
embutido e o replay terminou com exit code 0. As imagens de `--screenshot`
(normal e tela dividida) também ficaram byte a byte iguais com e sem o plugin.

Na R-2, a tela dividida passou a ser checada: `camera-split` fechou com 550
frames (215 deles divididos, 2 painéis) e `shared-world-two-links` com 450
frames (15 divididos), 0 diferenças, tanto no caminho sem plugin quanto com o
`ppu-ref` carregado de arquivo.

Casos com modo 7: `ref1`–`ref7`, `ref9`–`ref11`, `issue243`, `issue245` e
`issue255` exercitam o caminho
`PpuSetMode7PerspectiveCorrection`/`PpuDrawMode7Upsampled` (4×4) e fecharam com
0 diferenças (18.410 frames de modo 7 no total, nos perfis de 224 e 240).

### Custo

Medido com `--frame-draw` (sem captura), `--frame-desc` (com captura) e
`--frame-check` (captura + segundo render + comparação), 20.000 frames:

| Modo | Tempo total | Por frame |
|---|---:|---:|
| `--frame-draw` | 24,11 s | 1,21 ms |
| `--frame-desc` | 23,95 s | 1,20 ms |
| `--frame-check` | 37,59 s | 1,88 ms |

20.000 frames de `--replay-ref 3` com o perfil de 224 linhas e o renderizador
novo. A captura sozinha ficou dentro do ruído entre as duas primeiras linhas
(< 0,1 ms/frame); a checagem completa (captura + segundo render + comparação)
custa ~0,67 ms/frame a mais. O tempo inclui a simulação, que domina o total.

Custo da R-2, mesma máquina e mesmos 20.000 frames, média de duas execuções:

| Modo | Tempo total | Por frame |
|---|---:|---:|
| `--frame-draw` (linha de base) | 16,29 s | 0,81 ms |
| `--frame-desc` (captura) | 16,48 s | 0,82 ms |
| `--frame-check` (captura + descrição + comparação) | 26,59 s | 1,33 ms |
| `--frame-check --render-plugin ppu-ref` | 28,75 s | 1,44 ms |

O plugin de arquivo custa ~0,11 ms/frame a mais que a checagem embutida. A
maior parte é o render de captura que ainda roda antes de `present`; a
interface e o `dlopen` não aparecem no custo por frame. O tempo inclui a
simulação.

A captura copia o estado escalar pequeno, a OAM (544 bytes por painel) e até
~2 mil escritas de registrador por frame; VRAM/CGRAM continuam referenciadas,
não copiadas.

## Plugin HD tiles (R-3)

`plugins/hd-tiles/` implementa substituição de tiles no estilo dos HD packs do
Mesen. Ele consome a `FrameDesc`, desenha o frame em modo 1 e, para cada tile
8×8 de BG ou sprite, procura uma imagem no pack. Sem entrada no pack, o tile
original continua sendo desenhado a partir da CGRAM; com pack vazio e saída
1×, o resultado é byte a byte igual ao renderizador de referência.

O plugin refaz o caminho de linha do modo 1 (janelas, prioridade por camada,
mosaico, sprites, matemática de cor e brilho) em vez de pintar por cima do
frame pronto. Modo 7, force blank e modos que o jogo não usa caem em
`host->render_frame_desc()`. A lógica de mosaico considera
`kPpuRenderFlags_NewRenderer`: o renderizador antigo alinha o bloco pela linha
visível (`line - 1`) e o novo pela linha interna (`line`), como em `ppu.c`.

### Chave de tile

- **hash**: FNV-1a de 32 bits sobre os bytes do tile na VRAM, na ordem SNES
  (byte baixo, byte alto de cada word). 4bpp = 16 words (32 bytes); 2bpp = 8
  words (16 bytes). O mesmo hash é usado pelo dump e pelo pack.
- **paleta**: número da paleta do tile (0–7) para BG e sprites.
- **bpp**: 2 ou 4; o pack pode deixar `0` para aceitar qualquer um.
- **condições**: `indoors`, `room`, `area`, `floor`, `attr` (atributo de
  colisão da camada semântica, só para BG) e `layer` (`bg1`/`bg2`/`bg3`/
  `sprite` ou 0–3). São avaliadas na ordem do arquivo; a primeira entrada que
  casa vence.

A imagem é recortada no footprint lógico do tile (8×8): o pack pode desenhar
em 2×/4×, mas a substituição não invade o tile vizinho. Pixels com alfa < 128
caem no pixel original da CGRAM, o que permite desenhar só parte do tile.

### Formato do pack

Uma pasta com `pack.json` e as imagens referenciadas (caminhos relativos ao
manifesto):

```json
{
  "format": "zelda3-hd-pack",
  "version": 1,
  "scale": 2,
  "image": "tiles.png",
  "tiles": [
    {
      "hash": 1522944453,
      "palette": 2,
      "bpp": 4,
      "x": 0, "y": 0, "w": 16, "h": 16,
      "conditions": { "indoors": false, "attr": 72 }
    },
    {
      "hash": 1892290577,
      "palette": 3,
      "bpp": 4,
      "image": "dungeon.png",
      "x": 0, "y": 0, "w": 16, "h": 16,
      "conditions": { "indoors": true, "room": 261, "attr": 0 }
    }
  ]
}
```

- `scale` (global, 1–4) é a escala padrão das imagens; cada entrada pode
  sobrescrever com `"scale"`. A saída do plugin usa a maior escala encontrada
  no pack (pack vazio = 1×).
- `x`, `y`, `w`, `h` são o retângulo da imagem; se `w`/`h` forem omitidos,
  valem `8 * scale`.
- `image` pode ficar no manifesto ou por entrada. PNGs com alfa são
  carregados como RGBA.
- `palette`/`bpp`/`layer` ausentes significam “qualquer”. `hash` é
  obrigatório.
- `conditions` é opcional; `attr` só casa em tiles de BG (sprites usam -1).

O pack de exemplo em `plugins/hd-tiles/example-pack/` é desenhado do zero
(grama, arbusto, piso e parede de masmorra em 2×, formas geométricas), sem
nenhum pixel da ROM. Ele demonstra `indoors`, `room` e `attr`.

### Como usar

```sh
make -C plugins/hd-tiles

# pack de exemplo (2x); o pack vazio fica em qualquer pasta com tiles:[]
ZELDA3_HD_PACK=plugins/hd-tiles/example-pack \
  ./zelda3 --render-plugin plugins/hd-tiles/hd-tiles.so zelda3.sfc

# prova de passagem: pack vazio, 1x, comparação pixel a pixel
ZELDA3_HD_PACK=.agents/hd-empty-pack \
  ./zelda3 --config tests/regress.ini --headless --no-mods \
  --render-plugin plugins/hd-tiles/hd-tiles.so \
  --replay-ref 3 --frames 2000 --frame-check zelda3.sfc
```

O caminho do pack vem de `ZELDA3_HD_PACK` (pasta ou `pack.json`). Sem a
variável o plugin funciona como pass-through e registra isso no log.

A API de plugin passou para `kRenderPluginApiVersion = 3` (a versão 2 trouxe o
callback opcional `preferred_scale`; a 3 traz `command`): o host usa
`max(PpuGetCurrentRenderScale(), preferred_scale)` para alocar o buffer de
frames normais. O `ppu-ref` devolve 1 e continua idêntico. `--frame-check`
continua comparando na escala da descrição (1×), então a prova do pack vazio
não depende do HD.

Para medir só o custo do plugin, `--frame-plugin-draw` captura a descrição e
chama o `present` sem desenhar a referência nem comparar. A diferença para
`--frame-desc` (mesma captura, sem plugin) é o custo do plugin.

### Extração de tiles

`--frame-desc-dump <frame>` agora também escreve, em `.agents/shots/`:

- `frame-desc-<frame>-tiles.json`: inventário dos tiles visíveis de BG1/BG2/
  BG3 e dos sprites, com `hash`, `palette`, `bpp`, `kind`, `layer`, `attr`,
  posição na tela, contagem e retângulo na folha.
- `frame-desc-<frame>-tiles.png`: folha em 2×, 16 colunas, com fundo
  xadrez para o índice 0 (transparente). É só uma referência visual; os
  pixels vieram da ROM e por isso ficam fora do git.

O JSON e a folha usam o mesmo hash canônico do plugin, então dá para copiar
`hash`/`palette`/`attr` direto para o `pack.json`.

### Prova de pixels e custo

Pack vazio, `--frame-check` com o plugin de arquivo, 19 replays (13 `ref0`–
`ref12` e os 6 de `tests/replays/`), 2.000 frames por replay quando o save
tinha esse tamanho: 35.902 frames e 2.058.764.288 pixels, **0 pixels
diferentes** em todas as execuções. Isso inclui
frames de modo 7 (que caem no renderizador de referência) e frames com
mosaico, janelas, subscreen e matemática de cor. Os mesmos replays também
passaram com `NewRenderer = 1` em um spot de 2.000 frames (`ref1`, `ref5`,
`ref8`), 0 diferenças. Widescreen 16:9 e 240 linhas (`extend_y, 16:9`) foram
checados com `ref1` e `ref5` (1.000 frames cada, `NewRenderer` 0 e 1), também
com 0 diferenças. A tela dividida (`camera-split`, 550 frames, e
`shared-world-two-links`, 450 frames) passou pelo plugin com 0 diferenças;
cada painel é uma `FrameDesc` e o host emite um único `present`.

Custo medido nesta máquina com `--replay-ref 3 --frames 20000` (o tempo inclui
a simulação; média de 2 a 3 execuções):

| Modo | Tempo total | Por frame | Custo do plugin |
|---|---:|---:|---:|
| `--frame-desc` (captura, sem plugin) | 62,16 s | 3,11 ms | — |
| `--frame-plugin-draw`, pack vazio, 1× | 112,07 s | 5,60 ms | ~2,49 ms |
| `--frame-plugin-draw`, pack de exemplo, 2× | 127,55 s | 6,38 ms | ~3,27 ms |

O renderizador do plugin é deliberadamente simples (varredura por pixel, sem
os atalhos por span do `ppu.c`); o custo acima é o de um protótipo, não o de
um renderizador de produção. Quando não há imagem HD no pixel, a saída 2×
replica o pixel lógico e evita quatro composições completas.

## Plugin voxel (R-4/R-5)

`plugins/voxel/` monta uma cena 3D a partir da `FrameDesc`. Cada célula 8×8
visível de BG1/BG2 vira uma coluna de voxels com a cor do tile decodificado da
VRAM/CGRAM e a altura vinda do atributo de colisão da camada semântica. Sprites
com dono viram cartões verticais posicionados pelo `z` da descrição; BG3
(HUD/texto) e sprites sem dono continuam em 2D por cima. Modo 7, force blank,
mosaico e telas especiais caem no renderizador de referência.

A R-5 acrescenta câmera livre (girar/inclinar/aproximar e voltar ao padrão),
sombras dos cartões e das colunas, luz direcional, um atalho para ligar/desligar
o voxel em jogo e perfis por hack com herança (`extends`).

O desenho usa um contexto **EGL + OpenGL** fora da tela, com FBO, e lê o
resultado de volta para o buffer de `present`. O EGL surfaceless é tentado
primeiro, então funciona em `--headless` sem `DISPLAY`; se o contexto falhar,
o plugin registra uma mensagem e usa `host->render_frame_desc()` sem derrubar
o jogo. `ZELDA3_VOXEL_NO_GL=1` força esse caminho para teste.

### Arquivos

| Arquivo | Papel |
|---|---|
| `plugins/voxel/voxel.c` | ciclo de vida, montagem da cena, atlas de tiles, sombras, controles e fallback |
| `plugins/voxel/voxel_gl.c` | EGL/OpenGL, shaders (3D/2D/sombra), FBO, VBO e readback |
| `plugins/voxel/voxel_profile.c` | leitura do perfil JSON, herança e tabela de alturas |
| `plugins/voxel/default-profile.json` | perfil padrão (dados, não código) |
| `plugins/voxel/profiles/flat.json` | perfil "plano" de exemplo |
| `plugins/voxel/profiles/exaggerated.json` | perfil "exagerado" de exemplo |
| `plugins/voxel/Makefile` | build separado (`voxel.so`/`voxel.dll` não vai para o git) |

### Altura padrão

A altura é medida em blocos de 8 pixels SNES (1,0 = um tile). A tabela
completa, atributo por atributo, está em `default-profile.json`; por categoria:

| Altura | Atributos (resumo) |
|---:|---|
| `0,00` | chão, nada, gelo, spike floor, esteira, piso de rupee, chão de caverna |
| `0,25` | grama alta, warp, vegetação baixa |
| `0,50` | escadas, degraus, rampas, bordas/ledges |
| `1,00` | blocos levantáveis, baús, lápides, cristal |
| `1,50` | paredes, portas, hookshottables, tochas, placas |
| `-0,25` | água rasa, escada de água |
| `-0,75` | água funda |
| `-1,00` | buracos/pits |

Três atributos mudam de significado em ambientes fechados: `0x04` (grama
alta vira parede), `0x0b` (água funda vira parede) e `0x6c`–`0x6f` (chão vira
parede). O perfil padrão tem essas exceções com `"indoors": true`.

Em masmorras, BG1 é o andar de cima e BG2 o de baixo. A camada do andar atual
fica em `z = 0`; a outra sobe/desce `floor_step` (0,5 bloco por padrão). No
overworld BG2 é o terreno e BG1 é o overlay (árvores, pontes, chuva); as duas
camadas agora têm atributos próprios na `FrameDesc` e alturas independentes. A
captura usa a rolagem de cada faixa de 8 linhas, então HDMA de rolagem não
desloca mais o relevo em relação ao tile.

### Sprites, HUD e câmera

- Sprites com `owner_kind` Link/sprite/ancilla viram cartões no plano XZ. O
  `y` do OAM já inclui o `-z` do jogo; o plugin reconstrói a posição do chão e
  sobe o cartão por `z`, então o Link pula de verdade. O tamanho vertical do
  cartão é compensado pela inclinação da câmera para continuar com 8/16 px.
- Sprites sem dono (HUD, efeitos de tela) ficam em 2D na posição do OAM.
- BG3 é desenhado como quads 2D por cima da cena, com a paleta 2bpp correta.
  O split de BG3 da linha 128 (IRQ) é reproduzido por linha; as escritas de
  scroll de BG1/BG2 também são aplicadas por linha na busca do tilemap e na
  camada semântica.
- A câmera é uma projeção perspectiva inclinada, sem revelar tiles fora da
  área da `FrameDesc`: a geometria é limitada à largura/altura descritas e o
  recorte continua sendo o do painel. O fundo fora do diorama é a cor de
  backdrop (`cgram[0]`). Com `yaw`, a cena gira em torno do centro do painel;
  o zoom só aproxima/afasta, nunca busca tiles que não estão na descrição.
- Câmera livre em jogo (volta ao padrão com Alt+0):

| Tecla | Ação |
|---|---|
| F11 | liga/desliga o voxel (cai no renderizador de referência) |
| Alt+← / Alt+→ | gira a câmera (yaw, ±60°) |
| Alt+↑ / Alt+↓ | inclina (elevação, 10–89°) |
| Alt+= / Alt+- | aproxima/afasta (zoom 0,5–2,5×) |
| Alt+0 | volta câmera, luz e sombras ao perfil |
| F12 | liga/desliga as sombras |
| Shift+F11 | liga/desliga a luz direcional |

- Variáveis de ambiente (o perfil tem os mesmos campos): `..._CAMERA_ANGLE`
  (58°), `..._CAMERA_DISTANCE` (1,0; maior = menos perspectiva),
  `..._CAMERA_HEIGHT` (1,0), `..._CAMERA_FOLLOW` (1; segue o chão do Link, não
  o pulo), `..._CAMERA_YAW` (0°), `..._CAMERA_ZOOM` (1,0),
  `..._HEIGHT_SCALE` (1,0), `..._SIDE_SHADE`, `..._SPRITE_SCALE`,
  `..._LIGHTING`, `..._SHADOWS`, `..._SHADOW_STRENGTH`, `..._SHADOW_LENGTH`
  e `..._SCALE` (1, 2 ou 4).

### Luz e sombras

- A luz é direcional e calculada por face no CPU (`ambient + diffuse * N·L`).
  Topo e as quatro laterais recebem tons diferentes; com a luz desligada o
  resultado volta ao sombreado plano por `side_shade`.
- As sombras são quads escuros no chão, desenhados depois das colunas e antes
  dos cartões, com blend alfa e teste de profundidade. Colunas altas projetam
  uma sombra deslocada na direção oposta à luz; cada sprite com dono projeta
  uma sombra de contato. O perfil controla `strength`, `length`, `columns` e
  `sprites`, e F12 desliga tudo.
- O pass de sombra usa um programa GLSL próprio (mesma projeção 3D, fragmento
  de alfa constante), então não altera as cores dos tiles.

### Perfil

`ZELDA3_VOXEL_PROFILE` aponta para um JSON. Sem a variável, o plugin tenta
`plugins/voxel/default-profile.json`; sem o arquivo, usa a tabela embutida. Um
perfil pode usar `"extends": "caminho.json"` para herdar outro perfil e
sobrescrever só o que muda — é assim que um hack traz o seu ajuste sem copiar
a tabela inteira. O caminho é resolvido relativo ao arquivo do perfil e, se
não existir, como escrito (relativo ao diretório de lançamento). O formato:

```json
{
  "format": "zelda3-voxel-profile",
  "version": 1,
  "camera": { "angle": 58, "distance": 1.0, "height": 1.0, "follow_link": true,
              "yaw": 0.0, "zoom": 1.0 },
  "render": { "side_shade": 0.72, "sprite_scale": 1.0, "floor_step": 0.5,
              "skirt": 1.0, "height_scale": 1.0 },
  "lighting": { "enabled": true, "azimuth": 45, "elevation": 55,
                "ambient": 0.55, "diffuse": 0.45 },
  "shadows": { "enabled": true, "strength": 0.32, "length": 1.5,
               "columns": true, "sprites": true },
  "heights": { "0": 0.0, "1": 1.5, "0x20": -1.0 },
  "exceptions": [
    { "indoors": true, "attr": 4, "height": 1.5 },
    { "room": 86, "layer": "bg2", "tile": 512, "height": 2.0 }
  ]
}
```

`heights` sobrescreve a tabela por atributo (chave decimal ou hexadecimal).
`exceptions` são avaliadas na ordem e valem para o primeiro caso que casar:
`indoors`, `room`, `area`, `floor`, `layer` (`0`/`1` ou `"bg1"`/`"bg2"`),
`attr`, `raw` (byte da masmorra/Map16) e `tile` (número do tile 0–1023).
`height` é obrigatório. `camera`, `render`, `lighting` e `shadows` são
opcionais; variáveis de ambiente vencem o arquivo.

Perfis de exemplo: `profiles/flat.json` (quase plano, câmera alta, sem sombra)
e `profiles/exaggerated.json` (alturas 1,8×, câmera baixa e sombra longa).
Para um hack, basta distribuir um JSON com `extends` apontando para o perfil
padrão e ajustar `exceptions` por `room`/`area`/`raw`; o launcher do hack define
`ZELDA3_VOXEL_PROFILE=mods/<id>/voxel.json`.

### Como usar

```sh
make -C plugins/voxel

# janela normal ou headless
./zelda3 --render-plugin plugins/voxel/voxel.so zelda3.sfc
./zelda3 --config tests/regress.ini --headless --no-mods \
  --render-plugin plugins/voxel/voxel.so \
  --replay-ref 6 --frames 300 --screenshot .agents/shots/overworld.ppm \
  zelda3.sfc

# perfis alternativos
ZELDA3_VOXEL_PROFILE=plugins/voxel/profiles/flat.json \
  ./zelda3 --render-plugin plugins/voxel/voxel.so zelda3.sfc
ZELDA3_VOXEL_PROFILE=plugins/voxel/profiles/exaggerated.json \
  ./zelda3 --render-plugin plugins/voxel/voxel.so zelda3.sfc

# câmera girada e sombras desligadas, só para a prova
ZELDA3_VOXEL_CAMERA_YAW=30 ZELDA3_VOXEL_SHADOWS=0 \
  ./zelda3 --config tests/regress.ini --headless --no-mods \
  --render-plugin plugins/voxel/voxel.so --replay-ref 6 --frames 300 \
  --screenshot .agents/shots/camera-rotated.ppm zelda3.sfc

# prova do fallback (deve sair byte a byte igual ao render normal)
ZELDA3_VOXEL_NO_GL=1 ./zelda3 --config tests/regress.ini --headless \
  --no-mods --render-plugin plugins/voxel/voxel.so \
  --replay-ref 8 --frames 2000 --screenshot .agents/shots/fallback.ppm \
  zelda3.sfc
```

### Prova e custo

- `make -j2`, `make -j2 NO_LUA=1` e os três plugins (`ppu-ref`, `hd-tiles`,
  `voxel`): sem warnings.
- `tools/regress.sh`: 19 replays + 20 feature replays, todas as linhas `ok`
  e `regress: all 19 replays and 20 feature replays passed`.
- `ppu-ref`, spot de 500 frames de `ref3` com `--frame-check`: 28.672.000
  pixels, **0 diferenças**.
- Fallback com `ZELDA3_VOXEL_NO_GL=1` em `ref8`/2000: screenshot byte a byte
  igual ao render normal (mesmo MD5).
- Screenshots em `.agents/shots/` (fora do git): `overworld-side.png`
  (`ref6`, frame 300), `dungeon-side.png` (`ref8`, frame 2000),
  `split-voxel.png` (`camera-split`, frame 300, os dois painéis),
  `camera-rotated.png` (`ref6`, yaw 30°, zoom 1,15),
  `shadows-on.png`/`shadows-off.png` (`ref6`, frame 300),
  `profile-flat.png`/`profile-exaggerated.png` (`ref6`, frame 300),
  `hdma-before.png`/`hdma-after.png` (`f4e-mirror`, frame 260; o build da R-4
  ainda usava a rolagem inicial) e
  `jump-sequence.png`/`jump.gif` (`ref2`, frames 430–485).

Custo medido nesta máquina com `--replay-ref 4 --frames 3000` (inclui a
simulação; três execuções intercaladas):

| Modo | Tempo total | Por frame | Custo do plugin |
|---|---:|---:|---:|
| `--frame-desc` (captura, sem plugin) | 11,34–12,53 s | 3,78–4,18 ms | — |
| `--frame-plugin-draw`, luz + sombras | 21,17–22,56 s | 7,06–7,52 ms | ~3,2–3,6 ms |
| `--frame-plugin-draw`, `ZELDA3_VOXEL_SHADOWS=0` | 20,72–24,07 s | 6,91–8,02 ms | ~3,0–4,1 ms |

O custo é o de um protótipo: o atlas de 512×768 é remontado e enviado por
frame, a geometria é gerada na CPU e o frame é lido de volta com
`glReadPixels`. Não há cache de atlas entre frames nem instancing. A máquina
varia alguns ms/frame entre execuções; o custo das sombras ficou dentro dessa
variação (a geometria extra é pequena perto do atlas/readback), e o custo
dominante continua sendo o atlas/readback.

## Limitações conhecidas

- `FrameDesc` é um snapshot raso: VRAM/CGRAM/`link_chr`/`oam_is_player2` são
  referências ao PPU vivo (a OAM é a exceção, copiada por painel).
- O dono de OAM é heurístico (ver acima).
- O plugin de referência ainda roda o caminho normal uma vez para capturar a
  descrição antes de desenhar; um renderizador de produção pode evitar esse
  render extra quando a captura for separada da rasterização (R-3/R-4).
- Overlays de mods ficam no host, depois de `present` (ver acima); a descrição
  não carrega os comandos de `zelda.draw` nesta etapa.
- Modos de vídeo diferentes de 1 e 7 não são usados pelo jogo nativo; a
  descrição captura o estado, mas o caminho de renderização continua sendo o
  que `ppu.c` suporta.
- O plugin HD cobre modo 1. Modo 7 (mapa-múndi), force blank e outros modos
  chamam o renderizador de referência; não há substituição de tiles neles.
- Uma imagem HD fica limitada ao tile 8×8 que a originou (não cobre tiles
  vizinhos). Tiles compostos por várias células precisam de uma entrada por
  célula; o pack de exemplo mostra o caso com `attr`/`room`.
- Sprites também são identificados por hash + paleta, mas a condição `attr`
  não se aplica a eles.
- O voxel cobre modo 1. Modo 7, force blank, mosaico e telas com contexto GL
  indisponível chamam o renderizador de referência. A detecção de tela
  especial é heurística: `room`/`area` 0 com o Link em (0,0) (título/seleção)
  também cai no referência.
- BG1 do overworld agora tem camada semântica própria, mas quando ele é
  desenhado apenas na tela secundária (por exemplo o *warp* do espelho) o
  plugin não o inclui na cena, porque a matemática de cor ainda não é refeita.
- Janelas, matemática de cor, brilho por linha e mosaico não são refeitos na
  cena 3D; o plugin aplica só o brilho global da CGRAM. Por isso mosaico cai
  no fallback.
- Sprites são cartões planos (sem voxels de 1 de espessura). Eles recebem a luz
  direcional e uma sombra de contato, mas não têm sombreamento por pixel.
- As sombras são quads de tela alinhados à grade de tiles, não um volume
  projetado: funcionam como sombra "barata" e podem encostar em paredes
  vizinhas. A câmera livre limita yaw a ±60° e zoom a 0,5–2,5× justamente
  para o diorama continuar legível.
- O fundo fora do diorama é a cor de backdrop; em salas menores que a tela a
  câmera inclinada mostra essa borda, mas nunca tiles fora da `FrameDesc`.
- O perfil é lido uma vez no `init`; não há recarga em quente.
- EGL/OpenGL só é compilado no Linux. Em Windows/macOS o plugin compila e cai
  no renderizador de referência.

## Próximas etapas

A R-5 fechou câmera livre, sombras/luz e perfis por hack. Continuam fora do
protótipo: cache de atlas/instancing, mapeamento modo 7 → atributo, suporte a
janelas/matemática de cor na cena 3D e sombras projetadas de verdade. O plugin
HD (R-3) e o voxel (R-4/R-5) compartilham o mesmo caminho de fallback
(`host->render_frame_desc()`) e a mesma leitura de VRAM/CGRAM/camada semântica.
