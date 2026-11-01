// Aseprite KARN modifications
// Distributed under the terms of Aseprite's End-User License Agreement.
#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/color_target.h"
#include "app/color_utils.h"
#include "app/commands/command.h"
#include "app/context.h"
#include "app/ini_file.h"
#include "app/modules/gfx.h"
#include "app/modules/gui.h"
#include "app/tool_presets.h"
#include "app/tools/active_tool.h"
#include "app/tools/active_tool_observer.h"
#include "app/tools/controller.h"
#include "app/tools/ink.h"
#include "app/tools/intertwine.h"
#include "app/tools/point_shape.h"
#include "app/tools/tool.h"
#include "app/tools/tool_box.h"
#include "app/ui/context_bar.h"
#include "app/ui/dithering_selector.h"
#include "app/ui/dock.h"
#include "app/ui/main_window.h"
#include "app/ui/palette_view.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui/tool_presets_dock.h"
#include "base/scoped_value.h"
#include "doc/image.h"
#include "doc/palette_picks.h"
#include "fmt/format.h"
#include "obs/connection.h"
#include "ui/int_entry.h"
#include "ui/ui.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <list>

namespace app {
namespace {
using namespace ui;

bool supports_preset(tools::Tool* tool)
{
  if (!tool || tool->getInk(0)->isText())
    return false;
  return tool->getInk(0)->isPaint() || tool->getInk(0)->isEffect() || tool->getInk(0)->isEraser();
}

ToolPreset capture_preset(bool copyBrush = true)
{
  auto* tool = App::instance()->activeToolManager()->selectedTool();
  auto* bar = App::instance()->contextBar();
  auto& pref = Preferences::instance();
  ToolPreset preset;
  preset.toolId = tool->getId();
  preset.name = tool->getText();
  preset.settings = capture_tool_settings(
    pref.tool(tool),
    pref.tool(pref.shared.shareDynamics() ? nullptr : tool).dynamics);
  preset.brush = bar->activeBrush(tool);
  if (copyBrush)
    preset.brush = preset.brush->cloneWithNewImages();
  preset.shade = bar->getShade();
  preset.hasColors = true;
  preset.foreground = pref.colorBar.fgColor();
  preset.background = pref.colorBar.bgColor();
  preset.gradientType = int(bar->gradientType());
  preset.gradientDithering = bar->gradientDitheringName();
  return preset;
}

doc::BrushRef brush_for_preset(const ToolPreset& preset)
{
  auto brush = preset.brush->cloneWithNewImages();
  if (!preset.ignoreColors && preset.hasColors && brush->type() == doc::kImageBrushType) {
    const ColorTarget target(ColorTarget::TransparentLayer, brush->image()->pixelFormat(), -1);
    brush->setImageColor(doc::Brush::ImageColor::MainColor,
                         color_utils::color_for_target_mask(preset.foreground, target));
    brush->setImageColor(doc::Brush::ImageColor::BackgroundColor,
                         color_utils::color_for_target_mask(preset.background, target));
  }
  return brush;
}

void recall_preset(const ToolPreset& preset)
{
  auto* tool = App::instance()->toolBox()->getToolById(preset.toolId);
  if (!tool || !supports_preset(tool)) {
    Alert::show("Tool Presets<<This preset's tool is not available.||&OK");
    return;
  }
  auto& pref = Preferences::instance();
  auto* bar = App::instance()->contextBar();
  const auto previousShade = bar->getShade();
  const auto previousForeground = pref.colorBar.fgColor();
  const auto previousBackground = pref.colorBar.bgColor();
  App::instance()->activeToolManager()->setSelectedTool(tool);
  auto& toolPref = pref.tool(tool);
  apply_tool_settings(preset.settings,
                      toolPref,
                      pref.tool(pref.shared.shareDynamics() ? nullptr : tool).dynamics);
  bar->setInkType(toolPref.ink());
  pref.brush.pattern(preset.brush->pattern());
  // Every recall gets a fresh copy: recoloring or changing a stamp cannot change
  // the saved preset (or another document's later recall).
  if (!preset.ignoreColors && preset.hasColors) {
    pref.colorBar.fgColor(preset.foreground);
    pref.colorBar.bgColor(preset.background);
  }
  auto brush = brush_for_preset(preset);
  bar->setActiveBrush(brush);
  if (preset.toolId == "gradient")
    bar->setGradientOptions(render::GradientType(preset.gradientType), preset.gradientDithering);
  bar->updateForActiveTool();
  // Switching tools/ink can rebuild the ramp from palette picks. Restore after
  // that update, including when the preset deliberately leaves colors alone.
  if (preset.ignoreColors) {
    pref.colorBar.fgColor(previousForeground);
    pref.colorBar.bgColor(previousBackground);
    bar->setShade(previousShade);
  }
  else if (toolPref.ink() == tools::InkType::SHADING)
    bar->setShade(preset.shade);
}

class TrashButton : public Button {
public:
  TrashButton() : Button("") { setMinSize(gfx::Size(24, 20) * guiscale()); }

protected:
  void onPaint(PaintEvent& ev) override
  {
    Button::onPaint(ev);
    auto* g = ev.graphics();
    const auto* theme = skin::SkinTheme::get(this);
    const auto color = isEnabled() ? theme->colors.text() : theme->colors.disabled();
    const int z = guiscale();
    const auto center = clientBounds().center();
    const int x = center.x - 5 * z, y = center.y - 5 * z;
    g->drawRect(color, gfx::Rect(x + z, y + 3 * z, 8 * z, 8 * z));
    g->fillRect(color, gfx::Rect(x, y + z, 10 * z, z));
    g->fillRect(color, gfx::Rect(x + 3 * z, y - z, 4 * z, z));
    g->fillRect(color, gfx::Rect(x + 3 * z, y + 5 * z, z, 4 * z));
    g->fillRect(color, gfx::Rect(x + 6 * z, y + 5 * z, z, 4 * z));
  }
};

std::string preset_size_label(const ToolPreset& preset)
{
  if (preset.brush->type() == doc::kImageBrushType)
    return fmt::format("{} x {} px",
                       preset.brush->image()->width(),
                       preset.brush->image()->height());
  return fmt::format("{}px", preset.brush->size());
}

class PresetNumberField : public IntEntry {
public:
  PresetNumberField(int minimum, int maximum) : IntEntry(minimum, maximum) { setMaxTextLength(8); }
  obs::signal<void()> ValueChange;

protected:
  void onValueChange() override { ValueChange(); }
  bool onAcceptUnicodeChar(int c) override
  {
    return (m_min < 0 && c == '-') || IntEntry::onAcceptUnicodeChar(c);
  }
};

// All controls edit a private snapshot. Live tool preferences and saved presets
// are only changed by recalling/saving, never by opening or canceling this form.
class PresetSettingsEditor : public ui::Grid {
public:
  explicit PresetSettingsEditor(const ToolPreset& preset)
    : ui::Grid(2, false)
    , m_preset(preset)
    , m_settings(preset.settings.object_items())
  {
    m_preset.brush = preset.brush->cloneWithNewImages();
    disableFlags(IGNORE_MOUSE); // Receive wheel events over labels and empty space.
    setBorder(gfx::Border(10 * guiscale()));
    setGap(gfx::Size(12, 8) * guiscale());
    auto* tool = App::instance()->toolBox()->getToolById(preset.toolId);
    row("Tool", new Label(tool ? tool->getText() : preset.toolId));
    const bool image = preset.brush->type() == doc::kImageBrushType;
    if (image) {
      row("Image brush", new Label(preset_size_label(preset)));
      auto* pattern = choice("Pattern",
                             { "Aligned to source", "Aligned to destination", "Paint brush" },
                             int(preset.brush->pattern()));
      pattern->Change.connect([this, pattern] {
        m_preset.brush->setPattern(doc::BrushPattern(pattern->getSelectedItemIndex()));
      });
    }
    else {
      select("Brush shape", "brush_type", { "Round", "Square", "Line" });
      number("Brush size", "brush_size", 1, doc::Brush::kMaxBrushSize, "px");
      auto* angle = number("Brush angle", "brush_angle", -180, 180, " degrees");
      m_dependencies.push_back([this, angle] {
        angle->setEnabled(integer("brush_type") != 0 && integer("brush_size") > 1);
      });
    }
    const bool canInk = tool && tool->getInk(0)->isPaint() && !tool->getInk(0)->isEffect();
    if (canInk) {
      select("Ink",
             "ink",
             { "Simple Ink", "Alpha Compositing", "Copy Color", "Lock Alpha", "Shading" });
      auto* shading = choice("Shading mode",
                             { "Original", "Progressive Shading", "Once Per Stroke" },
                             boolean("shading_once")    ? 2 :
                             boolean("shading_reentry") ? 1 :
                                                          0);
      shading->Change.connect([this, shading] {
        m_settings["shading_once"] = shading->getSelectedItemIndex() == 2;
        m_settings["shading_reentry"] = shading->getSelectedItemIndex() == 1;
      });
      m_dependencies.push_back(
        [this, shading] { shading->setEnabled(integer("ink") == int(tools::InkType::SHADING)); });
    }
    auto* opacity = number("Opacity", "opacity", 0, 255);
    m_dependencies.push_back([this, opacity, canInk] {
      opacity->setEnabled(!canInk || tools::inkHasOpacity(tools::InkType(integer("ink"))));
    });
    const bool freehand = tool && tool->getController(0)->isFreehand();
    const bool filled = tool && tool->getFill(0) == tools::FillAlways;
    if (freehand)
      select("Stroke", "freehand", { "Regular", "Pixel Perfect", "Dots" });
    if (tool && tool->getPointShape(0)->isSpray()) {
      section("Spray");
      number("Spray width", "spray_width", 1, 500);
      number("Spray speed", "spray_speed", 1, 100);
    }
    if (tool && tool->getPointShape(0)->isFloodFill()) {
      section("Fill");
      number("Tolerance", "tolerance", 0, 255);
      check("Contiguous", "contiguous");
      select("Reference", "fill_reference", { "Active layer", "All layers" });
      select("Connectivity", "fill_connectivity", { "4 connected", "8 connected" });
      select("Stop at grid", "fill_grid", { "Never", "When visible", "Always" });
    }
    if (tool && tool->getIntertwine(0)->cornerRadiusSupport())
      number("Corner radius", "corner_radius", 0, 9999, "px");
    if (tool && tool->getFill(0) == tools::FillOptional) {
      check("Filled", "filled");
      check("Filled preview", "filled_preview");
    }
    if (preset.toolId == "gradient") {
      section("Gradient");
      auto* type = choice("Type", { "Linear", "Radial" }, preset.gradientType);
      type->Change.connect([this, type] { m_preset.gradientType = type->getSelectedItemIndex(); });
      dithering("Dithering",
                preset.gradientDithering,
                DitheringSelector::SelectBoth,
                [this](const std::string& value) { m_preset.gradientDithering = value; });
    }
    if (freehand && !filled && !image) {
      section("Dynamics");
      check("Stabilizer", "stabilizer");
      auto* stabilizer = number("Stabilizer amount", "stabilizer_factor", 0, 100);
      m_dependencies.push_back(
        [this, stabilizer] { stabilizer->setEnabled(boolean("stabilizer")); });
      select("Size", "dynamic_size", { "Off", "Pressure", "Velocity" });
      auto* minSize = number("Minimum size", "min_size", 1, doc::Brush::kMaxBrushSize, "px");
      select("Angle", "dynamic_angle", { "Off", "Pressure", "Velocity" });
      auto* minAngle = number("Minimum angle", "min_angle", -180, 180, " degrees");
      select("Color", "dynamic_gradient", { "Off", "Pressure", "Velocity" });
      auto* direction = select("Color direction",
                               "color_from_to",
                               { "Background to foreground", "Foreground to background" });
      auto* matrix = dithering("Dithering",
                               m_settings["matrix"].string_value(),
                               DitheringSelector::SelectMatrix,
                               [this](const std::string& value) { m_settings["matrix"] = value; });
      auto* minPressure = number("Minimum pressure", "min_pressure", 0, 100, "%", 100);
      auto* maxPressure = number("Maximum pressure", "max_pressure", 0, 100, "%", 100);
      auto* minVelocity = number("Minimum velocity", "min_velocity", 0, 100, "%", 100);
      auto* maxVelocity = number("Maximum velocity", "max_velocity", 0, 100, "%", 100);
      m_dependencies.push_back([=] {
        minSize->setEnabled(integer("dynamic_size") != 0);
        minAngle->setEnabled(integer("dynamic_angle") != 0);
        direction->setEnabled(integer("dynamic_gradient") != 0);
        matrix->setEnabled(integer("dynamic_gradient") != 0);
        const auto uses = [this](int sensor) {
          return integer("dynamic_size") == sensor || integer("dynamic_angle") == sensor ||
                 integer("dynamic_gradient") == sensor;
        };
        minPressure->setEnabled(uses(1));
        maxPressure->setEnabled(uses(1));
        minVelocity->setEnabled(uses(2));
        maxVelocity->setEnabled(uses(2));
      });
    }
    updateEnabled();
  }

