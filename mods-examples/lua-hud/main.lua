-- Exemplo de HUD proprio no evento FrameDraw.
--
-- O handler so desenha: leituras de zelda.link/zelda.state continuam liberadas,
-- mas qualquer escrita durante FrameDraw gera erro Lua. Nada aqui altera a
-- simulacao.

local box_w, box_h = 140, 38

zelda.on("FrameDraw", function()
  local screen_w = zelda.draw.width()
  local x = screen_w - box_w - 4
  local y = 4

  zelda.draw.rect(x, y, box_w, box_h, 0xA0000000)

  -- A vida conta 8 unidades por coracao; mostra em coracoes (com meio).
  local health = zelda.link.health / 8
  local max_health = zelda.link.max_health // 8
  local room = zelda.state.get("dungeon_room_index")
  local area = zelda.state.get("overworld_area_index")

  zelda.draw.text(x + 4, y + 3,
                  string.format("R:%03d  HP:%.1f/%d", zelda.link.rupees, health, max_health),
                  0xFFFFFFFF)
  zelda.draw.text(x + 4, y + 12,
                  string.format("ROOM:%03d AREA:%03d", room, area),
                  0xFFE0E060)
  zelda.draw.text(x + 4, y + 21,
                  string.format("FRAME:%d", zelda.frame()),
                  0xFF80FF80)
end)
