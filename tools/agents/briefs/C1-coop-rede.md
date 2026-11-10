# Tarefa C1-coop-rede — co-op pela rede compatível com o ALttPO clássico

Leia `AGENTS.md`, `docs/engine/COOP-DESENHO.md` (decisões: modelo A
primeiro, rede primeiro) e **`docs/engine/COOP-ALTTPO.md`** (o estudo do
protocolo). Decisão (09/11/26): **compatível com o ALttPO
clássico (protocolo externo `0x02`, serialização `0x13`) desde já.**

## Regras

- O ALttPO não declara licença: **implemente a partir do `COOP-ALTTPO.md`
  e da sua leitura do protocolo, sem copiar código** de
  `.agents/refs/alttpo/`.
- **Nunca conecte a servidores públicos** (`alttp.online` ou outros) nos
  testes. Escreva um **servidor de repasse local** mínimo em Python
  (`tools/coop_relay.py`, sem dependências novas) que faça o que o servidor
  do ALttPO faz (grupos, repasse) e use só `127.0.0.1`.
- Desligado por padrão: seção nova no `zelda3.ini` (`[Coop]`: `Enabled = 0`,
  `Server`, `Port = 4590`, `Group`, `PlayerName`, `PlayerColor`). Desligado =
  nenhum socket, nenhum custo; sem co-op, o jogo é idêntico (portão).

## O que fazer

1. **Cliente** em `src/mod/` (ex. `coop.{c,h}`), UDP não bloqueante,
   atendido entre frames: entrar no grupo, mandar e receber as mensagens
   `0x02`/`0x13` no formato do estudo (versão, frame, TLVs).
2. **Progresso compartilhado**: a lista de offsets da SRAM e as regras de
   mescla (maior valor, OR de bits, mutações) do estudo, aplicadas ao estado
   do zelda3 entre frames. Ao aplicar mudança vinda da rede, a comparação
   lado a lado desliga (como mods).
3. **Fantasmas**: posição, sala/área, OAM do Link remoto (até 12 sprites),
   tiles e paleta, desenhados por cima quando o outro está na mesma
   sala/área, sem entrar na simulação. Nome e cor opcionais.
4. **Modo corrida** (só ver, sem compartilhar) como opção.
5. `docs/engine/COOP.md`: como ligar, o que é compartilhado, o que ainda
   não (sons remotos, ancillas, tilemap, PvP), e como testar com o servidor
   local.

## Verificação a relatar

1. `make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com
   todas as linhas `ok` e a linha final.
2. **Duas instâncias headless** do zelda3 + `tools/coop_relay.py`, no mesmo
   grupo: (a) uma pega um item (replay ou roteiro) e a outra passa a ter o
   item (cole `--dump-state` dos dois lados); (b) as duas na mesma sala: a
   screenshot de uma mostra o fantasma da outra (PNG em `.agents/shots/`,
   fora do git); (c) modo corrida: o item não passa, o fantasma aparece.
3. Um teste de **formato**: capture os pacotes que o zelda3 manda e mostre
   que batem, campo a campo, com o formato do estudo (`0x02` + `0x13`).
4. Diga claramente o que não foi testado: **interoperabilidade real com o
   bsnes + ALttPO** (não temos o cliente rodando aqui), NAT, latência.
