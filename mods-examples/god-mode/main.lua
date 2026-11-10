-- God Mode: a vida nao cai. O filtro LinkDamage zera o dano antes de o jogo
-- gravar link_health_current, cobrindo sprite, ancilla, buraco e ambiente.
-- A capacidade de 20 coracoes e aplicada quando um arquivo e carregado ou
-- criado; nao e mais preciso repor a vida a cada frame.

local function full_health()
  if zelda.link.max_health < 160 then
    zelda.link.max_health = 160
  end
  if zelda.link.health < zelda.link.max_health then
    zelda.link.health = zelda.link.max_health
  end
end

zelda.on("FileLoad", full_health)
zelda.on("FileCreate", full_health)

zelda.filter("LinkDamage", function(damage, source)
  return 0
end)
