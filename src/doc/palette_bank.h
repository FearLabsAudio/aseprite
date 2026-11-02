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
#include <charconv>
#include <optional>

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

// Ordinary Aseprite files have no bank assignments, so every bank starts new.
// For older KARN files without history, retain the currently assigned banks.
inline int initialized_palette_banks(const Sprite* sprite)
{
  const auto& maps = sprite->userData().propertiesMaps();
  const auto group = maps.find(kPaletteBankProperties);
  if (group != maps.end()) {
    const auto it = group->second.find("initialized");
    if (it != group->second.end())
      return std::visit([](const auto& value) -> int {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_integral_v<T>)
          return int(value) & 0xffff;
        return 0;
      }, static_cast<const UserData::VariantBase&>(it->second));
  }
  int initialized = 0;
  for (const auto* layer : sprite->allLayers())
    if (layer->paletteBank() >= 0)
      initialized |= 1 << layer->paletteBank();
  return initialized;
}

inline bool palette_bank_used_by_other_layer(const Layer* layer, int bank)
{
  for (const auto* other : layer->sprite()->allLayers()) {
    if (other == layer || !other->isImage() || other->isReference() || other->isRgbReference())
      continue;
    if (other->paletteBank() == bank)
      return true;
  }
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

// Bank zero used to alias the document's first sixteen colors. Keep its
// independent, frame-specific colors in native sprite user data. Missing data
// deliberately uses the old colors so existing files keep their appearance.
inline const UserData::Properties* bank_zero_palettes(const Sprite* sprite)
{
  const auto& maps = sprite->userData().propertiesMaps();
  const auto group = maps.find(kPaletteBankProperties);
  if (group == maps.end()) return nullptr;
  const auto it = group->second.find("bank0Palettes");
  return it == group->second.end() ? nullptr : std::get_if<UserData::Properties>(&it->second);
}

inline Palette make_bank_palette(const Sprite* sprite, frame_t frame, int bank)
{
  const auto* source = sprite->palette(frame);
  Palette result = make_bank_palette(*source, bank);
  if (bank != 0) return result;
  const auto* palettes = bank_zero_palettes(sprite);
  if (!palettes) return result;
  int latest = 0;
  const UserData::Vector* colors = nullptr;
  for (const auto& entry : *palettes) {
    int at = 0;
    const auto parsed = std::from_chars(entry.first.data(), entry.first.data()+entry.first.size(), at);
    if (parsed.ec != std::errc() || parsed.ptr != entry.first.data()+entry.first.size() ||
        at <= latest || at > frame+1) continue;
    const auto* candidate = std::get_if<UserData::Vector>(&entry.second);
    if (candidate && candidate->size() == 16) { latest = at; colors = candidate; }
  }
  if (colors) {
    result.setFrame(latest-1);
    for (int i = 1; i < 16; ++i) {
      std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
          if (value >= 0 && uint64_t(value) <= UINT32_MAX) result.setEntry(i, color_t(value));
      }, static_cast<const UserData::VariantBase&>((*colors)[i]));
    }
  }
  return result;
}

inline UserData::Vector bank_palette_colors(const Palette& palette)
{
  UserData::Vector colors;
  for (int i = 0; i < 16; ++i) colors.emplace_back(uint32_t(i ? palette.getEntry(i) : 0));
  return colors;
}

inline bool needs_independent_bank_output(const Sprite* sprite)
{
  if (!bank_zero_palettes(sprite)) return false;
  for (const auto* layer : sprite->allLayers())
    if (layer->paletteBank() == 0) return true;
  return false;
}

// Merge a local palette edit without changing any other bank or its reserved slot.
inline Palette merge_bank_palette(const Layer* layer, frame_t frame, const Palette& local)
{
  if (!layer || (!uses_local_palette(layer) && !force_karn_rules(layer->sprite())))
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
