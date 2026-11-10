-- Versao simplificada do hack "A Link to the Past Quick Commands" (7575):
-- L/R + A/B/X/Y equipam um item fixo, sem passar pelo menu.
-- A versao completa permite escolher os oito itens no menu e gravar a
-- escolha; a API atual nao tem evento de menu nem persistencia de teclas,
-- entao aqui o mapa e fixo e vive no script.

local kL = 0x20
local kR = 0x10
local kA = 0x80
local kX = 0x40
local kB = 0x80
local kY = 0x40

-- mod = L ou R; face_l = A/X; face_h = B/Y.
local commands = {
  { mod = kL, face_l = kA, face_h = 0,  slot = 1,  item = 3,  name = "bow" },
  { mod = kL, face_l = 0,  face_h = kB, slot = 2,  item = 2,  name = "boomerang" },
  { mod = kL, face_l = kX, face_h = 0,  slot = 3,  item = 14, name = "hookshot" },
  { mod = kL, face_l = 0,  face_h = kY, slot = 12, item = 4,  name = "hammer" },
  { mod = kR, face_l = kA, face_h = 0,  slot = 4,  item = 1,  name = "bombs" },
  { mod = kR, face_l = 0,  face_h = kB, slot = 6,  item = 5,  name = "fire_rod" },
  { mod = kR, face_l = kX, face_h = 0,  slot = 7,  item = 6,  name = "ice_rod" },
  { mod = kR, face_l = 0,  face_h = kY, slot = 17, item = 18, name = "somaria" },
}

local function equip(cmd)
  if zelda.item.get(cmd.name) == 0 then
    return
  end
  if zelda.state.get("hud_cur_item") == cmd.slot and
     zelda.state.get("current_item_y") == cmd.item then
    return
  end
  zelda.state.set("hud_cur_item", cmd.slot)
  zelda.state.set("current_item_y", cmd.item)
end

zelda.on("FrameStart", function()
  local module = zelda.state.get("main_module_index")
  if module ~= 7 and module ~= 9 then
    return
  end
  local l = zelda.state.get("filtered_joypad_L")
  local h = zelda.state.get("filtered_joypad_H")
  if (l & 0x30) == 0 then
    return
  end
  for _, cmd in ipairs(commands) do
    if (l & cmd.mod) ~= 0 and (l & cmd.face_l) == cmd.face_l and
       (h & cmd.face_h) == cmd.face_h then
      equip(cmd)
      return
    end
  end
end)
