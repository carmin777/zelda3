-- Exemplo: mensagens em alguns eventos do jogo.

zelda.on("RoomEnter", function(ev)
  zelda.log(string.format("ola: entrou na sala %d (anterior %d)", ev.room, ev.prev))
end)

zelda.on("DialogueOpen", function(ev)
  zelda.log(string.format("ola: dialogo %d aberto", ev.msg))
end)

zelda.on("ItemReceive", function(ev)
  zelda.log(string.format("ola: recebeu item %d (bau %d, metodo %d)",
                          ev.item, ev.chest, ev.method))
end)
