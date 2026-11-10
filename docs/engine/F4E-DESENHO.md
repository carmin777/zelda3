# F4e — comportamento novo: desenho para decisão

**Aprovado em 09/11/26, com as 5 recomendações da seção 5.**
Fontes: `REDUX-MAPA.md` (seções 7 e 8), `ALTTI-MAPA.md` (seção 3),
`RANDOMIZER-COMPAT.md` e os 730 desvios de `hooks.asm` do `z3randomizer`.

## 1. O problema

Hoje um mod pode **trocar dados** (assets, tabelas, salas, opções) e
**reagir a 18 eventos** em Lua. Não pode **mudar o que o jogo decide**: o
dano de um golpe, o item que um NPC dá, o que a pá desenterra, como um
inimigo se comporta, um item que não existia. Isso é a F4e.

## 2. O que os três consumidores pedem

Classifiquei os pedidos concretos que já temos por **tipo de gancho**:

| Tipo | O que é | Redux | Islands | Randomizer (`z3randomizer`) |
|---|---|---|---|---|
| **Evento** | "aconteceu X" (já existe, faltam pontos) | — | — | `OnFileLoad`, `OnFileCreation`, `OnDungeonEntrance/Exit`, `OnOWTransition`, `OnQuit`, `OnAga1/2Defeated`, `OnDrawHud`… |
| **Filtro de valor** | o jogo calcula um valor e o mod pode trocar antes de usar (como o `ItemReceive` de hoje) | dano do sword beam, fada cura 64, máximos de bombas e flechas, túnicas | recarga automática de magia | `LoadModifiedArmorLevel`, `LoadModifiedMagicLevel`, `LookupDamageLevel`, `ItemCheck_*` (item que cada NPC dá), `RNG_*` (RNG de chefes e drops), `CheckMedallionSword` |
| **Decisão** | o jogo pergunta "pode?" e o mod responde | Pikit rouba o escudo?, bloco já foi empurrado?, primeiro warp, virar durante o charge | espelho no Mundo Sombrio?, quebra-cabeça da praia (para onde a transição leva) | `SmithDoorCheck`, `DecideIfBunny`, `PreventEnterOnBonk`, `LockAgahnimDoors`, portas do sapo e do ferreiro |
| **Substituição** | trocar uma rotina inteira por outra | menu de 24 com timer, HUD com magia horizontal, pá que desenterra | — | lojas (`Sprite_ShopKeeper`), `QuickSwap`, `ProcessMenuButtons`, final e créditos |
| **Registro de conteúdo** | conteúdo novo: item, sprite, texto | item "pá permanente" | — | tabelas de itens novas (`newitems.asm`, `itemdatatables.asm`), progressivos |
| **Estado salvo do mod** | dado do mod que vai no save | timer, ordem do inventário | — | SRAM estendida (`sram.asm`), multiworld |

Conclusão: **filtro de valor** e **decisão** cobrem a maior parte dos três
consumidores e são baratos. Substituição e registro de conteúdo são
poucos, mas grandes.

## 3. Proposta de mecanismo

### 3.1 Ganchos nomeados por intenção

Cada gancho é um ponto **nomeado pelo que significa**
(`Filter_SwordBeamDamage`, `Decide_MirrorAllowed`), não pelo endereço. No
código do jogo:

```c
damage = MOD_FILTER(kModFilter_SwordBeamDamage, damage, ancilla_index);
if (!MOD_DECIDE(kModDecide_MirrorAllowed, true, world)) return;
```

- **Custo zero sem mod**: como o `MOD_EMIT` atual, uma máscara diz se
  alguém assinou; sem assinante, o macro devolve o valor original e o
  portão prova que nada mudou.
- Cada gancho documentado em `HOOKS-MAPA.md` com o **endereço da rotina
  original** (`// 8xxxxx`). Isso liga direto aos desvios do
  `z3randomizer`, que também são por endereço.
- **Ganchos entram sob demanda**, cada um com um consumidor real, um mod
  de exemplo e um teste. Não abrimos 730 de uma vez.

### 3.2 Duas portas para o mesmo gancho

