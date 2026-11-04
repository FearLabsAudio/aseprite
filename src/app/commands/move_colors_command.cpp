// Aseprite
// Copyright (C) 2019  Igara Studio S.A.
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/commands/cmd_set_palette.h"
#include "app/commands/new_params.h"
#include "app/context.h"
#include "app/context_access.h"
#include "app/doc_api.h"
#include "app/pref/preferences.h"
#include "app/tx.h"
#include "app/util/pal_ops.h"
#include "app/util/palette_edit.h"
#include "doc/palette_bank.h"
#include "doc/palette.h"
#include "doc/palette_picks.h"
#include "doc/remap.h"

namespace app {

using namespace ui;

struct MoveColorsParams : public NewParams {
  Param<int> before{ this, 0, "before" };
};

class MoveColorsCommand : public CommandWithNewParams<MoveColorsParams> {
public:
  MoveColorsCommand(bool copy)
    : CommandWithNewParams<MoveColorsParams>(
        (copy ? CommandId::CopyColors() : CommandId::MoveColors()))
    , m_copy(copy)
  {
  }

protected:
  bool onEnabled(Context* ctx) override
  {
    return ctx->checkFlags(ContextFlags::ActiveDocumentIsWritable |
                           ContextFlags::HasSelectedColors);
  }

  void onExecute(Context* ctx) override
  {
    ContextWriter writer(ctx);
    Site site = ctx->activeSite();

    PalettePicks picks = site.selectedColors();
    if (picks.picks() == 0)
      return; // Do nothing

    ASSERT(writer.palette());
    if (!writer.palette())
      return;

    Tx tx(writer, friendlyName(), ModifyDocument);
    const int beforeIndex = params().before();
    int currentEntry = picks.firstPick();

    if (ctx->isUIAvailable()) {
      auto& fgColor = Preferences::instance().colorBar.fgColor;
      if (fgColor().getType() == app::Color::IndexType)
        currentEntry = fgColor().getIndex();
    }

    doc::Palette palette(*writer.palette());
    doc::Palette newPalette(palette);
    if (doc::uses_local_palette(writer.layer())) {
      picks.resize(16);
      // Copy inserts entries in stock palettes. Fixed-size banks instead
      // replace destination swatches, just like pasting colors into a bank.
      if (beforeIndex < 0 || beforeIndex > 16 ||
          (m_copy && beforeIndex + picks.picks() > 16))
        throw std::runtime_error("Choose a destination within the bank's 16 slots.");
      if (m_copy) {
        if (beforeIndex == 0 && !doc::force_karn_rules(writer.sprite()))
          throw std::runtime_error("Slot 0 is transparent. Choose a destination from 1-15.");
        const auto source = picks;
        const int sourceEntry = currentEntry;
        int destination = beforeIndex;
        picks.clear();
        for (int i = 0; i < source.size(); ++i) {
          if (!source[i]) continue;
          newPalette.setEntry(destination, palette.getEntry(i));
          picks[destination] = true;
          if (sourceEntry == i) currentEntry = destination;
          ++destination;
        }
      }
      else
        move_or_copy_palette_colors(palette, newPalette, picks, currentEntry, beforeIndex, false);
    }
    else
      move_or_copy_palette_colors(palette, newPalette, picks, currentEntry, beforeIndex, m_copy);

    if (writer.layer()) {
      if (auto* edit = make_palette_edit(writer.layer(), writer.frame(), newPalette)) tx(edit);
    }
    else
      writer.document()->getApi(tx).setPalette(writer.sprite(), writer.frame(), &newPalette);

    ctx->setSelectedColors(picks);

    if (ctx->isUIAvailable()) {
      auto& fgColor = Preferences::instance().colorBar.fgColor;
      if (fgColor().getType() == app::Color::IndexType)
        fgColor(Color::fromIndex(currentEntry));
    }

    tx.commit();
  }

private:
  bool m_copy;
};

Command* CommandFactory::createMoveColorsCommand()
{
  return new MoveColorsCommand(false);
}

Command* CommandFactory::createCopyColorsCommand()
{
  return new MoveColorsCommand(true);
}

} // namespace app
