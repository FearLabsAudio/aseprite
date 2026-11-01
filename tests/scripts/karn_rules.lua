-- Persistent KARN constraints, compatible indices, conversion and atomic undo.
local base=app.params.output or '_test_karn_rules'
local function force(on) app.command.ForceKarnRules{enabled=on,ui=false} end
local function enabled(s) return s.properties('karn.rules.v1').enabled==true end
local function palette(s)
  local p=s.palettes[1]; p:resize(256)
  for i=0,255 do p:setColor(i,Color(i%16*17,i//16*17,0)) end
end
local function paint(layer,x,y,c)
  app.useTool{tool='pencil',ink=Ink.SIMPLE,color=Color{index=c},bgColor=Color{index=0},
    brush=Brush(1),layer=layer,tilesetMode=TilesetMode.AUTO,points={Point(x,y)}}
end
local function indices(image)
  for p in image:pixels() do assert(p()>=0 and p()<16) end
end
local function rgb444(s)
  for n=1,#s.palettes do
    local p=s.palettes[n]
    for i=0,#p-1 do
      local c=p:getColor(i)
      assert(c.red%17==0 and c.green%17==0 and c.blue%17==0)
    end
  end
end
local s=Sprite(32,16,ColorMode.INDEXED); palette(s)
s.gridBounds=Rectangle(0,0,16,16)
local normal=s.layers[1]; normal.name='Unbanked allowed'
local im=Image(32,16,ColorMode.INDEXED); im:clear(0); im:putPixel(0,0,5); im:putPixel(1,0,6)
s:newCel(normal,1,im)
local bank=s:newLayer(); bank.paletteBank=3
s:newCel(bank,1,Image(im))
app.command.NewLayer{tilemap=true,name='Retile'}
local tiles=app.layer; tiles.paletteBank=2
paint(tiles,0,0,5); paint(tiles,1,0,6); paint(tiles,16,1,7)
-- A diagonal tile must be rasterized into the new 8x8 tiles, not mistaken for H+V.
local map=Image(tiles:cel(1).image)
local tile=app.pixelColor.tileI(map:getPixel(0,0))
map:putPixel(0,0,app.pixelColor.tile(tile,0x20000000)); tiles:cel(1).image=map
local before=tiles:renderLocal(1).bytes
local originalMap=tiles:cel(1).image.bytes
local id,uuid=tiles.id,tostring(tiles.uuid)
tiles.properties('karn.exports.v1').collage='keep export mapping'
tiles.isContinuous=true; s:newFrame(1)
assert(tiles:cel(1).image.id==tiles:cel(2).image.id)
app.frame=1
s.palettes[1]:setColor(37,Color(8,42,250))
force(true)
assert(enabled(s) and s.colorMode==ColorMode.INDEXED and s.transparentColor==0)
assert(s.gridBounds.width==8 and s.gridBounds.height==8)
assert(tiles.tileset.grid.tileSize.width==8 and tiles.tileset.grid.tileSize.height==8)
assert(tiles:renderLocal(1).bytes==before,'Retiling must preserve the exact local pixels')
assert(tiles:cel(1).image.id==tiles:cel(2).image.id and tiles:renderLocal(2).bytes==before)
assert(tiles.id==id and tostring(tiles.uuid)==uuid and tiles.paletteBank==2)
assert(tiles.properties('karn.exports.v1').collage=='keep export mapping')
assert(normal.paletteBank==-1 and bank.paletteBank==3 and #s.palettes[1]==256)
assert(normal:cel(1).image.bytes==im.bytes and bank:cel(1).image.bytes==im.bytes)
rgb444(s)
app.undo()
assert(not enabled(s) and s.gridBounds.width==16 and tiles.tileset.grid.tileSize.width==16)
assert(tiles:cel(1).image.bytes==originalMap)
assert(tiles:cel(1).image.id==tiles:cel(2).image.id)
assert(s.palettes[1]:getColor(37)==Color(8,42,250))
app.redo()
assert(enabled(s) and tiles:renderLocal(1).bytes==before)
assert(tiles:cel(1).image.id==tiles:cel(2).image.id and tiles:renderLocal(2).bytes==before)
force(true); app.undo(); assert(not enabled(s),'Repeated enable is a no-op'); app.redo()
-- Later palette edits are constrained; invalid edits roll back atomically.
s.palettes[1]:setColor(5,Color(8,42,250)); assert(s.palettes[1]:getColor(5)==Color(0,34,255))
local old=normal:cel(1).image.bytes
local bad=Image(normal:cel(1).image); bad:putPixel(0,0,200)
assert(not pcall(function() normal:cel(1).image=bad end))
assert(normal:cel(1).image.bytes==old and enabled(s))
assert(not pcall(function() s.gridBounds=Rectangle(0,0,16,16) end))
assert(s.gridBounds.width==8)
assert(not pcall(function() s.transparentColor=2 end)); assert(s.transparentColor==0)
app.command.ChangePixelFormat{format='rgb',ui=false}; assert(s.colorMode==ColorMode.INDEXED)
app.command.Force4BitColorChannels{enabled=false}; rgb444(s)
local badMap=Image(tiles:cel(1).image)
badMap:putPixel(0,0,app.pixelColor.tile(app.pixelColor.tileI(badMap:getPixel(0,0)),0x20000000))
assert(not pcall(function() tiles:cel(1).image=badMap end))
assert(tiles:renderLocal(1).bytes==before)
paint(normal,2,0,7); assert(normal:cel(1).image:getPixel(2,0)==7)
assert(not pcall(function() paint(normal,2,0,30) end))
assert(normal:cel(1).image:getPixel(2,0)==7)
-- Explicit alternate NewLayer grid arguments cannot override KARN defaults.
app.command.NewLayer{tilemap=true,name='KarnNew',gridBounds=Rectangle(0,0,32,32)}
local fresh=app.layer
assert(fresh.paletteBank==-1 and fresh.tileset.grid.tileSize.width==8)
paint(fresh,0,0,5); paint(fresh,15,0,5); paint(fresh,0,15,5); paint(fresh,15,15,5)
local m=fresh:cel(1).image
assert(app.pixelColor.tileI(m:getPixel(0,0))==app.pixelColor.tileI(m:getPixel(1,0)),
  'Horizontal matching is enabled for new tilesets')
assert(app.pixelColor.tileF(m:getPixel(1,0))==0x80000000)
assert(app.pixelColor.tileI(m:getPixel(0,1))==app.pixelColor.tileI(m:getPixel(0,0)))
assert(app.pixelColor.tileF(m:getPixel(0,1))==0x40000000)
assert(app.pixelColor.tileI(m:getPixel(1,1))==app.pixelColor.tileI(m:getPixel(0,0)))
assert(app.pixelColor.tileF(m:getPixel(1,1))==0xc0000000)
-- An unbanked KARN layer can safely edit a tileset also used by a banked layer.
app.command.NewLayer{tilemap=true,name='Shared unbanked'}
local shared=app.layer; shared.tileset=tiles.tileset
paint(shared,24,8,7); assert(shared.paletteBank==-1)
indices(shared:renderLocal(1)); s:deleteLayer(shared)
-- Local palette operations on unbanked layers must leave the other banks intact.
app.layer=normal
local bankColor=s.palettes[1]:getColor(53)
app.command.InvertColor()
assert(s.palettes[1]:getColor(53)==bankColor)
app.undo()
-- RGB paste fits into the unbanked layer's 16 local slots.
local sourceSprite=Sprite(8,8,ColorMode.RGB)
local sourceImage=Image(8,8,ColorMode.RGB); sourceImage:clear(app.pixelColor.rgba(230,155,100,255))
sourceSprite:newCel(sourceSprite.layers[1],1,sourceImage)
app.command.Copy()
app.sprite=s; app.frame=1; app.layer=normal
app.command.Paste(); indices(normal:cel(1).image); app.undo()
sourceSprite:close(); app.sprite=s; app.layer=normal
s:saveAs(base..'.aseprite')
local loaded=app.open(base..'.aseprite')
assert(enabled(loaded) and loaded.layers[1].paletteBank==-1)
loaded.palettes[1]:setColor(7,Color(8,42,250)); assert(loaded.palettes[1]:getColor(7)==Color(0,34,255))
loaded:close(); app.sprite=s
force(false); assert(not enabled(s))
s.gridBounds=Rectangle(0,0,12,12); assert(s.gridBounds.width==12)
s:close()
-- High indices reduce to the closest existing nontransparent slot; valid indices stay put.
local reduce=Sprite(8,8,ColorMode.INDEXED); palette(reduce)
reduce.palettes[1]:setColor(20,reduce.palettes[1]:getColor(7))
local pixels=Image(8,8,ColorMode.INDEXED); pixels:clear(0)
pixels:putPixel(0,0,20); pixels:putPixel(1,0,6)
reduce:newCel(reduce.layers[1],1,pixels)
force(true); assert(enabled(reduce))
assert(reduce.cels[1].image:getPixel(0,0)==7 and reduce.cels[1].image:getPixel(1,0)==6)
app.undo(); assert(reduce.cels[1].image:getPixel(0,0)==20 and not enabled(reduce))
reduce:close()
-- Transparency remapping preserves opaque index-zero pixels.
local mask=Sprite(8,8,ColorMode.INDEXED); palette(mask); mask.transparentColor=5
local mi=Image(8,8,ColorMode.INDEXED); mi:clear(5); mi:putPixel(0,0,0)
mask:newCel(mask.layers[1],1,mi)
force(true); assert(enabled(mask) and mask.transparentColor==0)
assert(mask.cels[1].image:getPixel(1,0)==0 and mask.cels[1].image:getPixel(0,0)>0)
app.undo(); assert(mask.transparentColor==5 and mask.cels[1].image.bytes==mi.bytes)
mask:close()
-- Opaque backgrounds become transparent layers without changing names or opaque pixels.
local bg=Sprite(8,8,ColorMode.INDEXED); palette(bg)
app.command.BackgroundFromLayer(); local bgLayer=bg.layers[1]; bgLayer.name='Keep background name'
local opaque=Image(8,8,ColorMode.INDEXED); opaque:clear(0); bg:newCel(bgLayer,1,opaque)
assert(bgLayer.isBackground)
force(true); assert(enabled(bg) and not bgLayer.isBackground and bgLayer.name=='Keep background name')
assert(bgLayer:cel(1).image:getPixel(0,0)>0)
app.undo(); assert(bgLayer.isBackground and bgLayer:cel(1).image:getPixel(0,0)==0 and not enabled(bg))
bg:close()
-- RGB/grayscale conversion is included in the same undo step.
for _,mode in ipairs{ColorMode.RGB,ColorMode.GRAY} do
  local rgb=Sprite(8,8,mode)
  local source=Image(8,8,mode); source:clear(0)
  source:putPixel(0,0,mode==ColorMode.RGB and app.pixelColor.rgba(255,0,0,255) or app.pixelColor.graya(100,255))
  rgb:newCel(rgb.layers[1],1,source)
  local original=rgb.cels[1].image.bytes
  force(true); assert(enabled(rgb) and rgb.colorMode==ColorMode.INDEXED)
  indices(rgb.cels[1].image); rgb444(rgb)
  assert(rgb.cels[1].image:getPixel(1,0)==0)
  app.undo(); assert(rgb.colorMode==mode and rgb.cels[1].image.bytes==original and not enabled(rgb))
  app.redo(); assert(enabled(rgb) and rgb.colorMode==ColorMode.INDEXED)
  rgb:close()
end
print('KARN rules: conversion, enforcement, bank/unbanked indices, tile flips, transparency, undo and save/load: PASS')
