-- The standalone BMP step and collage step share destinations and source bytes.
local E=dofile(app.params.exporter or '../data/extensions/karn-palette-banks/export.lua')
local root=E.absolute(app.params.output or '_test_karn_shared_exports')
assert(app.fs.makeAllDirectories(root..'/src'))
local function write(path,data)
  local f=assert(io.open(path,'wb')); assert(f:write(data)); assert(f:close())
end
local function read(path)
  local f=assert(io.open(path,'rb')); local v=f:read('*a'); f:close(); return v
end
write(root..'/src/constants.p8','; isolated shared export fixture')
local s=Sprite(24,8,ColorMode.INDEXED)
local pal=s.palettes[1]; pal:resize(256)
for i=0,255 do pal:setColor(i,Color(i%16*17,i//16*17,0)) end
pal:setColor(38,pal:getColor(37))
local bottom=s.layers[1]; bottom.name='Ground'; bottom.paletteBank=2
local base=Image(24,8,ColorMode.INDEXED); base:clear(5)
s:newCel(bottom,1,base)
local other=s:newLayer(); other.name='Other'; other.paletteBank=3
local im=Image(base); im:clear(0); im:putPixel(8,0,7); s:newCel(other,1,im)
local top=s:newLayer(); top.name='Detail'; top.paletteBank=2
local detail=Image(im); detail:clear(0); detail:putPixel(0,0,6); s:newCel(top,1,detail)
assert(not E.collageBmps(s,1),'First export needs the shared BMP window')
local settings={folder=root..'/Art/BMPs',baseName='Scene',mergeByBank=false,
  selected={},filenames={},bankFilenames={['2']='terrain.bmp',['3']='other.bmp'}}
for i,l in ipairs(s.layers) do
  settings.selected[tostring(l.uuid)]=true
  settings.filenames[tostring(l.uuid)]='layer'..i..'.bmp'
end
local separate=E.bmpFromSettings(s,1,settings); E.write(separate); E.remember(separate)
assert(not E.collageBmps(s,1),'Same-bank sources need merged BMPs for collage reuse')
settings.mergeByBank=true
local function exportBmps()
  local p=E.bmpFromSettings(s,1,settings); E.write(p); E.remember(p); return p
end
local bmpPlan=exportBmps()
local current,reason=E.collageBmps(s,1)
assert(#bmpPlan.files==2 and current,'Manual BMP export can skip step 1: '..tostring(reason))
local tilekits=root..'/Project/TileKits'
local function collagePlan(new)
  local current=assert(E.collageBmps(s,1))
  local groups=E.collageGroups(s,current,tilekits,new)
  return E.prepare(s,{mode='collage',frame=1,output=tilekits..(new and '/NewScene.json' or '/Scene.json'),
    workspace=root,tilekits=tilekits,bmpPlan=current,rows=E.collageRows(groups),new=new})
end
local plan=collagePlan(false)
assert(#plan.files==3 and #plan.inputs==2 and #plan.rows==3)
assert(plan.rows[1].kit==plan.rows[3].kit,'Shared BMP has one shared TileKit')
for _,f in ipairs(plan.files) do assert(app.fs.fileExtension(f.path)=='json','Collage stage must not write BMPs') end
local rename=os.rename
os.rename=function(from,to)
  assert(not to:lower():match('%.bmp$'),'Current BMPs must not be rewritten')
  return rename(from,to)
end
E.write(plan); E.remember(plan)
os.rename=rename
local terrain=E.readJson(plan.rows[1].kit)
assert(terrain.ImagePath=='Art/BMPs/terrain.bmp','BMP and TileKit folders are independent')
terrain.Brushes={{Id='keep',Name='Keep',Width=1,Height=1,Tiles={{X=0,Y=0}}}}
write(plan.rows[1].kit,json.encode(terrain))
local updated=collagePlan(false); E.write(updated); E.remember(updated)
assert(E.readJson(updated.rows[1].kit).Brushes[1].Id=='keep')
assert(updated.rows[1].kitId==plan.rows[1].kitId)
-- Clean sources are not re-exported, including after rename and native save/load.
top.name='Renamed'; assert(E.collageBmps(s,1))
s:saveAs(root..'/source.aseprite')
local opened=app.open(root..'/source.aseprite')
assert(E.collageBmps(opened,1),'BMP mappings survive reopening the document')
opened:close(); app.sprite=s
-- Detect pixel/palette/visibility/bank edits and external file deletion/modification.
local changed=Image(top:cel(1).image); changed:putPixel(0,0,8); top:cel(1).image=changed
assert(not E.collageBmps(s,1)); app.undo(); assert(E.collageBmps(s,1))
local old=pal:getColor(37); pal:setColor(37,Color(255,0,0)); assert(not E.collageBmps(s,1)); pal:setColor(37,old)
top.isVisible=false; assert(not E.collageBmps(s,1)); top.isVisible=true
top.paletteBank=4; assert(not E.collageBmps(s,1)); top.paletteBank=2
local path=bmpPlan.files[1].path
local bytes=read(path)
assert(os.remove(path)); assert(not E.collageBmps(s,1)); write(path,bytes)
write(path,bytes..'external'); assert(not E.collageBmps(s,1)); write(path,bytes)
assert(E.collageBmps(s,1))
-- Detect changes after planning as well: leave all JSON files untouched.
local stale=collagePlan(false)
local jsonBefore=read(stale.output)
write(path,bytes..'changed after planning')
assert(not pcall(E.write,stale)); assert(read(stale.output)==jsonBefore)
write(path,bytes)
-- Export As New can reuse current BMPs while requiring fresh JSON resources.
local newGroups=E.collageGroups(s,bmpPlan,tilekits,true)
for _,g in ipairs(newGroups) do os.remove(g.kit) end
os.remove(tilekits..'/NewScene.json')
local fresh=collagePlan(true); E.write(fresh)
assert(fresh.collageId~=plan.collageId and fresh.rows[1].kitId~=plan.rows[1].kitId)
assert(read(path)==bytes)
-- Remove only the extra fixture JSONs so the native loader sees one collage.
for _,f in ipairs(fresh.files) do assert(os.remove(f.path)) end
-- Selection changes require missing visible layers, but irrelevant hidden banks do not leak.
settings.selected[tostring(other.uuid)]=false
exportBmps(); assert(not E.collageBmps(s,1))
settings.selected[tostring(other.uuid)]=true
exportBmps()
local final=collagePlan(false); E.write(final); E.remember(final)
-- Drive the real collage window's routing/callbacks with a headless dialog.
-- This checks that current sources skip BMP setup, stale sources invoke it,
-- and cancelling that first step never opens or writes the collage details.
local shown,prompted,alerts=0,0,{}
local function dialog()
  local d={data={},buttons={}}
  for _,method in ipairs{'label','separator','samerow','newrow','close'} do d[method]=function(self) return self end end
  function d:file(o) self.data[o.id]=o.filename; return self end
  function d:button(o) self.buttons[o.text]=o.onclick; return self end
  function d:show()
    shown=shown+1
    self.buttons['Update Existing']()
    return self
  end
  return d
end
local proxy={sprite=s,frame={frameNumber=1},fs=app.fs,alert=function(a) alerts[#alerts+1]=a end}
local env=setmetatable({Dialog=dialog,app=proxy},{__index=_G})
local window=assert(loadfile(app.fs.joinPath(app.fs.filePath(app.params.exporter or
  '../data/extensions/karn-palette-banks/export.lua'),'collage_window.lua'),'t',env))()
local bmpWindow={show=function() prompted=prompted+1; return exportBmps() end}
local plugin={preferences={}}
window.show(plugin,E,'karn.exports.v1','karn.palette-banks.v1',bmpWindow)
assert(shown==1 and prompted==0 and alerts[#alerts].text:find('Exported TileKits'))
write(path,bytes..'stale')
window.show(plugin,E,'karn.exports.v1','karn.palette-banks.v1',bmpWindow)
assert(shown==2 and prompted==1 and alerts[#alerts].text:find('Exported TileKits'))
write(path,bytes..'cancelled')
window.show(plugin,E,'karn.exports.v1','karn.palette-banks.v1',
  {show=function() prompted=prompted+1; return nil end})
assert(shown==2 and prompted==2)
write(path,bytes)
write(root..'/manifest.json',json.encode{workspace=root,tilekits=tilekits,
  checks={{cell=0,pixels={6,5,5,5},bank=2},{cell=1,pixels={7,0,0,0},bank=3},
    {cell=2,pixels={5,5,5,5},bank=2}}})
s:close()
print('Shared BMP/collage workflow, freshness, independent folders, persistence, source guards and updates: PASS')
