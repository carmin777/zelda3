-- Pedido do PowerPanda: comecar com a vida cheia ao carregar um save.
-- FileLoad roda depois de o save ser copiado para a WRAM e antes de
-- Link_Initialize; este ultimo nao mexe em link_health_current.
local function fill_health()
  zelda.link.health = zelda.link.max_health
end

zelda.on("FileLoad", fill_health)
zelda.on("FileCreate", fill_health)
