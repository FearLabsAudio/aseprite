// Scoped RGB-to-indexed conversion, shared by paste and palette-bank changes.
#ifndef APP_UTIL_INDEXED_CONVERSION_H_INCLUDED
#define APP_UTIL_INDEXED_CONVERSION_H_INCLUDED
#pragma once

#include "doc/image_ref.h"
#include "app/util/palette_match.h"
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
};

inline doc::ImageRef convert_to_indexed(const doc::Image* source,
                                      const doc::Palette& palette,
                                      int transparent,
                                      const IndexedConversionOptions& options,
                                      render::TaskDelegate* delegate = nullptr)
{
  // A private map avoids altering the document's mapping/palette during preview.
  std::unique_ptr<doc::RgbMap> map;
  if (options.match != PaletteMatch::Existing)
    map.reset(new PaletteMatchMap(options.match));
  else if (options.mapping == doc::RgbMapAlgorithm::RGB5A3)
    map.reset(new doc::RgbMapRGB5A3);
  else
    map.reset(new doc::OctreeMap);
  map->regenerateMap(&palette, transparent, options.fit);
  return doc::ImageRef(render::convert_pixel_format(source, nullptr, doc::IMAGE_INDEXED,
    options.dithering, map.get(), &palette, transparent < 0, transparent, nullptr, delegate));
}

// Modal only at interactive entry points; false means Cancel and no document changes.
bool choose_indexed_conversion(Editor* editor, doc::Layer* layer,
                               const doc::ImageRef& source, const gfx::Point& position,
                               const doc::Palette& palette, int transparent,
                               bool paste, IndexedConversionOptions& options,
                               const doc::Mask* mask = nullptr);
bool choose_palette_bank_conversion(doc::Layer* layer, int bank,
                                    IndexedConversionOptions& options);
} // namespace app
#endif
