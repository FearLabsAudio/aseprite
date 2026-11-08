// Scoped RGB-to-indexed conversion, shared by paste and palette-bank changes.
#ifndef APP_UTIL_INDEXED_CONVERSION_H_INCLUDED
#define APP_UTIL_INDEXED_CONVERSION_H_INCLUDED
#pragma once

#include "app/util/optimal_palette.h"
#include "app/util/palette_match.h"
#include "app/util/rgb_conversion_adjustment.h"
#include "doc/image_ref.h"
#include "doc/octree_map.h"
#include "doc/rgbmap_rgb5a3.h"
#include "gfx/point.h"
#include "render/dithering.h"
#include "render/quantization.h"
#include <memory>

namespace doc { class Layer; class Mask; }
namespace app {
class Editor;

struct IndexedConversionOptions {
  render::Dithering dithering;
  doc::RgbMapAlgorithm mapping = doc::RgbMapAlgorithm::DEFAULT;
  doc::FitCriteria fit = doc::FitCriteria::DEFAULT;
  bool copyBankColors = false;
  bool remapBankColors = false;
  PaletteMatch match = PaletteMatch::Existing;
  RgbConversionAdjustment adjustment;
  OptimalPaletteSettings optimal;
  std::shared_ptr<OptimalPaletteResult> generated;
};

inline doc::ImageRef convert_to_indexed(const doc::Image* source,
                                      const doc::Palette& palette,
                                      int transparent,
                                      const IndexedConversionOptions& options,
                                      render::TaskDelegate* delegate = nullptr,
                                      bool karnRules = false)
{
  if (options.optimal.enabled && options.generated) {
    auto prepared = optimal_source(source, options.optimal, options.adjustment, delegate);
    return optimal_map(prepared.get(), *options.generated, options.dithering, delegate);
  }
  if (karnRules) {
    // Remove reserved entries before quantization. All mapping algorithms and
    // dithering must choose from legal colors, not repair forbidden indices
    // afterward (which would also diffuse the wrong color error).
    const int size = std::min(16, palette.size());
    if (size <= 3)
      throw std::runtime_error("KARN conversion requires artwork colors in palette slots 3-15.");
    doc::Palette compact(0, size - 2);
    compact.setEntry(0, palette.getEntry(0));
    for (int i = 3; i < size; ++i)
      compact.setEntry(i - 2, palette.getEntry(i));
    // KARN transparency is always local slot zero; backgrounds stay opaque.
    const int mask = transparent < 0 ? -1 : 0;
    auto result = convert_to_indexed(source, compact, mask, options, delegate);
    for (int y = 0; y < result->height(); ++y) {
      optimal_check(delegate);
      for (int x = 0; x < result->width(); ++x)
        if (auto index = result->getPixel(x, y))
          result->putPixel(x, y, index + 2);
    }
    if (mask >= 0)
      result->setMaskColor(0);
    return result;
  }
  // A private map avoids altering the document's mapping/palette during preview.
  std::unique_ptr<doc::RgbMap> map;
  if (options.match != PaletteMatch::Existing)
    map.reset(new PaletteMatchMap(options.match));
  else if (options.mapping == doc::RgbMapAlgorithm::RGB5A3)
    map.reset(new doc::RgbMapRGB5A3);
  else
    map.reset(new doc::OctreeMap);
  map->regenerateMap(&palette, transparent, options.fit);
  auto adjusted = options.adjustment.makeAdjustedCopy(source, delegate);
  if (adjusted) source = adjusted.get();
  return doc::ImageRef(render::convert_pixel_format(source, nullptr, doc::IMAGE_INDEXED,
    options.dithering, map.get(), &palette, transparent < 0, transparent, nullptr, delegate));
}

// Modal only at interactive entry points; false means Cancel and no document changes.
bool choose_indexed_conversion(Editor* editor,
                               doc::Layer* layer,
                               const doc::ImageRef& source,
                               const gfx::Point& position,
                               const doc::Palette& palette,
                               int transparent,
                               bool paste,
                               IndexedConversionOptions& options,
                               const doc::Mask* mask = nullptr,
                               int destinationBank = -1);
bool choose_palette_bank_conversion(doc::Layer* layer, int bank,
                                    IndexedConversionOptions& options);
} // namespace app
#endif
