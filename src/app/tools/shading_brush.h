// Aseprite
// This program is distributed under the terms of the Aseprite EULA.

#ifndef APP_TOOLS_SHADING_BRUSH_H_INCLUDED
#define APP_TOOLS_SHADING_BRUSH_H_INCLUDED
#pragma once

#include "doc/brush.h"

namespace app { namespace tools {

inline int progressive_shading_size(int nominalSize)
{
  return nominalSize >= 3 ? nominalSize + 2 : nominalSize;
}

// Keep UI brushes/presets nominal. Only rendering receives the extra edge ring.
inline doc::BrushRef progressive_shading_brush(const doc::BrushRef& brush)
{
  if (brush->type() == doc::kImageBrushType || brush->size() < 3)
    return brush;
  auto result = brush->cloneWithSharedImages();
  result->setSize(progressive_shading_size(brush->size()));
  return result;
}

}} // namespace app::tools

#endif
