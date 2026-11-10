# Tarefa S6B-soldado-canhao — implementar o Sprite_6B_CannonTrooper

Leia `AGENTS.md` e `docs/engine/BUGS-PROTOCOLO.md` (o oráculo é a verdade).

## Fatos (confira)

- `src/sprite_main.c` tem `Sprite_6B_CannonTrooper(int k)`: só trata o
  caso `sprite_C[k] != 0` (bala de canhão); o soldado em si termina num
  `assert(0)`. No jogo original esse inimigo não aparece em nenhuma sala,
  por isso o upstream nunca precisou dele. O Redux o coloca em uma sala da
  Ganon's Tower (sala 176), e o import de salas do zelda3 tem de pulá-lo.
- O código do soldado existe no assembly da ROM US (é dele que o
  `Sprite_6B` em C saiu; ache o endereço pelo comentário `// 8xxxxx` e pela
  tabela de rotinas de sprite).

## O que fazer

1. Reimplemente em C o comportamento do soldado do canhão **igual ao
   assembly**, rotina por rotina, com o comentário de endereço em cada
   função, seguindo o estilo dos outros sprites em `src/sprite_main.c`.
2. **Prova pelo oráculo**: como nenhuma sala o tem, force o aparecimento
   num teste: a partir de um replay de referência num ponto com o Link
   numa sala de masmorra, use um mecanismo de teste (por exemplo, um
   `--input-script` combinado com uma forma de criar o sprite que **não**
   desligue a comparação; se não houver, crie um flag de teste do headless
   que chame a mesma rotina de spawn no emulador e no C ao mesmo tempo)
   e rode muitos frames com a comparação ligada (`compare: on`) até ele
   andar, atirar e morrer. Exit 0 = igual ao assembly.
3. Torne a prova permanente: replay em `tests/replays/` (passo 5 do
   protocolo) se o mecanismo permitir; senão, explique e deixe o comando
   reproduzível no relatório.
4. Sem o spawn forçado, nada muda no jogo (portão verde).

## Verificação a relatar

`make -j2` sem warnings; `tools/regress.sh` verde com todas as linhas;
saída do teste com `compare: on` e exit 0, com quantos frames o soldado
viveu e o que fez; screenshot (fora do git). Diga o que do comportamento
não foi exercitado.
