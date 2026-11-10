-- Conta mortes por slot e guarda o contador no arquivo do mod ao lado do
-- save do jogo. zelda.save e separado por id de mod e por slot, entao o mesmo
-- contador nao se mistura entre arquivos.

local deaths = 0
local slot = 0

local function refresh()
  deaths = zelda.save.get("mortes") or 0
end

zelda.on("FileLoad", function(ev)
  slot = ev.slot
  refresh()
end)

zelda.on("FileCreate", function(ev)
  slot = ev.slot
  deaths = 0
  zelda.save.set("mortes", 0)
end)

zelda.on("LinkDeath", function()
  deaths = deaths + 1
  zelda.save.set("mortes", deaths)
end)

zelda.on("FrameDraw", function()
  local label = string.format("Mortes S%d: %d", slot + 1, deaths)
  zelda.draw.rect(4, 4, 100, 12, 0xA0000000)
  zelda.draw.text(7, 6, label, 0xFFFFFFFF)
end)
