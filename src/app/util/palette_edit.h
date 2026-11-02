#ifndef APP_UTIL_PALETTE_EDIT_H_INCLUDED
#define APP_UTIL_PALETTE_EDIT_H_INCLUDED
#pragma once

#include "app/cmd/set_palette.h"
#include "doc/palette_bank.h"
#include "doc/palette_rgb444.h"

namespace app {
// The caller owns the returned command. Null means no colors changed.
inline cmd::SetPalette* make_palette_edit(doc::Layer* layer, doc::frame_t frame,
                                          const doc::Palette& local)
{
  if (!layer) return nullptr;
  auto* sprite = layer->sprite();
  const bool zero = layer->paletteBank() == 0;
  doc::Palette edited = zero ? doc::make_bank_palette(sprite, frame, 0) :
                               doc::merge_bank_palette(layer, frame, local);
  if (zero)
    for (int i = 1; i < 16 && i < local.size(); ++i) edited.setEntry(i, local.getEntry(i));
  if (sprite->pixelFormat() == doc::IMAGE_INDEXED && doc::force_rgb444(sprite)) doc::snap_rgb444(edited);
  const doc::Palette current = zero ? doc::make_bank_palette(sprite, frame, 0) :
                                      *sprite->palette(frame);
  return edited == current ? nullptr : new cmd::SetPalette(sprite, frame, &edited, true, zero);
}
}
#endif
