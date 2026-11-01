-- Bank filtering, optional collage, and one metadata TileKit per source BMP.
local E=dofile(app.params.exporter or '../data/extensions/karn-palette-banks/export.lua')
local root=E.absolute(app.params.output or '_test_karn_optional_collage')
assert(app.fs.makeAllDirectories(root..'/src'))
local function write(path,data)
  local f=assert(io.open(path,'wb')); assert(f:write(data)); assert(f:close())
end
local function read(path)
  local f=assert(io.open(path,'rb')); local data=f:read('*a'); f:close(); return data
end
write(root..'/src/constants.p8','; isolated optional collage fixture')
local s=Sprite(16,8,ColorMode.INDEXED)
local pal=s.palettes[1]; pal:resize(256)
for i=0,255 do pal:setColor(i,Color(i%16*17,i//16*17,0)) end
local unbanked=s.layers[1]; unbanked.name='Ignore unbanked artwork'
local invalid=Image(16,8,ColorMode.INDEXED); invalid:clear(200)
s:newCel(unbanked,1,invalid)
assert(#E.layers(s,false)==0 and not E.canExportCollage(s))
local bottom=s:newLayer(); bottom.name='Ground'; bottom.paletteBank=2
local base=Image(16,8,ColorMode.INDEXED); base:clear(5); s:newCel(bottom,1,base)
assert(not E.canExportCollage(s),'One banked layer cannot enable collage')
local top=s:newLayer(); top.name='Detail'; top.paletteBank=2
local detail=Image(base); detail:clear(0); detail:putPixel(0,0,6); s:newCel(top,1,detail)
assert(not E.canExportCollage(s),'Two layers in one bank cannot enable collage')
local other=s:newLayer(); other.name='Other'; other.paletteBank=3
local im=Image(detail); im:clear(0); im:putPixel(8,0,7); s:newCel(other,1,im)
assert(E.canExportCollage(s))
other.isVisible=false; assert(not E.canExportCollage(s)); other.isVisible=true
local settings={folder=root..'/BMPs',baseName='Scene',mergeByBank=false,
  selected={},filenames={},bankFilenames={['2']='terrain.bmp',['3']='other.bmp'}}
for i,l in ipairs(s.layers) do
  settings.selected[tostring(l.uuid)]=true
  settings.filenames[tostring(l.uuid)]='layer'..i..'.bmp'
end
local p=E.bmpFromSettings(s,1,settings); E.write(p); E.remember(p)
assert(#p.rows==3 and #p.files==3 and #p.warnings==0)
assert(#E.defaults(s,'bmp',root..'/Default.bmp')==3)
-- Even explicitly supplied unbanked rows are ignored before pixels/paths are inspected.
local rows={{layer=unbanked},{layer=bottom,bmp=root..'/explicit.bmp'}}
local explicit=E.prepare(s,{mode='bmp',frame=1,output=root..'/ignored.bmp',rows=rows})
assert(#explicit.rows==1 and explicit.rows[1].layer==bottom)
local function prepare(bmps,collage)
  local groups=E.collageGroups(s,bmps,root..'/TileKits',false)
  local rows=E.collageRows(groups); rows[#rows+1]={layer=unbanked}
  return E.prepare(s,{mode='collage',frame=1,output=collage and root..'/TileKits/Scene.json' or '',
    workspace=root,tilekits=root..'/TileKits',bmpPlan=bmps,rows=rows,exportCollage=collage})
end
local unmerged=prepare(p,true); E.write(unmerged); E.remember(unmerged)
assert(#unmerged.files==4 and #unmerged.inputs==3)
local scene=E.readJson(unmerged.output)
assert(scene.Collage.Cells[1].TileKitId==unmerged.rows[2].kitId)
local topBmp=Image{fromFile=unmerged.rows[2].bmp}
assert(topBmp:getPixel(0,0)==6 and topBmp:getPixel(1,0)==0,'Same-bank layers remain separate unless explicitly merged')
assert(scene.Collage.Cells[2].TileKitId==unmerged.rows[3].kitId)
local sceneBytes=read(unmerged.output)
local onlyKits=prepare(p,false); E.write(onlyKits); E.remember(onlyKits)
assert(#onlyKits.files==3 and onlyKits.output==nil and onlyKits.collageId==nil)
assert(read(unmerged.output)==sceneBytes)
for _,f in ipairs(onlyKits.files) do
  local kit=E.readJson(f.path)
  assert(kit.Kind==0 and kit.Collage==nil and kit.ImagePath:find('BMPs/'))
end
-- Explicit merging produces two BMPs, hence two metadata TileKits.
settings.mergeByBank=true
p=E.bmpFromSettings(s,1,settings); E.write(p); E.remember(p)
local merged=prepare(p,true); E.write(merged); E.remember(merged)
assert(#p.files==2 and #merged.files==3 and #merged.inputs==2)
assert(merged.rows[1].kitId==merged.rows[2].kitId)
local combined=Image{fromFile=merged.rows[1].bmp}
assert(combined:getPixel(0,0)==6 and combined:getPixel(1,0)==5)
-- An additional selected hidden BMP still gets a TileKit, but no collage cells.
local hidden=s:newLayer(); hidden.paletteBank=4; hidden.isVisible=false
s:newCel(hidden,1,base)
settings.selected[tostring(hidden.uuid)]=true
settings.bankFilenames['4']='hidden.bmp'
p=E.bmpFromSettings(s,1,settings); E.write(p); E.remember(p)
local hiddenPlan=prepare(p,true); E.write(hiddenPlan)
assert(#p.files==3 and #hiddenPlan.files==4)
for _,c in ipairs(E.readJson(hiddenPlan.output).Collage.Cells) do
  assert(c.TileKitId~=hiddenPlan.rows[4].kitId)
end
s:close()
-- TileKit-only export has no collage canvas-size/path restrictions.
local large=Sprite(513,8,ColorMode.INDEXED); large.layers[1].paletteBank=0
local bp=E.prepare(large,{mode='bmp',frame=1,output=root..'/Large.bmp'}); E.write(bp)
local groups=E.collageGroups(large,bp,root..'/LargeKits',false)
local opts={mode='collage',frame=1,workspace=root,tilekits=root..'/LargeKits',
  bmpPlan=bp,rows=E.collageRows(groups),exportCollage=false}
local largePlan=E.prepare(large,opts); E.write(largePlan)
assert(#largePlan.files==1 and E.readJson(largePlan.files[1].path).Kind==0)
opts.exportCollage=true; opts.output=root..'/LargeKits/Scene.json'
assert(not pcall(E.prepare,large,opts))
large:close()
print('Bank filtering, optional collage, explicit merging and one TileKit per BMP: PASS')
