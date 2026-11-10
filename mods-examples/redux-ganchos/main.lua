-- Exemplo dos filtros e decisões da F4e.1, com os valores do Redux.
-- Nenhum destes ganchos existia antes: sem este mod o jogo não muda.

zelda.filter("SwordBeamDamage", function(damage_class, slot)
  -- O valor é uma classe de dano, não pontos de vida. O beam original usa a
  -- classe 1 (L1); o Redux usa a classe 2, a mesma da espada mestra (L2) no
  -- golpe normal: kSprite_Func14_Damage[1] == 2 em src/sprite.c.
  return 2
end)

zelda.filter("FairyReviveHealth", function(health, capacity)
  -- A fada original preenche 56; o Redux enche 64.
  return 64
end)

zelda.filter("MaxBombs", function(max_bombs, level)
  return 60
end)

zelda.filter("MaxArrows", function(max_arrows, level)
  return 99
end)

zelda.decide("PikitStealShield", function(steal, slot)
  return false
end)

zelda.decide("BlockAlreadyPushed", function(pushed, block)
  -- false deixa o bloco sempre reutilizável (blocos infinitos).
  return false
end)
