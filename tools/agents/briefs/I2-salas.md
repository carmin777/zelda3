# Tarefa I2-salas — importar as salas de masmorra de uma ROM de hack

Leia `AGENTS.md`, `docs/engine/MODS.md` (seção de ROM de hack, escrita no
I1), `docs/engine/ALTTI-MAPA.md` (com a **correção no topo**: tabelas que o
hack move têm de ser achadas pelo operando do código, nunca no endereço
fixo da US) e `assets/extract_resources.py` (`print_room`,
`decode_room_objects`, `get_chest_info`, `get_entrance_info`,
`pits_hurt_player`).

Esta tarefa é o **passo 3** do plano do ALTTI-MAPA: salas de masmorra.
Overworld é outra tarefa (não faça). ROMs fora do git:
`altti.sfc` (A Link to the Islands) e
`redux.sfc` (Redux, que muda 35 salas). Não commite ROMs, YAML extraído
delas, mods gerados nem imagens.

## Lição do I1 (leia com atenção)

No I1 o import leu a tabela de ponteiros do mapa de masmorra no endereço
fixo da US (`$8A:FBE4`), mas o hack a tinha movido para `$8A:FBB2`. Os
"ponteiros" lidos eram lixo (`$0A:2D14`, região de I/O), e a primeira
versão afrouxou o `assert (ea & 0x8000)` para engolir isso. **Não repita**:
se um ponteiro cair fora de uma janela de ROM, a tabela está errada; ache
a instrução do hack que a lê e use o operando dela.

## O que fazer

1. **Tabelas pelo código.** Para cada tabela que `print_room` e as funções
   que ele chama leem em endereço fixo (objetos `$1F:8000`, cabeçalhos
   `$04:F502` com a base `$04`, sprites `$89:D62E`, segredos `$81:DB69`,
   baús, `tele_msg` `$87:F61D`, pits `$80:990C`, entradas, starting
   points…), ache a instrução da rotina original que a lê (os comentários
   `// 8xxxxx` do C e o asm US ajudam) e leia o endereço do operando na ROM
   de origem, com conferência dos bytes vizinhos e erro claro se não
   reconhecer. Na ROM US o resultado tem que ser exatamente o de hoje.
   Liste no relatório cada tabela: endereço US, endereço no Islands, no
   Redux.
2. **Reverificar o volume de sprites.** O A0 disse que os sprites de
   masmorra do Islands estão "na metade baixa do banco `$09`" e somam
   83,5 KB, grande demais para `kDungeonSpriteOffs` (`uint16`). Isso pode
   ter sido a mesma leitura errada. Com a tabela certa, meça de novo e diga
   se cabe. Se **não** couber de verdade, pare nesse ponto (não mude o
   formato do C nesta tarefa) e relate o tamanho real.
3. **Extrair e compilar.** Com as tabelas certas, o `import-rom` extrai o
   YAML das 320 salas do hack (numa pasta temporária), compila os assets de
   masmorra (`kDungeonRoom*`, cabeçalhos, baús, segredos, sprites se
   couberem, `kDungeonRoomTeleMsg`, `kDungeonPitsHurtPlayer`, entradas e
   starting points se forem só de masmorra) e importa os que diferirem da
   US. Hacks com menos de 320 salas: preencha o resto com o que a US tem e
   diga quais. Dados inválidos (ponteiro 0, tipo desconhecido, `0xFF`):
   trate de forma explícita e liste o que foi trocado e por quê.
4. **Redux.** Rode o mesmo import no `redux.sfc`. O REDUX-MAPA diz 35
   salas diferentes (Ice Palace, Eastern Palace, Santuário, Skull Woods,
   Ganon's Tower…). Confira se as salas que o import marca como diferentes
   batem com essa lista.

## Prova

1. `make -j2` sem warnings; `tools/regress.sh` com todas as linhas `ok` e
   a linha final.
2. Base US e US+PT idênticas (SHA-1 do `.dat` gerado, como no I1); import
   PT idêntico (`diff -r`).
3. **Visual**: screenshots headless de **salas do hack** com o mod, e da
   mesma sala sem mod, em `.agents/shots/` (fora do git). Para entrar numa
   sala sem o overworld do hack, use o que for mais simples: um mod Lua de
   teste fora do repositório que coloque o Link numa entrada de masmorra,
   um save, ou o mecanismo de entrada/starting point. No Redux, mostre a
   sala do Eastern Palace com a estrela e uma do Ice Palace novo; no
   Islands, duas salas de masmorras diferentes.
4. Se for viável, compare com a **ROM do hack rodando no emulador do
   projeto** (`snes/`, como a W3b fez): mesma sala, mesma posição. Se não
   der, diga por quê.
5. `docs/engine/MODS.md` atualizado. Commit em inglês.

## Relatório

Além do pedido no AGENTS.md: a tabela de endereços (US/Islands/Redux), o
tamanho real dos sprites de masmorra do Islands, o que foi sanitizado, e
o que você não conseguiu provar.