  ToolPreset editedPreset()
  {
    // Keep threshold ranges ordered when either endpoint was edited.
    for (const auto& keys : {
           std::pair{ "min_pressure", "max_pressure" },
           std::pair{ "min_velocity", "max_velocity" }
    }) {
      if (m_settings[keys.first].number_value() > m_settings[keys.second].number_value())
        m_settings[keys.second] = m_settings[keys.first];
    }
    auto edited = m_preset;
    update_tool_preset_settings(edited, m_settings);
    return edited;
  }

protected:
  bool onProcessMessage(Message* msg) override
  {
    if (msg->type() == kMouseWheelMessage) {
      View::scrollByMessage(this, msg);
      return true;
    }
    return ui::Grid::onProcessMessage(msg);
  }

private:
  int integer(const char* key) const
  {
    const auto it = m_settings.find(key);
    return it == m_settings.end() ? 0 : it->second.int_value();
  }
  bool boolean(const char* key) const
  {
    const auto it = m_settings.find(key);
    return it != m_settings.end() && it->second.bool_value();
  }
  void updateEnabled()
  {
    for (auto& update : m_dependencies)
      update();
  }
  void row(const std::string& title, Widget* control)
  {
    auto* label = new Label(title);
    label->setMinSize(gfx::Size(110 * guiscale(), 0));
    addChildInCell(label, 1, 1, LEFT | MIDDLE);
    control->setExpansive(true);
    addChildInCell(control, 1, 1, HORIZONTAL | MIDDLE);
  }
  void section(const std::string& title)
  {
    auto* label = new Label(title);
    label->setMinSize(gfx::Size(0, 24 * guiscale()));
    addChildInCell(label, 2, 1, LEFT | MIDDLE);
  }
  ComboBox* choice(const std::string& title,
                   std::initializer_list<const char*> values,
                   int selected)
  {
    auto* combo = new ComboBox;
    for (auto* value : values)
      combo->addItem(value);
    combo->setSelectedItemIndex(std::clamp(selected, 0, combo->getItemCount() - 1));
    row(title, combo);
    return combo;
  }
  ComboBox* select(const std::string& title,
                   const char* key,
                   std::initializer_list<const char*> values)
  {
    auto* combo = choice(title, values, integer(key));
    combo->Change.connect([this, key, combo] {
      m_settings[key] = combo->getSelectedItemIndex();
      updateEnabled();
    });
    return combo;
  }
  void check(const std::string& title, const char* key)
  {
    auto* checkbox = new CheckBox("");
    checkbox->setSelected(boolean(key));
    row(title, checkbox);
    checkbox->Click.connect([this, key, checkbox] {
      m_settings[key] = checkbox->isSelected();
      updateEnabled();
    });
  }
  PresetNumberField* number(const std::string& title,
                            const char* key,
                            int lo,
                            int hi,
                            const char* suffix = "",
                            int scale = 1)
  {
    auto* entry = new PresetNumberField(lo, hi);
    entry->setSuffix(suffix);
    entry->setValue(int(std::round(m_settings[key].number_value() * scale)));
    row(title, entry);
    entry->ValueChange.connect([this, entry, key, scale] {
      m_settings[key] = double(entry->getValue()) / scale;
      updateEnabled();
    });
    return entry;
  }
  DitheringSelector* dithering(const std::string& title,
                               const std::string& value,
                               DitheringSelector::Type type,
                               std::function<void(const std::string&)> save)
  {
    auto* selector = new DitheringSelector(type);
    selector->setSelectedItemByName(value);
    row(title, selector);
    selector->Change.connect([selector, save] {
      if (auto* item = selector->getSelectedItem())
        save(item->text());
    });
    return selector;
  }
  ToolPreset m_preset;
  json11::Json::object m_settings;
  std::vector<std::function<void()>> m_dependencies;
};

class PresetItem : public ListItem {
public:
  explicit PresetItem(const ToolPreset& preset)
    : ListItem(fmt::format("{} ({})", preset.name, preset_size_label(preset)))
  {
    if (!preset.ignoreColors) {
      if (preset.settings["ink"].int_value() == int(tools::InkType::SHADING))
        m_colors = preset.shade;
      else if (preset.hasColors)
        m_colors = { preset.foreground, preset.background };
    }
  }

protected:
  void onSizeHint(SizeHintEvent& ev) override
  {
    const int z = guiscale();
    ev.setSizeHint(gfx::Size(textWidth() + (12 + 13 * std::max(1, int(m_colors.size()))) * z,
                             std::max(textHeight() + 6 * z, 18 * z)));
  }

