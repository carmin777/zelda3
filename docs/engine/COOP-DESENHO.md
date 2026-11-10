# Co-op — desenho para decisão

Rascunho de 09/11/26, feito com autorização para planejar sem
ele; **nada será implementado antes de ele revisar**.

## 1. Referências estudadas

| Referência | Modelo | O que ensina |
|---|---|---|
| **rail5/zelda3-ml** (fork do zelda3) | **mundo compartilhado, dois Links**: `ZeldaRunFrame(inputs1, inputs2)`, `Link_MainForAllPlayers()`; regiões de RAM "do jogador" têm cópias por jogador, trocadas para a RAM viva antes de rodar cada um (`src/ext/GameRAM.*`); sprites miram o jogador mais próximo; o segundo Link é desenhado com memória de tiles extra fora da VRAM do SNES | é possível no zelda3; o truque central é **trocar o estado do Link**; ainda experimental (dano e acerto "em geral" funcionam; câmera forçada larga) |
| **ALttPO** (`alttpo/alttpo`, bsnes + AngelScript + servidor) | **cada jogador roda o próprio jogo**; um servidor sincroniza o progresso (itens, cristais, pingentes, flags); os outros jogadores aparecem como sprites desenhados por cima, com nome e cor; modo corrida = só se veem, sem compartilhar | o modelo barato e testado pela comunidade; funciona pela internet; não exige mexer na simulação |
| **Four Swords** (GBA, oficial) | 2–4 Links no mesmo mundo, puzzles feitos para vários | referência de design: câmera, quem dispara a transição de tela, quem pega o item |

## 2. Dois modelos para nós

### Modelo A — "mundos paralelos, progresso compartilhado" (estilo ALttPO)

- Cada jogador é um **estado de jogo completo**. No zelda3 o estado é a
  RAM (`g_ram`, 128 KB) + SRAM + VRAM/PPU: rodar dois jogos no mesmo
  processo é trocar o bloco inteiro antes de cada frame (cópia de
  centenas de KB por frame, barato).
- O progresso compartilhado é um conjunto **declarado** de endereços da
  SRAM (itens, cristais, flags de baús e salas), sincronizado entre os
  estados a cada frame com regra de mescla (OU de flags, máximo de
  níveis). O ALttPO já mapeou quais são; o `z3randomizer` também.
- Os outros jogadores aparecem quando estão **na mesma sala/área**, como
  sprites por cima (`FrameDraw` já existe; ideal: OAM extra).
- Tela: local em **tela dividida** (dois frames lado a lado) ou em telas
  separadas; online, cada um na sua máquina.
- **Simulação de cada jogador continua idêntica ao jogo original**: o
  oráculo continua valendo por jogador.
- Limite: inimigos e puzzles não são compartilhados (cada um tem o seu).

### Modelo B — "mesmo mundo, vários Links" (estilo rail5 / Four Swords)

- Um estado de mundo; as regiões **do Link** (posição, direção, estado,
  animação, vida, item na mão, ancillas dele) viram por jogador e são
  trocadas na RAM viva antes de rodar cada Link.
- Exige decidir: **câmera** (segue o ponto médio; empurra os jogadores
  para dentro da tela; tela dividida quando se afastam), **transições**
  (quem passa pela porta leva o outro?), **itens** (quem pega, quem usa),
  **inimigos** (alvo mais próximo, como no rail5), **dano e morte**
  (respawn ao lado do outro?), **diálogos e cutscenes** (só um jogador
  "atua").
- É aqui que a F4d (RAM com nome) e a F4e (ganchos de alvo, dano,
  transição) pagam: cada decisão acima vira um gancho.
- A simulação deixa de ser a do original quando há dois Links; com um só,
  tem de continuar idêntica (portão).

## 3. Proposta

| Etapa | Entrega | Prova |
|---|---|---|
| **C1** | infraestrutura de **vários estados no mesmo processo** (trocar `g_ram`/SRAM/PPU por instância) e **tela dividida** local | dois jogos lado a lado, cada um passando no oráculo |
| **C2** | **Modelo A**: lista de progresso compartilhado + mescla + fantasmas dos outros jogadores na mesma sala | dois jogadores, um pega um item e ele aparece no outro |
| **C3** | rede para o Modelo A (protocolo simples; avaliar compatibilidade com o servidor do ALttPO) | duas máquinas |
| **C4** | **Modelo B** em cima de C1: estado do Link por jogador, câmera, transições, alvo, dano | os dois no mesmo quarto, lutando juntos |

C1–C2 dão um co-op jogável cedo e seguro. C4 é a etapa mais difícil e fica mais fácil depois da F4d e da F4e.3.

## 4. Decisões (09/11/26)

1. **Modelo A primeiro**, B depois (em cima da mesma base).
2. **Rede primeiro** (cada jogador na sua máquina); tela dividida local
   depois.
3. Compatibilidade com o servidor do **ALttPO**: **investigar o protocolo
   antes** de decidir.
4. Modelo B: **câmera compartilhada que se divide sozinha** quando os
   jogadores se afastam.

5. Depois do estudo (C0): **compatível com o ALttPO clássico já**
   (`0x02` + `0x13`), em vez de protocolo próprio.

### Ordem revisada

| Etapa | Entrega |
|---|---|
| **C0** | estudo do protocolo do ALttPO (cliente AngelScript + servidor): o que é sincronizado, formato, como os "fantasmas" são desenhados; recomendação de compatibilidade |
| **C1** | Modelo A pela rede: cada máquina roda o seu jogo; progresso compartilhado (lista declarada de SRAM + regra de mescla); fantasmas dos outros na mesma sala (OAM extra) |
| **C2** | tela dividida local (vários estados no mesmo processo) |
| **C3** | Modelo B (mesmo mundo, vários Links), câmera que se divide sozinha |

## 5. Decisões originais (para histórico)

1. Começar pelo **Modelo A** (C1–C2) e deixar o B para depois? *Recomendo
   sim.*
2. Local primeiro (tela dividida) e rede depois? *Recomendo sim.*
3. Tentar **compatibilidade com o servidor do ALttPO** na C3 (jogar com
   quem usa bsnes)? *Vale investigar o protocolo antes de decidir.*
4. No Modelo B, câmera **compartilhada com tela dividida automática**
   quando se afastam? *Recomendo; é o que mais funciona em jogos assim.*
