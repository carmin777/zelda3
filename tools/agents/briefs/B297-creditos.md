# Tarefa B297-creditos — upstream #297

"Credits run too fast": no final, as cenas andam mais rápido que a música,
ficam fora de sincronia e a música é cortada no fim. Config do relato:
"SNES PPU" ligado, `DisableFrameDelay` desligado, `MiscBugFixes` ligado.
Leia `docs/engine/BUGS-PROTOCOLO.md`.

Hipóteses a testar com método:
- O jogo original espera o APU em algum ponto do final (porta APU, fim da
  música) e o C trata isso como instantâneo (procure no código do final,
  `src/ending.c`, leituras de porta APU / `ZeldaIsMusicPlaying` /
  `music_unk1` / `last_music_control` — `src/audio.c` linha ~115 tem um
  caso com `MiscBugFixes`).
- O ritmo de frames do loop principal (`src/main.c`: atraso por frame,
  `DisableFrameDelay`, vsync, turbo) roda acima de 60,0988 Hz em alguma
  configuração, e o áudio (tempo real) não acompanha.
- `MiscBugFixes` muda algo no final.

1. O replay de referência 12 ("After Ganon's Tower") vai até o final?
   Veja com `--mod-trace` (módulos do ending). Se for, o oráculo cobre a
   lógica do final com features desligadas: confira se passa (o regress
   já roda; diga em que frame começa o final e quantos frames dura).
2. Meça, no headless, quantos frames o final leva **com** e **sem**
   `MiscBugFixes` (feature replay, compare off) e compare com o esperado
   (o original roda a ~60,1 fps; se souber a duração real da música do
   final em segundos, compare frames/60,1 com ela — o tocador SPC está em
   `src/spc_player.c`).
3. Ache a causa ou descarte as hipóteses com evidência. Se corrigir, a
   correção não pode mudar nada com features desligadas (regress normal).
   Se não achar, relate o que mediu. Não invente correção.
