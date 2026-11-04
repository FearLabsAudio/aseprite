// KARN palette-bank transactions shared by the UI and scripting API.
#ifndef APP_UTIL_PALETTE_BANK_H_INCLUDED
#define APP_UTIL_PALETTE_BANK_H_INCLUDED
#pragma once
#include "app/cmd/add_palette.h"
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
inline void clear_palette_bank(Tx& tx, doc::Sprite* sprite, int bank)
{
  if (sprite->pixelFormat() != IMAGE_INDEXED || bank < 0 || bank > 15)
    throw std::runtime_error("Choose a palette bank from 0-15 in an indexed document.");
  for (const auto* layer : sprite->allLayers())
    if (layer->paletteBank() == bank)
      throw std::runtime_error("This bank is assigned to a layer. Unassign it before clearing it.");
  const int initialized = doc::initialized_palette_banks(sprite);
  // Bank zero has independent storage. Never clear the Document Palette.
  if (bank != 0) {
    for (const auto* original : sprite->getPalettes()) {
      doc::Palette cleared(*original);
      for (int i = bank * 16; i < std::min((bank + 1) * 16, cleared.size()); ++i)
        cleared.setEntry(i, doc::rgba(0, 0, 0, 255));
      if (cleared != *original)
        tx(new cmd::SetPalette(sprite, original->frame(), &cleared));
    }
  }
  auto data = sprite->userData();
  auto& properties = data.properties(doc::kPaletteBankProperties);
  if (bank == 0) {
    doc::Palette cleared(0, 16);
    for (int i = 0; i < 16; ++i) cleared.setEntry(i, doc::rgba(0, 0, 0, 255));
    properties["bank0Palettes"] = doc::UserData::Properties{
      { "1", doc::bank_palette_colors(cleared) }
    };
  }
  properties["initialized"] = int32_t(initialized & ~(1 << bank));
  if (data.propertiesMaps() != sprite->userData().propertiesMaps())
    tx(new cmd::SetUserData(sprite, data, static_cast<Doc*>(sprite->document())));
}

inline bool inherit_bank_colors(doc::Layer* layer, int bank,
                                const IndexedConversionOptions& options = {})
{
  return bank >= 0 && !doc::palette_bank_used_by_other_layer(layer, bank) &&
         (options.copyBankColors || !(doc::initialized_palette_banks(layer->sprite()) & (1 << bank)));
}

inline doc::Palette palette_for_bank_conversion(doc::Layer* layer, int bank, doc::frame_t frame,
                                                const IndexedConversionOptions& options = {})
{
  const auto* palette = layer->sprite()->palette(frame);
  if (inherit_bank_colors(layer, bank, options))
    return layer->paletteBank() >= 0 ? doc::make_bank_palette(layer->sprite(), frame, layer->paletteBank()) :
                                       doc::make_bank_palette(*palette, 0, doc::force_karn_rules(layer->sprite()));
  if (bank >= 0) return doc::make_bank_palette(layer->sprite(), frame, bank);
  return doc::force_karn_rules(layer->sprite()) ? doc::make_bank_palette(*palette, 0, true) : *palette;
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
  const int initialized = doc::initialized_palette_banks(layer->sprite());
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
        const auto palette = palette_for_bank_conversion(layer, bank, cel->frame(), options);
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
    const bool inherit = inherit_bank_colors(layer, bank, options);
    // Snapshot the source before any palette commands change the native
    // timeline. All numbered banks inherit the same frame-specific colors,
    // including Bank 00 keyframes stored separately from the Document Palette.
    std::vector<doc::Palette> inherited;
    for (const auto frame : doc::palette_bank_frames(layer->sprite(), inherit ? layer->paletteBank() : -1)) {
      auto colors = palette_for_bank_conversion(layer, bank, frame, options);
      colors.setFrame(frame);
      inherited.push_back(colors);
    }
    if (inherit && bank == 0) {
      doc::UserData::Properties palettes;
      for (const auto& colors : inherited)
        palettes[std::to_string(colors.frame()+1)] = doc::bank_palette_colors(colors);
      auto data = layer->sprite()->userData();
      data.properties(doc::kPaletteBankProperties)["bank0Palettes"] = palettes;
      tx(new cmd::SetUserData(layer->sprite(), data, static_cast<Doc*>(layer->sprite()->document())));
    }
    for (const auto& previous : inherited) {
      const auto frame = previous.frame();
      const auto* original = layer->sprite()->palette(frame);
      doc::Palette expanded(*original);
      expanded.setFrame(frame);
      if (original->size() < (bank + 1) * 16)
        expanded.resize((bank + 1) * 16);
      if (inherit && bank != 0)
        for (int i = doc::force_karn_rules(layer->sprite()) ? 0 : 1; i < 16; ++i)
          expanded.setEntry(bank * 16 + i, previous.getEntry(i));
      if (expanded != *original) {
        if (original->frame() == frame)
          tx(new cmd::SetPalette(layer->sprite(), frame, &expanded));
        else
          tx(new cmd::AddPalette(layer->sprite(), &expanded));
      }
    }
  }
  auto spriteData = layer->sprite()->userData();
  spriteData.properties(doc::kPaletteBankProperties)["initialized"] =
    int32_t(initialized | (bank >= 0 ? (1 << bank) : 0) |
            (layer->paletteBank() >= 0 ? (1 << layer->paletteBank()) : 0));
  tx(new cmd::SetUserData(layer->sprite(), spriteData,
                         static_cast<Doc*>(layer->sprite()->document())));
  auto data = layer->userData();
  if (bank == -1)
    data.properties(doc::kPaletteBankProperties).erase("bank");
  else
    data.properties(doc::kPaletteBankProperties)["bank"] = int8_t(bank);
  tx(new cmd::SetUserData(layer, data, static_cast<Doc*>(layer->sprite()->document())));
}
} // namespace app
#endif
