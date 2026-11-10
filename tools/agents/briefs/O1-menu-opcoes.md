# Tarefa O1-menu-opcoes — menu de opções dentro do jogo

Leia `AGENTS.md`, `docs/engine/UPSTREAM-TRIAGEM.md` (onda 5: ficou escolhido trazer o menu de opções em 10/11/26), `docs/engine/MODS.md` e o
`zelda3.ini` (todas as seções e teclas).

Referência: o fork `hyukishi/zelda3` tem um menu de configurações dentro do
jogo com troca de teclas. **Confira a licença dele antes de olhar código**:
se não for compatível com MIT, só estude o comportamento (capturas, README)
e escreva do zero; se for MIT, ainda assim escreva do nosso jeito e dê
crédito no doc. Diga no relatório qual foi o caso.

## O que fazer

1. Um menu sobreposto ao jogo (tecla configurável, padrão livre que não
   colida com nada do `zelda3.ini`; confira contra `kDefaultKbdControls`),
   que pausa o jogo enquanto aberto.
2. Páginas: **Vídeo** (escala, tela cheia, renderizador, widescreen,
   plugin de render ligado/desligado), **Som** (volume, MSU),
   **Gameplay** (as opções de `[Features]`), **Mods** (lista dos mods com
   liga/desliga e prioridade, avisando quando precisa reiniciar),
   **Teclas** (trocar teclas do teclado e do controle).
3. Gravar as mudanças num arquivo do usuário (`zelda3.user.ini`, que já é
   ignorado pelo git), sem reescrever o `zelda3.ini` do repositório.
4. Desenho com o que o motor já tem (fonte do jogo ou overlay do host);
   funcionar em 224/240 linhas e widescreen; o voxel (plugin) desligado ou
   ligado não pode quebrar o menu.
5. Sem abrir o menu, nada muda (portão). Documente em `docs/engine/MENU.md`.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final; screenshots de cada página em
`.agents/shots/` (fora do git); um teste headless que abre o menu, muda uma
opção e confere o `zelda3.user.ini` gerado.
