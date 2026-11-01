// Aseprite KARN modifications
// Distributed under the terms of Aseprite's End-User License Agreement.
#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/resource_finder.h"
#include "app/tool_presets.h"
#include "base/base64.h"
#include "base/fs.h"
#include "base/fstream_path.h"
#include "doc/image.h"
#include "doc/primitives.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace app {
namespace {
using json11::Json;

// One inventory is used for both capture and recall. Bounds also protect against
// damaged/user-edited preset files; unknown fields are left alone.
template<typename F>
void visit_settings(ToolPreferences& p, ToolPreferences::Dynamics& d, F f)
{
  f("opacity", p.opacity, 0, 255);
  f("tolerance", p.tolerance, 0, 255);
  f("contiguous", p.contiguous, 0, 1);
  f("filled", p.filled, 0, 1);
  f("filled_preview", p.filledPreview, 0, 1);
  f("ink", p.ink, 0, 4);
  f("freehand", p.freehandAlgorithm, 0, 2);
  f("shading_once", p.shadingOncePerStroke, 0, 1);
  f("shading_reentry", p.shadingOnReentry, 0, 1);
  f("corner_radius", p.cornerRadius, 0, 9999);
  f("brush_type", p.brush.type, 0, 2);
  f("brush_size", p.brush.size, 1, doc::Brush::kMaxBrushSize);
  f("brush_angle", p.brush.angle, -180, 180);
  f("spray_width", p.spray.width, 1, 500);
  f("spray_speed", p.spray.speed, 1, 100);
  f("fill_grid", p.floodfill.stopAtGrid, 0, 2);
  f("fill_reference", p.floodfill.referTo, 0, 1);
  f("fill_connectivity", p.floodfill.pixelConnectivity, 0, 1);
  f("stabilizer", d.stabilizer, 0, 1);
  f("stabilizer_factor", d.stabilizerFactor, 0, 100);
  f("dynamic_size", d.size, 0, 2);
  f("dynamic_angle", d.angle, 0, 2);
  f("dynamic_gradient", d.gradient, 0, 2);
  f("min_size", d.minSize, 1, doc::Brush::kMaxBrushSize);
  f("min_angle", d.minAngle, -180, 180);
  f("color_from_to", d.colorFromTo, 0, 1);
  f("matrix", d.matrixName, 0, 0);
  f("min_pressure", d.minPressureThreshold, 0, 1);
  f("max_pressure", d.maxPressureThreshold, 0, 1);
  f("min_velocity", d.minVelocityThreshold, 0, 1);
  f("max_velocity", d.maxVelocityThreshold, 0, 1);
}

Json encode_image(const doc::Image* image)
{
  if (!image)
    return Json();
  if (image->width() > 8192 || image->height() > 8192 ||
      size_t(image->width()) * image->height() > 16777216)
    throw std::runtime_error("This image brush is too large to store in a tool preset.");
  // Canonical little-endian pixel values, independent of image stride/storage.
  base::buffer bytes;
  bytes.reserve(size_t(image->width()) * image->height() * 4);
  for (int y = 0; y < image->height(); ++y)
    for (int x = 0; x < image->width(); ++x) {
      const auto pixel = doc::get_pixel(image, x, y);
      for (int shift = 0; shift < 32; shift += 8)
        bytes.push_back((pixel >> shift) & 255);
    }
  std::string encoded;
  base::encode_base64(bytes, encoded);
  return Json::object{
    { "width",      image->width()             },
    { "height",     image->height()            },
    { "format",     int(image->pixelFormat())  },
    { "mask_color", double(image->maskColor()) },
    { "pixels",     encoded                    }
  };
}

doc::ImageRef decode_image(const Json& value)
{
  if (value.is_null())
    return nullptr;
  const int w = value["width"].int_value(), h = value["height"].int_value();
  const int format = value["format"].int_value();
  if (w < 1 || h < 1 || w > 8192 || h > 8192 || size_t(w) * h > 16777216 ||
      format < doc::IMAGE_RGB || format > doc::IMAGE_BITMAP)
    throw std::runtime_error("Invalid image brush in tool presets.");
  base::buffer bytes;
  base::decode_base64(value["pixels"].string_value(), bytes);
  if (bytes.size() != size_t(w) * h * 4)
    throw std::runtime_error("Incomplete image brush in tool presets.");
  doc::ImageRef image(doc::Image::create(doc::PixelFormat(format), w, h));
  image->setMaskColor(doc::color_t(value["mask_color"].number_value()));
  size_t pos = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      doc::color_t pixel = 0;
      for (int shift = 0; shift < 32; shift += 8)
        pixel |= doc::color_t(bytes[pos++]) << shift;
      doc::put_pixel(image.get(), x, y, pixel);
    }
  return image;
}
} // namespace

