-- Ao receber o arco (item 0x0B), garante 30 flechas.

local kItemBow = 0x0B

zelda.on("ItemReceive", function(ev)
  if ev.item == kItemBow then
    local before = zelda.link.arrows
    zelda.item.set("arrows", 30)
    zelda.log(string.format("flechas: arco recebido, flechas %d -> %d",
                            before, zelda.link.arrows))
  end
end)
