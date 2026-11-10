-- Mini-randomizer deterministico de baus.
--
-- O gerador e um LCG proprio com seed fixa: nada de math.random sem seed.
-- A lista evita itens que dependem de contexto especial (chaves, mapas,
-- cristais, pedantes, triforce etc.).

local seed = 0x5EED1234

local function random_index(n)
  seed = (seed * 1103515245 + 12345) % 2147483648
  return seed % n + 1
end

local safe_items = {
  0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, -- rods, hammer, hookshot, bow, boomerang, powder
  0x0F, 0x10, 0x11, 0x12, 0x15, 0x18, 0x19, -- medallions, lamp, somaria, byrna, cape
  0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x21,       -- gloves, book, flippers, pearl, net
  0x29, 0x2A, 0x2E, 0x2F, 0x30,             -- mushroom, boomerang 2, magic
  0x34, 0x35, 0x42, 0x43, 0x44, 0x45, 0x48, 0x4B, -- rupees, refills, fairy, boots
}

local chest_room = nil

zelda.on("ChestOpen", function(ev)
  chest_room = ev.room
end)

zelda.on("ItemReceive", function(ev)
  -- method 1 = bau. Outros metodos (drop, loja, presente) ficam intactos.
  if ev.method ~= 1 then
    return
  end
  local item = safe_items[random_index(#safe_items)]
  zelda.log(string.format("bau aleatorio: sala %s, item %d -> %d",
                          tostring(chest_room), ev.item, item))
  return item
end)
