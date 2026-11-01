-- Unmerged collages use the topmost nonempty tile, even within the same bank.
local E=dofile(app.params.exporter or '../data/extensions/karn-palette-banks/export.lua')
local root=E.absolute(app.params.output or '_test_karn_collage_merge')
assert(app.fs.makeAllDirectories(root..'/src'))
local function write(path,data)
  local f=assert(io.open(path,'wb')); assert(f:write(data)); assert(f:close())
end
write(root..'/src/constants.p8','; isolated collage merge fixture')
local s=Sprite(25,9,ColorMode.INDEXED)
local palette=s.palettes[1]; palette:resize(256)
for i=0,255 do palette:setColor(i,Color((i%16)*17,(i//16)*17,0)) end
palette:setColor(38,palette:getColor(37)) -- bank 2 slots 5 and 6 have equal RGB
s.gridBounds=Rectangle(0,0,8,8)
local bottom=s.layers[1]; bottom.name='Ground'; bottom.paletteBank=2
local base=Image(25,9,ColorMode.INDEXED); base:clear(5); base:putPixel(3,0,0)
s:newCel(bottom,1,base)
local middle=s:newLayer(); middle.name='Details'; middle.paletteBank=2
local detail=Image(25,9,ColorMode.INDEXED); detail:clear(0); detail:putPixel(1,0,6)
s:newCel(middle,1,detail)
local other=s:newLayer(); other.name='Different bank'; other.paletteBank=3
local different=Image(25,9,ColorMode.INDEXED); different:clear(0)
different:putPixel(2,0,9); different:putPixel(3,0,9); different:putPixel(8,0,10)
s:newCel(other,1,different)
app.command.NewLayer{tilemap=true,name='Top'}
local top=app.layer; top.paletteBank=2
local function paint(layer,x,y,index)
  app.useTool{tool='pencil',ink=Ink.SIMPLE,color=Color{index=index},bgColor=Color{index=0},
    brush=Brush(1),layer=layer,tilesetMode=TilesetMode.AUTO,points={Point(x,y)}}
end
paint(top,0,0,7); paint(top,24,0,8)
local group=s:newGroup(); group.isVisible=false
local hidden=s:newLayer(); hidden.parent=group; hidden.paletteBank=2
local hiddenImage=Image(base); hiddenImage:clear(15); s:newCel(hidden,1,hiddenImage)
local options={mode='collage',frame=1,workspace=root,tilekits=root..'/TileKits',output=root..'/TileKits/Scene.json'}
-- Reverse mappings deliberately: composition must use document layer order.
local rows=E.defaults(s,'collage',options.output)
options.rows={rows[4],rows[3],rows[2],rows[1]}
local plan=E.prepare(s,options); E.write(plan); E.remember(plan)
assert(#plan.rows==4 and #plan.files==9 and #plan.warnings==0)
local collage=E.readJson(options.output)
local function cell(x,y)
  for _,c in ipairs(collage.Collage.Cells) do if c.X==x and c.Y==y then return c end end
  error('Missing cell')
end
assert(cell(0,0).TileKitId==plan.rows[4].kitId)
assert(cell(1,0).TileKitId==plan.rows[3].kitId,'Empty top tile must not steal the bank')
assert(cell(2,0).TileKitId==plan.rows[1].kitId)
local topBmp=Image{fromFile=plan.rows[4].bmp}
assert(topBmp:getPixel(0,0)==7 and topBmp:getPixel(1,0)==0 and topBmp:getPixel(2,0)==0,
  'Unmerged sources must not bake lower same-bank pixels into upper tiles')
assert(topBmp:getPixel(3,0)==0,'Different banks cannot fill transparent holes')
assert(topBmp:getPixel(8,0)==0 and topBmp:getPixel(16,0)==0,'Originally empty upper cells stay empty')
assert(topBmp.width==32 and topBmp.height==16 and topBmp:getPixel(24,0)==8 and topBmp:getPixel(25,0)==0)
local otherBmp=Image{fromFile=plan.rows[3].bmp}
assert(otherBmp:getPixel(8,0)==10 and otherBmp:getPixel(9,0)==0,'Different-bank tile keeps its holes')
assert(top:renderLocal(1):getPixel(1,0)==0 and middle:renderLocal(1):getPixel(2,0)==0,
  'Export must not change source artwork')
-- Removing upper pixels changes ownership, without leaving baked pixels behind.
paint(top,0,0,0)
local changed=E.prepare(s,options); E.write(changed)
local cleared=Image{fromFile=changed.rows[4].bmp}
assert(cleared:getPixel(0,0)==0 and cleared:getPixel(1,0)==0)
local newCollage=E.readJson(options.output)
assert(newCollage.Collage.Cells[1].TileKitId==plan.rows[3].kitId,'Different bank below now wins the cell')
assert(newCollage.Id==collage.Id and changed.rows[4].kitId==plan.rows[4].kitId,'Resource IDs stay stable')
app.undo()
-- Rebuild the fixture for KarnEdit's actual collage resolver.
local final=E.prepare(s,options); E.write(final)
write(root..'/manifest.json',json.encode{workspace=root,tilekits=options.tilekits,
  checks={{cell=0,pixels={7,0,0,0},bank=2},{cell=1,pixels={10,0,0,0},bank=3},
    {cell=2,pixels={5,5,5,5},bank=2},{cell=3,pixels={8,0,0,0},bank=2},
    {cell=16,pixels={5,5,5,5},bank=2}}})
s:close()
print('Unmerged collage topmost tiles, bank isolation, empty tiles, padding and stable updates: PASS')
