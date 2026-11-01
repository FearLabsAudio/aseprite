-- Shared BMP setup for standalone exports and collage source preparation.
local UI={}
local directory=app.fs.filePath(debug.getinfo(1,'S').source:sub(2))
local layout=dofile(app.fs.joinPath(directory,'dialog_layout.lua'))
local function decode(value)
  if type(value)=='string' then local ok,t=pcall(json.decode,value); if ok then return t end end
  return {}
end
local function attempt(fn)
  local ok,err=pcall(fn)
  if not ok then app.alert{title='KARN BMP export',text=tostring(err)} end
  return ok
end
function UI.show(plugin,E,GROUP,BANKS,options)
  options=options or {}
  local s=app.sprite
  if not s or s.colorMode~=ColorMode.INDEXED then app.alert('Open an indexed document.'); return end
  local layers=E.layers(s,false)
  if #layers==0 then app.alert('There are no layers assigned to palette banks.'); return end
  local frame=app.frame.frameNumber
  local saved=decode(s.properties(GROUP).bmp)
  local settings=saved.bmpSettings or {}
  local visible={}; for _,l in ipairs(E.layers(s,true)) do visible[l.id]=true end
  local folder=settings.folder or (saved.output and app.fs.filePath(saved.output)) or
    plugin.preferences.bmpFolder or app.fs.filePath(s.filename)
  if not folder or folder=='' then folder=app.fs.userDocsPath end
  local base=settings.baseName or app.fs.fileTitle(saved.output or s.filename)
  if not base or base=='' then base='Artwork' end
  local merged=settings.mergeByBank==true or (settings.mergeByBank==nil and saved.mergeByBank==true)
  local names,bankNames,selected={},{},{}
  local defaults=E.defaults(s,'bmp',app.fs.joinPath(folder,base..'.bmp'))
  for i,l in ipairs(layers) do
    local id=tostring(l.uuid)
    local old=decode(l.properties(GROUP).bmp)
    local remembered=settings.selected and settings.selected[id]
    selected[i]=remembered==true or (remembered==nil and visible[l.id]==true and l.paletteBank>=0)
    if options.forCollage then selected[i]=visible[l.id]==true end
    names[i]=(settings.filenames and settings.filenames[id]) or
      (old.owner==id and old.bmp and app.fs.fileName(old.bmp)) or app.fs.fileName(defaults[i].bmp)
  end
  local bankDefaults=E.bankFilenames(s,E.safeName(base))
  for n=0,15 do
    bankNames[n]=(settings.bankFilenames and settings.bankFilenames[tostring(n)]) or
      bankDefaults[n]
  end
  local dlg=Dialog{title='Export layers as 4bpp BMPs',autofit=0}
  local completed
  local refreshing=false
  local function refresh()
    refreshing=true
    local count,files,banks=0,0,{}
    for i,l in ipairs(layers) do
      local name=merged and (bankNames[l.paletteBank] or 'Assign a bank first') or names[i]
      dlg:modify{id='name'..i,text=name,enabled=selected[i] and l.paletteBank>=0}
      if selected[i] then
        count=count+1
        if not merged or not banks[l.paletteBank] then files=files+1; banks[l.paletteBank]=true end
      end
    end
    dlg:modify{id='summary',text=string.format('%d layers -> %d BMPs | Frame %d | %d x %d px',
      count,files,frame,((s.width+7)//8)*8,((s.height+7)//8)*8)}
    dlg:modify{id='export',enabled=count>0}
    refreshing=false
  end
  local function resetNames()
    base=dlg.data.baseName
    local stem=E.safeName(base)
    local rows=E.defaults(s,'bmp',app.fs.joinPath(dlg.data.folder,stem..'.bmp'))
    for i,r in ipairs(rows) do names[i]=app.fs.fileName(r.bmp) end
    bankNames=E.bankFilenames(s,stem)
    refresh()
  end
  local function choose(which)
    refreshing=true
    for i,l in ipairs(layers) do
      selected[i]=which=='all' or (which=='visible' and visible[l.id]==true)
      dlg:modify{id='select'..i,selected=selected[i]}
    end
    refreshing=false
    refresh()
  end
  if options.forCollage then
    dlg:label{text='Step 1: export visible palette-bank layers.'}:newrow()
    dlg:label{text='Karnedit creates one TileKit per BMP. Merging is optional.'}:newrow()
  end
  dlg:file{id='folder',label='BMP output folder:',filename=folder,folder=true,entry=true,title='Select BMP output folder'}
  dlg:entry{id='baseName',label='Base name:',text=base}
  dlg:samerow():button{text='Reset filenames',hexpand=false,onclick=resetNames}
  dlg:check{id='merge',label='Output:',text='Merge layers sharing a palette bank',selected=merged,
    onclick=function() merged=dlg.data.merge; refresh() end}
  if not options.forCollage then
    dlg:button{text='All',onclick=function() choose('all') end}
    dlg:samerow():button{text='Visible',onclick=function() choose('visible') end}
    dlg:samerow():button{text='None',onclick=function() choose('none') end}
  end
  dlg:separator{text='Layers / bank / output filename'}
  for i,l in ipairs(layers) do
    local title=l.name..(visible[l.id] and '' or ' (hidden)')
    local bank=l.paletteBank
    local bankName=bank>=0 and (s.properties(BANKS)['name'..bank] or '') or ''
    local bankTitle=bank>=0 and (string.format('%02d',bank)..(bankName~='' and ' '..bankName or '')) or 'No bank'
    dlg:check{id='select'..i,label=title..' | '..bankTitle,text='',hexpand=false,
      selected=selected[i],enabled=not options.forCollage,onclick=function()
      if refreshing then return end
      selected[i]=dlg.data['select'..i]; refresh()
    end}
    dlg:samerow():entry{id='name'..i,text=names[i],onchange=function()
      if refreshing then return end
      if merged and bank>=0 then bankNames[bank]=dlg.data['name'..i]
      else names[i]=dlg.data['name'..i] end
      refresh()
    end}:newrow()
  end
  dlg:separator{}
  dlg:label{id='summary',text=''}
  dlg:newrow():label{text='Merged pixels follow the canvas layer order.'}
  local function export()
    attempt(function()
      local data=dlg.data
      local destination=E.absolute(data.folder)
      assert(data.baseName~='','Enter a base name.')
      local remembered={folder=destination,baseName=data.baseName,mergeByBank=merged,
        selected={},filenames={},bankFilenames={}}
      for n=0,15 do remembered.bankFilenames[tostring(n)]=bankNames[n] end
      for i,l in ipairs(layers) do
        local id=tostring(l.uuid)
        remembered.selected[id]=selected[i]
        remembered.filenames[id]=names[i]
      end
      local plan=E.bmpFromSettings(s,frame,remembered,false)
      if options.forCollage then E.validateCollageBmps(s,frame,plan,false) end
      if #plan.warnings>0 and app.alert{title='KARN export warnings',text=plan.warnings,
        buttons={'Export unchanged','Cancel'}}~=1 then return end
      for _,file in ipairs(plan.files) do
        if file.original~=nil then
          if app.alert{title='KARN BMP export',text='Overwrite / Update existing files?',
            buttons={'Overwrite / Update','Cancel'}}~=1 then return end
          break
        end
      end
      E.write(plan); E.remember(plan)
      completed=plan
      plugin.preferences.bmpFolder=destination
      dlg:close()
      if not options.forCollage then
        app.alert{title='KARN',text='Exported '..#plan.rows..' layers to '..#plan.files..' BMPs.'}
      end
    end)
  end
  dlg:button{id='export',text='Export',onclick=export}
  dlg:samerow():button{text='Cancel'}
  refresh()
  layout.show(dlg,plugin,'bmp')
  return completed
end
return UI
