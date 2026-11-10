# Tarefa C2-tela-dividida — co-op local em tela dividida

Leia `AGENTS.md`, `docs/engine/COOP-DESENHO.md` (ordem revisada: C2 =
tela dividida local) e **`docs/engine/COOP.md`** (o co-op pela rede da C1,
compatível com o ALttPO clássico).

## Ideia

Modelo A local: **dois jogos completos no mesmo processo**, cada um com o
seu estado (RAM, SRAM, VRAM/PPU, áudio de cada um misturado ou só do
jogador 1), lado a lado na mesma janela, com dois controles. O progresso e
os fantasmas usam **o mesmo código da C1**, só que trocando os pacotes em
memória (sem socket) entre as duas instâncias.

## O que fazer

1. **Várias instâncias**: descubra o que é estado global hoje (`g_ram`,
   `g_zenv`, PPU, DMA, APU/SPC, `g_emu_*`, buffers de áudio, estado dos
   mods/Lua) e crie a troca de instância por frame (salvar/restaurar ou
   ponteiros), com o menor custo possível. Diga quanto custa por frame.
2. **Tela dividida**: a janela mostra os dois jogos lado a lado (ou em cima
   e embaixo, opção), cada um com a sua escala; os dois controles mapeados
   (segundo jogador com o segundo gamepad ou teclas configuráveis em
   `[KeyMap2]`/`[GamepadMap2]`).
3. **Ligação local**: as duas instâncias trocam progresso e fantasmas pelo
   mesmo código de `src/mod/coop.c`, com um transporte em memória.
4. Opção no `zelda3.ini` (`[Coop]` `Local = 0`), desligada por padrão; sem
   ela, nada muda (portão).
5. `docs/engine/COOP.md`: seção "Local (tela dividida)".

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final. Headless com duas instâncias e roteiros de
entrada diferentes: uma pega um item e a outra passa a ter; screenshot da
tela dividida com o fantasma (PNG em `.agents/shots/`, fora do git). **Cada
instância, sozinha e sem co-op ligado, tem de continuar idêntica ao jogo
original** (rode um replay de referência com `compare: on` na instância 2).
Teste a janela num display virtual (Xvfb) como o R3a fez e diga o que não
testou.
