-- Item novo de exemplo: uma pena que faz o Link dar um pulo curto.
--
-- O baú da casa do Link (sala 0x104) dá a lâmpada no jogo original. O filtro
-- ItemReceive troca o item 0x12 pelo item registrado, e o engine usa o ícone,
-- o gráfico de recebimento e o texto do registro. Depois de recebido, o item
-- aparece no menu de 24 (NewStyleInventory) e pode ser equipado no Y.
--
-- O estado (tem/não tem e quantidade) é gravado sozinho no arquivo do mod
-- ao lado do save; o script também pode ler/escrever com zelda.item.get/set.

local pena = zelda.item.register {
  name = "pena",
  icon = "pena.png",
  receive_gfx = "pena-recebimento.png",
  text = "PENA",
  message = 0x51,  -- mensagem de recebimento (veja o README)
  max = 1,

  on_receive = function(ev)
    zelda.log(string.format("pena recebida (total %d, bau %d)", ev.count, ev.chest))
  end,

  on_use = function(ev)
    if ev.count == 0 then
      return
    end
    -- Um pulo curto usando o mesmo estado que o jogo usa para o recuo no ar.
    -- zelda.state.set exige a permissão state_write no mod.json.
    zelda.state.set("link_player_handler_state", 6)
    zelda.state.set("link_incapacitated_timer", 20)
    zelda.state.set("link_auxiliary_state", 2)
    zelda.state.set("link_actual_vel_z", 40)
    zelda.state.set("link_actual_vel_z_copy", 40)
    zelda.state.set("link_z_coord", 0)
    zelda.state.set("link_z_coord_mirror", 0)
    zelda.state.set("sound_effect_1", 33)
    zelda.log("pena usada")
  end,
}

-- 0x12 é a lâmpada do baú da casa do Link na sala 0x104 (260).
zelda.on("ItemReceive", function(ev)
  if ev.item == 0x12 and zelda.state.get("dungeon_room_index") == 260 then
    return pena
  end
end)
