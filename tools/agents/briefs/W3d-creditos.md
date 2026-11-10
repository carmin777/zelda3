# Tarefa W3d-creditos — créditos finais em PT com prova visual

Leia `AGENTS.md`, `docs/engine/MODS.md` (importação de ROM traduzida) e,
em `tools/modkit.py`, `read_ending_credits_addresses` /
`read_ending_credits_assets` e `TEXT_ASSETS`.

## Fatos (confira)

- O `ESTADO.md` diz: créditos PT em formato cru em `$8EB038`, sem prova
  visual, fora do import padrão. O modkit já sabe ler o texto e os offsets
  pelos ponteiros da ROM.
- ROM PT: `portuguese.sfc` (fora do git).

## O que fazer

1. Confirme que o `import-rom portuguese.sfc` lê `kEnding_Credits_Text` e
   `kEnding_Credits_Offs` certos (seguindo os ponteiros do código, não
   endereço fixo) e que o formato é o que o C espera.
2. Se estiverem certos, passe a importá-los por padrão no import de ROM
   traduzida (ou explique por que não).
3. **Prova visual**: o `ref12` termina nos créditos. Rode headless com
   `--replay-ref 12` e o mod gerado numa pasta temporária, tire
   screenshots (`--screenshot`, PNGs em `.agents/shots/`, fora do git) em
   vários frames dos créditos, com e sem o mod. O texto PT tem de aparecer
   legível e alinhado. Diga quantos frames e quais.
4. `docs/engine/MODS.md` atualizado.

## Verificação a relatar

`make -j2`; `tools/regress.sh` verde; `import-rom portuguese.sfc` antes e
depois (`diff -r`, só os arquivos dos créditos podem aparecer); a base
reconstruída **numa cópia isolada** com SHA-1
`188bfc8630e55345dc4dfea0ef16bb9bff2cd483` se mexer em `assets/`.
