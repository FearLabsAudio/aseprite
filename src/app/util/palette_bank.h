// KARN palette-bank transactions shared by the UI and scripting API.
#ifndef APP_UTIL_PALETTE_BANK_H_INCLUDED
#define APP_UTIL_PALETTE_BANK_H_INCLUDED
#pragma once
#include "app/cmd/set_palette.h"
#include "app/cmd/set_user_data.h"
#include "app/tx.h"
#include "doc/palette_bank.h"
#include "doc/tileset.h"
#include "doc/cel.h"

namespace app {
inline void set_layer_palette_bank(Tx& tx, doc::Layer* layer, int bank)
{
  if (!layer->isImage() || layer->isReference() || layer->isBackground() ||
      layer->sprite()->pixelFormat() != IMAGE_INDEXED)
    throw std::runtime_error("Palette banks require an indexed image or tilemap layer. Convert backgrounds to layers first.");
  if (bank < -1 || bank > 15)
    throw std::runtime_error("Palette bank must be between 0 and 15, or -1 for Document Palette.");
  if (bank == layer->paletteBank())
    return;
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
  if (bank < 0)
    data.properties(doc::kPaletteBankProperties).erase("bank");
  else
    data.properties(doc::kPaletteBankProperties)["bank"] = uint8_t(bank);
  tx(new cmd::SetUserData(layer, data, static_cast<Doc*>(layer->sprite()->document())));
}
} // namespace app
#endif
