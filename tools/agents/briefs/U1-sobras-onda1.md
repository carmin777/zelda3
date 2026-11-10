# Tarefa U1-sobras-onda1 — as duas sobras da onda 1 (bugs)

Leia `AGENTS.md` e `docs/engine/ESTADO.md` (tabela do topo e a linha
"1 bugs" da trilha 2).

## O que fazer

1. **`kSprite_PrepAndDrawSingleLarge_Tab2`**: o índice `Tab1[t] + graphics`
   pode passar do tamanho da tabela em teoria; no asm é um `ADC` de 8 bits
   (dá a volta em 256). Leia o asm original e o C, e faça o C reproduzir
   **exatamente** o que o asm faz (volta de 8 bits e, se o asm lê além da
   tabela, o mesmo byte que ele leria na ROM), sem mudar nenhum caso que já
   acontece hoje. Se não houver como alcançar o caso no jogo, diga isso e
   deixe só a garantia (sem leitura fora do array em C).
2. **`--load` + `--save-replay` sem alinhar o hash**: reproduza o problema
   (carregar um estado e gravar um replay a partir dele; o hash do replay
   gravado não bate ao reproduzir), ache a causa e corrija. Escreva um teste
   (replay de feature ou um passo no `tools/regress.sh`) que pegaria a
   regressão.
3. Atualize a tabela do topo e a linha "1 bugs" do `ESTADO.md`.

## Verificação a relatar

`make -j2` e `make -j2 NO_LUA=1` sem warnings; `tools/regress.sh` com todas
as linhas `ok` e a linha final; o comando que reproduzia o problema 2 antes e
depois.
