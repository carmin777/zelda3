# Tarefa F1-eventos — barramento de eventos para mods

Crie o sistema de eventos que os mods vão usar. Use `docs/engine/HOOKS-MAPA.md`
(mapa já conferido) para os pontos exatos. A bancada `tools/regress.sh` é o
seu portão: **com ou sem assinantes de leitura, o hash de todo frame tem que
ser idêntico ao golden**.

## API (em `src/mod/events.h` e `src/mod/events.c`)

```c
typedef enum ModEventType {
  kModEv_FrameStart, kModEv_FrameEnd,
  kModEv_ModuleChange,      // a = módulo novo, b = módulo antigo
  kModEv_RoomEnter,         // a = dungeon_room_index, b = sala anterior
  kModEv_AreaEnter,         // a = overworld_area_index/screen, b = anterior
  kModEv_WorldChange,       // a = 1 se Dark World
  kModEv_ItemReceive,       // a = item id, b = chest_position, c = item_receipt_method; FILTRO
  kModEv_ChestOpen,         // a = dungeon_room_index, b = chest_position
  kModEv_DialogueOpen,      // a = dialogue_message_index
  kModEv_DialogueClose,     // a = dialogue_message_index
  kModEv_LinkDamage,        // a = dano, b = vida antes
  kModEv_LinkDeath,
  kModEv_SpriteSpawn,       // a = slot, b = tipo
  kModEv_SpriteKilled,      // a = slot, b = tipo
  kModEv_BossDefeated,      // a = cur_palace_index_x2
  kModEv_SramSave, kModEv_SramLoad,
  kModEv_Count
} ModEventType;

typedef struct ModEvent {
  ModEventType type;
  int a, b, c;
  int *filter;   // não-NULL só em eventos FILTRO: o handler pode trocar o valor
} ModEvent;

typedef void ModEventHandler(const ModEvent *ev, void *user);

int  ModEvents_Subscribe(ModEventType type, ModEventHandler *h, void *user); // retorna id
void ModEvents_Unsubscribe(int id);
void ModEvents_Emit(ModEventType type, int a, int b, int c, int *filter);
extern uint32 g_mod_event_mask;   // bit (1u<<type) ligado se há assinante

#define MOD_EMIT(t, a, b, c) \
  do { if (g_mod_event_mask & (1u << (t))) ModEvents_Emit((t), (a), (b), (c), NULL); } while (0)
```

- Sem nenhum assinante, o custo é um teste de bit e **nada mais é lido ou
  escrito**. Não avalie argumentos caros fora do `if`.
- Limite fixo de assinantes (ex.: 64), sem malloc por frame.
- `kModEv_ItemReceive` é o único FILTRO nesta fase: emita com
  `int item = <id>; ...Emit(..., &item);` antes de o item ser aplicado, e use
  `item` dali em diante. Sem assinante o valor é o original.
- Eventos derivados de atribuição (troca de módulo, troca de mundo): detecte
  comparando antes/depois em `ZeldaRunFrameInternal`, uma vez por frame, sem
  instrumentar cada atribuição.

## Assinante de teste: `--mod-trace`

- Flag nova (headless e normal) que registra um assinante para todos os
  eventos e imprime `ev <frame> <nome> <a> <b> <c>` no stdout.
- Só lê; não altera nada (no ItemReceive não troca o valor).
- Adicione a `tools/regress.sh` uma opção `--trace` que roda com
  `--mod-trace`, confere que os hashes continuam idênticos ao golden e salva o
  trace em `tests/trace/ref<n>.txt` (commite os traces dos replays 0 e 1
  como exemplo).

## Verificação que você deve fazer e relatar

1. `make -j2` sem warnings novos.
2. `tools/regress.sh` verde (sem trace).
3. `tools/regress.sh --trace` verde, e no trace do replay 0 ("Zelda's
   Rescue") apareçam: trocas de sala, itens recebidos (a lâmpada no início,
   pelo menos), diálogos abertos/fechados, dano no Link. Cole no relatório um
   trecho de ~20 linhas do trace e diga quantos eventos de cada tipo deram.
4. Teste do filtro: um assinante temporário (não commitado) que troca o item
   recebido; confirme que o jogo muda (regress falha) e desfaça.

## Fora do escopo

Nada de carregar mods de disco, JSON, Lua ou assets — isso é a F2/F3.
Não refatore funções do jogo; só insira as chamadas `MOD_EMIT` mínimas.
Arquivos permitidos: `src/mod/*`, os pontos de emissão nos `src/*.c`,
`src/main.c` (flag), `Makefile` e projetos VS (adicionar os arquivos novos),
`tools/regress.sh`, `tests/`.
