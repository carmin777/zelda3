-- Como o A Link to the Islands: o espelho não funciona ao ar livre no Mundo
-- Sombrio. Em dungeons/interiores ele continua funcionando.

zelda.decide("MirrorAllowed", function(allowed, world)
  if world == 0x40 and zelda.state.get("player_is_indoors") == 0 then
    return false
  end
  return allowed
end)
