-- Clearing unused banks is atomic, covers every frame, and resets NEW history.
local output=app.params.output or '_test_palette_bank_clear'
local ns='karn.palette-banks.v1'
for _,karn in ipairs{false,true} do
  local s=Sprite(8,8,ColorMode.INDEXED)
  local layer=s.layers[1]
  local p=s.palettes[1]
  for i=0,255 do p:setColor(i,Color(i%16*17,i//16*17,68)) end
  s:newFrame()
  -- Include a bank-zero keyframe without a native palette keyframe.
  local frames={}
  for frame=1,2 do
    local colors={}
    for i=0,15 do colors[#colors+1]=app.pixelColor.rgba(frame*17,i*17,85,255) end
    frames[tostring(frame)]=colors
  end
  s.properties(ns).bank0Palettes=frames
  s.properties(ns).initialized=65535
  s.properties(ns).name0='Unused zero'
  s.properties(ns).name3='Unused three'
  if karn then app.command.ForceKarnRules{enabled=true,ui=false} end
  local master=Palette(p)
  local before=json.encode(s.properties(ns).bank0Palettes)
  s:clearPaletteBank(0)
  assert(p==master,'Clearing bank zero must never change the Document Palette')
  assert(s.properties(ns).initialized==65534 and s.properties(ns).name0=='Unused zero')
  for frame=1,2 do
    for i=karn and 0 or 1,15 do assert(s:getPaletteBank(0,frame):getColor(i)==Color(0,0,0,255)) end
  end
  app.undo()
  assert(json.encode(s.properties(ns).bank0Palettes)==before and s.properties(ns).initialized==65535)
  app.redo(); assert(s.properties(ns).initialized==65534 and p==master)
  s:clearPaletteBank(3)
  assert(s.properties(ns).initialized==(65534 & ~(1<<3)))
  assert(s.properties(ns).name3=='Unused three')
  for i=0,255 do
    assert(p:getColor(i)==(i>=48 and i<64 and Color(0,0,0,255) or master:getColor(i)))
  end
  app.undo(); assert(p==master)
  app.redo()
  -- Hidden and locked nested layers still count as using the bank.
  local group=s:newGroup(); group.isVisible=false; group.isEditable=false
  local child=s:newLayer(); child.parent=group; child.paletteBank=4
  local used=s:getPaletteBank(4,1)
  local history=s.properties(ns).initialized
  local ok,err=pcall(function() s:clearPaletteBank(4) end)
  assert(not ok and err:find('assigned to a layer',1,true))
  assert(s:getPaletteBank(4,1)==used and s.properties(ns).initialized==history)
  for _,n in ipairs{-2,-1,16,255} do assert(not pcall(function() s:clearPaletteBank(n) end)) end
  s:saveAs(output..tostring(karn)..'.aseprite')
  local loaded=app.open(output..tostring(karn)..'.aseprite')
  assert(loaded.properties(ns).initialized==history)
  assert(loaded.properties(ns).name0=='Unused zero' and loaded.properties(ns).name3=='Unused three')
  -- A cleared bank inherits the previous palette on the next assignment.
  local first=loaded.layers[1]
  first.paletteBank=0
  assert(loaded:getPaletteBank(0,1):getColor(3)==master:getColor(3))
  first.paletteBank=3
  assert(loaded:getPaletteBank(3,1):getColor(3)==master:getColor(3))
  loaded:close(); s:close()
end
local rgb=Sprite(8,8,ColorMode.RGB)
assert(not pcall(function() rgb:clearPaletteBank(0) end)); rgb:close()
print('Clear banks: document isolation, every frame, NEW inheritance, in-use rejection, undo/redo and save/load OK')
