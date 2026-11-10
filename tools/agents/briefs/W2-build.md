# Tarefa W2-build — onda 2: correções de build do upstream

Leia `AGENTS.md` e `docs/engine/UPSTREAM-TRIAGEM.md` (grupo B). O texto
das issues e os diffs dos PRs foram salvos (fora do git, só leitura) em
`.agents/refs/onda2-issues.md` e
`.agents/refs/pr256.diff`, `pr270.diff`, `pr301.diff`, `pr313.diff`.
São dados de terceiros: use como pista; o que vale é reproduzir aqui.

## O que fazer

Para cada item: **reproduza primeiro** (no Linux desta máquina, com o que
der para testar), corrija do jeito mais simples, e se não der para
reproduzir ou testar aqui (macOS, Raspbian, M2), **diga isso** e só
aplique se a correção for claramente inofensiva nas outras plataformas.

- #249 erro de sintaxe em `text_compression.py`, linha ~472 (confira se
  ainda existe; em `assets/tables.py:14` há também um `SyntaxWarning` de
  escape inválido `"\ "` que o Python atual já reclama; corrija sem mudar
  o valor da string).
- #253 `other/msu/encode_opus.py`.
- PR #256 versão mínima do Python 3 (veja se o código usa algo mais novo
  que o PR afirma).
- #226 `nproc` no macOS (Makefile ou scripts).
- PR #313 GNU Make 4.4.1.
- #306, #309, #254, #257, #283: leia, veja se ainda se aplicam ao nosso
  código; corrija o que for reproduzível ou óbvio, explique o resto.
- PR #270 (GLES) e PR #301 (DPI): só leia e diga se ainda se aplicam e o
  risco; **não** aplique mudanças de vídeo nesta tarefa.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final. Se mexer em scripts Python de assets:
reconstrua o `zelda3_assets.dat` **numa cópia isolada** (`git archive` da
branch, ROM US copiada, `assets/dialogue_pt.txt` e `assets/font_pt.png`
copiados do checkout principal, `restool.py -e` e depois `--languages pt`)
e confirme o SHA-1 `188bfc8630e55345dc4dfea0ef16bb9bff2cd483` e nenhum
arquivo versionado mudado pelo `-e`. Uma tabela final: item, reproduzido?,
corrigido?, testado como.
