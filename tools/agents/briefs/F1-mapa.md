# Tarefa F1-mapa — mapear os pontos de evento (só leitura + 1 documento)

Na próxima fase vamos criar um barramento de eventos para mods. Antes disso
precisamos saber **onde, no código C, cada evento acontece**. Sua tarefa é só
investigar e escrever `docs/engine/HOOKS-MAPA.md`. **Não altere nenhum
arquivo `.c` ou `.h`.**

## Eventos a localizar

Para cada um: função(ões), `arquivo:linha`, o que já está disponível naquele
ponto (variáveis de `src/variables.h` com o significado), e se o ponto roda
uma vez por evento ou todo frame enquanto dura.

1. início e fim de cada frame do jogo (dentro de `ZeldaRunFrameInternal`)
2. troca de módulo principal (`main_module_index` / `submodule_index`):
   título, file select, overworld, dungeon, menu, morte, créditos
3. entrar numa sala de dungeon (nova sala carregada) e trocar de área no
   overworld; incluindo troca Light World ↔ Dark World
4. Link recebe um item (baú, chão, NPC, loja, coração/rúpia) — achar a
   função central de "receber item" e o id do item
5. abrir um baú (antes do item ser dado)
6. começar e terminar uma caixa de diálogo (id da mensagem)
7. Link toma dano / morre / usa fada
8. sprite (inimigo/NPC) é criado e destruído; inimigo morre
9. chefe derrotado / cristal ou pingente obtido
10. salvar e carregar o jogo (SRAM), e carregar/salvar estado (save states
    de `SaveLoadSlot`)
11. carregamento de gráficos do Link (sprite do jogador) e de paletas, e
    onde o diálogo/fonte são lidos dos assets — pontos de override de assets
12. leitura das tabelas de conteúdo mais óbvias para mods de dados:
    conteúdo dos baús, itens das lojas, preços, dano de inimigos

## Formato do documento

Uma seção por evento, com: localização, trecho de 3–8 linhas mostrando o
ponto exato, dados disponíveis, e observações (ex.: "chamado também na
inicialização", "roda dentro do NMI", "cuidado: altera RAM comparada").
No fim, uma tabela-resumo. Seja literal: se não achou, escreva "não achado"
e o que você tentou. Cite só o que você viu no código.

Commite só esse documento.
