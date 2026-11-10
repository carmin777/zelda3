# Triagem do upstream e dos forks (09/11/26)

Fonte: `snesrev/zelda3` (4.992 estrelas, 473 forks, último push 02/10/26):
68 issues abertas, 23 PRs abertos. Forks: listados os 429 visíveis pela API,
medidos os 45 mais relevantes (estrelas ou atividade desde 2025) contra o
`master` do upstream.

## Princípio da "solução limpa"

O núcleo continua **fiel e verificado** (o oráculo compara com a ROM).
Tudo que muda o jogo de propósito vira **opção desligada por padrão** ou
**mod** usando a nossa API. Assim corrigir bug não quebra replay, e pedido
de feature não suja o núcleo.

## Issues e PRs por grupo

### A. Bugs de lógica — o nosso portão é feito para isso
- **#242, #245: divergência de RAM com `save0.zip` anexado.** São
  replays do próprio zelda3: dá para reproduzir no `--headless`, achar a
  função que diverge do assembly, corrigir e adicionar o replay ao
  `tools/regress.sh`. Mesmo tipo, sem anexo: #243, #255.
- #328 bow/fire rod/ice rod/hammer travando animação (o autor já aponta o
  índice fora da tabela em `LinkItem_Bow`); PR #329 (out-of-bounds/UB).
- #236 Book of Mudora fora do Y; #212 direção errada; #290 coração do jogo
  de cavar; #297 créditos rápidos; #241 som faltando no Dark Palace; #244
  mapa escurecido; #246 barreira do Agahnim no widescreen; PR #292 cursor.
- PRs #181, #187 (#126): correções de bugs do jogo original → entram como
  opção (`GameChangingBugFixes`), não como padrão.

### B. Build e portabilidade
- Já resolvidos na nossa branch: #274 / PR #300 / PR #316 (protótipos C23).
- #249 (sintaxe em `text_compression.py`), #253, PR #256 (Python mínimo),
  #226 `nproc` no macOS, #254, #257, #283, #306, #309, PR #313, PR #270
  (GLES), PR #301 (DPI).

### C. Tradução — boa parte vira modkit (F4b)
- **#199 "tradução sem ROM modificada"** e **#281 menus/itens ainda em
  inglês** (é a sua issue do PT-BR): diálogo em `.txt` já sai na F4b;
  menus/itens são gráficos → PNG pelo modkit.
- #285 / PR #305 italiano, #289 hash sueco, #332 hash do Redux, #294, #299
  russo; fork `sxunix` tem chinês com fonte CJK.

### D. Pedidos que viram mod (API pronta ou quase)
- #262 nível de espada para quebrar vaso, #295 cheats ligados por padrão,
  #222 ROM randomizada como fonte, #232 Redux, #126 "corrigir todos os
  bugs" (pacote de opções).

### E. Vídeo e controle
- Widescreen/proporção: #142, #227, #322, #224, #311, PR #193, PR #314
  (ultrawide); integer scaling #203; vsync #206; 120 fps #240; cursor
  PR #247; atalhos #261 / PR #237; controles #225, #331.

### F. Ports e empacotamento
- GC/Wii #179, 3DS #298, DJGPP #310, RPi #195, macOS app #279, Docker
  #278 / PR #186, Nix PR #207 / #282, AppImage PR #248, tvOS PR #327.
  Fora do nosso objetivo; não mexer.

## Forks com trabalho próprio

| Fork | À frente | O quê | Interesse |
|---|---:|---|---|
| Slashmolder/zelda3randomizer | 1001 | randomizer completo **dentro do binário**, em C, com seeds reproduzíveis, shopsanity, entrance shuffle | referência de randomizer; tratar como "o que a API precisa permitir" |
| pythoninthegrass/zelda3 | 500 | porta arquivo por arquivo para Zig com agentes | só referência de método |
| SiggeMcKvack/zelda3 | 194 | Android, widescreen que preenche as laterais, traduções galego/catalão/irlandês, BPS | widescreen e traduções |
| hyukishi/zelda3 | 96 | menu de configurações dentro do jogo, rebind de teclas, extração automática de assets | menu de opções |
| ghost19880/zelda3 | 57 | "HD": água HD, mundo contínuo, câmera suave, >60 Hz, HUD widescreen, italiano | ideias de engine; muito acoplado |
| rail5/zelda3-ml | 43 | **co-op local de 2 jogadores** (dois Links, câmera dinâmica, controles separados) | grande, mas é o tipo de coisa que a engine deveria permitir |
| sxunix/zelda3 | 4 | chinês com fonte CJK | i18n |

Licença: todos herdam o `LICENSE.txt` (MIT) do upstream, mas o GitHub não
identifica (`NOASSERTION`). Antes de copiar código de qualquer fork,
conferir o arquivo de licença dele e manter o crédito no commit.

## Proposta de ondas

1. **Bugs provados pelo oráculo** (grupo A): começar por #242 e #245
   (replays anexados), depois #328/#329. Cada correção ganha replay no
   regress. Um agente por bug.
2. **Build** (grupo B, só Linux/Python que dá para testar aqui).
3. **Tradução pelo modkit** (grupo C) logo depois da F4b — fecha a sua
   #281.
4. **Pedidos como mods de exemplo** (grupo D), mostrando a API.
5. **Vídeo** (grupo E) e **forks grandes** (co-op, randomizer, HD):
   decidir um por um depois das ondas 1–4.

Fica de fora: ports e empacotamento (grupo F).

## Projetos irmãos (achados em 09/11/26)

| Projeto | Base | O que tem de relevante | Licença (GitHub) |
|---|---|---|---|
| `YeOldPress/alttp-dx` | zelda3 reescrito em Zig | filtros CRT, widescreen com câmera, rumble, MSU, 49 conquistas, menu de opções, atualização própria, **seeds do alttpr.com rodando no emulador embutido** (LakeSnes) com tracker automático lendo o save; `zelda3-tools` para mods e idiomas | NOASSERTION |
| `AlexPerez7/alttp-recomp` | recompilação estática (`snes-recomp`), ROM US | 4.178 funções traduzidas para C, widescreen limpo, espanhol, save states e rewind, Android, testes automatizados por script | MIT (README) |
| `EstebanPdN/zelda-alttp-3ds` | zelda3 → Android → 3DS (duas telas) | 785 estrelas; 7 idiomas, mods de personagem, **randomizer nativo**, 15 opções de qualidade de vida; feito com ajuda de IA e documenta o processo | por componente |
| `Waterdish/zelda3-android` | zelda3 | port Android (base do 3DS) | sem licença declarada |
| `mstan/ZeldaAlttPSNESRecomp` | recompilação estática (`snesrecomp`) + núcleo do snes9x | "Seamless World": mundo expandido ao vivo, zoom, andares de masmorra conectados (ideias para a API de renderização e o voxel) | NOASSERTION |

Antes de copiar qualquer coisa, conferir a licença real de cada um.
