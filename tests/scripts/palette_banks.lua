-- KARN: local tile indices, per-layer banks, persistence, and export.
local out = app.params.output or '_test_palette_banks'
local s = Sprite(16, 8, ColorMode.INDEXED)
-- This fixture represents established banks, not first-time assignments.
s.properties('karn.palette-banks.v1').initialized = 0xffff
s.gridBounds = Rectangle(0, 0, 8, 8)
local pal = s.palettes[1]
pal:resize(256)
for i=0,255 do pal:setColor(i, Color(i, 255-i, i)) end
-- Equal RGB entries must remain distinct indices.
pal:setColor(21, Color(40, 60, 80))
pal:setColor(22, Color(40, 60, 80))
pal:setColor(53, Color(180, 100, 20))
app.command.NewLayer{tilemap=true, name='Bank 1'}
local a = app.layer
assert(a.paletteBank == -1)
a.paletteBank = 1
assert(a.paletteBank == 1)
app.undo(); assert(a.paletteBank == -1)
app.redo(); assert(a.paletteBank == 1)
local function paint(layer, x, color)
  app.useTool{tool='pencil', ink=Ink.SIMPLE, color=Color{index=color},
              bgColor=Color{index=0}, brush=Brush(1), layer=layer,
              tilesetMode=TilesetMode.MANUAL, points={Point(x, 0)}}
end
paint(a, 0, 5)
paint(a, 1, 6)
local ts = a.tileset
local ti = app.pixelColor.tileI(a:cel(1).image:getPixel(0,0))
assert(ts:tile(ti).image:getPixel(0,0) == 5)
assert(ts:tile(ti).image:getPixel(1,0) == 6)
-- Repeated painting must never bake the bank offset into the editable source.
paint(a, 2, 7)
assert(ts:tile(ti).image:getPixel(0,0) == 5)
assert(ts:tile(ti).image:getPixel(2,0) == 7)
app.undo(); assert(ts:tile(ti).image:getPixel(2,0) == 0)
app.redo(); assert(ts:tile(ti).image:getPixel(2,0) == 7)
app.command.NewLayer{tilemap=true, name='Bank 3'}
local b = app.layer
b.tileset = ts
b.paletteBank = 3
local map = Image(a:cel(1).image)
s:newCel(b, 1, map, Point(8,0))
local function check(sprite)
  local idx = Image(16,8,ColorMode.INDEXED)
  idx:drawSprite(sprite,1)
  assert(idx:getPixel(0,0)==21, 'indexed bank 1')
  assert(idx:getPixel(1,0)==22, 'duplicate RGB index preserved')
  assert(idx:getPixel(8,0)==53, 'indexed bank 3')
  assert(idx:getPixel(7,7)==0, 'transparent zero')
  local rgb = Image(16,8,ColorMode.RGB)
  rgb:drawSprite(sprite,1)
  assert(rgb:getPixel(0,0)==app.pixelColor.rgba(40,60,80,255), 'RGB bank 1')
  assert(rgb:getPixel(8,0)==app.pixelColor.rgba(180,100,20,255), 'RGB bank 3')
  assert(rgb:getPixel(7,7)==0, 'RGBA transparency')
end
check(s)
-- Changing a bank affects only interpretation, including a shared tileset.
b.paletteBank = 2
assert(ts:tile(ti).image:getPixel(0,0)==5)
app.undo(); assert(b.paletteBank==3)
check(s)
-- Existing user data remains intact and bank metadata clones with the document.
a.data = 'keep me'
s.properties('karn.palette-banks.v1').name1 = 'Mountains'
s:saveAs(out..'.aseprite')
local loaded = app.open(out..'.aseprite')
assert(loaded.layers[2].paletteBank==1)
assert(loaded.layers[3].paletteBank==3)
assert(loaded.layers[2].data=='keep me')
assert(loaded.properties('karn.palette-banks.v1').name1=='Mountains')
assert(loaded.layers[2].tileset==loaded.layers[3].tileset)
assert(loaded.layers[2].tileset:tile(ti).image:getPixel(0,0)==5)
check(loaded)
loaded:saveAs(out..'.png')
local png = app.open(out..'.png')
assert(png.colorMode == ColorMode.INDEXED)
assert(png.cels[1].image:getPixel(0,0)==21)
assert(png.cels[1].image:getPixel(1,0)==22)
assert(png.cels[1].image:getPixel(8,0)==53)
png:close()
app.sprite = s
app.command.ExportSpriteSheet{ui=false, type=SpriteSheetType.HORIZONTAL,
  textureFilename=out..'-sheet.png', dataFilename=out..'-sheet.json'}
