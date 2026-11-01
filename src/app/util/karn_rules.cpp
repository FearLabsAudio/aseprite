#include "app/util/karn_rules.h"

#include "app/cmd/add_tileset.h"
#include "app/cmd/replace_image.h"
#include "app/cmd/set_cel_image.h"
#include "app/cmd/set_grid_bounds.h"
#include "app/cmd/set_layer_flags.h"
#include "app/cmd/set_layer_tileset.h"
#include "app/cmd/set_palette.h"
#include "app/cmd/set_pixel_format.h"
#include "app/cmd/set_tileset_match_flags.h"
#include "app/cmd/set_transparent_color.h"
#include "app/tx.h"
#include "app/util/cel_ops.h"
#include "doc/cel.h"
#include "doc/cels_range.h"
#include "doc/karn_rules.h"
#include "doc/layer_tilemap.h"
#include "doc/palette_bank.h"
#include "doc/palette_rgb444.h"
#include "doc/tilesets.h"
#include "render/dithering.h"
#include "render/quantization.h"

#include <climits>
#include <functional>
#include <memory>
#include <set>

namespace app {
using namespace doc;
namespace {
void images(const Sprite* s, const std::function<void(const Image*)>& visit)
{
  std::set<ObjectId> seen;
  const auto once = [&](const Image* image) {
    if (image && seen.insert(image->id()).second)
      visit(image);
  };
  for (const auto* l : s->allLayers()) {
    if (!l->isImage() || l->isReference() || l->isRgbReference())
      continue;
    for (const auto* c : l->cels())
      once(c->image());
    if (l->isTilemap() && l->tileset())
      for (const auto& tile : *l->tileset())
        once(tile.image.get());
  }
}

bool has_diagonal(const Image* image)
{
  for (int y = 0; y < image->height(); ++y)
    for (int x = 0; x < image->width(); ++x)
      if (tile_getf(image->getPixel(x, y)) & tile_f_dflip)
        return true;
  return false;
}

int nearest_opaque(const Palette& palette, color_t color, int bank)
{
  int best = 1, distance = INT_MAX;
  for (int i = 1; i < 16; ++i) {
    const color_t target = palette.getEntry(bank * 16 + i);
    const int r = rgba_getr(color) - rgba_getr(target);
    const int g = rgba_getg(color) - rgba_getg(target);
    const int b = rgba_getb(color) - rgba_getb(target);
    const int d = r * r + g * g + b * b;
    if (d < distance) {
      best = i;
      distance = d;
    }
  }
  return best;
}
} // namespace

KarnImageVersions karn_image_versions(const Sprite* sprite)
{
  KarnImageVersions result;
  images(sprite, [&](const Image* image) { result[image->id()] = image->version(); });
  return result;
}

std::string karn_rules_violation(const Sprite* s, const KarnImageVersions& unchanged)
{
  if (s->pixelFormat() != IMAGE_INDEXED)
    return "KARN rules require Indexed color mode.";
  if (s->transparentColor() != 0 || s->backgroundLayer())
    return "KARN rules require index 0 to be transparent (no opaque background layer).";
  if (s->gridBounds().size() != gfx::Size(8, 8))
    return "KARN rules require an 8 x 8 pixel grid.";
  for (const auto* p : s->getPalettes()) {
    if (p->size() < 16 || p->size() > 256 || p->size() % 16)
      return "KARN palettes require complete 16-color banks (up to 256 entries).";
    for (int i = 0; i < p->size(); ++i)
      if (snap_rgb444(p->getEntry(i)) != p->getEntry(i))
        return "KARN rules require 4-bit color channels.";
  }
  for (const auto* l : s->allTilemaps()) {
    const auto* ts = l->tileset();
    if (!ts || ts->grid().tileSize() != gfx::Size(8, 8) || ts->matchFlags() != kKarnMatchFlags)
      return "KARN tilemaps require 8 x 8 tiles with horizontal/vertical matching only.";
  }
  std::string error;
  images(s, [&](const Image* image) {
    if (!error.empty())
      return;
    const auto it = unchanged.find(image->id());
    if (it != unchanged.end() && it->second == image->version())
      return;
    if (image->pixelFormat() == IMAGE_TILEMAP) {
      if (has_diagonal(image))
        error = "KARN rules allow horizontal and vertical tile flips, but not diagonal flips.";
    }
    else if (!is_local_bank_image(image))
      error = "KARN rules restrict artwork to local palette indices 0-15.";
  });
  return error;
}

void apply_karn_rules(Tx& tx, Sprite* s)
{
  // RGB/grayscale artwork gets a shared 15-color palette plus transparency.
  // Existing indexed palettes/valid local indices are kept in their original order.
  if (s->pixelFormat() != IMAGE_INDEXED) {
    Palette palette(0, 16);
    render::create_palette_from_sprite(s,
                                       0,
                                       s->lastFrame(),
                                       false,
                                       &palette,
                                       nullptr,
                                       true,
                                       RgbMapAlgorithm::OCTREE);
    snap_rgb444(palette);
    for (const auto* p : s->getPalettes())
      if (palette.countDiff(p, nullptr, nullptr))
        tx(new cmd::SetPalette(s, p->frame(), &palette));
    tx(new cmd::SetPixelFormat(s,
                               IMAGE_INDEXED,
                               render::Dithering(),
                               RgbMapAlgorithm::OCTREE,
                               nullptr,
                               nullptr,
                               FitCriteria::DEFAULT));
  }

  // Pad partial banks before mapping pixels; never discard an existing bank.
  for (const auto* p : s->getPalettes()) {
    Palette palette(*p);
    palette.resize(std::max(16, ((p->size() + 15) / 16) * 16));
    snap_rgb444(palette);
    if (palette.countDiff(p, nullptr, nullptr))
      tx(new cmd::SetPalette(s, p->frame(), &palette));
  }
  const int oldMask = s->transparentColor();
  std::set<ObjectId> normalized;
  const auto normalize = [&](const ImageRef& image, frame_t frame, int bank, bool background) {
    if (!image || image->pixelFormat() != IMAGE_INDEXED || !normalized.insert(image->id()).second)
      return;
    ImageRef copy(Image::createCopy(image.get()));
    bool changed = false;
    const Palette& pal = *s->palette(frame);
    for (int y = 0; y < image->height(); ++y) {
      for (int x = 0; x < image->width(); ++x) {
        const int old = image->getPixel(x, y);
        int value = old;
        if (!background && old == oldMask)
          value = 0;
        else if (old > 15 || old == 0) {
          const int global = old < 16 ? bank * 16 + old : old;
          value = nearest_opaque(pal, pal.getEntry(std::min(global, pal.size() - 1)), bank);
        }
        if (value != old) {
          copy->putPixel(x, y, value);
          changed = true;
        }
      }
    }
    copy->setMaskColor(0);
    if (changed) {
      tx(new cmd::ReplaceImage(s, image, copy));
      normalized.insert(copy->id());
    }
  };
  for (auto* l : s->allLayers()) {
    if (!l->isImage() || l->isReference() || l->isRgbReference())
      continue;
    const int bank = std::max(0, l->paletteBank());
    if (l->isTilemap()) {
      // Copy references because ReplaceImage updates the tileset as it runs.
      std::vector<ImageRef> tiles;
      for (const auto& t : *l->tileset())
        tiles.push_back(t.image);
      for (const auto& image : tiles)
        normalize(image, 0, bank, false);
    }
    else {
      std::vector<std::pair<ImageRef, frame_t>> cels;
      for (const auto* c : l->cels())
        cels.emplace_back(c->imageRef(), c->frame());
      for (const auto& c : cels)
        normalize(c.first, c.second, bank, l->isBackground());
    }
  }
  if (auto* background = s->backgroundLayer())
    tx(new cmd::SetLayerFlags(
      background,
      LayerFlags(int(background->flags()) & ~int(LayerFlags::BackgroundLayerFlags))));
  if (oldMask != 0)
    tx(new cmd::SetTransparentColor(s, 0));
  if (s->gridBounds().size() != gfx::Size(8, 8))
    tx(new cmd::SetGridBounds(s, gfx::Rect(s->gridBounds().origin(), gfx::Size(8, 8))));

  for (auto* l : s->allTilemaps()) {
    auto* ts = l->tileset();
    bool rebuild = ts->grid().tileSize() != gfx::Size(8, 8);
    for (const auto* c : l->cels())
      rebuild = rebuild || has_diagonal(c->image());
    if (!rebuild) {
      if (ts->matchFlags() != kKarnMatchFlags)
        tx(new cmd::SetTilesetMatchFlags(ts, kKarnMatchFlags));
      continue;
    }
    auto* replacement = new Tileset(s, Grid(gfx::Size(8, 8)), 1);
    replacement->setName(ts->name());
    replacement->setUserData(ts->userData());
    replacement->setBaseIndex(ts->baseIndex());
    replacement->setMatchFlags(kKarnMatchFlags);
    auto* add = new cmd::AddTileset(s, replacement);
    tx(add);
    // Keep the original layer/UUID and cel identities, including linked cels.
    LayerTilemap temporary(s, add->tilesetIndex());
    temporary.setUserData(l->userData());
    std::set<ObjectId> converted;
    std::vector<std::pair<Cel*, ImageRef>> cels;
    for (auto* c : l->cels()) {
      if (converted.insert(c->dataId()).second) {
        std::unique_ptr<Cel> copy(create_cel_copy(tx, c, s, &temporary, c->frame()));
        cels.emplace_back(c, copy->imageRef());
      }
    }
    tx(new cmd::SetLayerTileset(static_cast<LayerTilemap*>(l), add->tilesetIndex()));
    for (const auto& c : cels)
      tx(new cmd::SetCelImage(c.first, c.second));
  }
}
} // namespace app
