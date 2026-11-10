-- Exemplo: le variaveis nomeadas de src/variables.h pela API zelda.state.

zelda.on("RoomEnter", function(ev)
  local x = zelda.state.get("link_x_coord")
  local y = zelda.state.get("link_y_coord")
  local room = zelda.state.get("dungeon_room_index")
  zelda.log(string.format(
    "estado: link_x_coord=%d link_y_coord=%d dungeon_room_index=%d (sala %d)",
    x, y, room, ev.room))
end)