local sheet=app.open(out..'-sheet.png')
assert(sheet.cels[1].image:getPixel(0,0)==21)
assert(sheet.cels[1].image:getPixel(8,0)==53)
sheet:close()
app.sprite=s
app.layer=a
app.command.ExportTileset{ui=false, type=SpriteSheetType.HORIZONTAL,
  textureFilename=out..'-tiles.png', dataFilename=out..'-tiles.json'}
local tiles=app.open(out..'-tiles.png')
local found=false
for p in tiles.cels[1].image:pixels() do if p()==21 then found=true end end
assert(found, 'tileset export must apply selected layer bank')
tiles:close()
app.sprite=s
app.layer=a
-- Invalid assignment is rejected without altering pixels.
assert(not pcall(function() a.paletteBank=16 end))
assert(a.paletteBank==1)
local ordinary=s.layers[1]
ordinary.paletteBank=1
assert(ordinary.paletteBank==1)
app.undo(); assert(ordinary.paletteBank==-1)
-- Swapping an unrestricted tileset into a banked layer is rejected.
local invalid=s:newTileset(Grid{0,0,8,8},2)
invalid:tile(1).image:clear(20)
assert(not pcall(function() a.tileset=invalid end))
assert(a.tileset==ts)
-- Shared local tiles cannot be painted via an unrestricted layer.
b.paletteBank=-1
pcall(function() paint(b,8,20) end)
assert(ts:tile(ti).image:getPixel(0,0)==5)
b.paletteBank=3
check(s)
-- Copy extracts local pixels and their local palette. Batch Aseprite only
-- supports Paste into regular layers, so verify the extracted 16-slot image.
local nativeClipboard=app.preferences.experimental.use_native_clipboard
app.preferences.experimental.use_native_clipboard=false
app.layer=a
s.selection=Selection(Rectangle(0,0,2,1))
app.command.Copy()
local clipboardTarget=Sprite(2,1,ColorMode.INDEXED)
clipboardTarget.palettes[1]:resize(16)
clipboardTarget.palettes[1]:setColor(0,Color(0,0,0,0))
for i=1,15 do clipboardTarget.palettes[1]:setColor(i,s.palettes[1]:getColor(16+i)) end
assert(app.command.Paste{x=0,y=0})
assert(app.cel.image:getPixel(0,0)==5)
assert(app.cel.image:getPixel(1,0)==6)
clipboardTarget:close()
app.sprite=s
s.selection:deselect()
app.preferences.experimental.use_native_clipboard=nativeClipboard
check(s)
-- Conversion preserves the bank and local indices in either direction.
app.layer=a
app.command.ConvertLayer{to='layer'}
assert(not app.layer.isTilemap)
assert(app.layer.paletteBank==1)
assert(app.layer:cel(1).image:getPixel(0,0)==5)
check(s)
app.command.ConvertLayer{to='tilemap',ui=false}
assert(app.layer.paletteBank==1)
check(s)
app.undo()
app.undo(); assert(app.layer.isTilemap and app.layer.paletteBank==1)
check(s)
-- Loading a local palette edits one bank, never the master palette's size.
local replacement=Palette(16)
for i=0,15 do replacement:setColor(i,Color(17*i,0,0)) end
replacement:saveAs(out..'-local.gpl')
app.layer=a
app.command.LoadPalette{filename=out..'-local.gpl'}
assert(#s.palettes[1]==256)
assert(s.palettes[1]:getColor(21).red==85)
assert(s.palettes[1]:getColor(53).red==180)
assert(ts:tile(ti).image:getPixel(0,0)==5)
app.undo(); check(s)
-- A 16-color document can allocate a bank without losing its existing colors.
local small=Sprite(8,8,ColorMode.INDEXED)
small.palettes[1]:resize(16)
small.palettes[1]:setColor(5,Color(17,34,51))
app.command.NewLayer{tilemap=true}
app.layer.paletteBank=15
assert(#small.palettes[1]==256)
assert(small.palettes[1]:getColor(245)==Color(17,34,51))
app.undo(); assert(#small.palettes[1]==16 and app.layer.paletteBank==-1)
print('Palette banks: local editing, undo, save/load, RGB/indexed composition, compatible exports and conversion: PASS')
