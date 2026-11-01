-- Full color Reference is a bank assignment, not a native reference layer.
-- Compare real RGB tools against the same tools on an indexed document's RGB bank.
local pc=app.pixelColor
local base=app.params.output or '_test_full_color_reference'
app.fs.makeAllDirectories(base)
local function pixels(s,l)
  local im=Image(s.width,s.height,ColorMode.RGB)
  local c=l:cel(1)
  if c then im:drawImage(c.image,c.position) end
  return im.bytes
end
local function make(indexed)
  local s=Sprite(16,16,indexed and ColorMode.INDEXED or ColorMode.RGB)
  local p=s.palettes[1]; p:resize(32)
  for i=0,31 do p:setColor(i,Color((i%16)*17,(i//16)*119,0)) end
  local layer=s.layers[1]
  if indexed then
    layer.paletteBank=-2
    assert(layer.paletteBank==-2 and not layer.isReference and layer.isEditable)
    app.command.ForceKarnRules{enabled=true,ui=false}
  end
  return s,layer
end
local rgb,r=make(false)
local indexed,ref=make(true)
local function both(name,fn)
  app.sprite=rgb; app.layer=r; fn(rgb,r)
  app.sprite=indexed; app.layer=ref; fn(indexed,ref)
  assert(ref:cel(1).image.colorMode==ColorMode.RGB,name..': must remain RGB')
  assert(pixels(rgb,r)==pixels(indexed,ref),name..': must match an ordinary RGB document')
  local a,b=Image(16,16,ColorMode.RGB),Image(16,16,ColorMode.RGB)
  a:drawSprite(rgb,1); b:drawSprite(indexed,1)
  assert(a.bytes==b.bytes,name..': rendering must match RGB')
end
both('pencil',function(s,l)
  app.useTool{tool='pencil',ink=Ink.SIMPLE,color=Color(13,79,201,117),
    brush=Brush(3),points={Point(2,2),Point(12,10)},layer=l}
end)
both('alpha ink',function(s,l)
  app.useTool{tool='pencil',ink=Ink.ALPHA_COMPOSITING,color=Color(232,51,37,201),
    opacity=101,brush=Brush(3),points={Point(2,10),Point(12,2)},layer=l}
end)
both('eraser',function(s,l)
  app.useTool{tool='eraser',opacity=127,brush=Brush(2),points={Point(8,8)},layer=l}
end)
both('bucket',function(s,l)
  app.useTool{tool='paint_bucket',ink=Ink.SIMPLE,color=Color(6,81,178,71),
    tolerance=0,contiguous=true,points={Point(15,15)},layer=l}
end)
both('custom RGB brush',function(s,l)
  local im=Image(2,2,ColorMode.RGB)
  im:putPixel(0,0,pc.rgba(9,49,212,83)); im:putPixel(1,1,pc.rgba(212,71,3,229))
  app.useTool{tool='pencil',ink=Ink.ALPHA_COMPOSITING,brush=Brush(im),
    points={Point(4,4)},layer=l}
end)
both('invert',function() app.command.InvertColor{ui=false} end)
both('flip',function() app.command.Flip{orientation='horizontal',target='mask'} end)
both('copy/paste',function(s,l)
  s.selection:select(Rectangle(1,1,7,7))
  app.command.Copy(); app.command.Paste{x=7,y=7}; s.selection:deselect()
end)
local before=pixels(indexed,ref)
app.undo(); app.redo(); assert(pixels(indexed,ref)==before)
indexed.selection:deselect()
-- Hardware palette and exact local indices are unaffected by RGB operations.
app.sprite=indexed
local art=indexed:newLayer(); art.paletteBank=1
local im=Image(16,16,ColorMode.INDEXED); im:clear(5)
indexed:newCel(art,1,im); local artBytes=art:cel(1).image.bytes
app.layer=ref
local paletteColors={}
for i=0,#indexed.palettes[1]-1 do paletteColors[i]=indexed.palettes[1]:getColor(i).rgbaPixel end
app.useTool{tool='eraser',opacity=255,brush=Brush(1),points={Point(0,0)},layer=ref}
app.useTool{tool='pencil',ink=Ink.SIMPLE,color=Color(12,34,56,78),
  brush=Brush(1),points={Point(0,0)},layer=ref}
local c=ref:cel(1)
local actual=c.image:getPixel(-c.position.x,-c.position.y)
assert(actual==pc.rgba(12,34,56,78),string.format('exact RGBA: got %08x expected %08x',actual,pc.rgba(12,34,56,78)))
assert(art:cel(1).image.bytes==artBytes and art.paletteBank==1)
for i=0,#indexed.palettes[1]-1 do assert(indexed.palettes[1]:getColor(i).rgbaPixel==paletteColors[i]) end
-- Existing indexed pixels expand using their assigned bank; switching back is undoable.
app.layer=art; art.paletteBank=-2
assert(art:cel(1).image:getPixel(0,0)==indexed.palettes[1]:getColor(21).rgbaPixel)
app.undo(); assert(art.paletteBank==1 and art:cel(1).image.bytes==artBytes)
app.redo(); assert(art.paletteBank==-2)
app.undo()
app.layer=ref
local exact=ref:cel(1).image.bytes
ref.paletteBank=1
assert(ref:cel(1).image.colorMode==ColorMode.INDEXED)
for p in ref:cel(1).image:pixels() do assert(p()<16) end
app.undo(); assert(ref.paletteBank==-2 and ref:cel(1).image.bytes==exact)
-- Save/load retains 8-bit RGBA channels, bank assignment, and linked cels.
ref.isContinuous=true; indexed:newFrame(1)
assert(ref:cel(1).image.id==ref:cel(2).image.id)
ref.name='Full color Reference test'
local filename=app.fs.joinPath(base,'mixed.aseprite')
indexed:saveAs(filename)
local loaded=Sprite{fromFile=filename}
local found
for _,l in ipairs(loaded.layers) do if l.paletteBank==-2 then found=l end end
assert(found and not found.isReference)
assert(found:cel(1).image.colorMode==ColorMode.RGB and found:cel(1).image.bytes==exact)
assert(found:cel(1).image.id==found:cel(2).image.id)
assert(loaded.colorMode==ColorMode.INDEXED and loaded.properties('karn.rules.v1').enabled)
local export=dofile('../data/extensions/karn-palette-banks/export.lua')
for _,l in ipairs(export.layers(loaded)) do assert(l.paletteBank>=0) end
loaded:close(); indexed:close(); rgb:close()

-- Import/paste from a separate full-color document, then merge RGB layers.
local source=Sprite(8,8,ColorMode.RGB)
local imported=Image(8,8,ColorMode.RGB)
for y=0,7 do for x=0,7 do imported:putPixel(x,y,pc.rgba(11+x*19,23+y*21,199,37+x*23)) end end
source:newCel(source.layers[1],1,imported)
source.selection:select(Rectangle(0,0,8,8)); app.command.Copy()
local target=Sprite(8,8,ColorMode.INDEXED)
local lower=target.layers[1]; lower.paletteBank=-2
app.command.ForceKarnRules{enabled=true,ui=false}
app.command.Paste{x=0,y=0}; target.selection:deselect()
assert(lower:cel(1).image.bytes==imported.bytes,'RGB imports must not be quantized')
local upper=target:newLayer(); upper.paletteBank=-2
local overlay=Image(8,8,ColorMode.RGB); overlay:clear(pc.rgba(21,79,252,128))
target:newCel(upper,1,overlay)
local rendered=Image(8,8,ColorMode.RGB); rendered:drawSprite(target,1)
app.layer=upper; app.command.MergeDownLayer()
assert(#target.layers==1 and target.layers[1].paletteBank==-2)
local merged=Image(8,8,ColorMode.RGB); merged:drawSprite(target,1)
assert(rendered.bytes==merged.bytes,'Merging RGB banks must preserve full-color compositing')
app.undo(); assert(#target.layers==2)
app.redo(); assert(#target.layers==1 and target.layers[1]:cel(1).image.colorMode==ColorMode.RGB)
target:close(); source:close()
print('Full color Reference: RGB tool parity, exact pixels, bank isolation, undo and persistence OK')
