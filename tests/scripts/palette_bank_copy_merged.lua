local pc=app.pixelColor
app.preferences.experimental.use_native_clipboard=false
local function render(s)
  local im=Image(s.width,s.height,ColorMode.RGB); im:drawSprite(s,1); return im
end
for _,withReference in ipairs{false,true} do
  local s=Sprite(24,8,ColorMode.INDEXED)
  for i=0,255 do s.palettes[1]:setColor(i,Color(i%16*17,i//16*17,68)) end
  s.properties('karn.palette-banks.v1').initialized=65535
  local zero=s.layers[1]; zero.paletteBank=0
  local colors={}
  for i=0,15 do colors[#colors+1]=pc.rgba(i*17,119,17,255) end
  s.properties('karn.palette-banks.v1').bank0Palettes={['1']=colors}
  local im=Image(8,8,ColorMode.INDEXED); im:clear(3)
  s:newCel(zero,1,im)
  local other=s:newLayer(); other.paletteBank=3; s:newCel(other,1,im,Point(8,0))
  local reference
  if withReference then
    reference=s:newLayer(); reference.paletteBank=-2
    local rgb=Image(8,8,ColorMode.RGB); rgb:clear(pc.rgba(13,79,201,117))
    s:newCel(reference,1,rgb,Point(16,0))
  end
  local original=render(s)
  local actives={zero,other}
  if reference then actives[#actives+1]=reference end
  for _,active in ipairs(actives) do
    app.sprite=s; app.layer=active
    s.selection:select(Rectangle(1,1,20,6))
    app.command.CopyMerged()
    local expected=Image(24,8,ColorMode.RGB)
    for y=1,6 do for x=1,20 do expected:putPixel(x,y,original:getPixel(x,y)) end end
    for _,referenceDestination in ipairs{false,true} do
      local out=Sprite(24,8,referenceDestination and ColorMode.INDEXED or ColorMode.RGB)
      if referenceDestination then out.layers[1].paletteBank=-2 end
      local before=out.cels[1].image.bytes
      app.command.Paste() -- Default position must use the copied selection origin.
      assert(render(out).bytes==expected.bytes,'Copy Merged lost bank colors, RGB data, alpha or position')
      app.undo(); assert(out.cels[1].image.bytes==before)
      app.redo(); assert(render(out).bytes==expected.bytes)
      out:close()
    end
    -- RGB merged clipboard data must also quantize into a numbered destination.
    local indexed=Sprite(24,8,ColorMode.INDEXED)
    indexed.layers[1].paletteBank=3
    app.command.ForceKarnRules{enabled=true,ui=false}
    app.command.Paste()
    for pixel in indexed.cels[1].image:pixels() do assert(pixel()<16) end
    assert(indexed.transparentColor==0)
    indexed:close()
    assert(render(s).bytes==original.bytes)
  end
  s:close()
end
print('Copy Merged: bank palettes, RGB references, alpha, selection origins and undo/redo OK')
