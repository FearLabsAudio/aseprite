// Aseprite KARN modifications
// Distributed under the terms of Aseprite's End-User License Agreement.
#ifndef APP_TOOL_PRESETS_H_INCLUDED
#define APP_TOOL_PRESETS_H_INCLUDED
#pragma once

#include "app/pref/preferences.h"
#include "app/shade.h"
#include "doc/brush.h"
#include "json11.hpp"
#include <string>
#include <vector>

namespace app {

// Independent snapshots: neither editing nor recalling a preset mutates its brush.
struct ToolPreset {
  std::string name;
  std::string toolId;
  json11::Json settings;
  doc::BrushRef brush;
  Shade shade;
  bool ignoreColors = false;
  bool hasColors = false;
  Color foreground;
  Color background;
  int gradientType = 0;
  std::string gradientDithering;
};

void update_tool_preset_colors(ToolPreset& preset,
                               const Color& foreground,
                               const Color& background,
                               const Shade& shade);
void update_tool_preset_size(ToolPreset& preset, int size);
void update_tool_preset_settings(ToolPreset& preset, const json11::Json& settings);
// Compare only settings included by the preset; names and stroke placement are not settings.
bool tool_preset_matches(const ToolPreset& preset, const ToolPreset& current);

json11::Json capture_tool_settings(ToolPreferences& tool, ToolPreferences::Dynamics& dynamics);
void apply_tool_settings(const json11::Json& settings,
                         ToolPreferences& tool,
                         ToolPreferences::Dynamics& dynamics);
std::vector<ToolPreset> load_tool_presets(const std::string& filename);
void save_tool_presets(const std::string& filename, const std::vector<ToolPreset>& presets);
std::string tool_presets_filename();

} // namespace app
#endif
