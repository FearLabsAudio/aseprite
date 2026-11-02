-- Pick the visible RGB into the destination bank, never another bank's index.
local ns='karn.palette-banks.v1'
local rgba=app.pixelColor.rgba
for _,karn in ipairs{false,true} do
  local s=Sprite(16,8,ColorMode.INDEXED)
  local active=s.layers[1]; active.name='Destination'
  s.properties(ns).initialized=65535
  active.paletteBank=1
  local top=s:newLayer(); top.paletteBank=2; top.name='Top'
  local p=s.palettes[1]
  for i=0,255 do p:setColor(i,Color(17,17,17)) end
  p:setColor(18,Color(255,0,0)); p:setColor(22,Color(255,0,0))
  p:setColor(16,Color(0,0,255)) -- Slot zero cannot be used to paint opaque blue.
  p:setColor(37,Color(255,0,0)); p:setColor(39,Color(0,255,0)); p:setColor(40,Color(0,0,255))
  p:setColor(41,Color(17,34,51)) -- Near colors must not be approximated.
  p:setColor(19,Color(17,34,68))
  p:setColor(4,Color(255,0,0)) -- Document Palette has a different matching slot.
  local bottom=Image(16,8,ColorMode.INDEXED); bottom:clear(2)
  s:newCel(active,1,bottom)
  local overlay=Image(4,1,ColorMode.INDEXED)
  overlay:putPixel(0,0,5); overlay:putPixel(1,0,9)
  overlay:putPixel(2,0,8); overlay:putPixel(3,0,9)
  local topCel=s:newCel(top,1,overlay,Point(1,1))
  if karn then app.command.ForceKarnRules{enabled=true,ui=false} end
  app.layer=active
  local function pick(x,y,expected,button)
    app.layer=active
    app.fgColor=Color{index=11}; app.bgColor=Color{index=12}
    local value=app.useTool{tool='eyedropper',points={Point(x,y)},button=button or MouseButton.LEFT}
    if expected==nil then
      assert(value==nil and app.fgColor.index==11 and app.bgColor.index==12,
        'Unavailable color must leave foreground and background unchanged')
    else
      assert(value.index==expected,'Wrong local slot at '..x..','..y..': '..value.index)
      if button==MouseButton.RIGHT then
        assert(app.bgColor.index==expected and app.fgColor.index==11)
      else assert(app.fgColor.index==expected and app.bgColor.index==12) end
    end
    assert(app.layer==active,'Picking must not change the active layer')
  end
  pick(1,1,2); pick(1,1,2,MouseButton.RIGHT)
  pick(2,1,nil); pick(2,1,nil,MouseButton.RIGHT)
  pick(3,1,nil) -- Opaque blue matches only the destination's transparent slot.
  pick(0,0,2)
  -- No fallback to the lower red pixel when the visible top green is unavailable.
  top.isVisible=false; pick(2,1,2); top.isVisible=true
  topCel.zIndex=-2; pick(2,1,2); topCel.zIndex=0
  -- Same-bank duplicates preserve their exact source slot.
  top.paletteBank=1
  overlay:putPixel(0,0,6); topCel.image=overlay
  pick(1,1,6)
  top.paletteBank=2
  overlay:putPixel(0,0,5); overlay:putPixel(1,0,0); overlay:putPixel(3,0,9)
  topCel.image=overlay
  pick(2,1,2) -- Transparent upper pixels reveal lower layers.
  p:setColor(41,Color(17,34,51)); overlay:putPixel(3,0,9); topCel.image=overlay
  pick(4,1,nil)
  -- Sampling does not require a cel, visibility or editability on the target.
  s:deleteCel(active,1)
  active.isVisible=false; active.isEditable=false
  pick(1,1,2); pick(0,0,0)
  active.isVisible=true; active.isEditable=true
  s:newCel(active,1,bottom)
  -- Group visibility is respected for the sampled source.
  local group=s:newGroup(); top.parent=group
  group.isVisible=false; pick(4,1,2); group.isVisible=true
  -- Frame-specific palette changes use the selected frame.
  s:newFrame(); app.frame=2
  local second=Image(overlay); second:putPixel(0,0,8); top:cel(2).image=second
  pick(1,1,nil); app.frame=1; pick(1,1,2)
  -- Full color Reference can supply matching colors without palette conversion.
  local reference=s:newLayer(); reference.paletteBank=-2
  local rgb=Image(2,1,ColorMode.RGB)
  rgb:putPixel(0,0,rgba(255,0,0,255)); rgb:putPixel(1,0,rgba(0,255,0,255))
  s:newCel(reference,1,rgb,Point(8,1))
  app.layer=active
  pick(8,1,2); pick(9,1,nil)
  -- A transparent RGB pixel falls through to the lower indexed image.
  rgb:putPixel(1,0,0); reference:cel(1).image=rgb; pick(9,1,2)
  -- Document Palette is also a destination in a banked document.
  active.paletteBank=-1; pick(1,1,4)
  active.paletteBank=1
  -- A flipped tilemap samples the visible pixel, including its local palette.
  s.gridBounds=Rectangle(0,0,8,8)
  app.command.NewLayer{tilemap=true,name='Tiles'}
  local tiles=app.layer; tiles.paletteBank=2
  app.useTool{tool='pencil',ink=Ink.SIMPLE,color=Color{index=5},brush=Brush(1),
    tilesetMode=TilesetMode.AUTO,points={Point(0,4)}}
  app.useTool{tool='pencil',ink=Ink.SIMPLE,color=Color{index=9},brush=Brush(1),
    tilesetMode=TilesetMode.AUTO,points={Point(1,4)}}
  app.layer=active
  pick(0,4,2); pick(1,4,nil)
  local map=Image(tiles:cel(1).image)
  local ti=app.pixelColor.tileI(map:getPixel(0,0))
  map:putPixel(0,0,app.pixelColor.tile(ti,0x80000000)); tiles:cel(1).image=map
  pick(7,4,2); pick(6,4,nil)
  s:close()
end
print('Bank picker: cross-layer exact matches, unavailable colors, duplicate indices, transparency, RGB references, frames and flipped tiles OK')
