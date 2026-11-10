# Corrige todos os bugs (#126)

Mod só de dados: liga `MiscBugFixes` e `GameChangingBugFixes`, as duas opções
de correção que o zelda3 já tem. `MiscBugFixes` cobre as correções que não
mudam o fluxo do jogo; `GameChangingBugFixes` cobre as que mudam
comportamento de propósito. Nenhum código C foi adicionado.

## Pedido

Issue #126 do `snesrev/zelda3` ("corrigir todos os bugs"), citada no grupo D
da triagem. Os PRs #181/#187 já entraram como opções, não como padrão; este
mod só empacota as duas para quem quiser o pacote completo.

## Como testar

```sh
mkdir -p /tmp/mods-bugs
cp -a mods-examples/corrige-bugs-126 /tmp/mods-bugs/
./zelda3 --config tests/regress.ini --headless --mods-dir /tmp/mods-bugs \
  --frames 60 zelda3.sfc
```

O `stderr` deve mostrar `mods: loaded 'corrige-bugs-126'` e as linhas
`mods: feature 'MiscBugFixes' = on` e
`mods: feature 'GameChangingBugFixes' = on`.
