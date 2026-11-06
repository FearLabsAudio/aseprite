// Adjust a private RGB source before palette matching and dithering.
#pragma once

#include "doc/image.h"
#include "doc/image_ref.h"
#include "render/task_delegate.h"
#include <algorithm>
#include <array>

namespace app {
struct RgbConversionAdjustment {
  // Same normalized -1..1 controls and channel curve as BrightnessContrastFilter.
  double brightness = 0.0;
  double contrast = 0.0;

  // Null means no adjustment is needed; the caller can use its original image.
  doc::ImageRef makeAdjustedCopy(const doc::Image* source,
                                 render::TaskDelegate* task = nullptr) const
  {
    if (source->pixelFormat() != doc::IMAGE_RGB || (brightness == 0.0 && contrast == 0.0))
      return nullptr;
    std::array<int, 256> values;
    const double b = std::clamp(brightness, -1.0, 1.0);
    const double c = std::clamp(contrast, -1.0, 1.0);
    for (int i = 0; i < 256; ++i) {
      const double value = ((c + 1.0) * (i / 255.0 - 0.5) + 0.5) * (1.0 + b);
      values[i] = int(255.5 * std::clamp(value, 0.0, 1.0));
    }
    doc::ImageRef adjusted(doc::Image::createCopy(source));
    for (int y = 0; y < adjusted->height(); ++y) {
      if (task && !task->continueTask()) break;
      for (int x = 0; x < adjusted->width(); ++x) {
        const auto pixel = source->getPixel(x, y);
        // Retain hidden RGB data in fully transparent pixels too.
        if (doc::rgba_geta(pixel))
          adjusted->putPixel(x, y, doc::rgba(values[doc::rgba_getr(pixel)],
            values[doc::rgba_getg(pixel)], values[doc::rgba_getb(pixel)], doc::rgba_geta(pixel)));
      }
    }
    return adjusted;
  }
};
} // namespace app
