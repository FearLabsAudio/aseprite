// KARN: document-local VERA RGB444 palette constraint.
#ifndef DOC_PALETTE_RGB444_H_INCLUDED
#define DOC_PALETTE_RGB444_H_INCLUDED
#pragma once

#include "doc/palette.h"
#include "doc/sprite.h"
#include "doc/karn_rules.h"

namespace doc {

inline constexpr const char* kRgb444Properties = "karn.palette-rgb444.v1";

inline bool force_rgb444(const Sprite* sprite)
{
  if (!sprite)
    return false;
  if (force_karn_rules(sprite))
    return true;
  const auto& maps = sprite->userData().propertiesMaps();
  const auto group = maps.find(kRgb444Properties);
  if (group != maps.end()) {
    const auto value = group->second.find("enabled");
    if (value != group->second.end())
      if (const auto* enabled = std::get_if<bool>(&value->second))
        return *enabled;
  }
  return false;
}

inline color_t snap_rgb444(color_t c, bool opaque = false)
{
  const auto snap = [](int channel) { return ((channel + 8) / 17) * 17; };
  return rgba(snap(rgba_getr(c)), snap(rgba_getg(c)), snap(rgba_getb(c)),
              opaque ? 255 : rgba_geta(c));
}

inline void snap_rgb444(Palette& palette, bool opaque = false)
{
  for (int i = 0; i < palette.size(); ++i)
    palette.setEntry(i, snap_rgb444(palette.getEntry(i), opaque));
}

} // namespace doc
#endif
