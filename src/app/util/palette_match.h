// Color matching for explicit palette-bank remapping.
#pragma once

#include "doc/rgbmap_base.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace app {

enum class PaletteMatch { Existing, ClosestColor, Lightness, Hue };

// Search the small destination bank directly. Oklab separates lightness from
// chroma, and gives hue a continuous distance across the red hue boundary.
class PaletteMatchMap : public doc::RgbMapBase {
public:
  explicit PaletteMatchMap(PaletteMatch priority) : m_priority(priority) {}

  void regenerateMap(const doc::Palette* palette, int mask,
                     doc::FitCriteria fit) override
  {
    m_palette = palette;
    m_maskIndex = mask;
    m_fitCriteria = fit;
    m_colors.clear();
    for (int i = 0; i < palette->size(); ++i)
      m_colors.push_back(lab(palette->getEntry(i)));
  }
  void regenerateMap(const doc::Palette* palette, int mask) override
  {
    regenerateMap(palette, mask, doc::FitCriteria::DEFAULT);
  }
  doc::RgbMapAlgorithm rgbmapAlgorithm() const override
  {
    return doc::RgbMapAlgorithm::DEFAULT;
  }
  int mapColor(doc::color_t color) const override
  {
    if (doc::rgba_geta(color) == 0 && m_maskIndex >= 0)
      return m_maskIndex;
    const auto source = lab(color);
    int best = 0;
    double lowest = std::numeric_limits<double>::max();
    for (int i = 0; i < int(m_colors.size()); ++i) {
      if (i == m_maskIndex || !doc::rgba_geta(m_palette->getEntry(i)))
        continue;
      const auto& target = m_colors[i];
      const double dl = source.l - target.l;
      const double da = source.a - target.a;
      const double db = source.b - target.b;
      const double dc = source.chroma - target.chroma;
      const double alpha = (doc::rgba_geta(color) -
                            doc::rgba_geta(m_palette->getEntry(i))) / 255.0;
      double distance = dl * dl + da * da + db * db + alpha * alpha;
      if (m_priority == PaletteMatch::Lightness)
        distance += 7.0 * dl * dl;
      else if (m_priority == PaletteMatch::Hue)
        distance += 7.0 * std::max(0.0, da * da + db * db - dc * dc);
      if (distance < lowest) {
        best = i;
        lowest = distance;
      }
    }
    return best;
  }

public:
  struct Lab { double l, a, b, chroma; };
  static Lab lab(doc::color_t color)
  {
    const auto linear = [](int channel) {
      const double c = channel / 255.0;
      return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const double r = linear(doc::rgba_getr(color));
    const double g = linear(doc::rgba_getg(color));
    const double b = linear(doc::rgba_getb(color));
    const double l = std::cbrt(0.4122214708*r + 0.5363325363*g + 0.0514459929*b);
    const double m = std::cbrt(0.2119034982*r + 0.6806995451*g + 0.1073969566*b);
    const double s = std::cbrt(0.0883024619*r + 0.2817188376*g + 0.6299787005*b);
    Lab result{ 0.2104542553*l + 0.7936177850*m - 0.0040720468*s,
                1.9779984951*l - 2.4285922050*m + 0.4505937099*s,
                0.0259040371*l + 0.7827717662*m - 0.8086757660*s, 0.0 };
    result.chroma = std::hypot(result.a, result.b);
    return result;
  }
private:
  PaletteMatch m_priority;
  std::vector<Lab> m_colors;
};
} // namespace app
