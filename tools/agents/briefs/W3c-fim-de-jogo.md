# Tarefa W3c-fim-de-jogo — letreiro "FIM DO JOGO" da tradução PT

Leia `AGENTS.md`, `docs/engine/MODS.md` (seções de `table_overrides` e
"Importar gráficos de uma ROM traduzida") e `src/mod/game_tables.{def,c,h}`.

## O problema (verificado)

Com o mod local `mods/ptbr-graficos` (gerado por
`tools/modkit.py import-rom portuguese.sfc mods/ptbr-graficos`), a tela de
morte mostra **"FIMD OJDG"**. Os gráficos das letras vieram da ROM PT, mas a
tradução também **reescreveu o código** do letreiro: passou de 8 letras
("GAME OVER") para **9** ("FIM DO JOGO"), com tabelas novas. O zelda3 ainda
desenha 8 letras com as tabelas US.

Código C: `Death_InitializeGameOverLetters` (`src/messaging.c`, `// 8afe20`),
`GameOverText_SweepLeft` (`// 88f4f6`), `GameOverText_UnfurlRight`
(`// 88f56d`), `GameOverText_Draw` (`src/ancilla.c`, `// 88f5c4`). Os três
`.c` são **CRLF**.

Diferenças da ROM PT (`./portuguese.sfc`, LoROM) que eu já decodifiquei —
confira você mesmo antes de usar:

| Local | US | PT |
|---|---|---|
| 88F4FF `CPX #` | 07 | 08 |
| 88F514/88F55B `CMP abs,X` (Tab1) | $F4EE | $F5B4 |
| 88F523 `CPX #` (fim da varredura) | 08 | 09 |
| 88F527 `LDA #` (última letra) | 07 | 08 |
| 88F539 `CPX #` | 07 | 08 |
| 88F53D `LDY #` | 06 | 07 |
| 88F57E | `LDA $0C04,X; CMP $F565,Y` | `JML $A08024` → Tab2 em `$A0:8065` (9 bytes) |
| 88F593 `CMP #` | 08 | 09 |
| 88F5EF / 88F604 | `LDA $F5B4,X` (só o tile, atributo fixo `#$3C`) | `JML $A08000 / $A08013`: tabela em `$A0:8041`, 4 bytes por letra = tile de cima, atributo, tile de baixo, atributo (índice `k*4`) |
| 8AFE20 init | 8 letras em `$0C04..`/`$0C18..`; `$039D = 6` | 9 letras (`$0C04..$0C0C = $B0`, `$0C18..$0C20 = 0`); `$039D = 6` igual |

Tabelas PT: Tab1 = `30 40 50 68 78 90 a0 b0 30`, Tab2 = `50 58 60 70 78 88
90 98 a0`, letras = F I M D O J O G O (o "I" usa o tile 41 de cima e o 41
com atributo `$BC` — espelhado — embaixo).

## O que fazer

1. **Engine (sem mudar nada sem mod)**: o letreiro passa a ser guiado por
   dados, com defaults **idênticos** ao jogo US:
   - número de letras `N` (padrão 8; aceitar 2..10 — os vetores de ancilla
     têm 10 posições; valor fora disso → mod recusado na carga, ou limitado
     com motivo documentado);
   - Tab1 e Tab2 (posições), até 10 entradas;
   - tiles **com atributo** por metade (cima/baixo), até 10 letras; o
     default usa o atributo `0x3C` de hoje.
   - Troque as constantes 8/7/6 do código pelas derivadas de `N`
     exatamente como a ROM PT faz (tabela acima). Com `N = 8` o código tem
     que fazer o mesmo que hoje, byte a byte.
   - Exponha essas tabelas no registro `game_tables.def` (`table_overrides`).
     Hoje o registro só tem tabelas `uint16` lidas no `rom_addr` da ROM US.
     Se precisar de tabelas de bytes ou de tabelas sem leitura direta da
     ROM, estenda o formato do `.def` de forma limpa e mantenha
     `tools/modkit.py` lendo o mesmo arquivo (fonte única). Explique a
     escolha no relatório.
2. **modkit `import-rom`**: detecte o patch do letreiro **decodificando a
   ROM traduzida** (operandos das instruções nos endereços acima: contagem
   do `CPX #` em 88F523, endereço da Tab1 no `CMP` de 88F514, alvo dos
   `JML` e os operandos `CMP abs,Y` / `LDA long,X` das rotinas novas, com o
   banco de dados que elas usam). Sem hash da ROM fixo no código. Se os
   bytes forem os da ROM US, não gere nada; se não reconhecer o formato,
   avise e pule (não invente). Escreva os overrides no mod como as outras
   tabelas.
3. Regere o mod local (`mods/` está no `.gitignore`; **não commite**) e
   mostre o resultado.
4. **Prova visual**: screenshot headless (`--screenshot`) da tela de morte
   com o mod, depois de as letras pararem, PNG em `.agents/shots/`
   (fora do git; não commite imagens do jogo). Para chegar à morte, use o
   que for mais simples, por exemplo um mod Lua de teste fora do repositório
   que zere a vida do Link (`zelda.link`/`zelda.state`). A partir do frame
   em que o letreiro aparece, tire screenshots em intervalos até ele
   assentar. Faça o mesmo **sem** o mod e confira que continua "GAME OVER".
   Se der, compare com a ROM PT rodando no emulador do projeto (`snes/`),
   como a W3b fez: posições X das 9 letras e tiles na OAM.
5. Teste do caminho sem mod: `make -j2` sem warnings, `tools/regress.sh`
   com todas as linhas `ok` e a linha final. Nenhum replay de referência
   morre? Então diga se algum replay passa pela tela de morte (procure a
   transição de módulo para 0x12 com `--mod-trace`). Se nenhum passar,
   grave um replay curto de morte **sem mods** em `tests/replays/`
   (protocolo em `docs/engine/BUGS-PROTOCOLO.md`, passo 5), para que o
   oráculo compare o código novo com N = 8 contra o assembly.
6. `docs/engine/MODS.md`: documente as tabelas novas e a importação.
   Commit em inglês.

## Relatório

Além do pedido no AGENTS.md: as tabelas lidas da ROM PT (bytes), o formato
que você escolheu no `.def`, e o caminho/frame dos screenshots.
