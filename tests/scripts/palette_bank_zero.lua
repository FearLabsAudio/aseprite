-- Document Palette and bank 00 must survive independent edits and exports.
local output=app.params.output or '_test_palette_bank_zero'
local ns='karn.palette-banks.v1'
local E=dofile(app.params.exporter or '../data/extensions/karn-palette-banks/export.lua')
local paletteFile=output..'.gpl'
local f=assert(io.open(paletteFile,'w'))
f:write('GIMP Palette\nName: Paste fixture\nColumns: 16\n#\n')
for i=0,15 do f:write(string.format('255 %d 0 Color%d\n',i*17,i)) end
f:close()
local s=Sprite(16,8,ColorMode.INDEXED)
local p=s.palettes[1]
for i=0,255 do p:setColor(i,Color(i%16*17,i//16*17,68)) end
local doc=s.layers[1]; doc.name='Document'
local zero=s:newLayer(); zero.name='Zero'
local one=s:newLayer(); one.name='One'
-- Older files do not have the independent colors yet.
zero.properties(ns).bank=0; one.properties(ns).bank=1
local im=Image(8,8,ColorMode.INDEXED); im:clear(3)
s:newCel(doc,1,im); s:newCel(zero,1,im,Point(8,0))
app.command.ForceKarnRules{enabled=true,ui=false}
local before=s:getPaletteBank(0,1)
local other=s:getPaletteBank(1,1)
app.layer=doc
app.command.LoadPalette{filename=paletteFile,ui=false}
assert(p:getColor(3)==Color(255,51,0))
assert(s:getPaletteBank(0,1)==before and s:getPaletteBank(1,1)==other)
app.undo(); assert(p:getColor(3)==before:getColor(3) and s:getPaletteBank(0,1)==before)
app.redo(); assert(p:getColor(3)==Color(255,51,0) and s:getPaletteBank(0,1)==before)
local function check(sprite)
  assert(sprite.palettes[1]:getColor(3)==Color(255,51,0))
  assert(sprite:getPaletteBank(0,1)==before)
  local rgb=Image(16,8,ColorMode.RGB); rgb:drawSprite(sprite,1)
  assert(rgb:getPixel(0,0)==app.pixelColor.rgba(255,51,0,255))
  local c=before:getColor(3)
  assert(rgb:getPixel(8,0)==app.pixelColor.rgba(c.red,c.green,c.blue,255))
end
check(s)
s:saveAs(output..'.aseprite')
local loaded=app.open(output..'.aseprite'); check(loaded); loaded:close(); app.sprite=s
s:saveCopyAs(output..'.png')
local png=app.open(output..'.png')
assert(png.colorMode==ColorMode.RGB)
assert(png.cels[1].image:getPixel(8,0)~=png.cels[1].image:getPixel(0,0))
png:close(); app.sprite=s
local plan=E.prepare(s,{mode='bmp',frame=1,output=output..'.bmp',
  rows={{layer=zero,bmp=output..'-zero.bmp'}}})
E.write(plan)
local exported=Palette{fromFile=output..'-zero.bmp'}
assert(exported:getColor(3)==before:getColor(3))
local indices=Image{fromFile=output..'-zero.bmp'}
assert(indices:getPixel(8,0)==3,'Bank 00 BMPs must keep local indices')
-- Bank-zero imports and fresh bank inheritance must not modify document colors.
local master=Palette(p)
app.layer=zero
app.command.LoadPalette{filename=paletteFile,ui=false}
assert(p==master and s:getPaletteBank(0,1):getColor(3)==Color(255,51,0))
app.undo(); assert(s:getPaletteBank(0,1)==before and p==master)
local fresh=s:newLayer(); fresh.paletteBank=0
fresh.paletteBank=2
assert(s:getPaletteBank(2,1)==before,'A new bank must inherit independent bank-zero colors')
app.undo(); assert(fresh.paletteBank==0)
-- Leaving bank zero for the document uses the document palette for RGB conversion.
zero.paletteBank=-2
zero.paletteBank=-1
assert(zero:cel(1).image.colorMode==ColorMode.INDEXED)
for pixel in zero:cel(1).image:pixels() do assert(pixel()<16) end
s:close()
-- Independent bank keyframes can outlive redundant native palette chunks.
local animated=Sprite(8,8,ColorMode.INDEXED)
animated.layers[1].paletteBank=0
animated:newCel(animated.layers[1],1,im)
animated:newFrame()
local frames={}
for frame=1,2 do
  local colors={0}
  for i=1,15 do colors[#colors+1]=app.pixelColor.rgba(frame*17,i*17,0,255) end
  frames[tostring(frame)]=colors
end
animated.properties(ns).bank0Palettes=frames
local function checkFrames(sprite)
  for frame=1,2 do
    local color=app.pixelColor.rgba(frame*17,51,0,255)
    local rgb=Image(8,8,ColorMode.RGB); rgb:drawSprite(sprite,frame)
    assert(rgb:getPixel(0,0)==color,'Bank 00 must use the displayed frame, not a deduplicated native palette frame')
    assert(sprite:getPaletteBank(0,frame):getColor(3).red==frame*17)
  end
end
checkFrames(animated)
animated:saveAs(output..'-animated.aseprite')
local reopened=app.open(output..'-animated.aseprite'); checkFrames(reopened)
reopened:close(); animated:close()
os.remove(paletteFile)
print('Bank 00: document isolation, import, undo, rendering, save/load and BMP/PNG exports OK')
