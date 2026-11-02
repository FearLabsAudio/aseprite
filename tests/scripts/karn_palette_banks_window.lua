-- Exercise the real Palette Banks callbacks with a headless dialog and real
-- layers, including nested layers, undo/redo, and active-layer selection.
local namespace='karn.palette-banks.v1'
local s=Sprite(8,8,ColorMode.INDEXED)
local pal=s.palettes[1]
pal:resize(256)
for i=0,255 do pal:setColor(i,Color(i%16*17,i//16*17,68)) end
s.properties(namespace).initialized=65535
local document=s.layers[1]; document.name='Document'; document.isVisible=false; document.isEditable=false
local banked=s:newLayer(); banked.name='Banked'; banked.paletteBank=3
local group=s:newGroup(); group.name='Group'
local child=s:newLayer(); child.name='Child'; child.parent=group; child.paletteBank=4
local reference=s:newLayer(); reference.name='RGB'; reference.paletteBank=-2
app.layer=banked

local commands,alerts={},{}
local plugin={preferences={},newMenuGroup=function() end,
  newCommand=function(self,o) commands[o.id]=o end}
local shown=0
local function dialog()
  local d={data={},controls={},sections={},order={}}
  -- Intentionally no tab/endtabs methods: everything belongs on one page.
  for _,method in ipairs{'radio','entry','combobox','check','shades'} do
    d[method]=function(self,o)
      self.controls[o.id]=o
      self.order[#self.order+1]=o.id
      if o.selected~=nil then self.data[o.id]=o.selected
      else self.data[o.id]=o.option or o.text end
      return self
    end
  end
  function d:separator(o) self.sections[#self.sections+1]=o.text; return self end
  function d:samerow() return self end
  function d:newrow() return self end
  function d:modify(o)
    for k,v in pairs(o) do self.controls[o.id][k]=v end
    if o.selected~=nil then self.data[o.id]=o.selected end
    if o.option then self.data[o.id]=o.option end
    return self
  end
  function d:show()
    shown=shown+1
    assert(table.concat(self.sections,',')=='Layers,Document Palette,Palette Banks')
    local rows={}
    for id,o in pairs(self.controls) do
      local row=id:match('^layerName(%d+)$')
      if row then rows[o.text]=row end
    end
    local function select(layer)
      self.controls['current'..rows[layer.name]].onclick()
      assert(app.layer==layer)
      for name,row in pairs(rows) do assert(self.data['current'..row]==(name==layer.name)) end
    end
    local function click(layer,control,value)
      local id=control..rows[layer.name]
      self.data[id]=value
      self.controls[id].onclick()
      assert(self.data[id]==value)
    end
    assert(self.data['current'..rows.Banked])
    assert(not self.data['visible'..rows.Document] and self.data['locked'..rows.Document])
    assert(self.controls['bank'..rows.Group].enabled==false)
    assert(self.controls['bank'..rows.RGB].option=='Full color Reference')

    -- Selection works for ordinary, hidden/locked, grouped, and RGB layers.
    for _,layer in ipairs{document,child,group,reference,banked} do select(layer) end
    local paletteBefore=Palette(pal)
    click(document,'visible',true)
    assert(document.isVisible and app.layer==banked)
    app.undo(); assert(not document.isVisible)
    app.redo(); assert(document.isVisible)
    click(document,'locked',false)
    assert(document.isEditable and app.layer==banked)
    app.undo(); assert(not document.isEditable)
    app.redo(); assert(document.isEditable)
    click(group,'visible',false)
    assert(not group.isVisible and child.isVisible,'Parent visibility must preserve the child flag')
    app.undo(); assert(group.isVisible)
    click(group,'locked',true)
    assert(not group.isEditable and child.isEditable,'Parent locking must preserve the child flag')
    app.undo(); assert(group.isEditable)
    select(child)
    assert(self.data['visible'..rows.Group] and not self.data['locked'..rows.Group])
    assert(pal==paletteBefore,'Layer controls must leave every palette unchanged')
    assert(document.paletteBank==-1 and banked.paletteBank==3 and child.paletteBank==4)

    -- Palette swatches remain on the same page and follow normal refresh.
    local count=s.properties('karn.rules.v1').enabled==true and 16 or 256
    for row=0,15 do
      local c=self.controls['documentColors'..row]
      assert(c.visible==(row*16<count))
      if c.visible then
        for i=0,15 do assert(c.colors[i+1]==pal:getColor(row*16+i)) end
      end
    end
    self.data.name3='Hull'
    self.controls.name3.onchange()
    assert(s.properties(namespace).name3=='Hull')
    assert(self.controls['bank'..rows.Banked].option=='03 - Hull')
    assert(app.layer==child)
    app.undo()
    -- Return the fixture to its initial layer flags and selection for reopening.
    document.isVisible=false; document.isEditable=false
    app.layer=banked
  end
  return d
end
local proxy=setmetatable({alert=function(message) alerts[#alerts+1]=message end},
  {__index=function(_,k) return app[k] end,__newindex=function(_,k,v) app[k]=v end})
local env=setmetatable({Dialog=dialog,app=proxy},{__index=_G})
local path=app.params.windows or '../data/extensions/karn-palette-banks/windows.lua'
local ui=assert(loadfile(path,'t',env))()
ui.register(plugin,{},'karn.exports.v1',namespace)
commands.KarnPaletteBanks.onclick()
app.command.ForceKarnRules{enabled=true,ui=false}
commands.KarnPaletteBanks.onclick()
assert(shown==2 and #alerts==0,json.encode(alerts))
s:close()
print('Palette Banks window: combined page, selection, visibility, locks and undo/redo OK')
