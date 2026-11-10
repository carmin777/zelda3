# Revisão do IX — a fonte importada não casa com um .dat de vários idiomas

Bom trabalho (o diálogo e o Link do Islands aparecem certos). Mas no
checkout local o mod do Redux é **recusado**:

```
mods: refused 'redux': asset 'kDialogueFont' override
'mods-redux/redux/build/kDialogueFont.bin' has 4201 bytes, expected 8424
(allow_resize is false)
```

O `zelda3_assets.dat` local foi montado com `--languages pt`
(US+PT), e o `kDialogueFont` dele tem 8424 bytes (as fontes dos dois
idiomas). O import comparou e gerou a fonte contra uma base só US (4201).
O mesmo risco vale para o `kDialogue` (que hoje passou, confira por quê).

## O que fazer

1. Descubra no C como `kDialogueFont` e `kDialogue` são lidos com vários
   idiomas (slot por idioma? índice? tamanho fixo por fonte?).
2. Faça o `import-rom` gerar esses assets **compatíveis com o `.dat` que
   está em uso** (o modkit sabe qual é o `base_dat`): por exemplo, montar a
   fonte e o diálogo do hack com os mesmos idiomas do `.dat` base (o hack
   entra no slot `us` e os outros slots ficam com a base), ou marcar
   `allow_resize` só se o C aceitar tamanho variável de verdade. Diga qual
   escolheu e por quê.
3. Teste com um `.dat` **US+PT** (o local: monte numa cópia isolada
   como nos outros testes, ou use o `zelda3_assets.dat` do checkout
   principal só para leitura) e com um `.dat` só US: o mod do Redux e o do
   Islands têm de **carregar** (`mods: loaded`) nos dois, e o texto do hack
   tem de aparecer. Cole as saídas.
4. `make -j2`, `tools/regress.sh` verde, import PT idêntico (`diff -r`).
   Commit novo.
