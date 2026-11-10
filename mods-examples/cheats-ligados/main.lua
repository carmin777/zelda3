-- Reproduz os quatro cheats de teclado do zelda3 (PatchCommand) ao carregar
-- um arquivo. Eles ficam ligados enquanto este mod estiver ativo.
-- A issue #295 pede o contrario no jogo base ("cheats shouldn't be active by
-- default"); por isso isto e um mod opt-in, nao uma mudanca no padrao.

local function apply_cheats()
  -- CheatLife: vida e magia cheias.
  zelda.link.health = zelda.link.max_health
  zelda.link.magic = 128

  -- CheatEquipment: +10 bombas/flechas e +100 rupias.
  zelda.state.set("link_bomb_filler", 10)
  zelda.state.set("link_arrow_filler", 10)
  local rupees = zelda.state.get("link_rupees_goal")
  if rupees < 9999 then
    zelda.state.set("link_rupees_goal", math.min(9999, rupees + 100))
  end

  -- CheatKeys e CheatWalkThroughWalls.
  zelda.state.set("link_num_keys", 1)
  zelda.state.set("cheatWalkThroughWalls", 1)
end

-- FileLoad tambem roda no "continuar" depois da morte e no carregamento de
-- um arquivo novo; nao usamos FileCreate para nao escrever no meio da
-- inicializacao do arquivo recem-criado.
zelda.on("FileLoad", apply_cheats)