  void onPaint(PaintEvent& ev) override
  {
    auto* g = ev.graphics();
    const auto* theme = skin::SkinTheme::get(this);
    const auto fg = isSelected() ? theme->colors.listitemSelectedText() :
                                   theme->colors.listitemNormalText();
    const auto bg = isSelected() ? theme->colors.listitemSelectedFace() :
                                   theme->colors.listitemNormalFace();
    const int z = guiscale();
    const auto rc = clientBounds();
    g->fillRect(bg, rc);
    g->drawText(text(), fg, bg, gfx::Point(rc.x + 4 * z, rc.y + (rc.h - textHeight()) / 2));
    gfx::Rect swatch(rc.x + textWidth() + 10 * z, rc.y + (rc.h - 12 * z) / 2, 12 * z, 12 * z);
    if (m_colors.empty()) {
      g->drawRect(fg, swatch);
      g->drawLine(gfx::rgba(255, 64, 64),
                  gfx::Point(swatch.x + z, swatch.y2() - z - 1),
                  gfx::Point(swatch.x2() - z - 1, swatch.y + z));
    }
    else {
      for (const auto& color : m_colors) {
        g->drawRect(fg, swatch);
        auto inside = swatch;
        inside.shrink(z);
        draw_color(g, inside, color, doc::ColorMode::RGB);
        swatch.x += 13 * z;
      }
    }
  }

private:
  Shade m_colors;
};

class PresetList : public ListBox {
public:
  obs::signal<void()> Recall;

protected:
  bool onProcessMessage(Message* msg) override
  {
    const bool captured = hasCapture();
    const int previous = getSelectedIndex();
    const bool result = ListBox::onProcessMessage(msg);
    // A click on an already-selected preset must recall it again too.
    if ((msg->type() == kMouseUpMessage && captured) ||
        (msg->type() == kKeyDownMessage && previous != getSelectedIndex()))
      Recall();
    return result;
  }
};

class ToolPresetsPanel : public VBox,
                         public tools::ActiveToolObserver {
public:
  ToolPresetsPanel()
    : VBox()
    , m_presets(load_tool_presets(tool_presets_filename()))
    , m_checkTimer(1)
  {
    setExpansive(true);
    auto* box = new VBox;
    box->setExpansive(true);
    setBorder(gfx::Border(4 * guiscale()));
    auto* header = new HBox;
    auto* title = new Label("Tool Presets");
    title->setExpansive(true);
    header->addChild(title);
    placement = new Button("Float");
    header->addChild(placement);
    box->addChild(header);
    box->setChildSpacing(6 * guiscale());
    addChild(box);
    auto* toolbar = new HBox;
    m_add = new Button("+");
    m_edit = new Button("E");
    m_remove = new TrashButton;
    m_colors = new Button("Update color(s)");
    m_add->setMinSize(gfx::Size(24, 20) * guiscale());
    m_edit->setMinSize(gfx::Size(24, 20) * guiscale());
    toolbar->addChild(m_add);
    toolbar->addChild(m_edit);
    toolbar->addChild(m_remove);
    box->addChild(toolbar);
    box->addChild(m_colors);
    auto* tips = m_tips = new TooltipManager;
    addChild(tips);
    tips->addTooltipFor(m_add, "Save the current tool and brush settings", BOTTOM);
    tips->addTooltipFor(m_edit, "Edit the selected preset's brush settings", BOTTOM);
    tips->addTooltipFor(m_remove, "Delete the selected preset", BOTTOM);
    tips->addTooltipFor(m_colors,
                        "Update the selected preset's shading ramp or foreground/background colors",
                        BOTTOM);
    m_empty = new Label("Use + to save a preset.");
    box->addChild(m_empty);
    m_view = new View;
    m_list = new PresetList;
    m_view->attachToView(m_list);
    m_view->setExpansive(true);
    box->addChild(m_view);
    m_list->Change.connect([this] {
      m_checkTimer.stop();
      updateControls();
    });
    m_list->Recall.connect([this] {
      const int index = m_list->getSelectedIndex();
      if (index >= 0) {
        base::ScopedValue recalling(m_recalling, true);
        m_checkTimer.stop();
        recall_preset(m_presets[index]);
        m_colorTarget = index;
        watchSettings();
        updateControls();
      }
      scheduleCheck();
    });
    m_add->Click.connect([this] { editPreset(true); });
    m_edit->Click.connect([this] { editPreset(false); });
    m_colors->Click.connect([this] {
      const int index = m_colorTarget;
      if (index < 0)
        return;
      auto& pref = Preferences::instance();
      auto* bar = App::instance()->contextBar();
      auto shade = bar->getShade();
      // A non-shading tool doesn't keep the ramp in sync with palette picks.
      // When shading is active, keep the displayed ramp's ordering (including reverse).
      if (pref.tool(App::instance()->activeTool()).ink() != tools::InkType::SHADING) {
        doc::PalettePicks picks;
        ColorBar::instance()->getPaletteView()->getSelectedEntries(picks);
        shade.clear();
        int color = 0;
        for (bool pick : picks) {
          if (pick)
            shade.push_back(Color::fromIndex(color));
          ++color;
        }
      }
      auto updated = m_presets;
      update_tool_preset_colors(updated[index],
                                pref.colorBar.fgColor(),
                                pref.colorBar.bgColor(),
                                shade);
      if (store(updated))
        refresh(index);
    });
    m_remove->Click.connect([this] {
      const int index = m_list->getSelectedIndex();
      if (index < 0 || Alert::show("Tool Presets<<Delete tool preset?||&Delete||&Cancel") != 1)
        return;
      auto updated = m_presets;
      updated.erase(updated.begin() + index);
      if (store(updated))
        refresh(-1);
    });
    m_checkTimer.Tick.connect([this] {
      m_checkTimer.stop();
      checkSelection();
    });
    auto* bar = App::instance()->contextBar();
    m_brushChange = bar->BrushChange.connect([this] { scheduleCheck(); });
    m_settingsChange = bar->ToolSettingsChange.connect([this] { scheduleCheck(); });
    refresh(-1);
    App::instance()->activeToolManager()->add_observer(this);
    m_paletteChange = App::instance()->PaletteChange.connect([this] { m_list->invalidate(); });
  }
  ~ToolPresetsPanel() { App::instance()->activeToolManager()->remove_observer(this); }

  Button* placement;

private:
  void onActiveToolChange(tools::Tool*) override
  {
    updateControls();
    scheduleCheck();
  }
  void onSelectedToolChange(tools::Tool*) override
  {
    updateControls();
    scheduleCheck();
  }
  void scheduleCheck()
  {
    // Settle multi-option changes (ink/ramp updates, shared settings, etc.)
    // before comparing, and never inspect a half-applied preset.
    if (!m_recalling && m_list->getSelectedIndex() >= 0)
      m_checkTimer.start();
  }
  void watchSection(Section& section)
  {
    m_preferenceChanges.emplace_back(section.AfterChange.connect([this] { scheduleCheck(); }));
    for (auto* child : section.sectionList())
      watchSection(*child);
  }
  void watchSettings()
  {
    m_preferenceChanges.clear();
    auto& pref = Preferences::instance();
    watchSection(pref.tool(App::instance()->activeToolManager()->selectedTool()));
    watchSection(pref.tool(nullptr).dynamics);
    watchSection(pref.shared);
    watchSection(pref.brush);
    watchSection(pref.colorBar);
  }
  void checkSelection()
  {
    const int index = m_list->getSelectedIndex();
    if (m_recalling || index < 0)
      return;
    auto* tools = App::instance()->activeToolManager();
    bool matches = tools->selectedTool()->getId() == m_presets[index].toolId;
    // Temporary pan/eyedropper overrides do not replace the chosen painting tool.
    if (matches && tools->activeTool() != tools->selectedTool())
      return;
    if (matches) {
      auto expected = m_presets[index];
      expected.brush = brush_for_preset(expected);
      matches = tool_preset_matches(expected, capture_preset(false));
    }
    if (!matches) {
      m_list->selectChild(nullptr);
      updateControls();
    }
  }
  void updateControls()
  {
    m_add->setEnabled(supports_preset(App::instance()->activeToolManager()->selectedTool()));
    const bool selected = m_list->getSelectedIndex() >= 0;
    m_edit->setEnabled(selected);
    m_remove->setEnabled(selected);
    m_colors->setEnabled(m_colorTarget >= 0);
    m_tips->addTooltipFor(m_colors,
                          m_colorTarget >= 0 ?
                            "Update colors for: " + m_presets[m_colorTarget].name :
                            "Choose a preset to update its colors",
                          BOTTOM);
  }
  void refresh(int selected)
  {
    const auto children = m_list->children();
    for (auto* child : children)
      delete child;
    for (const auto& p : m_presets)
      m_list->addChild(new PresetItem(p));
    m_empty->setVisible(m_presets.empty());
    m_colorTarget = selected;
    m_list->selectIndex(selected);
    watchSettings();
    checkSelection();
    updateControls();
    layout();
    m_view->updateView();
    invalidate();
  }
  bool store(const std::vector<ToolPreset>& updated)
  {
    try {
      save_tool_presets(tool_presets_filename(), updated);
      m_presets = updated;
      return true;
    }
    catch (const std::exception& e) {
      Alert::show(fmt::format("Tool Presets<<Could not save tool presets.<<{}||&OK", e.what()));
      return false;
    }
  }
  void editPreset(bool create)
  {
    const int index = m_list->getSelectedIndex();
    if (create ? !supports_preset(App::instance()->activeToolManager()->selectedTool()) : index < 0)
      return;
    auto preset = create ? capture_preset() : m_presets[index];
    // Give legacy presets a color snapshot if the user enables color recall.
    if (!preset.hasColors) {
      preset.hasColors = true;
      preset.foreground = Preferences::instance().colorBar.fgColor();
      preset.background = Preferences::instance().colorBar.bgColor();
    }
    Window editor(Window::WithTitleBar, create ? "New Tool Preset" : "Edit Tool Preset");
    editor.setParentDisplay(display());
    editor.setSizeable(true);
    auto* box = new VBox;
    box->setChildSpacing(8 * guiscale());
    editor.addChild(box);
    box->addChild(new Label("Preset name"));
    auto* name = new Entry(128, "%s", preset.name.c_str());
    box->addChild(name);
    auto* ignoreColors = new CheckBox("Ignore colors");
    ignoreColors->setSelected(preset.ignoreColors);
    box->addChild(ignoreColors);
    auto* view = new View;
    view->setExpansive(true);
    auto* settings = new PresetSettingsEditor(preset);
    view->attachToView(settings);
    box->addChild(view);
    auto* buttons = new HBox;
    auto* save = new Button(create ? "Save New" : "Save");
    auto* cancel = new Button("Cancel");
    buttons->addChild(save);
    buttons->addChild(cancel);
    box->addChild(buttons);
    auto trimmedName = [&] {
      const auto value = name->text();
      const auto first = value.find_first_not_of(" \t\r\n");
      return first == std::string::npos ?
               std::string() :
               value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    };
    name->Change.connect([&] { save->setEnabled(!trimmedName().empty()); });
    save->Click.connect([&] {
      preset = settings->editedPreset();
      preset.ignoreColors = ignoreColors->isSelected();
      preset.name = trimmedName();
      if (preset.name.empty())
        return;
      auto updated = m_presets;
      if (create)
        updated.push_back(preset);
      else
        updated[index] = preset;
      if (store(updated))
        editor.closeWindow(save);
    });
    cancel->Click.connect([&] { editor.closeWindow(cancel); });
    const auto available = Manager::getDefault()->bounds().size();
    editor.setMinSize(
      gfx::Size(std::min(340 * guiscale(), available.w), std::min(260 * guiscale(), available.h)));
    editor.remapWindow();
    editor.expandWindow(
      gfx::Size(std::min(440 * guiscale(), available.w), std::min(480 * guiscale(), available.h)));
    editor.centerWindow(display());
    name->requestFocus();
    name->selectAllText();
    editor.openWindowInForeground();
    if (editor.closer() == save)
      refresh(create ? int(m_presets.size()) - 1 : index);
  }
  std::vector<ToolPreset> m_presets;
  Button* m_add;
  Button* m_edit;
  Button* m_remove;
  Button* m_colors;
  Label* m_empty;
  View* m_view;
  PresetList* m_list;
  TooltipManager* m_tips;
  int m_colorTarget = -1;
  bool m_recalling = false;
  Timer m_checkTimer;
  std::list<obs::scoped_connection> m_preferenceChanges;
  obs::scoped_connection m_brushChange;
  obs::scoped_connection m_settingsChange;
  obs::scoped_connection m_paletteChange;
};
} // namespace

struct ToolPresetsDock::Impl {
  ColorBar* colorBar;
  std::unique_ptr<ToolPresetsPanel> panel;
  std::unique_ptr<ui::Window> window;
  obs::scoped_connection exit;
  bool floating = false;
  bool visible = false;
  bool restored = false;
  bool moving = false;
  bool exiting = false;
};

ToolPresetsDock::ToolPresetsDock(ColorBar* colorBar)
  : Splitter(ByPercentage, ui::VERTICAL)
  , m_impl(std::make_unique<Impl>())
{
  m_impl->colorBar = colorBar;
  addChild(colorBar);
  setExpansive(true);
  setPosition(get_config_double("ToolPresets", "Divider", 60.0));
  m_impl->floating = get_config_bool("ToolPresets", "Floating", false);
  m_impl->exit = App::instance()->ExitGui.connect([this] {
    m_impl->exiting = true;
    if (m_impl->window) {
      save_window_pos(m_impl->window.get(), "ToolPresets");
      m_impl->window->closeWindow(nullptr);
    }
  });
}

ToolPresetsDock::~ToolPresetsDock()
{
  // ColorBar is owned by MainWindow, not by this container.
  removeChild(m_impl->colorBar);
  m_impl->exiting = true;
  m_impl->panel.reset();
  if (m_impl->window)
    m_impl->window->closeWindow(nullptr);
}

void ToolPresetsDock::restore()
{
  if (m_impl->restored)
    return;
  m_impl->restored = true;
  showPanel(get_config_bool("ToolPresets", "Visible", false));
}

bool ToolPresetsDock::isPanelVisible() const
{
  return m_impl->visible;
}

void ToolPresetsDock::toggle()
{
  m_impl->restored = true;
  showPanel(!m_impl->visible);
}

void ToolPresetsDock::showPanel(bool visible)
{
  m_impl->visible = visible;
  set_config_bool("ToolPresets", "Visible", visible);
  if (visible && !m_impl->panel) {
    m_impl->panel = std::make_unique<ToolPresetsPanel>();
    m_impl->panel->placement->Click.connect([this] { setFloating(!m_impl->floating); });
  }
  if (!m_impl->panel)
    return;
  setFloating(m_impl->floating);
}

void ToolPresetsDock::setFloating(bool floating)
{
  auto& state = *m_impl;
  state.moving = true;
  state.floating = floating;
  set_config_bool("ToolPresets", "Floating", floating);
  auto* panel = state.panel.get();
  if (panel->parent())
    panel->parent()->removeChild(panel);
  if (state.window && state.window->isVisible()) {
    save_window_pos(state.window.get(), "ToolPresets");
    state.window->closeWindow(nullptr);
  }
  panel->placement->setText(floating ? "Dock" : "Float");
  panel->setVisible(state.visible);
  if (floating) {
    if (!state.window) {
      state.window = std::make_unique<ui::Window>(ui::Window::WithTitleBar, "Tool Presets");
      auto* window = state.window.get();
      window->setSizeable(true);
      window->setWantFocus(false);
      window->setAutoRemap(false);
      window->setMinSize(gfx::Size(180, 180) * ui::guiscale());
      window->addChild(panel);
      window->remapWindow();
      window->expandWindow(gfx::Size(260, 310) * ui::guiscale());
      window->centerWindow();
      load_window_pos(window, "ToolPresets", false);
      window->Close.connect([this](ui::CloseEvent&) {
        save_window_pos(m_impl->window.get(), "ToolPresets");
        if (!m_impl->moving && !m_impl->exiting) {
          m_impl->visible = false;
          set_config_bool("ToolPresets", "Visible", false);
        }
      });
    }
    else
      state.window->addChild(panel);
    if (state.visible)
      state.window->openWindow();
  }
  else {
    addChild(panel);
  }
  // Retain the workspace's existing side and width; widen a narrow palette
  // only enough to make the preset controls usable.
  const int minWidth = state.visible && !floating ? 170 * ui::guiscale() : 0;
  setMinSize(gfx::Size(minWidth, 0));
  fitDockWidth();
  state.moving = false;
  App::instance()->mainWindow()->layout();
  App::instance()->mainWindow()->invalidate();
}

void ToolPresetsDock::fitDockWidth()
{
  if (auto* dock = dynamic_cast<Dock*>(parent())) {
    const int side = dock->whichSideChildIsDocked(this);
    auto size = dock->getUserDefinedSizeAtSide(side);
    if (size.w < minSize().w) {
      size.w = minSize().w;
      dock->undock(this);
      dock->dock(side, this, size);
    }
  }
}

void ToolPresetsDock::onPositionChange()
{
  const double position = std::clamp(getPosition(), 20.0, 80.0);
  if (position != getPosition()) {
    setPosition(position);
    return;
  }
  set_config_double("ToolPresets", "Divider", position);
  Splitter::onPositionChange();
}

void ToolPresetsDock::onSizeHint(ui::SizeHintEvent& ev)
{
  // Preset list length must never determine the sidebar's initial dimensions.
  ev.setSizeHint(m_impl->colorBar->sizeHint(ev.fitInSize()));
}

void ToolPresetsDock::onResize(ui::ResizeEvent& ev)
{
  Splitter::onResize(ev);
  auto* panel = m_impl->panel.get();
  if (!panel || panel->parent() != this || !panel->isVisible())
    return;
  // Leave room for both sets of controls on smaller windows, without
  // replacing the user's saved divider proportion when the window shrinks.
  const auto rc = childrenBounds();
  const int height = std::max(0, rc.h - childSpacing());
  const int paletteMin = std::min(120 * ui::guiscale(), height * 2 / 5);
  const int presetsMin = std::min(140 * ui::guiscale(), height * 3 / 5);
  const int top = std::clamp(m_impl->colorBar->bounds().h, paletteMin, height - presetsMin);
  m_impl->colorBar->setBounds(gfx::Rect(rc.x, rc.y, rc.w, top));
  panel->setBounds(gfx::Rect(rc.x, rc.y + top + childSpacing(), rc.w, height - top));
}

class ToolPresetsCommand : public Command {
public:
  ToolPresetsCommand() : Command(CommandId::ToolPresets()) {}

protected:
  bool onEnabled(Context* ctx) override { return ctx->isUIAvailable(); }
  bool onChecked(Context*) override
  {
    auto* dock = App::instance()->mainWindow()->toolPresetsDock();
    return dock && dock->isPanelVisible();
  }
  void onExecute(Context*) override { App::instance()->mainWindow()->toolPresetsDock()->toggle(); }
};

Command* CommandFactory::createToolPresetsCommand()
{
  return new ToolPresetsCommand;
}
} // namespace app
