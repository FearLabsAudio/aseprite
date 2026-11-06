-- Commands and standalone image exports must retain the selected bank's colors.
local root=app.params.output or '_test_palette_bank_operations'
assert(app.fs.makeAllDirectories(root))
local pc=app.pixelColor
local function render(s)
  local im=Image(s.width,s.height,ColorMode.RGB); im:drawSprite(s,1); return im
end
for _,rules in ipairs{false,true} do
  for bank=0,15 do
    local s=Sprite(8,8,ColorMode.INDEXED)
    local p=s.palettes[1]
    for i=0,255 do p:setColor(i,Color(i%16*17,i//16*17,68)) end
    s.properties('karn.palette-banks.v1').initialized=65535
    local layer=s.layers[1]; layer.paletteBank=bank
    if bank==0 then
      local colors={}
      for i=0,15 do colors[#colors+1]=pc.rgba(i*17,119,17,255) end
      s.properties('karn.palette-banks.v1').bank0Palettes={['1']=colors}
    end
    local im=Image(8,8,ColorMode.INDEXED); im:clear(3)
    s:newCel(layer,1,im)
    local other=s:newLayer(); other.paletteBank=(bank+1)%16
    s:newLayer() -- Document Palette remains separate.
    if rules then app.command.ForceKarnRules{enabled=true,ui=false} end
    app.layer=layer
    local master=Palette(p)
    local previous=s:getPaletteBank(bank)
    local otherPalette=s:getPaletteBank(other.paletteBank)
    local function isolated()
      assert(#p==256 and #s:getPaletteBank(bank)==16)
      for i=0,255 do
        if bank==0 or i<bank*16 or i>bank*16+15 then
          assert(p:getColor(i)==master:getColor(i),'Palette command changed another bank or document colors')
        end
      end
      assert(s:getPaletteBank(other.paletteBank)==otherPalette)
      assert(layer:cel(1).image.bytes==im.bytes,'Moving colors must not automatically remap pixel indices')
    end
    app.range.colors={3}
    app.command.MoveColors{before=8}
    assert(s:getPaletteBank(bank):getColor(7)==previous:getColor(3))
    isolated()
    app.undo(); assert(s:getPaletteBank(bank)==previous and p==master)
    app.redo(); assert(s:getPaletteBank(bank):getColor(7)==previous:getColor(3))
    app.undo()
    app.range.colors={3,7}
    app.command.CopyColors{before=8}
    local copied=s:getPaletteBank(bank)
    assert(copied:getColor(8)==previous:getColor(3) and copied:getColor(9)==previous:getColor(7))
    assert(copied:getColor(3)==previous:getColor(3) and copied:getColor(7)==previous:getColor(7))
    assert(app.range.colors[1]==8 and app.range.colors[2]==9)
    isolated()
    app.undo(); assert(s:getPaletteBank(bank)==previous and p==master)
    app.redo(); isolated(); app.undo()

    local expected=render(s):getPixel(0,0)
    for _,tableArgument in ipairs{false,true} do
      local filename=root..'/cel-'..bank..'-'..tostring(rules)..'-'..tostring(tableArgument)..'.png'
      assert(layer:cel(1).image:saveAs(tableArgument and {filename=filename} or filename))
      local out=Sprite{fromFile=filename}
      assert(out.colorMode==ColorMode.INDEXED and out.cels[1].image:getPixel(0,0)==3)
      assert(render(out):getPixel(0,0)==expected,'Cel image export used the wrong bank')
      out:close(); app.sprite=s
    end
    local override=Palette(previous); override:setColor(3,Color(238,17,136))
    local filename=root..'/override.png'
    assert(layer:cel(1).image:saveAs{filename=filename,palette=override})
    local out=Sprite{fromFile=filename}
    assert(render(out):getPixel(0,0)==pc.rgba(238,17,136,255),'Explicit export palette must take precedence')
    out:close(); app.sprite=s; app.layer=layer
    s.selection:select(Rectangle(1,1,5,5))
    app.command.NewSpriteFromSelection()
    out=app.sprite
    assert(out.id~=s.id and out.width==5 and out.height==5)
    assert(out.colorMode==ColorMode.INDEXED and out.layers[1].paletteBank==-1)
    assert(out.cels[1].image:getPixel(0,0)==3 and render(out):getPixel(0,0)==expected,
      'New Sprite from Selection lost the selected bank palette')
    out:close(); app.sprite=s
    assert(s:getPaletteBank(bank)==previous and p==master and layer:cel(1).image.bytes==im.bytes)
    s:close()
  end
end

-- The selected tilemap's local palette also applies to selection and image exports.
for _,bank in ipairs{0,3,15} do
  local s=Sprite(8,8,ColorMode.INDEXED)
  for i=0,255 do s.palettes[1]:setColor(i,Color(i%16*17,i//16*17,68)) end
  s.properties('karn.palette-banks.v1').initialized=65535
  s.gridBounds=Rectangle(0,0,8,8)
  local ts=s:newTileset(Grid{0,0,8,8},2)
  local im=Image(8,8,ColorMode.INDEXED); im:clear(3); ts:tile(1).image=im
  app.command.NewLayer{tilemap=true,name='Export tilemap'}
  local layer=app.layer; layer.paletteBank=bank; layer.tileset=ts
  local map=Image(1,1,ColorMode.TILEMAP); map:putPixel(0,0,pc.tile(1))
  s:newCel(layer,1,map)
  local expected=render(s):getPixel(0,0)
  local path=root..'/tilemap-'..bank..'.png'
  assert(layer:cel(1).image:saveAs(path))
  local out=Sprite{fromFile=path}; assert(render(out):getPixel(0,0)==expected)
  out:close(); app.sprite=s; app.layer=layer
  s.selection:select(Rectangle(0,0,8,8)); app.command.NewSpriteFromSelection()
  out=app.sprite
  assert(out.id~=s.id and render(out):getPixel(0,0)==expected)
  assert(out.cels[1].image:getPixel(0,0)==3,'Selection must retain local tile pixels')
  out:close(); s:close()
end
print('Palette commands, cel image exports and New Sprite from Selection: all banks and KARN modes OK')
