// KARN palette generation and conversion share the same legal color candidates.
#pragma once
#include "app/util/palette_match.h"
#include "app/util/rgb_conversion_adjustment.h"
#include "doc/palette_rgb444.h"
#include "render/dithering.h"
#include "render/quantization.h"
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace app {
struct OptimalPaletteSettings {
  bool enabled = false;
  int colors = 13;
  bool ditherTransparency = false;
  int alphaCutoff = 128;
  int transparencyDitherAmount = 100;
};
struct OptimalPaletteResult {
  doc::Palette palette{ 0, 16 };
  int colors = 0;
  int used = 0;
};
inline void optimal_check(render::TaskDelegate* task)
{
  if (task && !task->continueTask())
    throw std::runtime_error("Palette calculation canceled");
}
inline doc::ImageRef optimal_source(const doc::Image* image,
                                    const OptimalPaletteSettings& settings,
                                    const RgbConversionAdjustment& adjustment,
                                    render::TaskDelegate* task)
{
  auto result = adjustment.makeAdjustedCopy(image, task);
  if (!result)
    result.reset(doc::Image::createCopy(image));
  const int amount = settings.ditherTransparency ?
                       std::clamp(settings.transparencyDitherAmount, 0, 100) : 0;
  const int cutoff = settings.ditherTransparency ? 128 : settings.alphaCutoff;
  const int width = result->width();
  // Two scanlines with a padding pixel at each edge to absorb out-of-bounds error.
  std::vector<int> error(amount ? width + 2 : 0, 0);
  std::vector<int> nextError(error.size(), 0);
  for (int y = 0; y < result->height(); ++y) {
    optimal_check(task);
    // Mirror the Floyd–Steinberg kernel on alternating rows, as for RGB.
    const int step = (y & 1) ? -1 : 1;
    for (int x = step > 0 ? 0 : width - 1; x >= 0 && x < width; x += step) {
      const auto color = result->getPixel(x, y);
      const int i = x + 1;
      int alpha = doc::rgba_geta(color);
      if (amount)
        alpha = std::clamp(alpha + error[i], 0, 255);
      const int quantized = alpha > 0 && alpha >= cutoff ? 255 : 0;
      result->putPixel(x, y, quantized ? doc::rgba_seta(color, quantized) : 0);
      if (amount) {
        const int diff = (alpha - quantized) * amount / 100;
        error[i + step] += diff * 7 / 16;
        nextError[i - step] += diff * 3 / 16;
        nextError[i] += diff * 5 / 16;
        nextError[i + step] += diff / 16;
      }
    }
    error.swap(nextError);
    std::fill(nextError.begin(), nextError.end(), 0);
  }
  return result;
}
inline doc::ImageRef optimal_map(const doc::Image* prepared,
                                 const OptimalPaletteResult& result,
                                 const render::Dithering& dithering,
                                 render::TaskDelegate* task)
{
  doc::Palette compact(0, result.colors + 1);
  compact.setEntry(0, 0);
  for (int i = 0; i < result.colors; ++i)
    compact.setEntry(i + 1, result.palette.getEntry(i + 3));
  PaletteMatchMap map(PaletteMatch::ClosestColor);
  map.regenerateMap(&compact, 0);
  doc::ImageRef image(render::convert_pixel_format(prepared,
                                                   nullptr,
                                                   doc::IMAGE_INDEXED,
                                                   dithering,
                                                   &map,
                                                   &compact,
                                                   false,
                                                   0,
                                                   nullptr,
                                                   task));
  for (int y = 0; y < image->height(); ++y) {
    optimal_check(task);
    for (int x = 0; x < image->width(); ++x) {
      const auto c = image->getPixel(x, y);
      if (c)
        image->putPixel(x, y, c + 2);
    }
  }
  return image;
}
inline OptimalPaletteResult optimal_palette(const std::vector<doc::ImageRef>& images,
                                            const doc::Palette& existing,
                                            const OptimalPaletteSettings& settings,
                                            const RgbConversionAdjustment& adjustment,
                                            const render::Dithering& dithering,
                                            render::TaskDelegate* task = nullptr)
{
  using Lab = PaletteMatchMap::Lab;
  const auto distance = [](const Lab& a, const Lab& b) {
    return (a.l - b.l) * (a.l - b.l) + (a.a - b.a) * (a.a - b.a) + (a.b - b.b) * (a.b - b.b);
  };
  std::vector<doc::ImageRef> prepared;
  std::map<doc::color_t, double> histogram;
  for (const auto& image : images) {
    auto source = optimal_source(image.get(), settings, adjustment, task);
    for (int y = 0; y < source->height(); ++y)
      for (int x = 0; x < source->width(); ++x) {
        auto c = source->getPixel(x, y);
        if (doc::rgba_geta(c))
          histogram[c] += 1.0;
      }
    prepared.push_back(source);
  }
  OptimalPaletteResult result;
  for (int i = 0; i < 16; ++i)
    result.palette.setEntry(i, i < existing.size() ? existing.getEntry(i) : 0);
  // Palette alpha is opaque on VERA; transparency belongs to pixel index zero.
  for (int i = 3; i < 16; ++i)
    result.palette.setEntry(i, doc::rgba(0, 0, 0, 255));
  if (histogram.empty())
    return result;
  struct Sample {
    Lab lab;
    double weight;
    int candidate;
  };
  std::vector<Sample> samples;
  for (const auto& p : histogram) {
    auto c = doc::snap_rgb444(p.first);
    samples.push_back(
      { PaletteMatchMap::lab(p.first),
        p.second,
        doc::rgba_getr(c) / 17 + (doc::rgba_getg(c) / 17) * 16 + (doc::rgba_getb(c) / 17) * 256 });
  }
  std::array<Lab, 4096> legal;
  const auto color = [](int i) {
    return doc::rgba((i & 15) * 17, ((i >> 4) & 15) * 17, ((i >> 8) & 15) * 17, 255);
  };
  for (int i = 0; i < 4096; ++i)
    legal[i] = PaletteMatchMap::lab(color(i));
  std::vector<int> centers;
  const auto nearestLegal = [&](const Lab& lab, const std::vector<int>& exclude) {
    int best = -1;
    double error = 1e100;
    for (int i = 0; i < 4096; ++i) {
      if (std::find(exclude.begin(), exclude.end(), i) != exclude.end())
        continue;
      const double d = distance(lab, legal[i]);
      if (d < error) {
        error = d;
        best = i;
      }
    }
    return best;
  };
  const int count = std::clamp(settings.colors, 1, 13);
  // Deterministic weighted farthest-error seeds, followed by constrained Lloyd refinement.
  for (int k = 0; k < count; ++k) {
    optimal_check(task);
    double worst = -1;
    int chosen = -1;
    for (const auto& s : samples) {
      double error = centers.empty() ? 1.0 : 1e100;
      for (int c : centers)
        error = std::min(error, distance(s.lab, legal[c]));
      const int candidate = s.candidate;
      if (std::find(centers.begin(), centers.end(), candidate) != centers.end())
        continue;
      if (error * s.weight > worst) {
        worst = error * s.weight;
        chosen = candidate;
      }
    }
    if (chosen < 0)
      break;
    centers.push_back(chosen);
  }
  for (int iteration = 0; iteration < 12; ++iteration) {
    optimal_check(task);
    std::vector<Lab> sums(centers.size(), Lab{ 0, 0, 0, 0 });
    std::vector<double> weights(centers.size(), 0);
    for (const auto& s : samples) {
      int best = 0;
      double error = 1e100;
      for (int j = 0; j < int(centers.size()); ++j) {
        double d = distance(s.lab, legal[centers[j]]);
        if (d < error) {
          error = d;
          best = j;
        }
      }
      sums[best].l += s.lab.l * s.weight;
      sums[best].a += s.lab.a * s.weight;
      sums[best].b += s.lab.b * s.weight;
      weights[best] += s.weight;
    }
    std::vector<int> next;
    for (int j = 0; j < int(centers.size()); ++j) {
      Lab mean = legal[centers[j]];
      if (weights[j])
        mean = { sums[j].l / weights[j], sums[j].a / weights[j], sums[j].b / weights[j], 0 };
      next.push_back(nearestLegal(mean, next));
    }
    if (next == centers)
      break;
    centers = next;
  }
  // Validate actual dithered output. Remove unused entries and refill from the
  // largest remaining source error; bounded passes keep interactive work finite.
  for (int pass = 0; pass < 20; ++pass) {
    result.colors = int(centers.size());
    for (int j = 0; j < result.colors; ++j)
      result.palette.setEntry(j + 3, color(centers[j]));
    std::set<int> used;
    for (const auto& source : prepared) {
      auto indexed = optimal_map(source.get(), result, dithering, task);
      for (int y = 0; y < indexed->height(); ++y)
        for (int x = 0; x < indexed->width(); ++x)
          if (auto c = indexed->getPixel(x, y))
            used.insert(c - 3);
    }
    result.used = int(used.size());
    if (result.used == result.colors)
      break;
    std::vector<int> live;
    for (int j : used)
      live.push_back(centers[j]);
    centers = live;
    if (pass < 4) {
      double worst = 0;
      int candidate = -1;
      for (const auto& s : samples) {
        double error = 1e100;
        for (int c : centers)
          error = std::min(error, distance(s.lab, legal[c]));
        int c = s.candidate;
        if (std::find(centers.begin(), centers.end(), c) != centers.end())
          continue;
        if (error * s.weight > worst) {
          worst = error * s.weight;
          candidate = c;
        }
      }
      if (candidate >= 0)
        centers.push_back(candidate);
    }
  }
  // Inactive bank slots are padding, not additional generated shades. Clear
  // colors left by pruning so the destination cannot show stale candidates.
  for (int i = 3 + result.colors; i < 16; ++i)
    result.palette.setEntry(i, doc::rgba(0, 0, 0, 255));
  return result;
}
} // namespace app
