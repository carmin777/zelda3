# Randomizer compatível com a comunidade — o que isso pode querer dizer

Pesquisa de 09/11/26. Meta: **a mesma seed tem de dar a mesma
coisa** que no cenário que a comunidade já usa, preservando o trabalho que
existe. Este documento lista o que existe, o que "compatível" pode
significar e uma proposta. Nada aqui foi implementado.

## 1. O ecossistema

| Projeto | O que é | Licença | Relação com a gente |
|---|---|---|---|
| **ALttPR** (`sporchia/alttp_vt_randomizer`, alttpr.com) | o gerador de referência (PHP): lógica, preenchimento, configurações, API | MIT | é ele que define "a seed X" |
| **`z3randomizer`** (`KatDevsGames/z3randomizer`) | o **patch-base em assembly** que toda ROM do ALttPR leva: itens progressivos, tabelas de itens, HUD, contadores, multiworld… 102 arquivos `.asm`, ~3 MB de fonte, **730 pontos de desvio** em `hooks.asm` | MIT | é o **comportamento** que uma seed espera do jogo |
| **Archipelago** | multiworld entre jogos; o ALttP dele usa a mesma base | ver o repositório | conversa com o jogo pela RAM, via SNI |
| **ALttP Door Randomizer** (`aerinon`) | variante com portas embaralhadas | MIT | outro gerador sobre a mesma base |
| **SNI** (`alttpo/sni`) | ponte entre ferramentas (trackers, Archipelago) e o "console": FX Pak Pro, RetroArch, emuladores com Lua, **emuladores com o protocolo EmuNWA** | MIT | é a porta de entrada das ferramentas da comunidade |
| **Slashmolder/zelda3randomizer** | fork do zelda3 com gerador **em C**, lógica traduzida à mão do ALttPR, muito completo (modos, goals, entradas, hints, race mode, trackers próprios) | MIT (pelo README) | **declara não ser compatível** com seeds do alttpr.com, "nos dois sentidos, por escolha"; tracker externo com protocolo próprio (NDJSON), não SNI |

Como uma seed do ALttPR vira jogo: ROM US + patch-base `z3randomizer` +
**valores gravados por seed** em tabelas de endereço fixo e documentado
(`tables.asm`: 487 entradas a partir de `$B0:8000`, PC `0x180000`, como
itens de NPCs, medalhões, regras; os itens dos baús nos dados originais das
salas; `SeedHash`, os 5 ícones que aparecem na seleção de arquivo para
conferir a seed numa corrida).

## 2. Os sentidos de "compatível"

| # | Sentido | O que exige | Valor |
|---|---|---|---|
| **A** | **Abrir a seed do alttpr.com**: o jogador gera no site como sempre e roda a ROM (ou o patch) no zelda3; mesmos itens nos mesmos lugares, mesmos 5 ícones de hash | ler a seed como **dados** (as tabelas acima + baús) e reproduzir em C o **comportamento** do `z3randomizer` | é a definição mais forte de "mesma seed, mesma coisa" |
| **B** | **Ferramentas da comunidade funcionam**: auto-trackers (EmoTracker, PopTracker), o cliente do Archipelago | servidor **EmuNWA** (ou o protocolo de comando do RetroArch) no zelda3 expondo WRAM/SRAM/ROM; o SNI detecta sozinho | trackers de itens já funcionariam com o jogo **normal** (a RAM do zelda3 é idêntica à do SNES); o multiworld precisa também de A |
| **C** | **Corrida justa**: tempos e truques iguais ao console | o zelda3 sem opções já é provado frame a frame contra o assembly | vem de graça, é um argumento forte |
| **D** | **Gerar a seed dentro do zelda3** com o mesmo resultado do site | reimplementar o gerador e o RNG de cada versão | frágil; o Slashmolder traduziu a lógica mas não o resultado. Não recomendado |

## 3. A ideia central: provar a compatibilidade com o mesmo oráculo

O zelda3 roda cada frame em C e no assembly da ROM carregada e compara a
RAM. Se a ROM carregada for **a ROM da seed gerada pelo alttpr.com**, o
oráculo compara o nosso C com o assembly do `z3randomizer` frame a frame.
"Compatível" deixa de ser opinião: **a RAM é idêntica à da ROM da seed**,
com replays no portão como já fazemos com o jogo original.

Cuidados a confirmar antes de prometer:
- a ROM do ALttPR é FastROM e cresce (bancos `$B0+`); o emulador do
  projeto precisa rodá-la inteira;
- o `z3randomizer` usa RAM e SRAM extras (`ram.asm`, `sram.asm`); o C tem
  de usar os mesmos endereços para o oráculo e para o SNI;
- 730 desvios é muito: começar pelo preset mais comum (Open, itens
  padrão) e crescer por recurso, cada um com replay.

## 4. Proposta (para decidir)

1. **R1 — ponte SNI (EmuNWA)**, pequeno: o zelda3 vira um "emulador"
   que o SNI enxerga. Trackers já funcionam no jogo normal. Útil também
   para depurar.
2. **R2 — ler a seed**: `import-rom`/novo comando lê uma ROM (ou o patch)
   do alttpr.com: tabelas `$B0:8000`, baús, `SeedHash`; mostra os 5 ícones.
3. **R3 — comportamento do `z3randomizer` em C**, por recurso, provado
   pelo oráculo contra a ROM da seed (itens progressivos e tabela de itens
   primeiro; depois HUD, contadores, shuffles…).
4. **R4 — multiworld (Archipelago)** quando R1 + R3 cobrirem a RAM que
   o cliente usa.

O fork do Slashmolder continua valioso como referência de lógica,
trackers e UI, mesmo sem compatibilidade de seed.

## Decisão (09/11/26)

