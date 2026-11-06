local pc=app.pixelColor
local function setup(s,bank)
  local p=s.palettes[1]
  for i=0,255 do p:setColor(i,Color(0,255,0)) end
  p:setColor(1,Color(255,0,0)); p:setColor(2,Color(0,0,255)); p:setColor(200,Color(136,0,136))
  if bank>0 then
    p:setColor(bank*16+1,Color(0,0,0)); p:setColor(bank*16+2,Color(255,255,255))
    p:setColor(bank*16+3,Color(136,136,136))
  end
  s.properties('karn.palette-banks.v1').initialized=65535
  local colors={}
  for i=0,15 do colors[#colors+1]=pc.rgba(0,255,0,255) end
  colors[2]=pc.rgba(0,0,0,255); colors[3]=pc.rgba(255,255,255,255); colors[4]=pc.rgba(136,136,136,255)
  s.properties('karn.palette-banks.v1').bank0Palettes={['1']=colors}
end
local function source()
  local im=Image(2,2,ColorMode.INDEXED)
  for y=0,1 do im:putPixel(0,y,1); im:putPixel(1,y,2) end
  return im
end
for _,rules in ipairs{false,true} do
  for bank=0,15 do
    local s=Sprite(2,2,ColorMode.INDEXED); setup(s,bank)
    local layer=s.layers[1]; layer.paletteBank=bank; layer.isContinuous=true
    local im=source(); s:newCel(layer,1,im)
    if rules then app.command.ForceKarnRules{enabled=true,ui=false} end
    s:newFrame(1); app.frame=1
    local palette=s:getPaletteBank(bank)
    app.command.SpriteSize{ui=false,width=3,height=3,method='bilinear'}
    local function check()
      assert(s.width==3 and s.height==3,'Bank-aware resize was rejected')
      assert(layer:cel(1).image.id==layer:cel(2).image.id,'Resize broke linked frames')
      for _,cel in ipairs(layer.cels) do
        assert(cel.image.width==3 and cel.image.height==3)
        assert(cel.image:getPixel(1,1)==3,'Resize matched through the Document Palette')
        for pixel in cel.image:pixels() do assert(pixel()<16,'Resize emitted a global palette index') end
      end
      assert(s:getPaletteBank(bank)==palette)
    end
    check()
    app.undo(); assert(s.width==2 and layer:cel(1).image.bytes==im.bytes)
    app.redo(); check(); s:close()
  end
end

-- Full-color references match ordinary RGB resizing and retain exact undo data.
local rgb=Image(2,2,ColorMode.RGB)
rgb:putPixel(0,0,pc.rgba(91,35,127,0)); rgb:putPixel(1,0,pc.rgba(13,79,201,117))
rgb:putPixel(0,1,pc.rgba(255,34,17,255)); rgb:putPixel(1,1,pc.rgba(17,255,34,200))
local expected
for _,reference in ipairs{false,true} do
  local s=Sprite(2,2,reference and ColorMode.INDEXED or ColorMode.RGB)
  local layer=s.layers[1]
  if reference then layer.paletteBank=-2; app.command.ForceKarnRules{enabled=true,ui=false} end
  s:newCel(layer,1,rgb)
  app.command.SpriteSize{ui=false,width=3,height=3,method='bilinear'}
  if reference then assert(layer:cel(1).image.bytes==expected,'Reference resize lost RGB channels or alpha')
  else expected=layer:cel(1).image.bytes end
  app.undo(); assert(layer:cel(1).image.bytes==rgb.bytes,'Undo lost transparent RGB data')
  app.redo(); assert(layer:cel(1).image.bytes==expected)
  s:close()
end

-- Smooth resizing separates banks sharing one indexed tileset. Nearest keeps sharing.
for _,case in ipairs{
  {method='bilinear',canvas=2,tileSize=3},
  {method='nearest-neighbor',canvas=2,tileSize=3},
  {method='bilinear',canvas=4,tileSize=1},
  {method='nearest-neighbor',canvas=4,tileSize=1}} do
  local method=case.method
  local s=Sprite(case.canvas,case.canvas,ColorMode.INDEXED); setup(s,1)
  s.palettes[1]:setColor(19,Color(0,255,0))
  s.palettes[1]:setColor(20,Color(136,136,136))
  s.gridBounds=Rectangle(0,0,2,2)
  local tileset=s:newTileset(Grid{0,0,2,2},2)
  tileset:tile(1).image=source()
  tileset.name='Shared palette resize'; tileset.baseIndex=5
  tileset.properties('fixture').keep='tileset data'
  tileset:tile(1).properties('fixture').keep='tile data'
  local layers={}
  local map=Image(1,1,ColorMode.TILEMAP); map:putPixel(0,0,pc.tile(1,0x80000000))
  for i,bank in ipairs{0,0,1} do
    app.command.NewLayer{tilemap=true,name='Tilemap'..i}
    local layer=app.layer; layer.paletteBank=bank; layer.tileset=tileset
    s:newCel(layer,1,map); layers[#layers+1]=layer
  end
  local count=#s.tilesets
  app.command.SpriteSize{ui=false,width=3,height=3,method=method}
  local function check()
    assert(s.width==3 and layers[1].tileset==layers[2].tileset)
    assert((layers[1].tileset==layers[3].tileset)==(method~='bilinear'))
    assert(#s.tilesets==count+(method=='bilinear' and 1 or 0))
    for _,layer in ipairs(layers) do
      assert(layer:cel(1).image.bytes==map.bytes,'Resize changed tile IDs or flip flags')
      assert(layer.tileset.name=='Shared palette resize' and layer.tileset.baseIndex==5)
      assert(layer.tileset.properties('fixture').keep=='tileset data')
      assert(layer.tileset:tile(1).properties('fixture').keep=='tile data')
      assert(layer.tileset.grid.tileSize.width==case.tileSize)
      assert(layer.tileset:tile(1).image.width==case.tileSize and layer.tileset:tile(1).image.height==case.tileSize,
        'Fractional scaling must keep tile images consistent with the tileset grid')
      if method=='bilinear' and case.tileSize==3 then
        assert(layer.tileset:tile(1).image:getPixel(1,1)==(layer.paletteBank==0 and 3 or 4))
      end
      for pixel in layer.tileset:tile(1).image:pixels() do assert(pixel()<16) end
    end
  end
  check()
  app.undo(); assert(s.width==case.canvas and #s.tilesets==count and layers[1].tileset==tileset and layers[3].tileset==tileset)
  assert(tileset:tile(1).image.bytes==source().bytes)
  app.redo(); check(); s:close()
end
print('Bank-aware smooth resize: all banks, KARN rules, linked frames and shared tilesets OK')
