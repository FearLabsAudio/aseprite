-- KARN stores opaque colors, while the mask index keeps artwork transparent.
local output=app.params.output or '_test_karn_palette_opacity'
local ns='karn.palette-banks.v1'
local rgba=app.pixelColor.rgba
local s=Sprite(8,8,ColorMode.INDEXED)
local document=s.layers[1]
local p=s.palettes[1]
for i=0,255 do p:setColor(i,Color(i%16*17,i//16*17,68,i%3*85)) end
local ordinary=Palette(p)
app.command.Force4BitColorChannels{enabled=true}
assert(p==ordinary,'RGB444 alone must preserve alpha')
p:saveAs(output..'-import.aseprite')
local im=Image(8,8,ColorMode.INDEXED); im:clear(0)
im:putPixel(1,0,1); im:putPixel(2,0,2)
s:newCel(document,1,im)
local bank=s:newLayer(); bank.paletteBank=1
s:newCel(bank,1,im,Point(0,1))
local zero=s:newLayer(); zero.paletteBank=0
s:newCel(zero,1,im,Point(0,2))
local reference=s:newLayer(); reference.paletteBank=-2
local ref=Image(8,8,ColorMode.RGB); ref:clear(rgba(13,27,39,63))
s:newCel(reference,1,ref); reference.isVisible=false
local zeroBefore=json.encode(s.properties(ns).bank0Palettes)
local paletteBefore=Palette(p)
app.command.ForceKarnRules{enabled=true,ui=false}
local function check(sprite)
  assert(sprite.transparentColor==0)
  for frame=1,#sprite.palettes do
    local pal=sprite.palettes[frame]
    for i=0,#pal-1 do assert(pal:getColor(i).alpha==255,'Stored alpha at '..i) end
  end
  for n=0,15 do
    for i=0,15 do assert(sprite:getPaletteBank(n,1):getColor(i).alpha==255) end
  end
  for _,colors in pairs(sprite.properties(ns).bank0Palettes or {}) do
    for _,c in ipairs(colors) do assert(app.pixelColor.rgbaA(c)==255) end
  end
  local rendered=Image(8,8,ColorMode.RGB); rendered:drawSprite(sprite,1)
  for y=0,2 do
    assert(app.pixelColor.rgbaA(rendered:getPixel(0,y))==0,'Index zero must remain transparent')
    for x=1,2 do assert(app.pixelColor.rgbaA(rendered:getPixel(x,y))==255) end
  end
  assert(sprite.layers[4]:cel(1).image.bytes==ref.bytes,'Full color Reference alpha must survive')
end
check(s)
app.undo()
assert(p==paletteBefore,'Enabling Undo must restore all original alpha values')
assert(json.encode(s.properties(ns).bank0Palettes)==zeroBefore,'Enabling Undo must restore bank-zero history')
app.redo(); check(s)
-- Later edits/imports include slot zero's RGB and cannot introduce alpha.
p:setColor(0,Color(255,34,51,0))
assert(p:getColor(0)==Color(255,34,51,255)); check(s)
p:setColor(1,Color(17,34,51,0)); assert(p:getColor(1).alpha==255)
local replacement=Palette(p)
replacement:setColor(2,Color(17,34,51,85))
s:setPalette(replacement); assert(p:getColor(2).alpha==255)
for _,layer in ipairs{document,zero,bank} do
  app.layer=layer
  app.command.LoadPalette{filename=output..'-import.aseprite',ui=false}
  local actual=layer.paletteBank<0 and p or s:getPaletteBank(layer.paletteBank,1)
  for i=0,15 do
    local c=ordinary:getColor(i)
    assert(actual:getColor(i)==Color(c.red,c.green,c.blue,255),'Imported slot '..i)
  end
  check(s)
end
assert(not pcall(function() s.transparentColor=2 end))
check(s)
s:saveAs(output..'.aseprite')
local reopened=app.open(output..'.aseprite'); check(reopened); reopened:close()
app.sprite=s
app.command.ForceKarnRules{enabled=false,ui=false}
p:setColor(1,Color(17,34,51,85)); assert(p:getColor(1).alpha==85)
s:close()

-- Read a file written by the previous build, with KARN enabled and alpha-zero
-- colors. Only fixture creation runs on that old executable.
if app.params.legacy then
  local old=app.open(app.params.legacy)
  for frame=1,#old.palettes do
    local pal=old.palettes[frame]
    for i=0,#pal-1 do assert(pal:getColor(i).alpha==255) end
  end
  for _,colors in pairs(old.properties(ns).bank0Palettes) do
    for _,c in ipairs(colors) do assert(app.pixelColor.rgbaA(c)==255) end
  end
  assert(old:getPaletteBank(0,2):getColor(1)==Color(17,34,51,255))
  old.layers[1].name='Still editable'
  old:saveAs(output..'-upgraded.aseprite')
  old:close()
end
print('KARN palette opacity: enable/undo, edits/imports, index-zero rendering, RGB reference and save/load OK')
