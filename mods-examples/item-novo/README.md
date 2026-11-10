# Item novo: pena

Exemplo de item registrado em tempo de execução por `zelda.item.register`.
O baú da casa do Link passa a dar a **pena** em vez da lâmpada. A pena tem
ícone próprio (16×16), gráfico de recebimento, nome "PENA" no menu e um efeito
de uso: um pulo curto.

```sh
mkdir -p /tmp/mods-item
cp -a mods-examples/item-novo /tmp/mods-item/
./zelda3 --config tests/replays-features/f4e-item-novo.ini --headless \
  --mods-dir /tmp/mods-item --mod-trace \
  --replay tests/replays-features/f4e-item-novo.sav --frames 3750 zelda3.sfc
```

O `message = 0x51` usa a mensagem original "You got the Lamp!" como
placeholder, porque este exemplo não distribui um asset de diálogo derivado da
ROM. Para um texto próprio, use `tools/modkit.py` com `content/dialogue.txt`,
substitua uma mensagem e passe o id dela em `message`.

## Formato dos PNGs

`pena.png` e `pena-recebimento.png` são PNGs de **16×16, 8 bits por pixel,
escala de cinza**. O valor de cada pixel é o índice de cor SNES:

- `0` é transparente;
- `1..15` escolhem uma cor da paleta.

O ícone usa a paleta do ícone original cujo espaço no menu foi emprestado; o
gráfico de recebimento usa a paleta 3 dos sprites, como os recebimentos
originais. O mesmo formato vale para qualquer item registrado por um mod.
