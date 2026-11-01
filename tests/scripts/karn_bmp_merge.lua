-- Exact indexed BMP selection and same-bank pixel compositing.
local E=dofile(app.params.exporter or '../data/extensions/karn-palette-banks/export.lua')
local root=E.absolute(app.params.output or '_test_karn_bmp_merge')
local s=Sprite(16,8,ColorMode.INDEXED)
local pal=s.palettes[1]; pal:resize(256)
for i=0,255 do pal:setColor(i,Color(17*(i%16),17*(i//16),0)) end
pal:setColor(38,pal:getColor(37)) -- local slots 5 and 6 have identical RGB
local bottom=s.layers[1]; bottom.name='Base'; bottom.paletteBank=2
local full=Image(16,8,ColorMode.INDEXED); full:clear(5)
s:newCel(bottom,1,full)
local other=s:newLayer(); other.name='Other bank'; other.paletteBank=3
local otherImage=Image(16,8,ColorMode.INDEXED); otherImage:clear(0); otherImage:putPixel(1,0,7)
s:newCel(other,1,otherImage)
local top=s:newLayer(); top.name='Highlights'; top.paletteBank=2
local topImage=Image(16,8,ColorMode.INDEXED); topImage:clear(0)
topImage:putPixel(0,0,6); topImage:putPixel(2,0,1)
s:newCel(top,1,topImage)
local unchecked=s:newLayer(); unchecked.name='Unchecked invalid layer'
local invalid=Image(16,8,ColorMode.INDEXED); invalid:clear(200)
s:newCel(unchecked,1,invalid)
local rows={{layer=top,bmp=root..'/highlights.bmp'},
  {layer=other,bmp=root..'/other.bmp'}, {layer=bottom,bmp=root..'/base.bmp'},
  {layer=unchecked,bmp=root..'/unchecked.bmp',selected=false}}
local separate=E.prepare(s,{mode='bmp',frame=1,output=root..'/art.bmp',rows=rows})
assert(#separate.files==3 and #separate.rows==3)
E.write(separate); E.remember(separate)
local originalMapping=top.properties('karn.exports.v1').bmp
local settings={folder=root,baseName='art',mergeByBank=true,selected={},filenames={},bankFilenames={['2']='combined.bmp'}}
settings.selected[tostring(top.uuid)]=true
settings.selected[tostring(unchecked.uuid)]=false
local opts={mode='bmp',frame=1,output=root..'/art.bmp',rows=rows,mergeByBank=true,
  bankOutputs={['2']=root..'/combined.bmp'},bmpSettings=settings}
local merged=E.prepare(s,opts)
assert(#merged.files==2 and #merged.rows==3 and #merged.warnings==1)
E.write(merged); E.remember(merged)
assert(top.properties('karn.exports.v1').bmp==originalMapping,'Merge must not overwrite separate-layer mappings')
local image=Image{fromFile=root..'/combined.bmp'}
assert(image:getPixel(0,0)==6,'Higher layer must win regardless of mapping list order')
assert(image:getPixel(1,0)==5 and image:getPixel(7,0)==5,'Transparent upper pixels reveal lower pixels')
assert(image:getPixel(2,0)==1,'Reserved index must be preserved')
assert(bottom:cel(1).image:getPixel(0,0)==5 and top:cel(1).image:getPixel(1,0)==0,'Source must not change')
local differentBank=Image{fromFile=root..'/art_bank03.bmp'}
assert(differentBank:getPixel(1,0)==7 and differentBank:getPixel(0,0)==0,'Banks must never be mixed')
assert(not app.fs.isFile(root..'/unchecked.bmp'))
-- Selecting only one layer exports only that layer, even with merging enabled.
local selected=E.prepare(s,{mode='bmp',frame=1,output=root..'/only.bmp',mergeByBank=true,
  rows={{layer=top,bmp=root..'/only.bmp'},{layer=bottom,bmp=root..'/ignored.bmp',selected=false}}})
E.write(selected)
assert(#selected.files==1 and #selected.rows==1)
local only=Image{fromFile=root..'/only_bank02.bmp'}
assert(only:getPixel(1,0)==0)
assert(not pcall(E.prepare,s,{mode='bmp',frame=1,output=root..'/empty.bmp',rows={}}))
-- Invalid pixels cannot hide underneath an opaque upper layer during a merge.
local bad=Image(bottom:cel(1).image); bad:putPixel(0,0,20); bottom:cel(1).image=bad
assert(not pcall(E.prepare,s,opts)); app.undo()
top.opacity=128; assert(not pcall(E.prepare,s,opts)); top.opacity=255
opts.bankOutputs['3']=root..'/combined.bmp'
assert(not pcall(E.prepare,s,opts),'A destination shared by two banks must be rejected')
opts.bankOutputs['3']=nil
-- Remembered selection, output settings and identities survive native save/load.
s:saveAs(root..'/source.aseprite')
local opened=app.open(root..'/source.aseprite')
local restored=json.decode(opened.properties('karn.exports.v1').bmp).bmpSettings
assert(restored.folder==root and restored.mergeByBank)
assert(restored.selected[tostring(opened.layers[3].uuid)]==true)
assert(restored.selected[tostring(opened.layers[4].uuid)]==false)
assert(restored.bankFilenames['2']=='combined.bmp')
opened:close()
-- Named banks generate safe, distinct defaults for individual and merged BMPs.
s.properties('karn.palette-banks.v1').name2='Terrain'
s.properties('karn.palette-banks.v1').name3='terrain'
local named=E.bankFilenames(s,'Scene')
assert(named[2]=='Scene_Terrain.bmp' and named[3]=='Scene_terrain_2.bmp')
assert(named[4]=='Scene_bank04.bmp')
local defaults=E.defaults(s,'bmp',root..'/Scene.bmp')
assert(defaults[1].bmp==root..'/Scene_Terrain_Base.bmp')
local namedPlan=E.prepare(s,{mode='bmp',frame=1,output=root..'/Scene.bmp',rows=rows,mergeByBank=true})
assert(namedPlan.files[1].path==root..'/Scene_Terrain.bmp')
assert(namedPlan.files[2].path==root..'/Scene_terrain_2.bmp')
s.properties('karn.palette-banks.v1').name2='Rock/Stone'
s.properties('karn.palette-banks.v1').name3='Rock:Stone'
named=E.bankFilenames(s,'Scene')
assert(named[2]=='Scene_Rock_Stone.bmp' and named[3]=='Scene_Rock_Stone_2.bmp')
s:close()
print('BMP layer selection, exact same-bank merging, output separation and settings persistence: PASS')
