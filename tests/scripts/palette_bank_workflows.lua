-- Palette imports, bank lifecycle, KARN sizing, and RGB round trips.
local base=app.params.output or '_test_palette_bank_workflows'
local ns='karn.palette-banks.v1'
local s=Sprite(8,8,ColorMode.INDEXED)
local a=s.layers[1]
local p=s.palettes[1]
p:setColor(3,Color(17,34,51))
local im=Image(8,8,ColorMode.INDEXED); im:clear(3)
s:newCel(a,1,im)
assert(s.properties(ns).initialized==nil)
a.paletteBank=2
assert(p:getColor(35)==Color(17,34,51),'A new bank inherits the document colors')
assert(a:cel(1).image.bytes==im.bytes)
assert(s.properties(ns).initialized==4)
app.undo(); assert(a.paletteBank==-1 and s.properties(ns).initialized==nil)
app.redo(); assert(a.paletteBank==2 and s.properties(ns).initialized==4)
p:setColor(35,Color(68,85,102))
a.paletteBank=5
assert(p:getColor(83)==Color(68,85,102),'New banks inherit the previous bank, not bank zero')
p:setColor(35,Color(119,136,153))
a.paletteBank=2
assert(p:getColor(35)==Color(119,136,153),'Noninteractive reuse keeps existing colors')
local b=s:newLayer(); b.paletteBank=5
s:newCel(b,1,im)
local shared=s:newLayer(); shared.paletteBank=2
assert(p:getColor(35)==Color(119,136,153),'An assigned bank is never overwritten by assignment')
-- Real 4bpp BMP with sixteen palette entries; importing must not replace the
-- master palette or alter either the previous bank or the document palette.
local bmp=base..'.bmp'
local colors={}
for i=0,15 do colors[#colors+1]=string.char(0,17*i,255,0) end
local payload=string.rep('\x33',4)..string.rep('\0',28)
local bytes='BM'..string.pack('<I4I2I2I4',118+#payload,0,0,118)..
  string.pack('<I4i4i4I2I2I4I4i4i4I4I4',40,8,8,1,4,0,#payload,0,0,16,0)..
  table.concat(colors)..payload
local f=assert(io.open(bmp,'wb')); f:write(bytes); f:close()
app.layer=a
local before=Palette(p)
app.command.LoadPalette{filename=bmp,ui=false}
assert(#p==#before)
for i=0,#p-1 do
  if i>32 and i<48 then assert(p:getColor(i)==Color(255,(i-32)*17,0))
  else assert(p:getColor(i)==before:getColor(i),'Import changed another bank: '..i) end
end
app.undo(); assert(p==before)
app.redo(); assert(p:getColor(35)==Color(255,51,0))
-- The source is banked artwork, not a full-document palette operation.
before=Palette(p)
app.command.ColorQuantization{ui=false,maxColors=256}
for i=0,#p-1 do
  if i<=32 or i>=48 then assert(p:getColor(i)==before:getColor(i)) end
end
app.undo()
app.command.ForceKarnRules{enabled=true,ui=false}
local size=#p
local ok,err=pcall(function() p:resize(17) end)
assert(not ok and type(err)=='string' and err:find('16 colors',1,true))
assert(#p==size,'A rejected resize must roll back')
ok,err=pcall(function() p:resize(16) end)
assert(not ok and type(err)=='string' and err:find('every assigned',1,true))
assert(#p==size)
-- RGB -> Document Palette must use bank zero and leave every bank untouched.
before=Palette(p)
a.paletteBank=-2
assert(a:cel(1).image.colorMode==ColorMode.RGB)
a.paletteBank=-1
assert(p==before and #p==size)
for pixel in a:cel(1).image:pixels() do assert(pixel()<16) end
app.undo(); assert(a.paletteBank==-2)
app.redo(); assert(a.paletteBank==-1)
-- Lifecycle state survives save/reopen even if a bank no longer has any users.
local initialized=s.properties(ns).initialized
s:saveAs(base..'.aseprite')
local loaded=app.open(base..'.aseprite')
assert(loaded.properties(ns).initialized==initialized)
loaded:close(); s:close()
-- An ordinary/non-KARN file with a large palette still starts with NEW banks.
local ordinary=Sprite(8,8,ColorMode.INDEXED)
ordinary.palettes[1]:setColor(3,Color(34,51,68))
ordinary.palettes[1]:setColor(51,Color(255,0,0))
ordinary:saveAs(base..'-ordinary.aseprite'); ordinary:close()
ordinary=app.open(base..'-ordinary.aseprite')
assert(ordinary.properties(ns).initialized==nil)
ordinary.layers[1].paletteBank=3
assert(ordinary.palettes[1]:getColor(51)==Color(34,51,68))
ordinary:close()
-- Old KARN files predate the flag: keep their currently assigned bank colors.
local legacy=Sprite(8,8,ColorMode.INDEXED)
legacy.layers[1].properties(ns).bank=3
legacy.palettes[1]:setColor(51,Color(255,0,0))
local extra=legacy:newLayer(); extra.paletteBank=3
assert(legacy.palettes[1]:getColor(51)==Color(255,0,0))
assert(legacy.properties(ns).initialized==8)
legacy:deleteLayer(legacy.layers[1]); legacy:deleteLayer(extra)
local replacement=legacy:newLayer(); replacement.paletteBank=3
assert(legacy.palettes[1]:getColor(51)==Color(255,0,0),'Deletion must not reset bank history')
legacy:close()
os.remove(bmp)
print('Palette bank workflows: import isolation, inheritance, reuse, undo, sizing, RGB and persistence OK')