- **Lua** (`zelda.filter("SwordBeamDamage", fn)`, `zelda.decide(...)`)
  para mods comuns, com o sandbox e os limites de hoje.
- **C interno** (a mesma tabela de ganchos, chamada direto) para módulos
  pesados que moram no repositório, como o núcleo do randomizer
  compatível. Rápido e provado pelo oráculo.
- **Plugin nativo externo** (`.so`/`.dll`) fica **fora** da F4e: é o que
  co-op e renderizador alternativo vão precisar, com riscos próprios
  (segurança, ABI). Proponho tratar numa fase separada.

### 3.3 Substituição de rotina

Para os poucos casos grandes (menu, HUD, loja, comportamento de um tipo
de sprite): `zelda.override("Sprite:Pikit", { tick = fn })` ou o
equivalente em C. A rotina original continua chamável de dentro do mod
(`original()`), para o mod mudar só uma parte. Sprites dependem de acesso
por nome aos campos do slot; `zelda.state` já cobre boa parte, e a F4d
(buffers nomeados) completa.

### 3.4 Estado salvo do mod

Duas formas, para dois públicos:
- **Arquivo ao lado do save** (`save1.sav` + dados do mod, por id), para
  mods comuns. Não mexe no layout da SRAM, então o save continua abrindo
  sem o mod.
- **SRAM estendida com endereços fixos**, só para camadas de
  compatibilidade que precisam bater com o original (o randomizer, por
  causa do SNI e do Archipelago, que leem `sram.asm` do `z3randomizer`).

### 3.5 Registro de conteúdo

Itens novos são a parte mais cara: id, gráfico de recebimento, texto,
efeito ao usar, espaço no menu, máximo. Proponho deixar para a última
etapa e começar só com **reaproveitar** itens e slots existentes (que já
cobre o Redux e o núcleo do randomizer).

## 4. Fases propostas

| Etapa | Entrega | Prova | Tamanho |
|---|---|---|---|
| **F4e.1** | mecanismo `MOD_FILTER`/`MOD_DECIDE` + Lua + os ~12 ganchos do Redux e do Islands (sword beam, fada, máximos, túnicas, Pikit, blocos, warp, espelho no DW, transição da praia, magia) | portão verde; cada gancho com mod de exemplo e replay de feature | médio |
| **F4e.2** | eventos que faltam (`FileLoad`, `FileCreate`, `DungeonEnter/Exit`, `Quit`…) e estado salvo do mod (arquivo ao lado) | idem | pequeno |
| **F4e.3** | substituição de rotina, começando por um tipo de sprite e pelo menu/HUD | um inimigo com comportamento novo; menu do Redux completo | grande |
| **F4e.4** | núcleo do randomizer em C sobre os mesmos ganchos (R3), recurso por recurso | **oráculo contra a ROM de uma seed do alttpr.com** | grande, contínuo |
| **F4e.5** | registro de itens novos | um item inédito de ponta a ponta | grande |

Co-op e a API de renderização (voxel) ficam para fases próprias depois da
F4e. Muito do que a F4e.1–3 produz (estado do Link por nome, ganchos de
dano e colisão) é pré-requisito do co-op.

## 5. Decisões em aberto

1. **Ganchos sob demanda** (cada um com consumidor e teste) em vez de
   abrir muitos de uma vez? *Recomendo sob demanda.*
2. **Núcleo do randomizer em C dentro do repositório**, usando os mesmos
   ganchos que um mod Lua usaria, em vez de ser um mod Lua? *Recomendo C
   interno: velocidade, prova pelo oráculo e SRAM compatível com o SNI.*
3. **Plugins nativos externos fora da F4e**, numa fase própria para
   co-op e renderizador? *Recomendo sim.*
4. **Estado salvo em arquivo ao lado do save** para mods comuns, e SRAM
   estendida só para compatibilidade? *Recomendo sim.*
5. **Ordem**: F4e.1 → F4e.2 → F4e.3, com a F4e.4 começando assim que a
   F4e.1 existir (ela usa filtros e decisões)? *Recomendo sim.*
