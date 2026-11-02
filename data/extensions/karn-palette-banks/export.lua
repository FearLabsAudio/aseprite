-- KARN authoring exports. KarnEdit owns runtime conversion.
-- dofile() returns the planner/writer used by both the UI and headless tests.
local M = {}
local GROUP, BANKS = 'karn.exports.v1', 'karn.palette-banks.v1'
local directory=app.fs.filePath(debug.getinfo(1,'S').source:sub(2))
local sha256=dofile(app.fs.joinPath(directory,'sha256.lua'))
local function uid() return (tostring(Uuid()):gsub('-', '')) end
local function read(path)
  local f=assert(io.open(path,'rb')); local data=f:read('*a'); f:close(); return data
end
function M.readJson(path)
  if app.fs.isFile(path) then return json.decode(read(path)) end
end
function M.absolute(path)
  assert(path and path~='', 'Choose an output path.')
  path=path:gsub('\\','/')
  if not path:match('^%a:/') and path:sub(1,1)~='/' then
    path=app.fs.currentPath:gsub('\\','/')..'/'..path
  end
  local prefix,rest=path:match('^(%a:/)(.*)$')
  if not prefix then
    if path:sub(1,2)=='//' then prefix,rest='//',path:sub(3)
    else prefix,rest='/',path:sub(2) end
  end
  local parts={}
  for p in rest:gmatch('[^/]+') do
    if p=='..' then assert(#parts>0,'Path escapes its root.'); table.remove(parts)
    elseif p~='.' then parts[#parts+1]=p end
  end
  return prefix..table.concat(parts,'/')
end
function M.within(path,root)
  path,root=M.absolute(path),M.absolute(root):gsub('/$','')
  if path:sub(1,#root+1):lower()==(root..'/'):lower() then return path:sub(#root+2) end
  error('Choose a path inside '..root..': '..path)
end
local function extension(path,ext)
  assert(app.fs.fileExtension(path):lower()==ext,'Expected a .'..ext..' file: '..path)
end
local function palette(s,frame)
  local p=s.palettes[1]
  for i=1,#s.palettes do if s.palettes[i].frame.frameNumber<=frame then p=s.palettes[i] end end
  return p
end
function M.safeName(name)
  local s=name:gsub('[<>:"/\\|?*%c]','_'):gsub('[%. ]+$','')
  return s~='' and s or 'Layer'
end
function M.bankNames(s)
  local names,used={},{}
  for bank=0,15 do
    local name=(s.properties(BANKS)['name'..bank] or ''):match('^%s*(.-)%s*$')
    local base=name~='' and M.safeName(name) or string.format('bank%02d',bank)
    local candidate,n=base,2
    while used[candidate:lower()] do candidate=base..'_'..n; n=n+1 end
    used[candidate:lower()]=true
    names[bank]=candidate
  end
  return names
end
function M.bankFilenames(s,stem)
  local paths={}
  for bank,name in pairs(M.bankNames(s)) do paths[bank]=stem..'_'..name..'.bmp' end
  return paths
end
function M.sourceId(relativePath)
  return sha256('1\n'..app.fs.upperInvariant(relativePath:gsub('\\','/'):gsub('^/+',''))):sub(1,32)
end
-- Lowest layer first; children of hidden groups remain hidden.
function M.layers(s,visibleOnly)
  local out={}
  local function visit(layers,visible)
    for _,l in ipairs(layers) do
      local shown=visible and l.isVisible
      if l.isGroup then visit(l.layers,shown)
      elseif not l.isReference and l.paletteBank>=0 and (not visibleOnly or shown) then out[#out+1]=l end
    end
  end
  visit(s.layers,true)
  return out
end
function M.defaults(s,mode,output)
  local layers=M.layers(s,mode=='collage')
  local counts,used,rows={},{},{}
  for _,l in ipairs(layers) do counts[l.paletteBank]=(counts[l.paletteBank] or 0)+1 end
  local stem=app.fs.filePathAndTitle(output)
  local bankNames=M.bankNames(s)
  for _,l in ipairs(layers) do
    local base=stem..'_'..bankNames[math.max(0,l.paletteBank)]..
      (counts[l.paletteBank]>1 and '_'..M.safeName(l.name) or '')
    local candidate,n=base,2
    while used[candidate:lower()] do candidate=base..'_'..n; n=n+1 end
    used[candidate:lower()]=true
    rows[#rows+1]={layer=l,bmp=candidate..'.bmp',kit=candidate..'.json'}
  end
  return rows
end
-- BI_RGB 4bpp, 16 palette entries, including blank images. No trimming,
-- RGB matching, index remapping, or palette-channel quantization.
local function compositePixels(destination,source)
  -- Zero is transparent; copy slots directly, including equal-RGB slots.
  for pixel in source:pixels() do
    local index=pixel()
    if index~=0 then destination:putPixel(pixel.x,pixel.y,index) end
  end
end
local function bmp(image,pal,bank,width,height,bankPalette)
  local stride=((width+1)//2+3)&~3
  local size=stride*height
  local data={'BM',string.pack('<I4I2I2I4',118+size,0,0,118),
    string.pack('<I4i4i4I2I2I4I4i4i4I4I4',40,width,height,1,4,0,size,0,0,16,16)}
  local rounded=false
  for i=0,15 do
    local at=bank*16+i
    assert(at<#pal,'Palette bank '..bank..' is incomplete.')
    local c=bank==0 and bankPalette:getColor(i) or pal:getColor(at)
    data[#data+1]=string.char(c.blue,c.green,c.red,0)
    if c.red%17~=0 or c.green%17~=0 or c.blue%17~=0 then rounded=true end
  end
  local reserved,nonempty=false,{}
  for y=height-1,0,-1 do
    local row={}
    for x=0,width-1,2 do
      local a=(x<image.width and y<image.height) and image:getPixel(x,y) or 0
      local b=(x+1<image.width and y<image.height) and image:getPixel(x+1,y) or 0
      assert(a>=0 and a<=15 and b>=0 and b<=15,'Layer pixels must use local indices 0-15.')
      if a==1 or a==2 or b==1 or b==2 then reserved=true end
      if a~=0 then nonempty[(y//8)*(width//8)+(x//8)]=true end
      if b~=0 then nonempty[(y//8)*(width//8)+((x+1)//8)]=true end
      row[#row+1]=string.char(a*16+b)
    end
    data[#data+1]=table.concat(row)..string.rep('\0',stride-#row)
  end
  return table.concat(data),reserved,rounded,nonempty
end
function M.prepare(s,options)
  assert(s and s.colorMode==ColorMode.INDEXED,'Use an indexed document.')
  assert(s.transparentColor==0,'Transparent index must be zero.')
  local mode=options.mode or 'bmp'
  assert(mode=='bmp' or mode=='collage','Unknown export mode.')
  local frame=options.frame or app.frame.frameNumber
  assert(frame>=1 and frame<=#s.frames,'Invalid frame.')
  local exportCollage=mode=='collage' and options.exportCollage~=false
  local output
  if mode=='bmp' or exportCollage then
    output=M.absolute(options.output)
    extension(output,mode=='bmp' and 'bmp' or 'json')
  end
  local rows=options.rows or M.defaults(s,mode,output or app.fs.joinPath(options.tilekits,'Artwork.json'))
  assert(#rows>0,'There are no layers to export.')
  local width,height=((s.width+7)//8)*8,((s.height+7)//8)*8
  local workspace,root,side
  if mode=='collage' then
    workspace,root=M.absolute(options.workspace),M.absolute(options.tilekits)
    assert(app.fs.isFile(app.fs.joinPath(workspace,'src','constants.p8')),
      'Choose the KarnEdit Game Workspace containing src/constants.p8.')
    M.within(root,workspace)
    if exportCollage then
      M.within(output,root)
      for _,n in ipairs{16,32,64} do if n*8>=math.max(width,height) then side=n; break end end
      assert(side,'Collages support up to 64 x 64 cells (512 x 512 pixels).')
    end
  end
  local plan={sprite=s,mode=mode,output=output,workspace=workspace,tilekits=root,frame=frame,
    files={},rows={},warnings={},new=options.new==true,mergeByBank=mode=='bmp' and options.mergeByBank==true,
    bmpSettings=options.bmpSettings,exportCollage=exportCollage}
  local shared,sharedKits={},{}
  if options.bmpPlan then
    assert(mode=='collage','Shared BMP sources are only used by collage export.')
    M.validateCollageBmps(s,frame,options.bmpPlan,exportCollage)
    assert(M.isCurrent(options.bmpPlan),'BMP exports are out of date. Open BMP exports to update them.')
    plan.inputs={}
    for _,r in ipairs(options.bmpPlan.rows) do shared[r.layer.id]=r end
  end
  local paths,ids,seenLayers,visible={},{},{},{}
  for _,l in ipairs(M.layers(s,true)) do visible[l.id]=true end
  local function add(path,content)
    path=M.absolute(path)
    assert(not paths[path:lower()],'Two outputs use the same path: '..path)
    paths[path:lower()]=true
    assert(not app.fs.isDirectory(path),'Output is a directory: '..path)
    local old=app.fs.isFile(path) and read(path) or nil
    assert(not plan.new or not old,'Export As New requires unused filenames: '..path)
    plan.files[#plan.files+1]={path=path,content=content,original=old}
  end
  local cells,merged={},{}
  -- Sort mappings by actual layer order, never by filename or dialog row order.
  local order={}; for i,l in ipairs(M.layers(s,false)) do order[l.id]=i end
  local sorted={}; for _,r in ipairs(rows) do if r.selected~=false then sorted[#sorted+1]=r end end
  assert(#sorted>0,'Select at least one layer to export.')
  table.sort(sorted,function(a,b) return (order[a.layer.id] or 0)<(order[b.layer.id] or 0) end)
  for _,row in ipairs(sorted) do
    local l=row.layer
    assert(l and l.sprite==s and not l.isGroup and not l.isReference,'Invalid source layer.')
    assert(not seenLayers[l.id],'A layer appears more than once in the export mapping.')
    seenLayers[l.id]=true
    if l.paletteBank>=0 and (mode~='collage' or options.bmpPlan or visible[l.id]) then
      local cel=l:cel(frame)
      if (exportCollage and visible[l.id]) or plan.mergeByBank then
        assert(not cel or cel.zIndex==0,'Reset cel Z-index on "'..l.name..'"; export uses layer order.')
        local p=l
        while p and p~=s do
          assert(p.opacity==255 and p.blendMode==BlendMode.NORMAL,
            'Merging requires Normal blending and 100% opacity to preserve indices: '..p.name)
          p=p.parent
        end
        assert(not cel or cel.opacity==255,'Use 100% cel opacity on '..l.name)
      end
      local localImage=l:renderLocal(frame)
      local bytes,reserved,rounded,occupied=bmp(localImage,palette(s,frame),l.paletteBank,width,height,
        s:getPaletteBank(l.paletteBank,frame))
      local bmpPath=M.absolute(shared[l.id] and shared[l.id].bmp or row.bmp)
      if plan.mergeByBank then
        local bank=l.paletteBank
        bmpPath=M.absolute((options.bankOutputs or {})[tostring(bank)] or
          M.bankFilenames(s,app.fs.filePathAndTitle(output))[bank])
        if not merged[bank] then
          merged[bank]={path=bmpPath,image=Image(localImage)}
        else
          compositePixels(merged[bank].image,localImage)
        end
      end
      extension(bmpPath,'bmp')
      if not plan.mergeByBank and not options.bmpPlan then add(bmpPath,bytes) end
      if reserved then plan.warnings[#plan.warnings+1]=l.name..': indices 1/2 preserved; KarnEdit imports them as transparent.' end
      if rounded then plan.warnings[#plan.warnings+1]=l.name..': KarnEdit rounds palette RGB channels to 4-bit values.' end
      local mapped={layer=l,bmp=bmpPath}
      if mode=='collage' then
        local kitPath=M.absolute(row.kit)
        extension(kitPath,'json'); M.within(kitPath,root)
        local relative=M.within(bmpPath,workspace)
        local previous=sharedKits[bmpPath:lower()]
        if previous then
          assert(previous.path:lower()==kitPath:lower(),'One shared BMP needs one TileKit destination.')
        end
        local kit=previous and previous.kit or M.readJson(kitPath)
        if kit then
          assert(kit.Kind==0 and kit.TileW==8 and kit.TileH==8 and type(kit.Id)=='string',
            'Choose an 8x8 bitmap TileKit: '..kitPath)
          assert(not row.kitId or row.kitId==kit.Id,'Mapped TileKit identity changed: '..kitPath)
        else kit={Id=uid(),Kind=0,TileW=8,TileH=8,Brushes={}} end
        assert(previous or not ids[kit.Id],'Each BMP needs its own TileKit: '..kitPath); ids[kit.Id]=true
        if not kit.SourceFileId or kit.SourceFileId=='' or
           (kit.ImagePath or ''):gsub('\\','/'):lower()~=relative:lower() then
          kit.SourceFileId=M.sourceId(relative)
        end
        kit.ImagePath=relative
        if not previous then
          add(kitPath,json.encode(kit))
          if options.bmpPlan then
            sharedKits[bmpPath:lower()]={path=kitPath,kit=kit}
            for _,f in ipairs(options.bmpPlan.files) do
              if f.path:lower()==bmpPath:lower() then
                plan.inputs[#plan.inputs+1]={path=f.path,content=f.content}
                break
              end
            end
          end
        end
        mapped.kit,mapped.kitId=kitPath,kit.Id
        if exportCollage and visible[l.id] then
          for cell in pairs(occupied) do
            local x,y=cell%(width//8),cell//(width//8)
            cells[y*side+x]={X=x,Y=y,TileKitId=kit.Id,SourceTileX=x,SourceTileY=y,
              PaletteTileKitId='',PaletteOffset=0,HFlip=false,VFlip=false}
          end
        end
      end
      plan.rows[#plan.rows+1]=mapped
    end
  end
  assert(#plan.rows>0,'There are no palette-bank layers to export.')
  if plan.mergeByBank then
    for bank=0,15 do
      local group=merged[bank]
      if group then add(group.path,(bmp(group.image,palette(s,frame),bank,width,height))) end
    end
  end
  if exportCollage then
    local collage=M.readJson(output)
    if collage then
      assert(collage.Kind==1 and type(collage.Id)=='string','Destination is not a KarnEdit collage.')
      assert(not options.collageId or options.collageId==collage.Id,'Destination collage identity changed.')
    else collage={Id=uid(),Kind=1,SourceFileId='',ImagePath='',TileW=8,TileH=8,Brushes={}} end
    assert(not ids[collage.Id],'Collage and source TileKit share an ID.')
    local ordered={}; for i=0,side*side-1 do if cells[i] then ordered[#ordered+1]=cells[i] end end
    collage.Collage={Width=side,Height=side,Cells=ordered}
    plan.collageId=collage.Id
    add(output,json.encode(collage))
  end
  return plan
end
-- Stage files, install the collage last, restore old outputs on failure.
function M.write(plan)
  local token,staged=uid(),{}
  local function checkInputs()
    for _,f in ipairs(plan.inputs or {}) do
      assert(app.fs.isFile(f.path) and read(f.path)==f.content,
        'A source BMP changed. Open BMP exports to update it: '..f.path)
    end
  end
  local ok,err=pcall(function()
    checkInputs()
    for _,f in ipairs(plan.files) do
      local current=app.fs.isFile(f.path) and read(f.path) or nil
      assert(current==f.original,'Output changed since preview; reopen export: '..f.path)
      if current~=f.content then
        assert(app.fs.makeAllDirectories(app.fs.filePath(f.path)),'Cannot create output directory.')
        f.temp,f.backup=f.path..'.karn-'..token..'.tmp',f.path..'.karn-'..token..'.bak'
        staged[#staged+1]=f
        local handle=assert(io.open(f.temp,'wb'))
        local wrote,why=handle:write(f.content); local closed,closeWhy=handle:close()
        assert(wrote and closed,why or closeWhy)
      end
    end
    for _,f in ipairs(staged) do
      checkInputs()
      assert((app.fs.isFile(f.path) and read(f.path) or nil)==f.original,
        'Output changed during export: '..f.path)
      if f.original then assert(os.rename(f.path,f.backup)); f.saved=true end
      assert(os.rename(f.temp,f.path)); f.installed=true
    end
  end)
  if not ok then
    local recovery={}
    for i=#staged,1,-1 do
      local f=staged[i]
      if f.installed and not os.remove(f.path) then recovery[#recovery+1]=f.path end
      if f.saved and not os.rename(f.backup,f.path) then recovery[#recovery+1]=f.backup end
      os.remove(f.temp)
    end
    error(tostring(err)..(#recovery>0 and '\nRecovery files: '..table.concat(recovery,', ') or ''))
  end
  for _,f in ipairs(staged) do if f.saved then os.remove(f.backup) end end
end
local function decode(value)
  if type(value)=='string' then
    local ok,t=pcall(json.decode,value)
    if ok and (type(t)=='table' or type(t)=='userdata') then return t end
  end
  return {}
end
function M.remember(plan)
  -- Native layer UUIDs are otherwise session-only in ordinary documents.
  plan.sprite.useLayerUuids=true
  app.transaction('Remember KARN export',function()
    local previous=decode(plan.sprite.properties(GROUP)[plan.mode])
    plan.sprite.properties(GROUP)[plan.mode]=json.encode{
      output=plan.output or previous.output,workspace=plan.workspace,tilekits=plan.tilekits,
      collageId=plan.collageId or previous.collageId,
      bmpSettings=plan.bmpSettings,mergeByBank=plan.mergeByBank}
    for _,r in ipairs(plan.rows) do
      if not plan.mergeByBank then
        r.layer.properties(GROUP)[plan.mode]=json.encode{bmp=r.bmp,kit=r.kit,kitId=r.kitId,owner=tostring(r.layer.uuid)}
      end
    end
  end)
end
function M.bmpFromSettings(s,frame,settings,new)
  assert(settings and settings.folder,'Set up the BMP exports first.')
  local folder=M.absolute(settings.folder)
  local function path(name)
    assert(type(name)=='string' and name~='' and name==app.fs.fileName(name) and not name:find('[:/\\]'),
      'Enter a BMP filename without folders.')
    extension(name,'bmp')
    return app.fs.joinPath(folder,name)
  end
  local rows,bankOutputs={},{}
  for _,l in ipairs(M.layers(s,false)) do
    local id=tostring(l.uuid)
    if (settings.selected or {})[id] then
      local name=settings.mergeByBank and (settings.bankFilenames or {})[tostring(l.paletteBank)] or
        (settings.filenames or {})[id]
      local destination=path(name)
      rows[#rows+1]={layer=l,bmp=destination}
      bankOutputs[tostring(l.paletteBank)]=destination
    end
  end
  return M.prepare(s,{mode='bmp',frame=frame,output=app.fs.joinPath(folder,M.safeName(settings.baseName or 'Artwork')..'.bmp'),
    rows=rows,mergeByBank=settings.mergeByBank,bankOutputs=bankOutputs,bmpSettings=settings,new=new})
end
function M.canExportCollage(s)
  local banks={}
  for _,l in ipairs(M.layers(s,true)) do
    banks[l.paletteBank]=true
  end
  local count=0; for _ in pairs(banks) do count=count+1 end
  return count>1
end
function M.validateCollageBmps(s,frame,plan,exportCollage)
  assert(plan.mode=='bmp' and plan.sprite==s and plan.frame==frame,'Export BMPs for the current document and frame.')
  local visible,required,banks={},{},{}
  for _,l in ipairs(M.layers(s,true)) do visible[l.id]=true; required[l.id]=true; banks[l.paletteBank]=true end
  assert(next(required),'There are no visible layers to export.')
  for _,r in ipairs(plan.rows) do
    required[r.layer.id]=nil
    assert(exportCollage==false or not plan.mergeByBank or visible[r.layer.id] or not banks[r.layer.paletteBank],
      'A hidden layer shares a collage bank. Export only visible layers for this collage.')
  end
  assert(not next(required),'Export all visible layers before creating the collage.')
end
function M.isCurrent(plan)
  for _,f in ipairs(plan.files) do
    if not app.fs.isFile(f.path) or read(f.path)~=f.content then return false end
  end
  return true
end
function M.collageBmps(s,frame)
  local ok,plan=pcall(function()
    local saved=decode(s.properties(GROUP).bmp)
    local p=M.bmpFromSettings(s,frame,saved.bmpSettings)
    M.validateCollageBmps(s,frame,p,false)
    return p
  end)
  if not ok then return nil,tostring(plan) end
  if not M.isCurrent(plan) then return nil,'BMP exports are missing or out of date.' end
  return plan
end
function M.collageGroups(s,bmpPlan,tilekits,new)
  local groups,byPath,used={},{},{}
  for _,r in ipairs(bmpPlan.rows) do
    if r.layer.paletteBank>=0 then
      local key=M.absolute(r.bmp):lower()
      local group=byPath[key]
      if not group then
        local old=decode(r.layer.properties(GROUP).collage)
        local remembered=not new and old.owner==tostring(r.layer.uuid) and old.kit and not used[old.kit:lower()]
        local stem=app.fs.joinPath(tilekits,app.fs.fileTitle(r.bmp)..(new and '_new' or ''))
        local kit=remembered and old.kit or stem..'.json'
        local n=2
        while used[kit:lower()] do kit=stem..'_'..n..'.json'; n=n+1 end
        group={bmp=r.bmp,kit=kit,kitId=remembered and old.kitId or nil,layers={}}
        used[kit:lower()]=true; byPath[key]=group; groups[#groups+1]=group
      end
      group.layers[#group.layers+1]=r.layer
    end
  end
  return groups
end
function M.collageRows(groups)
  local rows={}
  for _,g in ipairs(groups) do
    for _,l in ipairs(g.layers) do rows[#rows+1]={layer=l,bmp=g.bmp,kit=g.kit,kitId=g.kitId} end
  end
  return rows
end
function init(plugin)
  dofile(app.fs.joinPath(directory,'windows.lua')).register(plugin,M,GROUP,BANKS)
end
function exit(plugin) end
return M
