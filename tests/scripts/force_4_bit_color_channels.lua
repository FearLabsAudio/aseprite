-- RGB444 conversion must change palette RGB only, across all banks.
local s = Sprite(16, 16, ColorMode.INDEXED)
local group = 'karn.palette-rgb444.v1'
assert(s.properties(group).enabled~=true)
local p = s.palettes[1]
p:resize(256)
for i=0,255 do
  p:setColor(i, Color{r=i, g=255-i, b=(i*73)%256, a=i})
end
-- Different indices remain distinct, even when their colors match.
p:setColor(6, p:getColor(5))
local image = s.cels[1].image
for pixel in image:pixels() do pixel((pixel.x+pixel.y)%16) end
local pixels = image.bytes
local layer = app.layer
layer.paletteBank = 1
s.properties('karn.palette-banks.v1').name1 = 'Test bank'
app.command.NewLayer{tilemap=true, name='Tiles'}
local tiles = app.layer
tiles.paletteBank = 3
app.useTool{tool='pencil', ink=Ink.SIMPLE, color=Color{index=5},
  brush=Brush(1), layer=tiles, tilesetMode=TilesetMode.MANUAL, points={Point(0,0)}}
local tileId = app.pixelColor.tileI(tiles:cel(1).image:getPixel(0,0))
local tilePixels = tiles.tileset:tile(tileId).image.bytes
local original = Palette(s.palettes[1])
local function checkPalette(converted)
  assert(#s.palettes[1]==256)
  for i=0,255 do
    local before, after = original:getColor(i), s.palettes[1]:getColor(i)
    local function expected(c) return converted and math.floor((c+8)/17)*17 or c end
    assert(after.red==expected(before.red), 'red '..i)
    assert(after.green==expected(before.green), 'green '..i)
    assert(after.blue==expected(before.blue), 'blue '..i)
    assert(after.alpha==before.alpha, 'alpha '..i)
  end
  assert(layer.paletteBank==1 and tiles.paletteBank==3)
  assert(s.properties('karn.palette-banks.v1').name1=='Test bank')
  assert(layer:cel(1).image.bytes==pixels)
  assert(tiles.tileset:tile(tileId).image.bytes==tilePixels)
  assert(s.transparentColor==0)
  assert((s.properties(group).enabled==true)==converted)
end
app.command.Force4BitColorChannels()
checkPalette(true)
assert(s.palettes[1]:getColor(8)==Color{r=0,g=255,b=68,a=8})
assert(s.palettes[1]:getColor(9)==Color{r=17,g=238,b=153,a=9})
app.undo(); checkPalette(false)
app.redo(); checkPalette(true)
-- Explicitly enabling an already-enabled constraint is a no-op.
app.command.Force4BitColorChannels{enabled=true}
app.undo(); checkPalette(false)
app.redo(); checkPalette(true)

-- Later color edits snap immediately, including alpha preservation.
s.palettes[1]:setColor(53,Color{r=8,g=42,b=250,a=77})
assert(s.palettes[1]:getColor(53)==Color{r=0,g=34,b=255,a=77})
app.undo(); checkPalette(true)
-- Palette loads into the selected bank are constrained too.
local replacement=Palette(16)
for i=0,15 do replacement:setColor(i,Color(42,8,250)) end
local base=app.params.output or '_test_force_4_bit_color_channels'
replacement:saveAs(base..'.gpl')
app.command.LoadPalette{filename=base..'.gpl'}
assert(s.palettes[1]:getColor(53)==Color(34,0,255))
assert(layer:cel(1).image.bytes==pixels)
assert(tiles.tileset:tile(tileId).image.bytes==tilePixels)
app.undo(); checkPalette(true)

-- Turning off keeps the snapped colors but permits free color edits.
app.command.Force4BitColorChannels()
assert(s.properties(group).enabled==false)
s.palettes[1]:setColor(53,Color(8,42,250))
assert(s.palettes[1]:getColor(53)==Color(8,42,250))
app.undo()
app.undo(); checkPalette(true)

local output = (app.params.output or '_test_force_4_bit_color_channels')..'.aseprite'
s:saveAs(output)
local loaded = app.open(output)
for i=0,255 do assert(loaded.palettes[1]:getColor(i)==s.palettes[1]:getColor(i)) end
assert(loaded.layers[1]:cel(1).image.bytes==pixels)
assert(loaded.properties(group).enabled==true)
loaded.palettes[1]:setColor(53,Color(8,42,250))
assert(loaded.palettes[1]:getColor(53)==Color(0,34,255))
loaded:close()

for _,mode in ipairs{ColorMode.RGB,ColorMode.GRAY} do
  local other=Sprite(8,8,mode)
  other.palettes[1]:setColor(1,Color(8,42,250))
  app.command.Force4BitColorChannels()
  assert(other.palettes[1]:getColor(1)==Color(8,42,250))
  assert(other.properties(group).enabled~=true)
  other:close()
end

-- A document can leave Indexed mode and later resume its saved constraint.
local ordinary=Sprite(8,8,ColorMode.INDEXED)
app.command.Force4BitColorChannels{enabled=true}
app.command.ChangePixelFormat{format='rgb'}
assert(ordinary.colorMode==ColorMode.RGB)
ordinary.palettes[1]:setColor(1,Color(8,42,250))
assert(ordinary.palettes[1]:getColor(1)==Color(8,42,250))
app.command.ChangePixelFormat{format='indexed'}
assert(ordinary.colorMode==ColorMode.INDEXED)
assert(ordinary.palettes[1]:getColor(1)==Color(0,34,255))
app.undo()
assert(ordinary.colorMode==ColorMode.RGB)
assert(ordinary.palettes[1]:getColor(1)==Color(8,42,250))
app.redo()
assert(ordinary.palettes[1]:getColor(1)==Color(0,34,255))

-- Build a two-frame ASE fixture with distinct palette keyframes. Lua's
-- setPalette edits the first palette, so append a native palette chunk here.
local animated=Sprite(8,8,ColorMode.INDEXED)
animated.palettes[1]:resize(2)
animated.palettes[1]:setColor(1,Color(8,42,250))
local animationFile=base..'-animated.aseprite'
animated:saveAs(animationFile)
animated:close()
local f=assert(io.open(animationFile,'rb'))
local contents=f:read('*a'); f:close()
local paletteData=string.pack('<I4I4I4',2,0,1)..string.rep('\0',8)..
  string.pack('<I2BBBBI2BBBB',0,0,0,0,0,0,25,43,123,255)
local chunk=string.pack('<I4I2',6+#paletteData,0x2019)..paletteData
local frame=string.pack('<I4I2I2I2c2I4',16+#chunk,0xF1FA,1,100,'\0\0',0)..chunk
f=assert(io.open(animationFile,'wb'))
f:write(string.pack('<I4',#contents+#frame)..contents:sub(5,6)..
  string.pack('<I2',2)..contents:sub(9)..frame)
f:close()
animated=app.open(animationFile)
assert(#animated.palettes==2 and #animated.frames==2)
app.command.Force4BitColorChannels{enabled=true}
assert(animated.palettes[1]:getColor(1)==Color(0,34,255))
assert(animated.palettes[2]:getColor(1)==Color(17,51,119))
app.undo()
assert(animated.palettes[1]:getColor(1)==Color(8,42,250))
assert(animated.palettes[2]:getColor(1)==Color(25,43,123))
assert(animated.properties(group).enabled~=true)
app.redo()
animated.palettes[2]:setColor(1,Color(8,42,250))
assert(animated.palettes[2]:getColor(1)==Color(0,34,255))
print('RGB444: persistent constraint, palette edits/loads, alpha/indices, undo/redo, save/load and color modes: PASS')
