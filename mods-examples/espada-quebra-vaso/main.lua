-- #262: escolhe o nivel minimo de espada que quebra vasos. O gancho recebe
-- link_sword_type; o comportamento da versao GBA e L4.
zelda.decide("PotBreakSwordLevel", function(default, level)
  return level >= 4
end)
