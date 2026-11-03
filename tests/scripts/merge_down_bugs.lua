-- Copyright (C) 2019-2024  Igara Studio S.A.
--
-- This file is released under the terms of the MIT license.
-- Read LICENSE.txt for more information.

local red = Color{ r=255, g=0, b=0 }
local blue = Color{ r=0, g=0, b=255 }

-- Reproduces the bug reported in https://community.aseprite.org/t/2894
do
  local s = Sprite(32, 32)
  local a = s.layers[1]
  app.useTool{ color=red, layer=a, points={ Point(2, 2) }}

  local b = s:newLayer()
  app.useTool{ color=blue, layer=b, points={ Point(1, 1) }}

  a.isContinuous = true
  b.isContinuous = true

  app.command.NewFrame()
  app.activeLayer = b
  app.command.MergeDownLayer()

  assert(#s.cels == 2)
  assert(s.cels[1].image:isEqual(s.cels[2].image))
end

-- Checks that merge down accounts for z-index (issue #4468)
do
  local s = Sprite(32, 32)
  local a = s.layers[1]
  app.useTool{ color=red, layer=a, points={ Point(1, 1) }}

  local b = s:newLayer()
  app.useTool{ color=blue, layer=b, points={ Point(1, 1) }}

  a.cels[1].zIndex = 1
  b.cels[1].zIndex = 0

  local before = s.cels[1].image:clone()

  app.activeLayer = b
  app.command.MergeDownLayer()

  local after = s.cels[1].image
  assert(before:isEqual(after))
end

-- Check that linked cels are not broken (regression in issue #4685)
-- We create two layers, the bottom one with 4 linked frames, and the
-- top one with one cel at 2nd frame, when we merge them, the
-- resulting layer should have frame 1, 3, and 4 linked.
do
  local s = Sprite(32, 32)
  app.useTool{ color=Color(255, 0, 0), points={ {0,0}, {32,32} } }
  app.layer.isContinuous = true
  app.command.NewFrame{ content=cellinked }
  app.command.NewFrame{ content=cellinked }
  app.command.NewFrame{ content=cellinked }
  s:newLayer()
  app.frame = 2
  app.useTool{ color=Color(0, 0, 255), points={ {32,0}, {0,32} } }
  app.command.MergeDownLayer()
  local cels = app.layer.cels
  -- Check that frame 1, 3, and 4 have the same image (linked cels)
  assert(cels[1].image ~= cels[2].image)
  assert(cels[1].image == cels[3].image)
  assert(cels[1].image == cels[4].image)
end

-- Merge into the destination bank's local indices, including duplicate colors.
-- Two distinct banks can also have the same palette.
for _, upperBank in ipairs{3, 4, -1} do
  local s = Sprite(4, 1, ColorMode.INDEXED)
  local lower = s.layers[1]
  local upper = s:newLayer()
  lower.paletteBank = 3
  upper.paletteBank = upperBank
  app.command.ForceKarnRules{enabled=true, ui=false}
  local pal = s.palettes[1]
  for _, bank in ipairs{3, math.max(0, upperBank)} do
    pal:setColor(bank*16+5, red)
    pal:setColor(bank*16+6, red)
  end
  pal:setColor(3*16, blue) -- The stored mask color can differ across banks.
  local a = Image(4, 1, ColorMode.INDEXED)
  a:putPixel(0, 0, 5)
  a:putPixel(3, 0, 5)
  s:newCel(lower, 1, a)
  local b = Image(2, 1, ColorMode.INDEXED)
  b:putPixel(0, 0, 6)
  s:newCel(upper, 1, b, Point(2, 0))
  app.layer = upper
  app.command.MergeDownLayer()
  local function check()
    assert(#s.layers == 1 and lower.paletteBank == 3)
    assert(s.properties('karn.rules.v1').enabled)
    local merged = lower:cel(1).image
    for x, expected in ipairs{5, 0, 6, 5} do
      assert(merged:getPixel(x-1, 0) == expected)
    end
  end
  check()
  app.undo()
  assert(#s.layers == 2 and upper:cel(1).image:isEqual(b))
  app.redo()
  check()
  s:close()
end
