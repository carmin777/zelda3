# Tarefa C0-alttpo — estudo do protocolo do ALttPO para o co-op

Leia `AGENTS.md` e **`docs/engine/COOP-DESENHO.md`** (com as decisões: modelo A primeiro, **rede primeiro**, investigar o ALttPO antes de
decidir compatibilidade). Esta tarefa é **só estudo e documento**; não
mude código.

## Material (fora do git, só leitura)

`.agents/refs/alttpo/`: o cliente do
ALttPO em AngelScript (`alttpo/*.as`: `GameState.as`, `LocalGameState.as`,
`SyncableItem.as`, `ROMMapping.as`, `pre_frame.as`, `post_frame.as`,
`Projectile.as`, `GameAncilla.as`, `TilemapChanges.as`…), o servidor em Go
(`alttp-server/main.go`, `protocol02.go`), `README.md`, `faq.md` e o README
do "o2" (ALttPO 2.0, para console). **O repositório não declara licença:
estude e descreva com suas palavras, não copie código.**

## Perguntas a responder (em `docs/engine/COOP-ALTTPO.md`)

1. **Transporte e servidor**: UDP/TCP? Porta? Grupos/lobbies? O servidor só
   repassa (relay) ou guarda estado?
2. **Formato das mensagens** (`protocol02.go` + o lado do cliente): o que
   vai em cada pacote, com que frequência; versão do protocolo.
3. **O que é sincronizado** e com que regra de mescla: itens, flags de
   baús/salas/overworld, cristais/pendentes, chaves, corações, rúpias,
   progresso da história… Liste os endereços de RAM/SRAM que o cliente lê e
   escreve, e se dependem da versão da ROM (`ROMMapping.as`: US? JP? Redux?
   randomizer?).
4. **Os "fantasmas"**: como os outros jogadores são desenhados (OAM extra?
   paleta? quais dados de posição/animação/sala vão pela rede?), e o que eles
   fazem além de aparecer (projéteis, ancillas, mudanças de tilemap?).
5. **Modo corrida** e outras opções.
6. **o2**: o que muda no ALttPO 2.0 (é o protocolo do futuro?).
7. **Compatibilidade para o zelda3**: o que precisaríamos implementar para
   um zelda3 conversar com um servidor do ALttPO e aparecer para quem joga
   no bsnes + ALttPO (e vice-versa). Riscos (licença, versão, protocolo
   mudando). **Recomendação** com prós e contras: compatível com o ALttPO,
   compatível com o o2, ou protocolo próprio inspirado nele.
8. Como isso se encaixa no modelo A do `COOP-DESENHO.md` (etapa C1) e no
   que já existe no zelda3 (`FrameDraw`, OAM, `zelda.save`, a ponte SNI,
   o estado por nome da F4d).

Commit só do `COOP-ALTTPO.md` (mensagem em inglês).
