# Tarefa F2-modloader — carregar mods de pastas

Crie o carregador de mods. Um mod é uma pasta `mods/<id>/` com um
`mod.json`, e pode trocar assets inteiros, remendar bytes de assets e trocar o
sprite do Link — tudo sem recompilar. O barramento de eventos da F1 já existe
em `src/mod/events.h`; a F2 não precisa dele ainda, mas o código novo vai em
`src/mod/` também.

## Contexto (já verificado)

- `src/main.c` `LoadAssets()` lê `zelda3_assets.dat` e preenche
  `g_asset_ptrs[165]` / `g_asset_sizes[165]`. O buffer vem de `ReadWholeFile`
  (heap, gravável). `src/assets.h` define cada asset por nome
  (`#define kDungeonRoomChests ((uint8*)g_asset_ptrs[8])`).
- `ParseLinkGraphics()` / `LoadLinkGraphics()` em `src/main.c` já carregam um
  ZSPR (sprite do Link) a partir de `g_config.link_graphics`.
- `ZeldaRunFrame` só compara com a ROM quando `enhanced_features0 == 0` e
  `g_zenv.dialogue_flags == 0`. **Com qualquer mod ativo que mude conteúdo, a
  comparação tem de ser desligada** (senão o modo verificado acusa
  divergência, corretamente).
- Bancada: `tools/regress.sh` (sem mods) e `tools/regress.sh --trace`.

## O que fazer

1. **Tabela de nomes dos assets**: `kAssetNames[kNumberOfAssets]` gerada a
   partir de `src/assets.h` (ou do `assets/restool.py`, se for ele quem gera
   o `assets.h` — descubra e, se for, gere pelo mesmo caminho). Função
   `int FindAssetByName(const char *name)`.
2. **Manifesto** `mods/<id>/mod.json`:
   ```json
   {
     "id": "exemplo", "name": "Exemplo", "version": "1.0.0",
     "api": 1, "enabled": true, "priority": 0,
     "depends": [],
     "link_sprite": "link.zspr",
     "asset_overrides": { "kDungeonRoomChests": "assets/chests.bin" },
     "patches": ["patches/chests.json"]
   }
   ```
   Todos os campos além de `id` e `api` são opcionais. Use um parser JSON
   pequeno vendorizado em `third_party/` (cJSON, MIT) — não escreva um parser
   à mão.
3. **Patches** `patches/*.json`: lista de
   `{ "asset": "kDungeonRoomChests", "offset": 12, "bytes": "0A 0B" }` com
   opção `"expect": "XX YY"` (bytes esperados antes; se não baterem, o patch
   falha com mensagem clara — protege contra versão errada de assets).
   Antes de remendar um asset, copie-o para um buffer próprio do mod loader.
4. **Ordem de carga**: ordena por `priority` (menor primeiro), desempate por
   `id`; quem carrega depois vence. `depends` ausente → erro claro e o mod é
   desativado. Imprima no stderr uma linha por mod carregado/recusado.
5. **Configuração**: seção `[Mods]` no ini com `Enabled` (padrão 1) e
   `Directory` (padrão `mods`); flags `--no-mods` e `--mods-dir <dir>`.
   `tests/regress.ini` deve desligar mods, e `tools/regress.sh` também deve
   passar `--no-mods`, para a bancada nunca depender da pasta do usuário.
   Adicione `[Mods]` com comentários ao `zelda3.ini` (o arquivo versionado,
   não o `zelda3.user.ini`).
6. **Comparação**: se algum mod aplicou override/patch/sprite, desligue a
   comparação com a ROM (flag global, ex.: `g_mods_change_content`, checada
   em `ZeldaRunFrame` junto com `enhanced_features0`). Imprima
   `compare: off` no headless nesse caso.
7. **Mod de exemplo** em `mods-examples/bau-da-casa/` (não em `mods/`):
   troca o item do baú da casa do Link (sala 260 / 0x104; no trace do replay
   0 aparece `ChestOpen 260 7024` seguido de `ItemReceive 18`, que é a
   lâmpada). Descubra o formato de `kDungeonRoomChests` no código
   (`OpenChestForItem` em `src/dungeon.c`), escolha outro item que não quebre
   o começo do jogo (ex.: arco ou bumerangue) e documente o formato em
   `docs/engine/MODS.md`.
8. **`docs/engine/MODS.md`**: como criar um mod (manifesto, overrides,
   patches, sprite, ordem de carga), com o exemplo.

## Verificação que você deve fazer e relatar

1. `make -j2` sem warnings novos.
2. `tools/regress.sh` verde.
3. Com `--mods-dir mods-examples --mod-trace` no headless, replay 0:
   `compare: off`, e o trace mostra `ChestOpen 260 ...` seguido de
   `ItemReceive <outro id>`. Cole as linhas reais.
4. Patch com `expect` errado → mensagem de erro clara, mod recusado, jogo
   segue sem ele. Cole a saída.
5. Mod com `depends` inexistente → recusado. Cole a saída.

## Fora do escopo

Scripting (F3), troca de texto de diálogo a partir de .txt (fica para
depois; um override de asset inteiro já cobre quem gerar o asset com o
restool), qualquer refactor do jogo.
