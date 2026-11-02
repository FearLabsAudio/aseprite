-- Shared, scrollable authoring windows for the bundled KARN extension.
local UI={}
local directory=app.fs.filePath(debug.getinfo(1,'S').source:sub(2))
local layout=dofile(app.fs.joinPath(directory,'dialog_layout.lua'))
local function decode(value)
  if type(value)=='string' then local ok,t=pcall(json.decode,value); if ok then return t end end
  return {}
end
local function attempt(fn)
  local ok,err=pcall(fn)
  if not ok then app.alert{title='KARN',text=tostring(err)} end
  return ok
end
function UI.register(plugin,E,GROUP,BANKS)
  local function bankTitle(s,n)
    local name=s.properties(BANKS)['name'..n] or ''
    return string.format('%02d',n)..(name~='' and ' - '..name or '')
  end
  local function banksWindow()
    local s=app.sprite
    if not s or s.colorMode~=ColorMode.INDEXED then app.alert('Open an indexed document.'); return end
    local layers={}
    local function visit(items)
      for _,l in ipairs(items) do layers[#layers+1]=l; if l.isGroup then visit(l.layers) end end
    end
    visit(s.layers)
    local options={'Full color Reference','Document Palette'}
    for n=0,15 do options[#options+1]=bankTitle(s,n) end
    local dlg=Dialog{title='Palette Banks',autofit=0}
    local refreshing=false
    local function currentPalette()
      local pal=s.palettes[1]
      for i=1,#s.palettes do
        if s.palettes[i].frame.frameNumber<=app.frame.frameNumber then pal=s.palettes[i] end
      end
      return pal
    end
    local function colorsForBank(n)
      local pal=currentPalette()
      local colors={}
      for j=0,15 do colors[#colors+1]=(n*16+j<#pal) and pal:getColor(n*16+j) or Color(0,0,0) end
      return colors
    end
    local function documentColors(row)
      local pal=currentPalette()
      local count=s.properties('karn.rules.v1').enabled==true and 16 or #pal
      local colors={}
      for j=row*16,math.min(row*16+15,count-1) do
        colors[#colors+1]=j<#pal and pal:getColor(j) or Color(0,0,0)
      end
      return colors
    end
    local function refreshBanks()
      refreshing=true
      options={'Full color Reference','Document Palette'}
      for n=0,15 do options[#options+1]=bankTitle(s,n) end
      for i,l in ipairs(layers) do
        dlg:modify{id='bank'..i,options=options,option=options[l.paletteBank+3]}
      end
      for n=0,15 do dlg:modify{id='colors'..n,colors=colorsForBank(n)} end
      for row=0,15 do
        local colors=documentColors(row)
        dlg:modify{id='documentColors'..row,colors=colors,visible=#colors>0}
      end
      refreshing=false
      app.refresh()
    end
    dlg:tab{id='layersTab',text='Layers'}
    dlg:separator{text='Document Palette'}
    for row=0,15 do
      local colors=documentColors(row)
      dlg:shades{id='documentColors'..row,colors=colors,visible=#colors>0}:newrow()
    end
    dlg:separator{text='Layers'}
    for i,l in ipairs(layers) do
      dlg:entry{id='layerName'..i,text=l.name,onchange=function()
        if refreshing then return end
        attempt(function()
          local name=dlg.data['layerName'..i]
          if name~=l.name then l.name=name; app.refresh() end
        end)
      end}
      dlg:samerow():combobox{id='bank'..i,options=options,option=options[l.paletteBank+3],
        enabled=not l.isBackground and not l.isGroup and not l.isReference,
        onconfirm=function()
          if refreshing then return end
          attempt(function()
            local selected=dlg.data['bank'..i]
            for j,label in ipairs(options) do
              if selected==label then
                if l.paletteBank~=j-3 then
                  l:convertPaletteBank(j-3)
                end
                break
              end
            end
          end)
          -- Also restore the current assignment if validation rejected the edit.
          refreshBanks()
        end}:newrow()
    end
    dlg:tab{id='banksTab',text='Banks'}
    for n=0,15 do
      dlg:entry{id='name'..n,label=string.format('%02d',n),text=s.properties(BANKS)['name'..n] or '',
        onchange=function()
          if refreshing then return end
          attempt(function()
            local name=dlg.data['name'..n]
            if name~=(s.properties(BANKS)['name'..n] or '') then
              app.transaction('Rename palette bank',function() s.properties(BANKS)['name'..n]=name end)
            end
          end)
          refreshBanks()
        end}
      dlg:samerow():shades{id='colors'..n,colors=colorsForBank(n)}:newrow()
    end
    dlg:endtabs{selected='layersTab'}
    layout.show(dlg,plugin,'banks')
  end

  plugin:newMenuGroup{id='karn_exports',title='KARN',group='file_export'}
  plugin:newCommand{id='KarnForceRules',title='Force KARN rules',group='karn_exports',
    onenabled=function() return app.sprite~=nil end,
    onchecked=function() return app.sprite and app.sprite.properties('karn.rules.v1').enabled==true end,
    onclick=function() app.command.ForceKarnRules() end}
  local bmpWindow=dofile(app.fs.joinPath(directory,'bmp_window.lua'))
  local collageWindow=dofile(app.fs.joinPath(directory,'collage_window.lua'))
  plugin:newCommand{id='KarnExportBmps',title='Export layers as 4bpp BMPs',group='karn_exports',
    onclick=function() attempt(function() bmpWindow.show(plugin,E,GROUP,BANKS) end) end}
  plugin:newCommand{id='KarnExportCollage',title='Export to Karnedit',group='karn_exports',
    onclick=function() attempt(function() collageWindow.show(plugin,E,GROUP,BANKS,bmpWindow) end) end}
  plugin:newCommand{id='KarnPaletteBanks',title='Palette Banks',
    onclick=function() attempt(banksWindow) end}
end
return UI
