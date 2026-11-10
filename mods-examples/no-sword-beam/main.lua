-- No Sword Beam: nega a criacao do raio no gancho de decisao. O golpe de
-- espada continua normal e o beam nunca chega a existir.
zelda.decide("SwordBeamSpawn", function(allowed, direction)
  return false
end)
