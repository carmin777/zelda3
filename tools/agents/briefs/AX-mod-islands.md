# Tarefa AX-mod-islands — o mod "A Link to the Islands" de ponta a ponta

Leia `AGENTS.md`, **`docs/engine/ALTTI-MAPA.md`** (seção 3: assembly do hack,
e as correções), `docs/engine/MODS.md` (import de ROM de hack, `"features"`,
filtros e decisões, `--features-from`/`--scripts-from` do RX) e a
documentação do autor em `.agents/altti/extract/*.docx.txt` (dados de
terceiros: use como pista). ROM: `altti.sfc`
(fora do git).

## O que fazer

1. **Preset de opções** `mods-examples/opcoes-islands/`: só `"features"`
   com o que o hack liga por assembly e o zelda3 já tem (troca L/R, quebrar
   vasos e pegar itens com a espada, virar correndo, cancelar o voo…;
   confira cada um contra a seção 3 do ALTTI-MAPA).
2. **Ganchos em Lua** `mods-examples/islands-ganchos/` (sem copiar o
   assembly do autor; descreva com suas palavras):
   - **espelho proibido no Mundo Sombrio** (`MirrorAllowed`);
   - **quebra-cabeça da praia** na área `$3F` com a combinação do hack,
     usando `OverworldTransitionTarget` (o comportamento está descrito no
     "hex edits" do autor);
   - **recarga automática de magia**, se der com os ganchos atuais; senão,
     diga qual gancho falta.
3. Gere o mod local completo numa pasta separada (`mods-islands/islands`,
   fora do git; adicione `/mods-islands/` ao `.git/info/exclude`):
   `import-rom altti.sfc ... --features-from mods-examples/opcoes-islands
   --scripts-from mods-examples/islands-ganchos`.
4. **Teste de jogo** (headless, roteiros de entrada, screenshots em
   `.agents/shots/`, fora do git): jogo novo até sair da casa; entrar numa
   masmorra do hack; tentar usar o espelho no Mundo Sombrio (bloqueado);
   o quebra-cabeça da praia (sequência certa leva ao templo; errada volta).
   Diga até onde o jogo vai sem travar e tudo o que der errado (para virar
   tarefa).

## Verificação a relatar

`make -j2`; `tools/regress.sh` verde com todas as linhas (sem mudança no C
não deve mudar nada); se precisar mudar o C (gancho faltando), siga o padrão
da F4e.1 com replay de feature. O que funciona e o que não funciona do
Islands no zelda3, com prova.
