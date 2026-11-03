// Aseprite
// Copyright (C) 2019-2023  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/commands/filters/filter_target_buttons.h"

#include "app/i18n/strings.h"
#include "app/modules/gfx.h"
#include "app/modules/gui.h"
#include "app/ui/skin/skin_theme.h"
#include "doc/image.h"
#include "fmt/format.h"
#include "ui/box.h"
#include "ui/button.h"
#include "ui/label.h"
#include "ui/theme.h"
#include "ui/widget.h"

#include <cstring>

namespace app {

using namespace app::skin;
using namespace doc;
using namespace filters;
using namespace ui;

FilterTargetButtons::FilterTargetButtons(int imgtype, WithChannels channels)
  : ButtonSet(4, true)
  , m_target(0)
  , m_celsTarget(CelsTarget::Selected)
  , m_red(nullptr)
  , m_green(nullptr)
  , m_blue(nullptr)
  , m_alpha(nullptr)
  , m_gray(nullptr)
  , m_index(nullptr)
{
  setMultiMode(MultiMode::Set);
  addChild(&m_tooltips);

  if (channels != WithoutChannelsSelector) {
    switch (imgtype) {
      case IMAGE_RGB:
      case IMAGE_INDEXED:
        m_red = addItem("R");
        m_green = addItem("G");
        m_blue = addItem("B");
        m_alpha = addItem("A");

        if (imgtype == IMAGE_INDEXED && channels == WithChannelsSelector) {
          m_index = addItem(Strings::filters_palette_index(), 4, 1);
          m_tooltips.addTooltipFor(m_index, Strings::filters_palette_index_tooltip(), LEFT);
        }
        break;

      case IMAGE_GRAYSCALE:
        m_gray = addItem("K", 2, 1);
        m_alpha = addItem("A", 2, 1);
        break;
    }
  }

  addChildInCell(new Label(Strings::layer_combo_layer()), 2, 1, LEFT | MIDDLE);
  m_scope = addItem(Strings::filters_selected_cels(), 2, 1);
  m_scope->InitTheme.connect([this] {
    // Reserve room for either localized value when opening with All active.
    m_scope->setText(Strings::filters_selected_cels());
    const auto selectedSize = m_scope->sizeHint();
    m_scope->setText(Strings::filters_all_cels());
    m_scope->setMinSize(selectedSize.createUnion(m_scope->sizeHint()));
    setCelsTarget(m_celsTarget);
  });
  setCelsTarget(m_celsTarget);
  initTheme();
}

void FilterTargetButtons::setTarget(const int target)
{
  m_target = target;
  selectTargetButton(m_red, TARGET_RED_CHANNEL);
  selectTargetButton(m_green, TARGET_GREEN_CHANNEL);
  selectTargetButton(m_blue, TARGET_BLUE_CHANNEL);
  selectTargetButton(m_alpha, TARGET_ALPHA_CHANNEL);
  selectTargetButton(m_gray, TARGET_GRAY_CHANNEL);
  selectTargetButton(m_index, TARGET_INDEX_CHANNEL);
  updateFromTarget();
}

void FilterTargetButtons::setCelsTarget(const CelsTarget celsTarget)
{
  m_celsTarget = celsTarget;
  const bool all = (celsTarget == CelsTarget::All);
  m_scope->setText(all ? Strings::filters_all_cels() : Strings::filters_selected_cels());
  m_scope->setSelected(true);
  m_tooltips.addTooltipFor(m_scope,
                           all ? Strings::filters_all_cels_tooltip() :
                                 Strings::filters_selected_cels_tooltip(),
                           LEFT);
}

void FilterTargetButtons::selectTargetButton(Item* item, Target specificTarget)
{
  if (item)
    item->setSelected((m_target & specificTarget) == specificTarget);
}

void FilterTargetButtons::updateFromTarget()
{
  updateComponentTooltip(m_red, "Red", BOTTOM);
  updateComponentTooltip(m_green, "Green", BOTTOM);
  updateComponentTooltip(m_blue, "Blue", BOTTOM);
  updateComponentTooltip(m_gray, "Gray", BOTTOM);
  updateComponentTooltip(m_alpha, "Alpha", BOTTOM);
}

void FilterTargetButtons::updateComponentTooltip(Item* item, const char* channelName, int align)
{
  if (item) {
    std::string buf =
      fmt::format("{} {} Component", (item->isSelected() ? "Modify" : "Ignore"), channelName);
    m_tooltips.addTooltipFor(item, buf, align);
  }
}

void FilterTargetButtons::onItemChange(Item* item)
{
  ButtonSet::onItemChange(item);

  if (item == m_scope) {
    setCelsTarget(m_celsTarget == CelsTarget::Selected ? CelsTarget::All : CelsTarget::Selected);
    TargetChange();
    return;
  }

  if (m_index && item && item->isSelected()) {
    if (item == m_index) {
      m_red->setSelected(false);
      m_green->setSelected(false);
      m_blue->setSelected(false);
      m_alpha->setSelected(false);
    }
    else if (item == m_red || item == m_green || item == m_blue || item == m_alpha) {
      m_index->setSelected(false);
    }
  }

  Target target = 0;
  if (m_red && m_red->isSelected())
    target |= TARGET_RED_CHANNEL;
  if (m_green && m_green->isSelected())
    target |= TARGET_GREEN_CHANNEL;
  if (m_blue && m_blue->isSelected())
    target |= TARGET_BLUE_CHANNEL;
  if (m_gray && m_gray->isSelected())
    target |= TARGET_GRAY_CHANNEL;
  if (m_index && m_index->isSelected())
    target |= TARGET_INDEX_CHANNEL;
  if (m_alpha && m_alpha->isSelected())
    target |= TARGET_ALPHA_CHANNEL;

  if (m_target != target) {
    m_target = target;
    updateFromTarget();
    TargetChange();
  }
}

} // namespace app
