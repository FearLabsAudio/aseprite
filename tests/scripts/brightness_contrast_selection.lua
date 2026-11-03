-- A selected transparent index keeps its meaning even if its swatch is opaque.
for _, mask in ipairs{ 0, 5 } do
  local s = Sprite(5, 1, ColorMode.INDEXED)
  local layer = s.layers[1]
  local palette = s.palettes[1]
  palette:resize(16)
  for i = 0, 15 do
    palette:setColor(i, Color(i*17, i*17, i*17, 255))
  end
  if mask == 0 then
    layer.paletteBank = 2
    app.command.ForceKarnRules{ enabled=true, ui=false }
  else
    s.transparentColor = mask
  end

  local spec = s.spec
  spec.width = 4
  local image = Image(spec)
  image:clear(4)
  image:putPixel(1, 0, mask)
  s:newCel(layer, 1, image, Point(1, 0))
  s.selection = Selection(Rectangle(0, 0, 3, 1))
  app.command.BrightnessContrast{ brightness=50, ui=false }
  local cel = layer:cel(1)
  assert(cel.position == Point(1, 0), 'Transparent space outside the cel must stay empty')
  assert(cel.image:getPixel(0, 0) == 6, 'Selected artwork should brighten')
  assert(cel.image:getPixel(1, 0) == mask, 'Transparent hole should keep its index')
  assert(cel.image:getPixel(2, 0) == 4, 'Artwork outside the selection should keep its index')
  app.undo()
  assert(layer:cel(1).image:isEqual(image))
  s:close()
end

-- On an opaque background even index zero is artwork.
do
  local s = Sprite(2, 1, ColorMode.INDEXED)
  app.command.BackgroundFromLayer()
  assert(s.layers[1].isBackground)
  s.palettes[1]:resize(2)
  s.palettes[1]:setColor(0, Color(68, 68, 68))
  s.palettes[1]:setColor(1, Color(102, 102, 102))
  s.cels[1].image:clear(0)
  s.selection = Selection(Rectangle(0, 0, 1, 1))
  app.command.BrightnessContrast{ brightness=50, ui=false }
  assert(s.cels[1].image:getPixel(0, 0) == 1)
  assert(s.cels[1].image:getPixel(1, 0) == 0)
  s:close()
end
