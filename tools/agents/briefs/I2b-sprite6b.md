# Tarefa I2b-sprite6b — o import de salas para de pular o soldado do canhão

Leia `AGENTS.md`, `docs/engine/MODS.md` (import de salas, I2) e o histórico:
o I2 pulava o sprite `0x6B` (CannonTrooper) porque o C terminava em
`assert(0)`; o S6B implementou o sprite igual ao assembly (replay
`tests/replays/cannon-trooper.sav`).

## O que fazer

1. Em `assets/extract_resources.py`/`tools/modkit.py`, deixe de pular o
   `0x6B` no import de salas (Redux sala 176; Islands salas 184 e 216) e
   atualize a lista de saneamento e o `MODS.md`.
2. Prova visual: entre nessas salas com o mod de cada hack (mesma técnica
   do I2, mod Lua de teste fora do repositório) e tire screenshots do
   soldado (PNGs em `.agents/shots/`, fora do git); rode uns 600 frames
   ali sem crash.
3. Base intacta: reconstrução isolada (`git archive`, ROM US, `-e`,
   `--languages pt` com `assets/dialogue_pt.txt` e `assets/font_pt.png`
   copiados do checkout principal) com SHA-1
   `188bfc8630e55345dc4dfea0ef16bb9bff2cd483`.

## Verificação a relatar

`make -j2`; `tools/regress.sh` verde com todas as linhas; saídas dos
imports do Redux e do Islands (o 6B não aparece mais como pulado).
