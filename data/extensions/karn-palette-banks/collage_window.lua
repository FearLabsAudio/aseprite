-- Step 2 consumes the same BMP exports as the standalone command.
local UI={}
local directory=app.fs.filePath(debug.getinfo(1,'S').source:sub(2))
local layout=dofile(app.fs.joinPath(directory,'dialog_layout.lua'))
local function decode(value)
  if type(value)=='string' then
    local ok,t=pcall(json.decode,value)
    if ok and (type(t)=='table' or type(t)=='userdata') then return t end
  end
  return {}
end
local function attempt(fn)
  local ok,err=pcall(fn)
  if not ok then app.alert{title='Export to Karnedit',text=tostring(err)} end
end
local function workspaceFrom(filename)
  local p=app.fs.filePath(filename)
  while p and p~='' do
    if app.fs.isFile(app.fs.joinPath(p,'src','constants.p8')) then return p end
    local parent=app.fs.filePath(p); if parent==p then break end; p=parent
  end
  return ''
end
function UI.show(plugin,E,GROUP,BANKS,bmpWindow)
  local s=app.sprite
  if not s or s.colorMode~=ColorMode.INDEXED then app.alert('Open an indexed document.'); return end
  local frame=app.frame.frameNumber
  if #E.layers(s,true)==0 then app.alert('There are no visible layers assigned to palette banks.'); return end
  local exportCollage=E.canExportCollage(s)
  local bmpPlan=E.collageBmps(s,frame)
  if not bmpPlan then
    bmpPlan=bmpWindow.show(plugin,E,GROUP,BANKS,{forCollage=true})
    if not bmpPlan then return end
  end
  local saved=decode(s.properties(GROUP).collage)
  local prefs=plugin.preferences
  local workspace=saved.workspace or prefs.workspace or workspaceFrom(bmpPlan.files[1].path)
  if workspace=='' then workspace=workspaceFrom(s.filename) end
  local tilekits=saved.tilekits or prefs.tilekits or
    (workspace~='' and app.fs.joinPath(workspace,'karnedit_project','TileKits') or '')
  local output=saved.output or app.fs.joinPath(tilekits,E.safeName(app.fs.fileTitle(s.filename))..'.json')
  local groups=E.collageGroups(s,bmpPlan,tilekits,false)
  while true do
    local action
    local current=E.collageBmps(s,frame)
    local dlg=Dialog{title='Export to Karnedit',autofit=0}
    dlg:label{text='Step 2: one TileKit per BMP | Frame '..frame}:newrow()
    dlg:label{text=current and 'BMP exports are current.' or 'Update BMP exports before continuing.'}:newrow()
    local available=E.canExportCollage(s)
    exportCollage=available and exportCollage
    dlg:check{id='exportCollage',text='Export Collage',selected=exportCollage,enabled=available,
      onclick=function() dlg:modify{id='output',enabled=dlg.data.exportCollage} end}
    if not available then
      dlg:label{text='Requires visible layers assigned to different palette banks.'}:newrow()
    end
    dlg:file{id='output',label='Collage:',filename=output,save=true,entry=true,filetypes={'json'},enabled=exportCollage}
    dlg:file{id='workspace',label='Game workspace:',filename=workspace,folder=true,entry=true}
    dlg:file{id='tilekits',label='TileKit folder:',filename=tilekits,folder=true,entry=true}
    dlg:samerow():button{text='Reset TileKit paths',hexpand=false,onclick=function()
      for i,g in ipairs(groups) do
        local path=app.fs.joinPath(dlg.data.tilekits,app.fs.fileTitle(g.bmp)..'.json')
        g.kitId=nil
        dlg:modify{id='kit'..i,filename=path}
      end
    end}
    for i,g in ipairs(groups) do
      local names={}; for _,l in ipairs(g.layers) do names[#names+1]=l.name end
      dlg:separator{text=table.concat(names,' + ')}
      dlg:label{label='BMP:',text=app.fs.fileName(g.bmp)}
      dlg:file{id='kit'..i,label='TileKit:',filename=g.kit,entry=true,save=true,filetypes={'json'}}
    end
    local function capture()
      local d=dlg.data
      output,workspace,tilekits=d.output,d.workspace,d.tilekits
      exportCollage=available and d.exportCollage
      for i,g in ipairs(groups) do
        local path=d['kit'..i]
        if E.absolute(path):lower()~=E.absolute(g.kit):lower() then g.kitId=nil end
        g.kit=path
      end
    end
    dlg:label{text='Existing files keep their IDs and brushes.'}
    dlg:button{text='Export',onclick=function()
      attempt(function()
        capture()
        local fresh,why=E.collageBmps(s,frame)
        assert(fresh,(why or 'BMP exports are out of date.')..' Open BMP exports to update them.')
        local same=exportCollage and saved.output and E.absolute(output):lower()==E.absolute(saved.output):lower()
        local plan=E.prepare(s,{mode='collage',frame=frame,output=output,workspace=workspace,tilekits=tilekits,
          rows=E.collageRows(groups),bmpPlan=fresh,exportCollage=exportCollage,
          collageId=same and saved.collageId or nil})
        for _,file in ipairs(plan.files) do
          if file.original~=nil then
            if app.alert{title='Export to Karnedit',text='Overwrite / Update existing files?',
              buttons={'Overwrite / Update','Cancel'}}~=1 then return end
            break
          end
        end
        E.write(plan); E.remember(plan)
        prefs.workspace,prefs.tilekits=workspace,tilekits
        action='done'; dlg:close()
        app.alert{title='KARN',text=exportCollage and 'Exported TileKits and collage using the current BMPs.' or
          'Exported TileKits using the current BMPs.'}
      end)
    end}
    dlg:samerow():button{text='BMP exports...',onclick=function()
      attempt(function() capture(); action='bmps'; dlg:close() end)
    end}
    dlg:samerow():button{text='Cancel'}
    layout.show(dlg,plugin,'collage')
    if action=='bmps' then
      local updated=bmpWindow.show(plugin,E,GROUP,BANKS,{forCollage=true})
      if updated then
        local previous={}
        for _,g in ipairs(groups) do previous[g.bmp:lower()]=g end
        bmpPlan=updated
        groups=E.collageGroups(s,bmpPlan,tilekits,false)
        for _,g in ipairs(groups) do
          local old=previous[g.bmp:lower()]
          if old then g.kit,g.kitId=old.kit,old.kitId end
        end
      end
    else return end
  end
end
return UI
