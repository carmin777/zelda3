# Revisão do I1 — os ponteiros `$0A:2D14` eram lixo; corrigir

Bom trabalho no resto (tilemaps pelo NMI, base idêntica, PT idêntico).
Mas a explicação dos ponteiros está errada, e a correção mascarou um bug.

## O que eu conferi

- Em LoROM, `$0A:2D14` com o banco de dados `$0A` é região de I/O
  (`$2000-$5FFF` dos bancos `$00-$3F`), **não ROM**. O hardware não leria o
  arquivo em `0x52D14`.
- A rotina que usa a tabela dos ponteiros do mapa de masmorra, em
  `$8A:E63F` (US: `C2 20 BD E4 FB 85 0C` = `LDA $FBE4,X ; STA $0C`), no
  Islands é `C2 20 BD B2 FB 85 0C` = **`LDA $FBB2,X`**. O hack moveu a
  tabela para `$8A:FBB2`, onde estão ponteiros válidos
  (`$FBCE, $FBCF, $FBE5, $FBF3, …`, todos na metade alta).
- `assets/compile_resources.py:448` lê sempre `0x8AFBE4` (endereço fixo
  da US). No Islands, isso pega outros dados (`14 2d 2a 2d …`) como se
  fossem ponteiros. Por isso apareceu `$0A:2D14`, e por isso o
  `kDungMap_Tiles` cresceu de 214 para 258 bytes: o import dele está
  errado.

## O que fazer

1. **Volte o `assert (ea & 0x8000)`** em `assets/util.py` `get_byte`.
   Endereço na metade baixa de banco LoROM `$00-$3F`/`$80-$BF` deve
   continuar sendo erro (se um banco `$40-$6F`/`$C0-$EF` aparecer, trate
   esse caso explicitamente e diga se apareceu).
2. Em `print_dungeon_map` (e em toda leitura de tabela com **endereço fixo**
   que o import do hack usa: confira cada `ROM.get_word/get_byte(0x8.....)`
   das funções que entram no import), leia o endereço da tabela **do
   operando da instrução que a usa**, como você fez para os tilemaps:
   verifique os bytes ao redor (opcode e vizinhos) e falhe com mensagem
   clara se não reconhecer. Na ROM US o resultado tem que ser exatamente o
   endereço de hoje. Liste no relatório cada tabela que passou a ser lida
   assim, com o endereço US e o do Islands.
3. Refaça as verificações: base US e US+PT idênticas (SHA-1), PT idêntico
   (`diff -r`), Redux, e o import do Islands (cole a saída; diga se o
   `kDungMap_Tiles` mudou de tamanho e por quê). Screenshot do mapa de
   masmorra com e sem o mod, de novo.
4. Corrija a explicação no `docs/engine/MODS.md`. `make -j2`,
   `tools/regress.sh` verde. Commit novo (não reescreva o anterior).
