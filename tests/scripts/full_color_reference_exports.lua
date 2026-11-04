-- Composed exports preserve RGBA without requiring a Bank 00 layer.
local root=app.params.output or '_test_full_color_reference_exports'
assert(app.fs.makeAllDirectories(root))
local pc=app.pixelColor
local function rendered(s,frame)
  local image=Image(s.width,s.height,ColorMode.RGB)
  image:drawSprite(s,frame)
  return image
end
local function check(path,expected,scale)
  local output=Sprite{fromFile=path}
  assert(output.colorMode==ColorMode.RGB,'Full color Reference exports must retain RGB')
  local actual=rendered(output,1)
  assert(actual.width==expected.width*scale and actual.height==expected.height*scale)
  for y=0,actual.height-1 do for x=0,actual.width-1 do
    assert(actual:getPixel(x,y)==expected:getPixel(x//scale,y//scale),
      string.format('Composed export changed RGBA at %d,%d: %s (got %08x, expected %08x)',
        x,y,path,actual:getPixel(x,y),expected:getPixel(x//scale,y//scale)))
  end end
  output:close()
end
for _,rules in ipairs{false,true} do
  for _,bank in ipairs{-1,0,1} do
    local s=Sprite(8,8,ColorMode.INDEXED)
    local reference=s.layers[1]; reference.paletteBank=-2
    local image=Image(8,8,ColorMode.RGB)
    image:clear(pc.rgba(13,79,201,117))
    s:newCel(reference,1,image)
    if bank>=0 then
      local art=s:newLayer(); art.paletteBank=bank
      local indexed=Image(8,8,ColorMode.INDEXED)
      indexed:putPixel(1,0,3)
      s:newCel(art,1,indexed)
    end
    if rules then app.command.ForceKarnRules{enabled=true,ui=false} end
    local stem=root..'/bank'..bank..'-rules'..tostring(rules)
    local expected=rendered(s,1)
    s:saveCopyAs(stem..'.png')
    check(stem..'.png',expected,1)
    app.sprite=s
    app.command.SaveFileCopyAs{filename=stem..'-scaled.png',scale=2}
    check(stem..'-scaled.png',expected,2)
    app.sprite=s
    s:newFrame()
    image:clear(pc.rgba(232,51,37,201))
    s:newCel(reference,2,image)
    local sheet=Image(16,8,ColorMode.RGB)
    sheet:drawImage(expected)
    sheet:drawImage(rendered(s,2),Point(8,0))
    app.command.ExportSpriteSheet{ui=false,type=SpriteSheetType.HORIZONTAL,
      textureFilename=stem..'-sheet.png',dataFilename=stem..'-sheet.json',
      trim=false,trimSprite=false,mergeDuplicates=false,ignoreEmpty=false}
    check(stem..'-sheet.png',sheet,1)
    s:close()
  end
end
print('Full color Reference: PNG, scaled PNG and animated sprite-sheet RGBA exports OK')