json11::Json capture_tool_settings(ToolPreferences& p, ToolPreferences::Dynamics& d)
{
  Json::object result;
  visit_settings(p, d, [&](const char* key, auto& option, int, int) {
    using T = std::decay_t<decltype(option())>;
    if constexpr (std::is_enum_v<T>)
      result[key] = int(option());
    else
      result[key] = option();
  });
  return result;
}

void apply_tool_settings(const Json& settings, ToolPreferences& p, ToolPreferences::Dynamics& d)
{
  visit_settings(p, d, [&](const char* key, auto& option, int lo, int hi) {
    using T = std::decay_t<decltype(option())>;
    const auto& value = settings[key];
    if constexpr (std::is_same_v<T, std::string>) {
      if (value.is_string())
        option(value.string_value());
    }
    else if constexpr (std::is_same_v<T, bool>) {
      if (value.is_bool())
        option(value.bool_value());
    }
    else {
      if (value.is_number() && std::isfinite(value.number_value()) && value.number_value() >= lo &&
          value.number_value() <= hi)
        option(static_cast<T>(value.number_value()));
    }
  });
}

std::string tool_presets_filename()
{
  ResourceFinder finder;
  finder.includeUserDir("tool-presets.json");
  return finder.getFirstOrCreateDefault();
}

bool tool_preset_matches(const ToolPreset& preset, const ToolPreset& current)
{
  if (preset.toolId != current.toolId)
    return false;
  for (const auto& item : preset.settings.object_items()) {
    if (current.settings[item.first] != item.second)
      return false;
  }
  if (preset.toolId == "gradient" && (preset.gradientType != current.gradientType ||
                                      preset.gradientDithering != current.gradientDithering))
    return false;
  if (!preset.ignoreColors) {
    const bool shading = preset.settings["ink"].int_value() == int(tools::InkType::SHADING);
    if (!shading && preset.hasColors &&
        (preset.foreground != current.foreground || preset.background != current.background))
      return false;
    if (shading && preset.shade != current.shade)
      return false;
  }
  const auto& a = preset.brush;
  const auto& b = current.brush;
  if (!a || !b)
    return a == b;
  if (a->type() != b->type() || a->size() != b->size() || a->angle() != b->angle() ||
      a->pattern() != b->pattern() || a->center() != b->center())
    return false;
  if (a->type() != doc::kImageBrushType)
    return true;
  auto sameImage = [](const doc::Image* a, const doc::Image* b) {
    if (!a || !b)
      return a == b;
    return a->pixelFormat() == b->pixelFormat() && a->width() == b->width() &&
           a->height() == b->height() && a->maskColor() == b->maskColor() &&
           doc::count_diff_between_images(a, b) == 0;
  };
  return sameImage(preset.ignoreColors ? a->originalImage() : a->image(),
                   preset.ignoreColors ? b->originalImage() : b->image()) &&
         sameImage(a->maskBitmap(), b->maskBitmap());
}

void update_tool_preset_settings(ToolPreset& preset, const Json& settings)
{
  ToolPreferences edited("preset-editor");
  apply_tool_settings(preset.settings, edited, edited.dynamics);
  apply_tool_settings(settings, edited, edited.dynamics);
  preset.settings = capture_tool_settings(edited, edited.dynamics);
  if (preset.brush && preset.brush->type() != doc::kImageBrushType) {
    auto brush = std::make_shared<doc::Brush>(doc::BrushType(edited.brush.type()),
                                              edited.brush.size(),
                                              edited.brush.angle());
    brush->setPattern(preset.brush->pattern());
    brush->setPatternOrigin(preset.brush->patternOrigin());
    preset.brush = brush;
  }
}

void update_tool_preset_size(ToolPreset& preset, int size)
{
  if (!preset.brush || preset.brush->type() == doc::kImageBrushType)
    return;
  size = std::clamp(size, doc::Brush::kMinBrushSize, doc::Brush::kMaxBrushSize);
  // Dialog copies share the saved brush until edited. Cancel must leave it intact.
  preset.brush = preset.brush->cloneWithNewImages();
  preset.brush->setSize(size);
  auto settings = preset.settings.object_items();
  settings["brush_size"] = size;
  preset.settings = settings;
}

void update_tool_preset_colors(ToolPreset& preset,
                               const Color& foreground,
                               const Color& background,
                               const Shade& shade)
{
  if (preset.settings["ink"].int_value() == int(tools::InkType::SHADING)) {
    preset.shade = shade;
  }
  else {
    preset.hasColors = true;
    preset.foreground = foreground;
    preset.background = background;
  }
}

