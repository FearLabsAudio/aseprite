// KARN palette banks. Pixel data stays indexed by local slots, never RGB matched.
#ifndef DOC_PALETTE_BANK_H_INCLUDED
#define DOC_PALETTE_BANK_H_INCLUDED
#pragma once

#include "doc/image.h"
#include "doc/layer.h"
#include "doc/palette.h"
#include "doc/sprite.h"
#include "doc/tileset.h"
#include "doc/karn_rules.h"

namespace doc {

inline constexpr const char* kPaletteBankProperties = "karn.palette-banks.v1";

inline bool uses_local_palette(const Layer* layer)
{
  return layer && !layer->isRgbReference() &&
         (layer->paletteBank() >= 0 || force_karn_rules(layer->sprite()));
}

inline bool has_palette_banks(const Sprite* sprite)
{
  for (const auto* layer : sprite->allLayers())
    if (layer->paletteBank() >= 0)
      return true;
  return false;
}

inline bool tileset_uses_palette_banks(const Tileset* tileset)
{
  if (!tileset || !tileset->sprite())
    return false;
  if (force_karn_rules(tileset->sprite()))
    return true;
  for (const auto* layer : tileset->sprite()->allTilemaps())
    if (layer->tileset() == tileset && layer->paletteBank() >= 0)
      return true;
  return false;
}

inline bool is_local_bank_image(const Image* image)
{
  if (!image || image->pixelFormat() != IMAGE_INDEXED)
    return false;
  for (int y = 0; y < image->height(); ++y)
    for (int x = 0; x < image->width(); ++x)
      if (image->getPixel(x, y) > 15)
        return false;
  return true;
}

inline std::string palette_bank_name(const Sprite* sprite, int bank)
{
  const auto& maps = sprite->userData().propertiesMaps();
  const auto group = maps.find(kPaletteBankProperties);
  if (group != maps.end()) {
    const auto it = group->second.find("name" + std::to_string(bank));
    if (it != group->second.end())
      if (const auto* name = std::get_if<std::string>(&it->second))
        return *name;
  }
  return std::string();
}

inline int bank_global_index(int bank, int local)
{
  return local == 0 ? 0 : bank * 16 + local;
}

inline Palette make_bank_palette(const Palette& source, int bank)
{
  Palette result(source.frame(), 16);
  result.setEntry(0, 0);
  for (int i = 1; i < 16; ++i)
    result.setEntry(i, source.getEntry(bank_global_index(bank, i)));
  return result;
}

// Merge a local palette edit without changing any other bank or its reserved slot.
inline Palette merge_bank_palette(const Layer* layer, frame_t frame, const Palette& local)
{
  if (!uses_local_palette(layer))
    return local;
  Palette result(*layer->sprite()->palette(frame));
  const int bank = std::max(0, layer->paletteBank());
  if (result.size() < (bank + 1) * 16)
    result.resize((bank + 1) * 16);
  for (int i = 1; i < 16 && i < local.size(); ++i)
    result.setEntry(bank_global_index(bank, i), local.getEntry(i));
  return result;
}

// A temporary display/export copy. The original tileset is never modified.
inline ImageRef expand_bank_image(const Image* source, int bank)
{
  ImageRef result(Image::createCopy(source));
  result->setMaskColor(0);
  for (int y = 0; y < result->height(); ++y)
    for (int x = 0; x < result->width(); ++x) {
      const int local = source->getPixel(x, y);
      // Invalid data is visible as its original index; never silently wrap it.
      result->putPixel(x, y, local < 16 ? bank_global_index(bank, local) : local);
    }
  return result;
}

} // namespace doc
#endif
