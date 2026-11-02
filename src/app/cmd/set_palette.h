// Aseprite
// Copyright (C) 2019-2021  Igara Studio S.A.
// Copyright (C) 2001-2015  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_CMD_SET_PALETTE_H_INCLUDED
#define APP_CMD_SET_PALETTE_H_INCLUDED
#pragma once

#include "app/cmd.h"
#include "app/cmd/with_sprite.h"
#include "doc/color.h"
#include "doc/frame.h"
#include "doc/user_data.h"
#include <optional>

#include <vector>

namespace doc {
class Palette;
class Sprite;
} // namespace doc

namespace app { namespace cmd {
using namespace doc;

class SetPalette : public Cmd,
                   public WithSprite {
public:
  SetPalette(Sprite* sprite, frame_t frame, const Palette* newPalette,
             bool constrainChannels = true, bool bankZero = false);

protected:
  void onExecute() override;
  void onUndo() override;
  void onFireNotifications() override;
  size_t onMemSize() const override
  {
    size_t size = sizeof(*this) + sizeof(doc::color_t) * (m_oldColors.size() + m_newColors.size());
    for (const auto* palettes : { &m_oldBankZero, &m_newBankZero }) {
      if (!*palettes) continue;
      for (const auto& entry : **palettes) {
        size += sizeof(entry) + entry.first.size();
        if (const auto* colors = std::get_if<doc::UserData::Vector>(&entry.second))
          size += sizeof(doc::UserData::Variant) * colors->size();
      }
    }
    return size;
  }

private:
  frame_t m_frame;
  int m_from, m_to;
  int m_oldNColors;
  int m_newNColors;
  int m_oldTransparentIndex;
  int m_newTransparentIndex;
  std::vector<color_t> m_oldColors;
  std::vector<color_t> m_newColors;
  std::optional<doc::UserData::Properties> m_oldBankZero, m_newBankZero;
  bool m_changeBankZero = false;
};

}} // namespace app::cmd

#endif