std::vector<ToolPreset> load_tool_presets(const std::string& filename)
{
  if (!base::is_file(filename))
    return {};
  std::ifstream in(FSTREAM_PATH(filename), std::ios::binary);
  if (!in)
    throw std::runtime_error("Cannot read tool presets: " + filename);
  std::string error;
  const auto root = Json::parse(std::string(std::istreambuf_iterator<char>(in), {}), error);
  if (!error.empty() || root["version"].int_value() != 1 || !root["presets"].is_array())
    throw std::runtime_error("Cannot read tool presets: invalid or unsupported file.");
  std::vector<ToolPreset> result;
  for (const auto& entry : root["presets"].array_items()) {
    ToolPreset preset;
    preset.name = entry["name"].string_value();
    preset.toolId = entry["tool"].string_value();
    preset.settings = entry["settings"];
    if (preset.name.empty() || preset.toolId.empty() || !preset.settings.is_object())
      throw std::runtime_error("Invalid tool preset entry.");
    preset.shade = shade_from_string(entry["shade"].string_value());
    const auto& colors = entry["colors"];
    if (colors["foreground"].is_string() && colors["background"].is_string()) {
      preset.hasColors = true;
      preset.foreground = Color::fromString(colors["foreground"].string_value());
      preset.background = Color::fromString(colors["background"].string_value());
    }
    preset.gradientType = std::clamp(entry["gradient_type"].int_value(), 0, 1);
    // Older regular presets without stored colors already left colors unchanged.
    preset.ignoreColors = entry["ignore_colors"].is_bool() ?
                            entry["ignore_colors"].bool_value() :
                            (!preset.hasColors &&
                             preset.settings["ink"].int_value() != int(tools::InkType::SHADING));
    preset.gradientDithering = entry["gradient_dithering"].string_value();
    const auto& brush = entry["brush"];
    const int type = brush["type"].int_value();
    if (type < doc::kCircleBrushType || type > doc::kImageBrushType)
      throw std::runtime_error("Invalid brush type in tool presets.");
    preset.brush = std::make_shared<doc::Brush>(
      doc::BrushType(type),
      std::clamp(brush["size"].int_value(), 1, doc::Brush::kMaxBrushSize),
      std::clamp(brush["angle"].int_value(), -180, 180));
    if (type == doc::kImageBrushType) {
      const auto image = decode_image(brush["image"]);
      const auto mask = decode_image(brush["mask"]);
      if (!image ||
          (mask && (mask->width() != image->width() || mask->height() != image->height() ||
                    mask->pixelFormat() != doc::IMAGE_BITMAP)))
        throw std::runtime_error("Invalid image brush mask in tool presets.");
      preset.brush->setImage(image.get(), mask.get());
      preset.brush->setCenter(
        gfx::Point(std::clamp(brush["center_x"].int_value(), 0, image->width() - 1),
                   std::clamp(brush["center_y"].int_value(), 0, image->height() - 1)));
    }
    preset.brush->setPattern(doc::BrushPattern(std::clamp(brush["pattern"].int_value(), 0, 2)));
    preset.brush->setPatternOrigin(
      gfx::Point(brush["origin_x"].int_value(), brush["origin_y"].int_value()));
    result.push_back(std::move(preset));
  }
  return result;
}

void save_tool_presets(const std::string& filename, const std::vector<ToolPreset>& presets)
{
  Json::array entries;
  for (const auto& preset : presets) {
    if (!preset.brush)
      throw std::runtime_error("Tool preset has no brush.");
    const auto& b = preset.brush;
    Json::object brush{
      { "type",     int(b->type())       },
      { "size",     b->size()            },
      { "angle",    b->angle()           },
      { "pattern",  int(b->pattern())    },
      { "center_x", b->center().x        },
      { "center_y", b->center().y        },
      { "origin_x", b->patternOrigin().x },
      { "origin_y", b->patternOrigin().y }
    };
    if (b->type() == doc::kImageBrushType) {
      brush["image"] = encode_image(b->image());
      brush["mask"] = encode_image(b->maskBitmap());
    }
    Json::object entry{
      { "name",               preset.name                   },
      { "tool",               preset.toolId                 },
      { "settings",           preset.settings               },
      { "brush",              brush                         },
      { "shade",              shade_to_string(preset.shade) },
      { "ignore_colors",      preset.ignoreColors           },
      { "gradient_type",      preset.gradientType           },
      { "gradient_dithering", preset.gradientDithering      }
    };
    if (preset.hasColors)
      entry["colors"] = Json::object{
        { "foreground", preset.foreground.toString() },
        { "background", preset.background.toString() }
      };
    entries.push_back(entry);
  }
  const Json root = Json::object{
    { "version", 1       },
    { "presets", entries }
  };
  // Replace only after a complete write. A failed save leaves the previous file intact.
  const auto temp = filename + ".tmp";
  std::ofstream out(FSTREAM_PATH(temp), std::ios::binary | std::ios::trunc);
  out << root.dump() << '\n';
  out.close();
  if (!out)
    throw std::runtime_error("Cannot save tool presets: " + filename);
  base::move_file(temp, filename, true);
}
} // namespace app
