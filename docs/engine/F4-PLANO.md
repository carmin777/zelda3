# F4 — de "port com mods" para engine

Proposta para aprovação (09/11/26). Nada despachado ainda.

## O que a medição mostrou

1. **O estado já tem nome.** `src/variables.h` tem 1.417 variáveis nomeadas
   (`link_x_coord`, `main_module_index`…), cada uma um offset fixo em `g_ram`.
   As "1.730 referências a `g_ram`" eram quase todas essas definições. Acesso
   cru `g_ram[...]` no código do jogo são só ~250, e a maioria são buffers de
   rascunho (descompressão em `0x14000`, tilemaps em `0x12000`) em
   `load_gfx.c` (107) e `nmi.c` (41).
2. **Tirar o estado da RAM do SNES seria um erro.** O layout de `g_ram` é o
   que o oráculo compara, é o formato dos replays e dos save states. Mudar
   isso perde a verificação byte a byte (que é o que deixa os agentes
   trabalharem com segurança) e quebra saves, para ganhar pouco em modding.
   Também não faremos ECS nem múltiplas instâncias.
3. **O conteúdo em dados já existe, mas só offline.** `assets/restool.py`
   extrai da ROM 322 salas de dungeon em YAML (`Header`, `Sprites`,
   `Secrets`, `Chests`, `Entrances`, camadas), 112 áreas de overworld em YAML,
   sprites em PNG e diálogo em `.txt`, e compila tudo no `zelda3_assets.dat`.
   Falta ligar isso aos mods.
4. **Simulação e desenho já são separados por natureza.** O jogo escreve no
   estado do PPU; `ZeldaDrawPpuFrame` desenha depois. Dá para pôr uma camada
   de desenho para mods depois do PPU sem tocar na simulação.

Conclusão: a engine não vem de reescrever o núcleo, e sim de **expor o que
já está organizado** e **fechar o ciclo do conteúdo**.

## Subfases

| | Entrega | Risco | Depende |
|---|---|---|---|
| **F4a — Registro de estado** | tabela gerada de `variables.h` (nome, offset, tipo, tamanho de array). Lua: `zelda.state.get("link_x_coord")`, `zelda.state.list()`; escrita só se o mod declarar `"permissions": ["state_write"]`. Flag `--dump-state` no headless. | baixo (só leitura de tabela) | — |
| **F4b — Modkit de conteúdo** | `tools/modkit.py build mods/<id>`: o mod traz `content/` com YAML de salas/overworld, `dialogue.txt`, PNG de sprites; o modkit sobrepõe à extração base, roda o compilador num diretório temporário, compara com o `zelda3_assets.dat` base e grava os overrides (em pares quando o asset tem tabela de offsets, com `allow_resize`) no `mod.json`. Resolve também o diálogo em texto que ficou pendente na F2. | médio (só Python; o C não muda) | — |
| **F4c — Desenho para mods** | evento `FrameDraw` depois do PPU; Lua `zelda.draw.text/rect/pixel` sobre o framebuffer, sem tocar na RAM. HUD custom, overlays de debug, contador de randomizer. | baixo | F4a (mesmos arquivos Lua) |
| **F4d — Buffers nomeados** | trocar os ~250 `g_ram[...]` crus por nomes (`kRam_DecompBuffer` etc.). Limpeza mecânica provada pelo regress. | baixo | — |
| **F4e — Comportamento novo** | pontos de extensão para conteúdo novo: sobrescrever a IA de um tipo de sprite, item novo ou reaproveitado, efeito ao usar item. Precisa de desenho próprio; só depois de a–c. | alto | a, c |

## Ordem proposta

1. **F4a e F4b em paralelo**: arquivos disjuntos (C/Lua × Python), dois
   agentes.
2. **F4c** depois da F4a.
3. **F4d** quando sobrar fôlego (não bloqueia nada).
4. **F4e**: eu escrevo um desenho separado e trago para você antes.

O portão continua o mesmo: `tools/regress.sh` verde e revisão do diff. Na F4b
o teste extra é "build sem mudança nenhuma no `content/` → zero overrides" e
"mudar o baú da sala 260 no YAML → mesmo efeito do mod `bau-da-casa`".
