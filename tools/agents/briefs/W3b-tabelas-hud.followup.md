Revisão 2 do W3b. Olhei a montagem inteira: 33 dos 35 painéis estão certos (ótimo). Problemas restantes, só no arco:
- "01 bow 2" (US "BOW & ARROWS") mostra "Arco e / Flech:" — "Flech:" truncado.
- "01 bow 3" (US "BOW") mostra também "Arco e / Flech:" — deveria ser algo como "Arco".
- "01 bow 4" está certo ("Arco e / Flecha de Prata").

A montagem em runtime está adivinhando a composição. E está no lugar errado: **a engine C não pode conter conhecimento de uma tradução específica**.

1. **Remova `ModTable_PrepareOverride()`** e qualquer reconhecimento de assinatura de tradução do C. O C só aplica `table_overrides` como bytes.
2. **Descubra a verdade com a própria ROM PT**: o emulador do projeto (`snes/`, usado em `src/zelda_cpu_infra.c`) roda código 65816 de uma ROM. Rode a `portuguese.sfc` no emulador num estado em que o Link tem o arco em cada variante (2, 3 e 4 — os mesmos estados da montagem; dá para partir de um snapshot do replay e escrever a variável do arco na RAM do emulador), abra o menu (ou chame a rotina que desenha o nome do item na ROM PT) e leia o tilemap/VRAM que **a ROM PT** escreve para o painel do nome. Isso diz exatamente quais palavras de tile a tradução mostra em cada estado. Se for inviável rodar o fluxo, leia a rotina da ROM PT que indexa a tabela do arco (compare com a US: `src/hud.c` e o comentário de endereço) e derive o índice real.
3. **Corrija no modkit** (`tools/modkit.py import-rom`): uma etapa de "ajuste de layout" que monta a tabela no **layout US** que o zelda3 espera, com o conteúdo que a ROM PT mostraria em cada estado. Documente em `docs/engine/MODS.md` o que a tradução faz diferente e como o import resolve. A tabela gravada em `build/` passa a ser a já ajustada.
4. Regenere `mods/ptbr-hud` e a montagem `.agents/shots/w3b-itens.png` (só as linhas do arco já bastam, mas pode refazer tudo). `make -j2`, `tools/regress.sh` com todas as linhas `ok` (17 + 2 features). Commit e relatório dizendo o texto de cada variante do arco segundo a ROM PT.
