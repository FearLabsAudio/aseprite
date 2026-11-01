-- Keep editing/resetting fields from resizing the window. Bounds use Aseprite's
-- logical UI coordinates, so the width follows its scale and available space.
local UI={}
function UI.show(dlg,plugin,key)
  dlg.autofit=0
  local bounds
  if app.window then
    local natural=dlg.bounds
    local available=app.window
    local width=math.min(math.max(natural.width,math.min(960,math.floor(available.width*0.85))),
      math.max(1,available.width-32))
    local height=math.min(natural.height,math.max(1,available.height-32))
    bounds=Rectangle(math.floor((available.width-width)/2),math.floor((available.height-height)/2),width,height)
    local saved=plugin and plugin.preferences['windowBounds_'..key]
    if type(saved)=='string' then
      local ok,b=pcall(json.decode,saved)
      if ok and (type(b)=='table' or type(b)=='userdata') and
         type(b.x)=='number' and type(b.y)=='number' and
         type(b.width)=='number' and type(b.height)=='number' then
        local w=math.min(math.max(320,math.floor(b.width)),math.max(1,available.width-32))
        local h=math.min(math.max(120,math.floor(b.height)),math.max(1,available.height-32))
        bounds=Rectangle(math.max(0,math.min(math.floor(b.x),available.width-w)),
          math.max(0,math.min(math.floor(b.y),available.height-h)),w,h)
      end
    end
  end
  dlg:show{bounds=bounds,autoscrollbars=true}
  if plugin and app.window then
    local b=dlg.bounds
    plugin.preferences['windowBounds_'..key]=json.encode{x=b.x,y=b.y,width=b.width,height=b.height}
  end
end
return UI
