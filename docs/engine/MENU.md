# Menu de opções dentro do jogo (O1)

Menu sobreposto ao jogo para ajustar vídeo, som, features, mods e teclas sem
editar o `zelda3.ini` à mão. O código fica em `src/mod/menu.c` (com os ganchos
de host em `src/main.c`) e não depende de Lua: funciona igual em `make -j2` e
em `make -j2 NO_LUA=1`.

## Abrir e navegar

- Teclado: `Home` abre e fecha (padrão; `[KeyMap] Menu`).
- Controle: `L3` abre e fecha (padrão; `[GamepadMap] Menu`).
- `Home` foi escolhido por não colidir com nenhuma tecla de
  `kDefaultKbdControls` nem com as seções `[KeyMap2]`/`[GamepadMap2]`.
- Enquanto o menu está aberto o jogo não roda nenhum frame de simulação e o
  áudio é pausado. O último frame continua sendo desenhado com o menu por cima.
- Setas movem a seleção; esquerda/direita mudam o valor; Enter/A ativa;
  Esc/B fecha. `PageUp`/`PageDown` (ou Tab/L1/R1) trocam de página.

Páginas:

| Página | Opções |
|---|---|
| Video | escala da janela, tela cheia, renderizador novo/antigo, widescreen, 224/240 linhas, plugin de render ligado/desligado |
| Sound | volume geral, Mono (ON/OFF), MSU (off/MSU/deluxe/deluxe+opuz), volume do MSU, retomar MSU |
| Gameplay | todas as opções de `[Features]` e `DimFlashes` |
| Mods | liga/desliga e prioridade de cada mod; avisa que é preciso reiniciar |
| Keys | as 12 teclas do joypad, a tecla do menu e os 12 botões do controle |

O desenho usa a fonte 5x7 do overlay (`zelda.draw`), com painel centralizado
calculado a partir de `ModDraw_Width()`/`ModDraw_Height()`. Funciona em 224 e
240 linhas e nas laterais do widescreen; listas longas rolam sozinhas.

## Aplicação e gravação

As mudanças de vídeo, som e features valem na hora:

- escala/tela cheia redimensionam a janela e reaplicam a textura;
- widescreen/240 linhas atualizam `extraLeftRight`, `g_snes_width`,
  `g_snes_height` e as flags do PPU;
- o renderizador alterna o bit `kPpuRenderFlags_NewRenderer`;
- o plugin é ligado/desligado com `RenderPlugin_SetEnabled`, sem descarregar a
  biblioteca (e o menu é desenhado depois do `present`, então o voxel ligado ou
  desligado não quebra a sobreposição);
- features atualizam `g_wanted_zelda_features`; layout de HUD (`NewStyleInventory`,
  `HorizontalMagicBar`, `ReduxHud`) chama `Hud_Rebuild()`, e `DimFlashes`
  reaplica a paleta;
- MSU chama `ZeldaEnableMsu` de novo; o volume usa o mixer SDL ou o volume do
  sistema quando disponível;
- Mono muda `AudioChannels` entre 2 e 1, pausa o dispositivo SDL antigo, abre
  um novo com a contagem pedida e troca os buffers sob `g_audio_mutex`. O
  dispositivo novo começa pausado, como o menu. Se a abertura falhar, o
  dispositivo antigo continua e o rodapé mostra o aviso de reinício. O downmix
  de Mono é o do SPC/DSP; a mistura de MSU continua no caminho estéreo original.

Ao fechar o menu (ou ao sair do jogo com ele aberto), as mudanças pendentes são
gravadas em `zelda3.user.ini`, no diretório atual. O `zelda3.ini` do repositório
nunca é reescrito.

O arquivo gerado começa com `!include zelda3.ini` (se ainda não houver um
`!include`) e guarda só as chaves controladas pelo menu:

```ini
[General]
ExtendedAspectRatio = 16:9

[Graphics]
WindowScale = 3
Fullscreen = 0
NewRenderer = 1
RenderPluginEnabled = 1
DimFlashes = 0

[Sound]
Volume = 100
AudioChannels = 2
EnableMSU = 0
MSUVolume = 100
ResumeMSU = 1

[Features]
ItemSwitchLR = 1
...

[KeyMap]
Controls = Up, Down, Left, Right, Right Shift, Return, X, Z, S, A, C, V
Menu = Home

[GamepadMap]
Controls = DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, B, A, Y, X, L1, R1
Menu = L3

[Mods]
EnableOverrides =
DisableOverrides =
PriorityOverrides =
```

Linhas e seções desconhecidas do `zelda3.user.ini` são preservadas; apenas as
chaves acima são substituídas. O `Volume` só é gravado depois que o menu muda o
volume, para não sobrescrever o volume do sistema de quem nunca mexeu nele.

### Mods

