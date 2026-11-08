#pragma once
#include "app/cmd/set_palette.h"
#include "app/cmd/set_pixel_format.h"
#include "app/tx.h"
#include "app/util/indexed_conversion.h"
#include "doc/cel.h"
#include "doc/cels_range.h"
#include "doc/tilesets.h"

namespace app {
inline void optimal_convert_sprite(Tx& tx,
                                   doc::Sprite* sprite,
                                   IndexedConversionOptions& options,
                                   render::TaskDelegate* task = nullptr)
{
  std::vector<doc::ImageRef> images;
  for (auto* cel : sprite->cels())
    if (!cel->layer()->isTilemap())
      images.push_back(cel->imageRef());
  for (auto* tileset : *sprite->tilesets())
    if (tileset)
      for (const auto& tile : *tileset)
        if (tile.image)
          images.push_back(tile.image);
  options.generated = std::make_shared<OptimalPaletteResult>(optimal_palette(images,
                                                                             *sprite->palette(0),
                                                                             options.optimal,
                                                                             options.adjustment,
                                                                             options.dithering,
                                                                             task));
  // Preserve reserved colors at every palette keyframe.
  for (auto* original : sprite->getPalettes()) {
    auto palette = *original;
    if (palette.size() < 16)
      palette.resize(16);
    for (int i = 3; i < 16; ++i)
      palette.setEntry(i, options.generated->palette.getEntry(i));
    tx(new cmd::SetPalette(sprite, original->frame(), &palette, false));
  }
  tx(new cmd::SetPixelFormat(sprite,
                             doc::IMAGE_INDEXED,
                             options.dithering,
                             options.mapping,
                             nullptr,
                             task,
                             options.fit,
                             options.adjustment,
                             options));
}
} // namespace app
