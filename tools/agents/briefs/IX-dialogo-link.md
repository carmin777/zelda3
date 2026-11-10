# Tarefa IX-dialogo-link — importar diálogos e o sprite do Link de hacks

Leia `AGENTS.md`, `docs/engine/MODS.md` (import de ROM de hack e de
tradução; idiomas e `--languages`; ZSPR/`link_sprite`), `assets/restool.py`
(`--extract-dialogue`), `assets/compile_resources.py` (`print_dialogue`) e
`docs/engine/ALTTI-MAPA.md`.

## O problema (achado no AX)

Com o mod do A Link to the Islands, salas, mapas e gráficos vêm do hack,
mas **o texto em jogo continua o da base** (397 mensagens do hack não são
importadas) e o **sprite do Link** (o do Minish Cap que o hack usa) também
não. O mesmo vale para o Redux (textos "Ocarina", "Sages"…; a variante
Retranslation tem o roteiro inteiro novo).

## O que fazer

1. **Diálogo**: o `import-rom` extrai o diálogo do hack (seguindo os
   ponteiros pelo código, como sempre; o ALTTI-MAPA cita `$9C:8000` /
   `$8E:DF40`), compila `kDialogue` (e a fonte, se mudou) e importa no mod
   quando difere da base. O C já carrega `kDialogue` por asset? Se for
   preciso, use o caminho de idiomas que a tradução PT usa. Hacks com
   comandos de texto desconhecidos: trate e liste, não invente.
2. **Sprite do Link**: importe `kLinkGraphics` (e paletas do Link) do hack
   quando diferir, ou gere um `.zspr` e aponte `link_sprite` no mod.
3. Teste com **Islands**, **Redux** e **Redux Retranslation**
   (`redux-retrans.sfc`), e confira que o
   import da tradução PT continua idêntico (`diff -r`).
4. Prova visual (headless, PNGs em `.agents/shots/`, fora do git): uma
   caixa de diálogo do hack (por exemplo o tio, ou uma placa) com e sem o
   mod; o Link do Islands.

## Verificação a relatar

`make -j2`; `tools/regress.sh` verde com todas as linhas; se mexer em
`assets/`, base reconstruída **numa cópia isolada** com SHA-1
`188bfc8630e55345dc4dfea0ef16bb9bff2cd483`; saídas dos imports dos três
hacks; o que não foi possível.
