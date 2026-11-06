#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/util/palette_remap.h"
#include "app/cmd/add_tileset.h"
#include "app/cmd/replace_tileset.h"
#include "app/cmd/set_cel_data.h"
#include "app/cmd/set_cel_image.h"
#include "app/cmd/set_layer_tileset.h"
#include "app/cmd/unlink_cel.h"
#include "app/tx.h"
#include "doc/cel.h"
#include "doc/layer_tilemap.h"
#include "doc/palette_bank.h"
#include "doc/primitives.h"
#include "doc/remap.h"
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace app {
using namespace doc;

bool uses_scoped_palette_remap(const Sprite* sprite)
{
  if (!sprite) return false;
  if (has_palette_banks(sprite) || force_karn_rules(sprite)) return true;
  for (const auto* layer : sprite->allLayers())
    if (layer->isRgbReference()) return true;
  return false;
}

void remap_palette_bank(Tx& tx, Sprite* sprite, int bank,
                        const Palette& previous, const Palette& current)
{
  if (sprite->pixelFormat() != IMAGE_INDEXED || bank < -1 || bank > 15)
    throw std::runtime_error("Remap an indexed Document Palette or a numbered bank from 0-15.");
  if (previous.size() < 1 || current.size() < 1 ||
      previous.size() > 256 || current.size() > 256 ||
      (bank >= 0 && (previous.size() != 16 || current.size() != 16)))
    throw std::runtime_error("Palette banks require 16 colors; the Document Palette supports up to 256.");
  const int mask = bank >= 0 ? 0 : sprite->transparentColor();
  if (bank >= 0 && sprite->transparentColor() != 0)
    throw std::runtime_error("Numbered palette banks require transparent index 0.");
  if (mask >= previous.size() || mask >= current.size())
    throw std::runtime_error("The palette must include the transparent index.");
  const Remap remap = create_remap_to_change_palette(&previous, &current, mask, false);
  if (remap.isIdentity()) return;

  const auto matches = [bank](const Layer* layer) {
    return layer->isImage() && !layer->isRgbReference() && layer->paletteBank() == bank;
  };
  std::map<ObjectId, std::vector<Cel*>> cels;
  std::map<ObjectId, int> dataUsers;
  std::map<tileset_index, std::vector<LayerTilemap*>> tilemaps;
  for (auto* layer : sprite->allLayers()) {
    for (auto* cel : layer->cels()) ++dataUsers[cel->dataId()];
    if (!matches(layer)) continue;
    if (layer->isTilemap())
      tilemaps[static_cast<LayerTilemap*>(layer)->tilesetIndex()].push_back(static_cast<LayerTilemap*>(layer));
    else
      for (auto* cel : layer->cels())
        cels[cel->dataId()].push_back(cel);
  }

  for (const auto& entry : cels) {
    const auto& linked = entry.second;
    Cel* anchor = linked.front();
    if (anchor->image()->pixelFormat() != IMAGE_INDEXED) continue;
    if (bank >= 0 && !is_local_bank_image(anchor->image()))
      throw std::runtime_error("Palette-bank pixels must use local indices 0-15 before remapping.");
    ImageRef image(Image::createCopy(anchor->image()));
    remap_image(image.get(), remap);
    if (!count_diff_between_images(image.get(), anchor->image())) continue;
    // Keep links inside this scope, separating any links to another palette.
    // UnlinkCel anchors the new CelData so SetCelData can resolve it on redo.
    if (dataUsers[entry.first] > int(linked.size())) {
      tx(new cmd::UnlinkCel(anchor));
      for (size_t i = 1; i < linked.size(); ++i)
        tx(new cmd::SetCelData(linked[i], anchor->dataRef()));
    }
    tx(new cmd::SetCelImage(anchor, image));
  }

  for (const auto& entry : tilemaps) {
    auto* original = entry.second.front()->tileset();
    std::unique_ptr<Tileset> copy(Tileset::MakeCopyCopyingImages(original));
    bool changed = false;
    for (tile_index i = 0; i < copy->size(); ++i) {
      auto image = copy->get(i);
      if (!image || image->pixelFormat() != IMAGE_INDEXED) continue;
      if (bank >= 0 && !is_local_bank_image(image.get()))
        throw std::runtime_error("Palette-bank tiles must use local indices 0-15 before remapping.");
      remap_image(image.get(), remap);
      changed |= count_diff_between_images(image.get(), original->get(i).get()) != 0;
      copy->notifyTileContentChange(i);
    }
    if (!changed) continue;
    bool shared = false;
    for (const auto* layer : sprite->allTilemaps())
      if (layer->tileset() == original && !matches(layer)) shared = true;
    if (shared) {
      auto* add = new cmd::AddTileset(sprite, copy.release());
      tx(add);
      for (auto* layer : entry.second)
        tx(new cmd::SetLayerTileset(layer, add->tilesetIndex()));
    }
    else
      tx(new cmd::ReplaceTileset(sprite, entry.first, copy.release()));
  }
}
} // namespace app
