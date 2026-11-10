-- Loja de Kakariko (sala 0x1f) com a pena da F4e.5 no lugar do coração.
--
-- O registro da pena é o mesmo de item-novo; o que muda é zelda.shop.set:
-- o mod define os três itens, os preços e a quantidade entregue. O item do
-- meio é o que o roteiro de teste compra (o coração no jogo original).
--
-- A sala é o byte baixo de dungeon_room_index usado por
-- SpritePrep_Shopkeeper; 0x1f é a loja visitada no replay ref1.

local pena = zelda.item.register {
  name = "pena",
  icon = "pena.png",
  receive_gfx = "pena-recebimento.png",
  text = "PENA",
  message = 0x51,
  max = 1,

  on_receive = function(ev)
    zelda.log(string.format("pena comprada (total %d)", ev.count))
  end,

  on_use = function(ev)
    if ev.count == 0 then
      return
    end
    zelda.state.set("link_player_handler_state", 6)
    zelda.state.set("link_incapacitated_timer", 20)
    zelda.state.set("link_auxiliary_state", 2)
    zelda.state.set("link_actual_vel_z", 40)
    zelda.state.set("link_actual_vel_z_copy", 40)
    zelda.state.set("link_z_coord", 0)
    zelda.state.set("link_z_coord_mirror", 0)
    zelda.state.set("sound_effect_1", 33)
  end,
}

zelda.shop.set(0x1f, {
  { item = "red_potion", price = 150 },
  { item = pena, price = 10, count = 1 },
  { item = "bombs", price = 50 },
})
