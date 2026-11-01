// KARN document constraints, stored in standard Aseprite user data.
#ifndef DOC_KARN_RULES_H_INCLUDED
#define DOC_KARN_RULES_H_INCLUDED
#pragma once

#include "doc/sprite.h"
#include "doc/tile.h"

namespace doc {
inline constexpr const char* kKarnRulesProperties = "karn.rules.v1";
inline constexpr tile_flags kKarnMatchFlags = tile_f_xflip | tile_f_yflip;

inline bool force_karn_rules(const Sprite* sprite)
{
  if (!sprite)
    return false;
  const auto& maps = sprite->userData().propertiesMaps();
  const auto group = maps.find(kKarnRulesProperties);
  if (group != maps.end()) {
    const auto value = group->second.find("enabled");
    if (value != group->second.end())
      if (const auto* enabled = std::get_if<bool>(&value->second))
        return *enabled;
  }
  return false;
}
} // namespace doc
#endif
