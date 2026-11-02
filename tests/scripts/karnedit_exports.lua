-- End-to-end authoring contract. Fixtures are isolated from real game assets.
local exporterPath=app.params.exporter or '../data/extensions/karn-palette-banks/export.lua'
local E=dofile(exporterPath)
local hash=dofile(app.fs.joinPath(app.fs.filePath(exporterPath),'sha256.lua'))
assert(hash('abc')=='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
assert(hash('')=='e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855')
local root=E.absolute(app.params.output or '_test_karnedit_exports')
assert(app.fs.makeAllDirectories(root..'/src'))
local function write(path,data)
  assert(app.fs.makeAllDirectories(app.fs.filePath(path)))
  local f=assert(io.open(path,'wb')); assert(f:write(data)); assert(f:close())
end
local function read(path)
  local f=assert(io.open(path,'rb')); local data=f:read('*a'); f:close(); return data
end
write(root..'/src/constants.p8','; isolated exporter fixture')
local s=Sprite(24,16,ColorMode.INDEXED)
-- These pre-authored banks already have their own colors.
s.properties('karn.palette-banks.v1').initialized=0xffff
local p=s.palettes[1]; p:resize(256)
for i=0,255 do p:setColor(i,Color((i%16)*17,((i//16)%16)*17,0)) end
p:setColor(54,Color(85,51,0)) -- duplicate RGB, different local slots 5 and 6
s.gridBounds=Rectangle(0,0,8,8)
local bottom=s.layers[1]; bottom.name='Ground'; bottom.paletteBank=1
local im=Image(24,16,ColorMode.INDEXED); im:clear(3)
s:newCel(bottom,1,im)
app.command.NewLayer{tilemap=true,name='Trees'}
local top=app.layer; top.paletteBank=3
local function paint(layer,x,y,index)
  app.useTool{tool='pencil',ink=Ink.SIMPLE,color=Color{index=index},bgColor=Color{index=0},
    brush=Brush(1),layer=layer,tilesetMode=TilesetMode.AUTO,points={Point(x,y)}}
end
paint(top,0,0,5); paint(top,1,0,6)
paint(top,8,0,1) -- preserved, warned, still counts as a nonempty upper tile
local hidden=s:newLayer(); hidden.name='Hidden'; hidden.paletteBank=4; hidden.isVisible=false
s:newCel(hidden,1,Image(im))
local options={mode='collage',frame=1,workspace=root,tilekits=root..'/TileKits',output=root..'/TileKits/Scene.json'}
local plan=E.prepare(s,options)
assert(#plan.rows==2 and #plan.files==5)
assert(#plan.warnings==1 and plan.warnings[1]:find('indices 1/2'),
  json.encode(plan.warnings)..' pixel='..top:renderLocal(1):getPixel(8,0))
E.write(plan); E.remember(plan)
local collage=E.readJson(options.output)
assert(collage.Kind==1 and collage.Collage.Width==16 and collage.Collage.Height==16)
assert(#collage.Collage.Cells==6)
local ground=E.readJson(plan.rows[1].kit)
local trees=E.readJson(plan.rows[2].kit)
assert(collage.Collage.Cells[1].TileKitId==trees.Id)
assert(collage.Collage.Cells[2].TileKitId==trees.Id)
assert(collage.Collage.Cells[3].TileKitId==ground.Id,'empty upper tile must reveal lower tile')
assert(collage.Collage.Cells[4].TileKitId==ground.Id)
local bmp=app.open(plan.rows[2].bmp)
assert(bmp.colorMode==ColorMode.INDEXED and #bmp.palettes[1]==16)
assert(bmp.cels[1].image:getPixel(0,0)==5 and bmp.cels[1].image:getPixel(1,0)==6)
assert(bmp.cels[1].image:getPixel(2,0)==0,'transparent holes stay empty; lower pixels are not baked')
assert(bmp.cels[1].image:getPixel(8,0)==1)
assert(bmp.palettes[1]:getColor(5)==bmp.palettes[1]:getColor(6))
bmp:close(); app.sprite=s
local raw=read(plan.rows[2].bmp)
assert(string.unpack('<I2',raw,29)==4 and string.unpack('<I4',raw,31)==0)
assert(string.unpack('<I4',raw,3)==#raw)
assert(ground.ImagePath:sub(1,9)=='TileKits/')
assert(#ground.SourceFileId==32)
-- Update retains external IDs, brush definitions, and extra owner metadata.
write(plan.rows[1].kit,json.encode{Id=ground.Id,Kind=0,TileW=8,TileH=8,ImagePath=ground.ImagePath,
  SourceFileId=ground.SourceFileId,Brushes={{Id='brush-one',Name='Keep',Width=1,Height=1,Tiles={{X=0,Y=0}}}},Extra='Keep'})
paint(bottom,23,15,7)
local updated=E.prepare(s,options)
E.write(updated); E.remember(updated)
local kept=E.readJson(updated.rows[1].kit)
assert(kept.Id==ground.Id and kept.SourceFileId==ground.SourceFileId)
assert(kept.Brushes[1].Id=='brush-one' and kept.Extra=='Keep')
assert(E.readJson(options.output).Id==collage.Id)
-- Missing files get fresh IDs, even when previous IDs were remembered.
assert(os.remove(updated.rows[2].kit)); assert(os.remove(options.output))
local recovered=E.prepare(s,{mode='collage',frame=1,workspace=root,tilekits=options.tilekits,
  output=options.output,rows=updated.rows,collageId=collage.Id})
E.write(recovered); E.remember(recovered)
assert(E.readJson(updated.rows[2].kit).Id~=trees.Id)
assert(E.readJson(options.output).Id~=collage.Id)
assert(E.readJson(updated.rows[1].kit).Id==ground.Id)
assert(E.readJson(updated.rows[1].kit).Brushes[1].Id=='brush-one')
assert(E.readJson(options.output).Collage.Cells[1].TileKitId==recovered.rows[2].kitId)
-- Persistence and rename do not change export paths.
s:saveAs(root..'/source.aseprite')
local loaded=app.open(root..'/source.aseprite')
assert(loaded.layers[1].paletteBank==1 and loaded.layers[2].paletteBank==3)
local remembered=json.decode(loaded.layers[1].properties('karn.exports.v1').collage)
assert(remembered.bmp==updated.rows[1].bmp and remembered.owner==tostring(loaded.layers[1].uuid))
loaded.layers[1].name='Renamed'
assert(json.decode(loaded.layers[1].properties('karn.exports.v1').collage).bmp==remembered.bmp)
loaded:close(); app.sprite=s
-- Current layer-local extraction must ignore hidden state and preserve offsets/flips.
local idx=app.pixelColor.tileI(top:cel(1).image:getPixel(0,0))
local map=Image(top:cel(1).image); map:putPixel(0,0,app.pixelColor.tile(idx,0xc0000000))
top:cel(1).image=map
local localImage=top:renderLocal(1)
assert(localImage:getPixel(7,7)==5 and localImage:getPixel(6,7)==6)
app.undo()
-- Ordinary layers: exact local painting, global composition, undo, round-trip conversion.
paint(bottom,0,0,6)
assert(bottom:cel(1).image:getPixel(0,0)==6)
app.undo(); assert(bottom:cel(1).image:getPixel(0,0)==3)
local normal=bottom:renderLocal(1); assert(normal:getPixel(23,15)==7)
app.layer=bottom; app.command.ConvertLayer{to='tilemap',ui=false}
assert(app.layer.paletteBank==1 and app.layer:renderLocal(1):getPixel(23,15)==7)
app.command.ConvertLayer{to='layer'}
bottom=app.layer
assert(bottom.paletteBank==1 and bottom:cel(1).image:getPixel(23,15)==7)
-- All-layer BMP export includes hidden layers; collisions get separate names.
hidden.paletteBank=1
local all=E.prepare(s,{mode='bmp',frame=1,output=root..'/all.bmp'})
assert(#all.rows==3 and all.rows[1].bmp~=all.rows[3].bmp)
E.write(all)
-- Visible descendants of hidden groups must not appear in collages.
local group=s:newGroup(); hidden.parent=group; hidden.isVisible=true; group.isVisible=false
assert(#E.layers(s,true)==2)
-- An invalid source or failed preparation must not touch previous outputs.
local before=read(options.output)
local bad=Image(bottom:cel(1).image); bad:putPixel(0,0,20); bottom:cel(1).image=bad
assert(not pcall(E.prepare,s,options)); assert(read(options.output)==before)
app.undo()
options.new=true
assert(not pcall(E.prepare,s,options)); options.new=false
local conflicting=E.prepare(s,options)
local untouched=read(conflicting.rows[1].bmp)
write(options.output,before..' ')
assert(not pcall(E.write,conflicting))
assert(read(conflicting.rows[1].bmp)==untouched)
write(options.output,before)
-- A failed second installation restores the first output and both backups.
paint(bottom,2,0,7); paint(top,2,0,7)
local rollback=E.prepare(s,options)
local rename,installs=os.rename,0
os.rename=function(from,to)
  if from:match('%.tmp$') then
    installs=installs+1
    if installs==2 then return nil,'Injected installation failure' end
  end
  return rename(from,to)
end
local installed=pcall(E.write,rollback)
os.rename=rename
assert(not installed and installs==2)
for _,f in ipairs(rollback.files) do assert(read(f.path)==f.original,'Previous output not restored: '..f.path) end
app.undo(); app.undo()
-- Large and rectangular documents are padded or rejected, never cropped.
local large=Sprite(513,8,ColorMode.INDEXED); large.layers[1].paletteBank=0
assert(not pcall(E.prepare,large,options)); large:close(); app.sprite=s
local tiny=Sprite(9,3,ColorMode.INDEXED); tiny.layers[1].paletteBank=0
local tinyPlan=E.prepare(tiny,{mode='bmp',frame=1,output=root..'/tiny.bmp'})
E.write(tinyPlan)
local tinyData=read(tinyPlan.rows[1].bmp)
assert(string.unpack('<i4',tinyData,19)==16 and string.unpack('<i4',tinyData,23)==8)
tiny:close(); app.sprite=s
-- Leave clean deterministic fixture files for KarnEdit's real C# loader checks.
local final=E.prepare(s,options); E.write(final)
write(root..'/manifest.json',json.encode{collage=options.output,ground=final.rows[1].bmp,
  trees=final.rows[2].bmp,workspace=root,tilekits=options.tilekits,
  identityCases={{path='Art/Trees.bmp',id=E.sourceId('Art/Trees.bmp')},
    {path='Art/étoile.bmp',id=E.sourceId('Art/étoile.bmp')}}})
print('KarnEdit BMP/collage exports, identities, mappings, updates, visibility, and local indices: PASS')
