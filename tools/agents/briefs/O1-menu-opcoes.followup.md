# O1 — rodada 3: opção Mono na página Som

Traga a branch para o estado atual (`git merge engine/modloader`); `git status`
limpo antes.

Uma caixa de som do lado esquerdo ruim deixou os efeitos com pan quase
inaudíveis; a solução foi tocar em mono. O motor já faz o downmix quando
`[Sound] AudioChannels = 1` (`dsp_getSamples` em `snes/dsp.c` e o caminho do
`ZeldaRenderAudio`), mas só no início.

1. Na página **Som** do menu, uma linha **Mono** (ON/OFF) que grava
   `AudioChannels = 1` / `2` no `zelda3.user.ini`, como as outras.
2. Se der com segurança, aplique na hora (fechar e reabrir o dispositivo de
   áudio do SDL com o mutex de áudio do jogo); se não, mostre o mesmo aviso de
   "vale ao reiniciar" que a página Mods usa, e documente.
3. Teste headless como o `tools/test-menu.sh`: mudar a opção grava a linha
   certa no ini gerado num diretório temporário (nunca no ini real).

Docs e comentários impessoais (`docs/engine/MENU.md`). Verificação: `make -j2`,
`make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas as linhas `ok`
e a linha final; screenshot da página Som em `.agents/shots/`.
