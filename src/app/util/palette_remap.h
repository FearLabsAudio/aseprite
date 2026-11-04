// Palette remapping scoped to a numbered bank or the Document Palette.
#ifndef APP_UTIL_PALETTE_REMAP_H_INCLUDED
#define APP_UTIL_PALETTE_REMAP_H_INCLUDED
#pragma once

namespace doc { class Sprite; class Palette; }
namespace app {
class Tx;
bool uses_scoped_palette_remap(const doc::Sprite* sprite);
// Rewrites pixels in all frames using this palette, retaining the mask index.
// The palette edit itself precedes this operation, as in stock Aseprite.
void remap_palette_bank(Tx& tx, doc::Sprite* sprite, int bank,
                        const doc::Palette& previous, const doc::Palette& current);
}
#endif
