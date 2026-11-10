-- Hero Mode, como o hack de romhacking.net 8097: dano dobrado e sem drops de
-- coracao. O filtro LinkDamage dobra o dano antes de o jogo gravar a vida, e o
-- evento DropSpawned remove o coracao no instante em que o sprite e criado
-- (antes de ele aparecer no frame).

zelda.filter("LinkDamage", function(damage, source)
  local doubled = damage * 2
  if doubled > 255 then
    doubled = 255
  end
  return doubled
end)

zelda.on("DropSpawned", function(ev)
  if ev.item == 0xd8 then
    zelda.state.set("sprite_state", ev.slot, 0)
  end
end)
