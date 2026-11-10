# zelda3 → engine + modloader

Objetivo: o zelda3 vira uma base fácil de modificar. Mods ficam em pastas
(`mods/<id>/`), são carregados sem recompilar e podem trocar assets e dados e
reagir a eventos do jogo.

## Regra de ouro

**O jogo sem mods continua idêntico, byte a byte, ao original.** O zelda3 já
tem o oráculo para isso: com a ROM carregada e sem features extras, cada frame
roda em C e no assembly 65816 original, e a RAM é comparada
(`src/zelda_cpu_infra.c`, `EmuRunFrameWithCompare`). Os replays em `saves/ref/`
cobrem o jogo inteiro. Toda mudança de engine passa por `tools/regress.sh`
antes de ser aceita.

## Fases

| Fase | Entrega | Portão |
|---|---|---|
| **F0 — Bancada** | modo `--headless` que roda replays sem janela, imprime hash da RAM por frame e falha se divergir do original; `tools/regress.sh` com hashes de referência | regress verde no master atual |
| **F1 — Eventos** | `src/mod/events.{c,h}`: barramento de eventos em C (`on_frame`, `on_room_enter`, `on_item_get`, `on_dialogue`, `on_save/load`…) chamados nos pontos certos do jogo; sem mods = no-op | regress idêntico |
| **F2 — Mod loader** | `mods/<id>/mod.json` (id, versão, dependências, ordem), override de assets (sprites ZSPR/PNG, diálogo, fonte, MSU-1), patches de dados (baús, lojas…) | regress idêntico sem mods; mods de exemplo funcionando |
| **F3 — Scripting** | Lua 5.4 embutido, API sobre os eventos da F1 e acesso nomeado ao estado (não offsets crus), sandbox | mod de exemplo em Lua |
| **F4 — Engine de verdade** | refactor progressivo: `g_ram`/macros de `variables.h` → acessores nomeados; separar simulação de render; conteúdo em dados | regress idêntico a cada passo |

A F4 é contínua e pode ir em paralelo depois da F1, por subsistema.

## Como o trabalho é feito

- Os agentes (DeepSeek v4.1 flash via `opencode run`) fazem o trabalho braçal,
  cada um num git worktree próprio (`.agents/<tarefa>`, branch `agent/<tarefa>`).
- O Claude escreve o briefing (`tools/agents/briefs/<tarefa>.md`), despacha
  (`tools/agents/run.sh`), revisa o diff, roda o portão e faz o merge em
  `engine/modloader`.
- Regras que todo agente segue: `AGENTS.md` na raiz.

## Estado

Ver `docs/engine/ESTADO.md`.
