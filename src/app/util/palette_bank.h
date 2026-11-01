// KARN palette-bank transactions shared by the UI and scripting API.
#ifndef APP_UTIL_PALETTE_BANK_H_INCLUDED
#define APP_UTIL_PALETTE_BANK_H_INCLUDED
#pragma once
#include "app/cmd/set_palette.h"
#include "app/cmd/set_cel_image.h"
#include "app/cmd/set_user_data.h"
#include "app/tx.h"
#include "app/util/indexed_conversion.h"
#include "doc/palette_bank.h"
#include "doc/tileset.h"
#include "doc/cel.h"
#include "doc/cel_data.h"
#include "doc/rgbmap.h"
#include "doc/octree_map.h"
#include "render/quantization.h"
#include "render/dithering.h"
#include <set>

namespace app {
inline doc::Palette palette_for_bank_conversion(doc::Layer* layer, int bank, doc::frame_t frame)
{
  const auto* palette = layer->sprite()->palette(frame);
  return (bank >= 0 || doc::force_karn_rules(layer->sprite()) ?
    doc::make_bank_palette(*palette, palette->size() <= 16 ? 0 : std::max(0, bank)) : *palette);
}

inline void set_layer_palette_bank(Tx& tx, doc::Layer* layer, int bank,
                                   const IndexedConversionOptions& options = {})
{
  if (!layer->isImage() || layer->isReference() || layer->isBackground() ||
      layer->sprite()->pixelFormat() != IMAGE_INDEXED)
    throw std::runtime_error("Palette banks require an indexed image or tilemap layer. Convert backgrounds to layers first.");
  if (bank < -2 || bank > 15)
    throw std::runtime_error("Palette bank must be 0-15, -1 for Document Palette, or -2 for Full color Reference.");
  if (bank == layer->paletteBank())
    return;
  if (bank == -2 && layer->isTilemap())
    throw std::runtime_error("Convert this tilemap to a regular layer before assigning Full color Reference.");
  const bool fromRgb = layer->isRgbReference();
  // Convert once per linked cel data, retaining animation links and undo history.
  if (bank == -2 || fromRgb) {
    std::set<doc::ObjectId> seen;
    for (auto* cel : layer->cels()) {
      if (!seen.insert(cel->data()->id()).second)
        continue;
      doc::ImageRef image;
      if (bank == -2) {
        image.reset(render::convert_pixel_format(cel->image(), nullptr, IMAGE_RGB,
          render::Dithering(), layer->rgbMap(cel->frame()), layer->palette(cel->frame()),
          false, 0));
      }
      else {
        const auto palette = palette_for_bank_conversion(layer, bank, cel->frame());
        const int mask = bank >= 0 ? 0 : layer->sprite()->transparentColor();
        image = convert_to_indexed(cel->image(), palette, mask, options);
      }
      tx(new cmd::SetCelImage(cel, image));
    }
  }
  if (bank >= 0) {
    if (layer->sprite()->transparentColor() != 0)
      throw std::runtime_error("Palette banks require transparent index 0 (Sprite Properties).");
    if (layer->isTilemap()) {
      for (const auto& tile : *layer->tileset())
        if (tile.image && !doc::is_local_bank_image(tile.image.get()))
          throw std::runtime_error("This tileset contains indices above 15. Use local 0-15 indices before assigning a bank.");
    }
    else {
      doc::CelList cels;
      layer->getCels(cels);
      for (const auto* cel : cels)
        if (!doc::is_local_bank_image(cel->image()))
          throw std::runtime_error("This layer contains indices above 15. Use local 0-15 indices before assigning a bank.");
    }
    for (const auto* original : layer->sprite()->getPalettes()) {
      if (original->size() < (bank + 1) * 16) {
        doc::Palette expanded(*original);
        expanded.resize((bank + 1) * 16);
        // Import a standalone 16-color palette into a newly allocated bank.
        if (original->size() <= 16)
          for (int i = 1; i < original->size(); ++i)
            expanded.setEntry(doc::bank_global_index(bank, i), original->getEntry(i));
        tx(new cmd::SetPalette(layer->sprite(), original->frame(), &expanded));
      }
    }
  }
  auto data = layer->userData();
  if (bank == -1)
    data.properties(doc::kPaletteBankProperties).erase("bank");
  else
    data.properties(doc::kPaletteBankProperties)["bank"] = int8_t(bank);
  tx(new cmd::SetUserData(layer, data, static_cast<Doc*>(layer->sprite()->document())));
}
} // namespace app
#endif
