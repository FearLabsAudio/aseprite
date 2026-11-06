-- Bank-local remapping shares the UI implementation and stock color matching.
local root=app.params.output or '_test_palette_bank_remap'
assert(app.fs.makeAllDirectories(root))
local pc=app.pixelColor
local function writePalette(path,palette,swap,solid)
  local f=assert(io.open(path,'w'))
  f:write('GIMP Palette\nName: Remap fixture\nColumns: 16\n#\n')
  for i=0,#palette-1 do
    local at=swap and (i==3 and 7 or i==7 and 3 or i) or i
    local c=solid or palette:getColor(at)
    f:write(string.format('%d %d %d Color%d\n',c.red,c.green,c.blue,i))
  end
  f:close()
end
local function render(s,frame)
  local image=Image(s.width,s.height,ColorMode.RGB)
  image:drawSprite(s,frame or 1)
  return image.bytes
end
local function pixels()
  local image=Image(8,8,ColorMode.INDEXED)
  image:putPixel(0,0,3); image:putPixel(1,0,7)
  return image
end
for _,rules in ipairs{false,true} do
  for bank=0,15 do
    local s=Sprite(32,8,ColorMode.INDEXED)
    local pal=s.palettes[1]
    for i=0,255 do pal:setColor(i,Color(i%16*17,i//16*17,68)) end
    s.properties('karn.palette-banks.v1').initialized=65535
    local document=s.layers[1]; document.name='Document'
    s:newCel(document,1,pixels(),Point(16,0))
    local target=s:newLayer(); target.name='Target'; target.paletteBank=bank
    target.isContinuous=true
    s:newCel(target,1,pixels())
    local group=s:newGroup()
    local sibling=s:newLayer(); sibling.parent=group; sibling.paletteBank=bank
    sibling.isVisible=false; sibling.isEditable=false
    s:newCel(sibling,1,pixels())
    local other=s:newLayer(); other.name='Other'; other.paletteBank=(bank+1)%16
    s:newCel(other,1,pixels(),Point(8,0))
    local reference=s:newLayer(); reference.name='RGB'; reference.paletteBank=-2
    local rgb=Image(8,8,ColorMode.RGB); rgb:clear(pc.rgba(13,79,201,117))
    s:newCel(reference,1,rgb,Point(24,0))
    if rules then app.command.ForceKarnRules{enabled=true,ui=false} end
    s:newFrame(1); app.frame=1; app.layer=target
    assert(target:cel(1).image.id==target:cel(2).image.id)
    local previous=s:getPaletteBank(bank,1)
    local original=target:cel(1).image.bytes
    local otherBytes=other:cel(1).image.bytes
    local expected={render(s,1),render(s,2)}
    local filename=root..'/swap.gpl'
    writePalette(filename,previous,true)
    app.command.LoadPalette{filename=filename,ui=false}
    assert(target:cel(1).image.bytes==original,'Palette edits must not automatically remap pixels')
    s:remapPalette(previous,bank,1)
    local function check()
      assert(target:cel(1).image.id==target:cel(2).image.id,'Linked cels must remain linked')
      for frame=1,2 do
        assert(target:cel(frame).image:getPixel(0,0)==7,
          string.format('bank=%d rules=%s frame=%d got=%d old3=%08x new7=%08x',bank,tostring(rules),frame,
            target:cel(frame).image:getPixel(0,0),previous:getColor(3).rgbaPixel,s:getPaletteBank(bank,1):getColor(7).rgbaPixel))
        assert(target:cel(frame).image:getPixel(1,0)==3)
        assert(target:cel(frame).image:getPixel(2,0)==0)
        assert(sibling:cel(frame).image:getPixel(0,0)==7,'Hidden/locked same-bank layers must follow the bank')
        assert(other:cel(frame).image.bytes==otherBytes)
        assert(document:cel(frame).image.bytes==original)
        assert(reference:cel(frame).image.bytes==rgb.bytes)
        assert(render(s,frame)==expected[frame],'Exact remapping must preserve composed artwork')
      end
      assert(s.transparentColor==0)
    end
    check()
    app.undo(); assert(target:cel(1).image.bytes==original)
    app.undo(); assert(s:getPaletteBank(bank,1)==previous)
    app.redo(); app.redo(); check()
    if bank==0 or bank==15 then
      s:saveAs(root..'/bank'..bank..'-'..tostring(rules)..'.aseprite')
      local reopened=app.open(s.filename)
      assert(render(reopened,1)==expected[1] and render(reopened,2)==expected[2])
      reopened:close(); app.sprite=s
    end
    s:close()
  end
end

-- Shared tilesets split by palette scope, retaining tile IDs, flags and metadata.
for _,bank in ipairs{-1,0,3} do
  local s=Sprite(16,8,ColorMode.INDEXED)
  for i=0,255 do s.palettes[1]:setColor(i,Color(i%16*17,i//16*17,68)) end
  s.properties('karn.palette-banks.v1').initialized=65535
  s.gridBounds=Rectangle(0,0,8,8)
  app.command.NewLayer{tilemap=true,name='Target tilemap'}
  local target=app.layer; target.paletteBank=bank
  local otherBank=bank==3 and 4 or 3
  local function paint(layer)
    app.useTool{tool='pencil',ink=Ink.SIMPLE,color=Color{index=3},
      brush=Brush(1),layer=layer,tilesetMode=TilesetMode.AUTO,points={Point(0,0)}}
  end
  paint(target)
  local tileset=target.tileset
  tileset.name='Original tiles'
  tileset.properties('fixture').keep='tileset metadata'
  local tileIndex=pc.tileI(target:cel(1).image:getPixel(0,0))
  tileset:tile(tileIndex).properties('fixture').keep='tile metadata'
  app.command.NewLayer{tilemap=true,name='Same-bank tilemap'}
  local sibling=app.layer; sibling.paletteBank=bank; sibling.tileset=tileset
  s:newCel(sibling,1,Image(target:cel(1).image))
  app.command.NewLayer{tilemap=true,name='Other-bank tilemap'}
  local other=app.layer; other.paletteBank=otherBank; other.tileset=tileset
  s:newCel(other,1,Image(target:cel(1).image),Point(8,0))
  local mapBytes=target:cel(1).image.bytes
  local original=tileset:tile(tileIndex).image.bytes
  local before=render(s)
  app.layer=target
  local previous=bank<0 and Palette(s.palettes[1]) or s:getPaletteBank(bank)
  writePalette(root..'/swap.gpl',previous,true)
  app.command.LoadPalette{filename=root..'/swap.gpl',ui=false}
  s:remapPalette(previous,bank)
  local function check()
    assert(target.tileset~=other.tileset and target.tileset==sibling.tileset)
    assert(other.tileset==tileset and tileset:tile(tileIndex).image.bytes==original)
    assert(target.tileset:tile(tileIndex).image:getPixel(0,0)==7)
    assert(target:cel(1).image.bytes==mapBytes and sibling:cel(1).image.bytes==mapBytes)
    assert(target.tileset.properties('fixture').keep=='tileset metadata')
    assert(target.tileset:tile(tileIndex).properties('fixture').keep=='tile metadata')
    assert(render(s)==before)
  end
  check()
  app.undo(); assert(target.tileset==tileset and sibling.tileset==tileset)
  app.redo(); check()
  s:saveAs(root..'/tiles-bank'..bank..'.aseprite')
  local reopened=app.open(s.filename); assert(render(reopened)==before); reopened:close()
  app.sprite=s; app.layer=target
  local changed=bank<0 and Palette(s.palettes[1]) or s:getPaletteBank(bank)
  writePalette(root..'/original.gpl',previous,false)
  app.command.LoadPalette{filename=root..'/original.gpl',ui=false}
  local count=#s.tilesets
  s:remapPalette(changed,bank)
  assert(#s.tilesets==count,'A tileset used only in this scope should be replaced in place')
  assert(target.tileset:tile(tileIndex).image.bytes==original and render(s)==before)
  app.undo(); assert(target.tileset:tile(tileIndex).image:getPixel(0,0)==7)
  app.redo(); assert(target.tileset:tile(tileIndex).image.bytes==original)
  s:close()
end

-- The Document Palette remaps indexed artwork without touching RGB references.
for _,kind in ipairs{'mixed','reference-only','karn-unbanked'} do
  local s=Sprite(24,8,ColorMode.INDEXED)
  for i=0,255 do s.palettes[1]:setColor(i,Color(i%16*17,i//16*17,68)) end
  s.properties('karn.palette-banks.v1').initialized=65535
  local document=s.layers[1]
  s:newCel(document,1,pixels())
  local other
  if kind=='mixed' then
    other=s:newLayer(); other.paletteBank=2
    s:newCel(other,1,pixels(),Point(8,0))
  end
  local reference=s:newLayer(); reference.paletteBank=-2
  local rgb=Image(8,8,ColorMode.RGB); rgb:clear(pc.rgba(13,79,201,117))
  s:newCel(reference,1,rgb,Point(16,0))
  if kind=='karn-unbanked' then app.command.ForceKarnRules{enabled=true,ui=false} end
  if kind=='reference-only' then s.transparentColor=5 end
  app.layer=document
  local before=render(s)
  local previous=Palette(s.palettes[1])
  if kind=='karn-unbanked' then previous:resize(16) end
  writePalette(root..'/document.gpl',previous,true)
  app.command.LoadPalette{filename=root..'/document.gpl',ui=false}
  s:remapPalette(previous,-1)
  assert(document:cel(1).image:getPixel(0,0)==7 and document:cel(1).image:getPixel(1,0)==3)
  assert(not other or other:cel(1).image:getPixel(0,0)==3)
  assert(reference:cel(1).image.bytes==rgb.bytes and render(s)==before)
  assert(s.transparentColor==(kind=='reference-only' and 5 or 0))
  app.undo(); assert(document:cel(1).image:getPixel(0,0)==3)
  app.redo(); assert(document:cel(1).image:getPixel(0,0)==7)
  s:close()
end

-- Many-to-one/nearest-color remaps still restore exact indices on undo.
local s=Sprite(8,8,ColorMode.INDEXED)
local layer=s.layers[1]; layer.paletteBank=0
s:newCel(layer,1,pixels())
local previous=s:getPaletteBank(0)
local original=layer:cel(1).image.bytes
writePalette(root..'/solid.gpl',previous,false,Color(17,238,119))
app.command.LoadPalette{filename=root..'/solid.gpl',ui=false}
s:remapPalette(previous,0)
assert(layer:cel(1).image:getPixel(0,0)==1 and layer:cel(1).image:getPixel(1,0)==1)
assert(layer:cel(1).image:getPixel(2,0)==0)
app.undo(); assert(layer:cel(1).image.bytes==original)
app.redo(); assert(layer:cel(1).image:getPixel(0,0)==1)
assert(not pcall(function() s:remapPalette(previous,-2) end))
assert(not pcall(function() s:remapPalette(previous,16) end))
assert(not pcall(function() s:remapPalette(previous,0,99) end))
s:close()
-- A rejected remap rolls back earlier layers and releases the transaction.
s=Sprite(8,8,ColorMode.INDEXED)
for i=0,255 do s.palettes[1]:setColor(i,Color(i%16*17,i//16*17,68)) end
layer=s.layers[1]; layer.paletteBank=0
s:newCel(layer,1,pixels())
previous=s:getPaletteBank(0)
local invalid=s:newLayer(); invalid.paletteBank=0
local high=Image(8,8,ColorMode.INDEXED); high:clear(200)
s:newCel(invalid,1,high)
app.layer=layer
writePalette(root..'/invalid.gpl',previous,true)
app.command.LoadPalette{filename=root..'/invalid.gpl',ui=false}
local ok,err=pcall(function() s:remapPalette(previous,0) end)
assert(not ok and err:find('local indices 0-15',1,true))
assert(layer:cel(1).image:getPixel(0,0)==3 and invalid:cel(1).image.bytes==high.bytes)
s:deleteLayer(invalid)
s:remapPalette(previous,0)
assert(layer:cel(1).image:getPixel(0,0)==7)
s:close()
print('Palette remapping: all banks, scopes, linked frames, shared tilesets, undo and persistence OK')
