-- Bank validation errors must reach Lua callers as useful messages, not the
-- numeric bank argument left on the stack by an uncaught C++ exception.
local s=Sprite(8,8,ColorMode.INDEXED)
local layer=s.layers[1]
local p=s.palettes[1]
p:setColor(3,Color(17,34,51))
local image=Image(8,8,ColorMode.INDEXED)
image:clear(0)
image:putPixel(1,1,3)
s:newCel(layer,1,image)
assert(layer:convertPaletteBank(-2)==true)
assert(layer.paletteBank==-2)
assert(layer:cel(1).image.colorMode==ColorMode.RGB)
assert(layer:cel(1).image:getPixel(1,1)==app.pixelColor.rgba(17,34,51,255))
app.undo()
assert(layer.paletteBank==-1 and layer:cel(1).image.bytes==image.bytes)
app.redo()
assert(layer.paletteBank==-2)

local function rejected(l,bank,message)
  local original=l.paletteBank
  local cel=l:cel(1)
  local bytes=cel and cel.image.bytes
  for _,change in ipairs({
    function() return l:convertPaletteBank(bank) end,
    function() l.paletteBank=bank end,
  }) do
    local ok,err=pcall(change)
    assert(not ok,'Invalid assignment unexpectedly succeeded')
    assert(type(err)=='string','Expected a readable error, got '..tostring(err))
    assert(err:find(message,1,true),'Unexpected error: '..err)
    assert(l.paletteBank==original,'Rejected assignment changed the bank')
    assert(not cel or l:cel(1).image.bytes==bytes,'Rejected assignment changed pixels')
  end
end

app.command.NewLayer{tilemap=true,name='Cannot convert tilemap directly'}
local tilemap=app.layer
rejected(tilemap,-2,'Convert this tilemap to a regular layer')
-- Failed assignments must release the transaction/lock so another edit works.
assert(tilemap:convertPaletteBank(0)==true and tilemap.paletteBank==0)
app.undo()
assert(tilemap.paletteBank==-1)

local high=s:newLayer()
local highImage=Image(8,8,ColorMode.INDEXED)
highImage:clear(17)
s:newCel(high,1,highImage)
rejected(high,0,'This layer contains indices above 15')
rejected(s:newGroup(),-2,'Palette banks require an indexed image or tilemap layer')
s:close()
print('Palette bank errors: readable diagnostics, RGB conversion, rollback and undo OK')
