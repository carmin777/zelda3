# Regras para agentes neste repositório

Você está trabalhando no zelda3 (reimplementação em C do Zelda: A Link to the
Past). O objetivo do projeto está em `docs/engine/PLANO.md`. Sua tarefa está no
briefing que veio na mensagem; faça só o que ele pede.

## Obrigatório

1. **Não mude o comportamento do jogo sem mods.** O jogo é verificado frame a
   frame contra a ROM original. Qualquer byte diferente na RAM é um bug seu.
   Código novo de mods deve ser no-op quando nenhum mod estiver ativo.
2. **Nunca adicione ao git**: `*.sfc`, `*.zip`, `*.dat`, `zelda3.user.ini`,
   `saves/save*`, binários compilados. ROM e assets são do usuário.
3. **Compile antes de terminar**: `make -j2` (a máquina tem pouca RAM, não use
   mais que -j2). Zero warnings novos.
4. Se `tools/regress.sh` existir, rode e só termine com ele verde. Se ele
   falhar por causa da sua mudança, conserte; não altere os hashes de
   referência para "fazer passar".
5. **Commite** o resultado na sua branch atual com uma mensagem clara em
   inglês. Não mude de branch, não faça push, não mexa em outros worktrees.
6. Estilo: C99/C11, 2 espaços, siga o estilo do arquivo que estiver editando.
   Código novo de engine/mods vai em `src/mod/`.
7. **Preserve o fim de linha** de cada arquivo: vários `.c/.h` do jogo são
   CRLF (`git ls-files --eol <arquivo>`). Se editar com script, abra em modo
   binário ou com `newline=''`; um diff que troca o arquivo inteiro está
   errado.
8. Ao rodar `tools/regress.sh`, confira que apareceu uma linha `ok` para
   cada replay e a linha final `regress: all N replays passed`.
9. Não invente: se algo do briefing não bater com o código, diga isso no
   relatório em vez de adivinhar.

## Relatório final

Termine com uma mensagem curta contendo:
- o que mudou (arquivos e o porquê),
- comandos que você rodou para verificar e o resultado real,
- o que ficou faltando ou o que você não tem certeza.