Decidido: **compatível com os dois**. O zelda3 abre seeds do
alttpr.com (sentido A, provado pelo oráculo) e conversa com as ferramentas
da comunidade (sentido B, via SNI). Um gerador nativo, como o do
Slashmolder/zelda3randomizer (MIT, com crédito ou em conversa com o autor),
pode conviver com isso. Ordem: R1 (ponte SNI) primeiro, junto com a
verificação das dúvidas da seção 5.

## Mudança de rumo (10/11/26)

Depois de 47 rodadas do oráculo estrito (seed `xvJo08oxyJ`, 11200 frames,
até Kakariko), foi aprovado trocar a meta: em vez de o C reproduzir a
ROM JP 1.0 + z3randomizer byte a byte em toda a RAM, **o C lê a seed**.

- **Importador de seed**: lê da ROM da seed (e do JSON do alttpr.com, quando
  tiver spoiler) o item de cada local, as configurações, os textos e o
  inventário inicial, e o motor em C aplica isso pelos ganchos da F4e.
- **"Mesma seed, mesmo resultado"** passa a ser: mesmo item em cada local,
  mesmas regras de progresso e mesmas dicas, conferido contra a ROM da seed
  local por local.
- **O oráculo estrito fica como regressão** (`seed-oracle` no
  `tools/regress.sh`, rota de Kakariko); não é mais a meta.
- **Compatibilidade total de corrida** continua disponível por `--emu-rom`
  (a ROM da seed no emulador embutido, com SNI).

A primeira fatia do importador (baús + item do tio) está em
`RANDOMIZER-IMPORT.md`.

## 5. Ainda a confirmar

Respondido em 09/11/26:
- **Formato do alttpr.com** (lido no código do ALttPR, `routes/web.php`):
  a seed é um JSON em `/hash/{hash}` com `patch` (lista de bytes por
  endereço), o spoiler (parcial em torneio), o `hash` e o `md5` do build
  do patch-base, que fica em `/bps/<md5>.bps`. ROM da seed = ROM US +
  BPS do patch-base + lista da seed. Tudo é dado e reproduzível; o zelda3
  pode montar a ROM sozinho a partir dos dois arquivos.
- **Emulador do projeto**: `snes/cart.c` lê LoROM com máscara
  `romSize - 1` (2 MB, potência de 2, ok) e trata o `$420D` (FastROM). O
  risco é o `PatchRom` do oráculo (`src/zelda_cpu_infra.c`), que confere
  bytes da ROM US antes de aplicar 56 pequenos patches. Medida grosseira
  contra os 729 `org` de `hooks.asm`: **1 ponto de sobreposição**
  (perto de PC `0x443FD`). Outros arquivos do `z3randomizer` também têm
  `org`; refazer a conta com a ROM de uma seed real antes do R3.

**Descoberta importante (09/11/26, com download autorizado):** o ALttPR
**não parte da ROM US**. O patch-base (`/bps/79bbb4ad….bps`, 99 KB,
build `2023-09-22`) espera uma origem de 1 MB com CRC `3322EFFC`, que é a
ROM **japonesa v1.0** ("Zelda no Densetsu: Kamigami no Triforce v1.0"). O
próprio site explica: o texto em inglês vem do patch, e a base japonesa
mantém os glitches exclusivos dessa versão. Consequências:
- montar a ROM de uma seed exige a ROM JP 1.0 local (fora do git) (não baixamos
  ROMs); sem ela, R2/R3 param aqui;
- o zelda3 reimplementa a versão **US**; as diferenças de código entre JP
  1.0 e US (glitches, alguns comportamentos) entram na conta da
  compatibilidade e do oráculo. Para o oráculo comparar contra a ROM da
  seed, o C teria de reproduzir também essas diferenças (um "modo JP 1.0"),
  ou a prova vale só para os trechos iguais;
- o JSON da seed (`patch`: 295 trechos; `spoiler`; `size: 2`) escreve em
  endereços da ROM JP; para ler os itens sem a ROM, é preciso mapear essas
  posições para os dados do zelda3.

Arquivos baixados (fora do git, em `.agents/alttpr/download/`): seed
diária de 09/11/26 `xvJo08oxyJ` (Open, NoGlitches, chaves embaralhadas,
torneio) e o patch-base correspondente.

**Precedente: ALTTP-DX** (`YeOldPress/alttp-dx`, `docs/randomizer.md`)
joga seeds do alttpr.com, também a partir da ROM JP 1.0, mas **não** no
motor nativo: "a seed reescreve código demais", então a ROM da seed roda
no emulador SNES embutido (LakeSnes, o mesmo do nosso oráculo), montado
como console completo; o tracker lê o save a cada frame. É uma opção E
para nós: compatibilidade total (é a própria ROM) e barata, perdendo os
recursos do motor nativo durante a seed. Ela pode ser o primeiro passo,
com o caminho nativo provado pelo oráculo (A) vindo depois.

**ROM JP 1.0 disponível (09/11/26):** há uma cópia local em `japanese.sfc`
(CRC32 `3322EFFC`, MD5 `03a63945…`, fora do git). ROM JP + patch-base deu
exatamente o MD5 do build (`79bbb4ad…`); + a seed diária deu a ROM da seed
(2 MB, título interno `VT xvJo08oxyJ`). A cadeia é reproduzível de ponta a
ponta.

Continua em aberto:

- formato exato que a API do alttpr.com entrega (ROM inteira? lista de
  bytes + BPS do patch-base?) e versões do patch-base por seed;
- se o emulador `snes/` do projeto roda a ROM do ALttPR (FastROM, 2 MB+);
- detalhes do protocolo EmuNWA (comandos, domínios de memória) que o SNI
  usa;
- licença do Archipelago antes de reaproveitar qualquer código dele.
