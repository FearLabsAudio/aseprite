// Aseprite KARN modifications
// Distributed under the terms of Aseprite's End-User License Agreement.
#pragma once

#include "app/ui/dockable.h"
#include "ui/splitter.h"
#include <memory>

namespace app {
class ColorBar;

// Keeps the palette and presets together in saved/customized workspace layouts.
class ToolPresetsDock : public ui::Splitter,
                        public Dockable {
public:
  explicit ToolPresetsDock(ColorBar* colorBar);
  ~ToolPresetsDock();
  void restore();
  void toggle();
  void fitDockWidth();
  bool isPanelVisible() const;
  int dockableAt() const override { return ui::LEFT | ui::RIGHT | ui::EXPANSIVE; }

protected:
  void onPositionChange() override;
  void onSizeHint(ui::SizeHintEvent& ev) override;
  void onResize(ui::ResizeEvent& ev) override;
  // The "colorbar" ID identifies this sidebar in workspace layouts. Keep
  // splitter state in ToolPresets instead of the generic widget-layout slot.
  void onLoadLayout(ui::LoadLayoutEvent&) override {}
  void onSaveLayout(ui::SaveLayoutEvent&) override {}

private:
  void showPanel(bool visible);
  void setFloating(bool floating);
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace app