Ligar/desligar e mudar prioridade no menu **não recarrega** os mods na hora: o
loader já rodou e os assets/scripts estão aplicados. O menu mostra
`RESTART REQUIRED FOR MOD CHANGES` e grava as escolhas nas três chaves de
`[Mods]`:

- `EnableOverrides`: ids forçados a ligar (vencem `"enabled": false`);
- `DisableOverrides`: ids forçados a desligar;
- `PriorityOverrides`: `id=prioridade`, separado por vírgulas.

O loader aplica esses valores antes de ordenar/carregar e continua usando
`priority` crescente e `id` como desempate. Ids com vírgula, espaço, `=` ou `%`
não podem ser representados nessas listas; o menu avisa no `stderr` e não grava
aquele mod. Mods sem override continuam exatamente como o `mod.json` declara.
Na lista, o `*` depois da prioridade significa que o manifesto foi lido mas o
mod não chegou a carregar nesta execução (por exemplo, asset ausente ou
dependência recusada).

### Teclas

A página Keys edita os mesmos comandos de `[KeyMap] Controls` e
`[GamepadMap] Controls`, mais a tecla do menu. Enter inicia a captura; a próxima
tecla/botão é gravada, Esc cancela. A gravação usa os nomes do SDL
(`Up`, `Right Shift`, `Ctrl+F1`, ...) e os nomes do controle (`A`, `DpadUp`,
`L3`, ...). Se a tecla já estiver em uso, ela é movida para o novo comando,
exceto a tecla do menu: o menu recusa a captura para nunca ficar sem uma tecla
de saída.

## Portão sem mods

Sem abrir o menu, nada muda:

- `OptionsMenu_IsOpen()` é falso, então o desenho, o bloqueio de input e a
  pausa não fazem nada;
- as funções de host só são chamadas a partir de uma ação do menu;
- o loader de mods só consulta as listas de override (vazias por padrão);
- a única adição permanente é o comando `Menu` (Home/L3), que antes não existia
  e não colide com nenhum padrão.

Por isso os replays e o `compare: on` continuam iguais.

## Licença e crédito

O fork `hyukishi/zelda3` foi usado só como referência de comportamento
(README: menu aberto com Escape/F12, páginas de configuração e rebind de
teclas). O `LICENSE.txt` dele é a licença MIT do upstream `snesrev/zelda3`
(confirmado em 10/11/26; o GitHub mostra `NOASSERTION`, mas o arquivo é MIT).
Ainda assim, este menu foi escrito do zero para a nossa engine, sem copiar
código do fork; o crédito fica registrado aqui.

## Verificação

```sh
make -j2
make -j2 NO_LUA=1
tools/regress.sh
tools/test-menu.sh
```

O teste headless abre o menu, alterna `ItemSwitchLR` na página Gameplay e
`Mono` na página Sound pelo mesmo caminho do Enter, grava um
`zelda3.user.ini` temporário e confere `!include zelda3.ini`,
`ItemSwitchLR = 1`, `AudioChannels = 1` e `Menu = Home`. O
`tools/regress.sh` chama esse teste e imprime uma linha `ok menu-headless`.

Screenshots de cada página (gerados fora do git, em `.agents/shots/`):

```sh
for p in video sound gameplay mods keys; do
  ./zelda3 --config tests/regress.ini --headless --no-mods --replay-ref 0 \
    --frames 200 --menu-screenshot "$p" \
    --screenshot ".agents/shots/menu-$p.ppm" zelda3.sfc
done
```

`--menu-screenshot` abre a página **depois** de imprimir o hash final, então a
captura não altera a simulação. Para testar as variações de tela, use um ini
com `ExtendedAspectRatio = extend_y, 16:9` (240 linhas) ou
`ExtendedAspectRatio = 16:9` (widescreen).

Com `[Coop] SharedWorld = 1`, o menu pausa o mundo inteiro: o passo do jogador
2 (`SharedWorld_FrameStep`) roda dentro de `ZeldaRunFrame`, que não é chamado
enquanto o menu está aberto. O frame congelado guarda as duas metades quando a
tela está dividida; o menu é desenhado sobre a metade esquerda. Capturas de
verificação:

```sh
# dois Links juntos (split desligado)
./zelda3 --config tests/replays-features/shared-world-two-links.ini \
  --headless --no-mods --replay tests/replays-features/shared-world-two-links.sav \
  --input-script2 tests/replays-features/shared-world-two-links.script2 \
  --frames 300 --menu-screenshot video \
  --screenshot .agents/shots/menu-coop-together.ppm zelda3.sfc

# tela dividida (CameraSplit 1 no frame 437)
./zelda3 --config tests/replays-features/shared-world-two-links.ini \
  --headless --no-mods --replay tests/replays-features/shared-world-two-links.sav \
  --input-script2 tests/replays-features/shared-world-two-links.script2 \
  --frames 450 --menu-screenshot video \
  --screenshot .agents/shots/menu-coop-split.ppm zelda3.sfc
```
