-- Ganchos de comportamento do A Link to the Islands.
--
-- O hack muda algumas regras por assembly. Aqui as mesmas regras são
-- descritas em Lua, sem copiar o código do autor: só o efeito observável
-- (documentado em docs/engine/ALTTI-MAPA.md, seção 3) é reproduzido.

-- 1. Espelho proibido no Mundo Sombrio ao ar livre.
--
-- O jogo original só bloqueia o espelho no Mundo da Luz; no Mundo Sombrio
-- ele permite a viagem de volta. O hack inverte isso: ao ar livre no Mundo
-- Sombrio o espelho toca o som de erro e não faz nada. Em dungeons e
-- interiores o espelho continua funcionando (é assim que o hack devolve o
-- Link à entrada da dungeon).
zelda.decide("MirrorAllowed", function(allowed, world)
  if world == 0x40 and zelda.state.get("player_is_indoors") == 0 then
    return false
  end
  return allowed
end)

-- 2. Quebra-cabeça da praia na área $3F.
--
-- A área $3F é o canto inferior direito do mapa claro. Sair dela por baixo
-- ou pela direita cai fora da grade normal de 8x8 telas; o hack intercepta
-- justamente essas transições. A combinação gravada no binário do hack é
-- cima, baixo, direita, cima (as telas $37, $47, $40, $37). Enquanto a
-- sequência não fecha, a tentativa de saída é desviada de volta para $3F;
-- ao acertar a quarta direção, o destino vira $37 (Beach Temple). Sair pela
-- esquerda ($3E) é sempre permitido e zera o contador.
--
-- O estado da combinação fica só no script. Uma transição que não parta de
-- $3F também zera o contador, como o hack faz ao carregar a área normal.
local beach_step = 0
local beach_combo = { 0x37, 0x47, 0x40, 0x37 }

-- A tela de destino que o hack compara é a tela "crua" antes do bit de Mundo
-- Sombrio. O C do zelda3 já aplica a tabela de áreas-cabeça e, para baixo e
-- direita, o índice é mascarado para 6 bits; por isso a direção é a fonte
-- mais fiel aqui. Os bits são os mesmos do jogo: cima 8, baixo 4,
-- esquerda 2, direita 1.
local function beach_screen(direction)
  if direction == 8 then return 0x37 end
  if direction == 4 then return 0x47 end
  if direction == 1 then return 0x40 end
  if direction == 2 then return 0x3E end
  return nil
end

-- Com o veto da transição (a engine não inicia a rolagem quando o destino é
-- a tela atual), Link é colocado na borda oposta da mesma tela, como o efeito
-- visível do hack. A área $3F ocupa as coordenadas $E00..$FFF nos dois eixos.
local function set16(name, value)
  zelda.state.set(name, value & 0xFFFF)
end

local function beach_nudge(direction)
  if direction == 1 then
    set16("link_x_coord", 0xE10)
  elseif direction == 2 then
    set16("link_x_coord", 0xFF0)
  elseif direction == 4 then
    set16("link_y_coord", 0xE10)
  elseif direction == 8 then
    set16("link_y_coord", 0xFF0)
  end
end

zelda.filter("OverworldTransitionTarget", function(target, direction)
  local old = zelda.state.get("overworld_screen_index")
  if old ~= 0x3F then
    beach_step = 0
    return target
  end

  local screen = beach_screen(direction)
  if screen == 0x3E then
    -- Saída normal pela esquerda, sem combinação.
    beach_step = 0
    return target
  end

  if screen ~= nil and screen == beach_combo[beach_step + 1] then
    beach_step = beach_step + 1
  else
    beach_step = 0
  end

  if beach_step == #beach_combo then
    beach_step = 0
    zelda.state.set("sound_effect_2", 0x1B)
    return 0x37
  end

  -- Ainda não fechou: devolve a tela atual para vetar a transição e coloca
  -- Link na borda oposta da mesma tela.
  beach_nudge(direction)
  return 0x3F
end)

-- 3. Recarga automática de magia.
--
-- O hack reabastece a magia continuamente enquanto o jogador está no jogo
-- normal: um ponto a cada 256 frames, saturando em 128, com uma taxa maior
-- quando o livro está equipado. O evento FrameStart roda no começo de cada
-- frame e permite state_write, então o efeito cabe nos ganchos atuais.
-- O acumulador fracionário fica no script, como o contador de 8 bits que o
-- hack mantém na RAM.
local magic_fraction = 0
local kMagicMax = 128

zelda.on("FrameStart", function()
  local module = zelda.state.get("main_module_index")
  -- A tabela do hack libera os módulos 0..$15; fora deles não há jogo.
  if module > 0x15 then
    return
  end
  -- O hack só recarrega fora de transições e com o Link livre.
  if zelda.state.get("submodule_index") ~= 0 or
     zelda.state.get("link_player_handler_state") ~= 0 then
    return
  end

  local magic = zelda.state.get("link_magic_power")

  -- 1/256 de ponto por frame; com o livro equipado o hack soma mais 16.
  local rate = 1
  if zelda.state.get("hud_cur_item") == 0x0F then
    rate = rate + 0x10
  end

  magic_fraction = magic_fraction + rate
  if magic_fraction >= 0x100 then
    magic_fraction = magic_fraction - 0x100
    if magic < kMagicMax then
      magic = magic + 1
      zelda.state.set("link_magic_power", magic)
    end
  end
end)
