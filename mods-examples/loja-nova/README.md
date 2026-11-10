# loja-nova

Define uma loja sem C. Registra a pena da F4e.5 e usa `zelda.shop.set` para
vender a pena no lugar do coração do meio da loja de Kakariko (sala `0x1f`).

```lua
zelda.shop.set(0x1f, {
  { item = "red_potion", price = 150 },
  { item = pena,         price = 10, count = 1 },
  { item = "bombs",      price = 50 },
})
```

O limite é de três itens por loja. `price` vai de 0 a 999. `count` vale para
itens novos e é a quantidade entregue; os itens originais usam a quantidade
normal do recebimento. O mod não precisa de `permissions` para definir a
loja, mas este exemplo usa o mesmo pulo do `item-novo` no `on_use`, por isso
declara `state_write`.

Teste:

```sh
rm -rf /tmp/mods-loja && mkdir -p /tmp/mods-loja
cp -a mods-examples/loja-nova /tmp/mods-loja/
./zelda3 --config tests/replays-features/f4e3-shop.ini --headless \
  --mods-dir /tmp/mods-loja \
  --replay tests/replays-features/f4e3-shop.sav --frames 250 \
  --mod-trace zelda3.sfc | grep -E '^(compare|ev |final)'
```

O trace deve mostrar `ItemReceive 128` (o id do item novo) no frame 150.
