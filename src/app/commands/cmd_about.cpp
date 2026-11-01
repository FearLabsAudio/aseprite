// Aseprite
// Copyright (C) 2020-2025  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/commands/command.h"
#include "app/ui/main_window.h"
#include "app/ui/browser_view.h"
#include "app/ui/skin/skin_theme.h"
#include "app/resource_finder.h"
#include "app/modules/gui.h"
#include "os/system.h"
#include "os/sampling.h"
#include "ui/box.h"
#include "ui/button.h"
#include "ui/combobox.h"
#include "ui/graphics.h"
#include "ui/label.h"
#include "ui/manager.h"
#include "ui/paint_event.h"
#include "ui/scale.h"
#include "ui/size_hint_event.h"
#include <algorithm>
#include "fmt/format.h"
#include "ver/info.h"

#include "about.xml.h"
#include "app/context.h"

namespace app {

using namespace ui;

class AboutCommand : public Command {
public:
  AboutCommand();

protected:
  bool onEnabled(Context* context) override;
  void onExecute(Context* context) override;
};

AboutCommand::AboutCommand() : Command(CommandId::About())
{
}

bool AboutCommand::onEnabled(Context* context)
{
  return context->isUIAvailable();
}

void AboutCommand::onExecute(Context* context)
{
  gen::About window;
  window.title()->setText(fmt::format("{} v{}", get_app_name(), get_app_version()));
  window.licenses()->Click.connect([&window] {
    window.closeWindow(nullptr);
    App::instance()->mainWindow()->showBrowser("docs/LICENSES.md");
  });
  window.credits()->Click.connect([&window] {
    window.closeWindow(nullptr);
    App::instance()->mainWindow()->showBrowser("AUTHORS.md", "Authors");
  });
  window.i18nCredits()->Click.connect([&window] {
    window.closeWindow(nullptr);
    App::instance()->mainWindow()->showBrowser("strings/README.md", "Translators");
  });
  window.openWindowInForeground();
}

Command* CommandFactory::createAboutCommand()
{
  return new AboutCommand;
}

namespace {
class KarnLogo : public Widget {
public:
  KarnLogo()
  {
    ResourceFinder resources;
    resources.includeDataDir("icons/karn-mods.png");
    if (resources.findFirst())
      m_logo = os::System::instance()->loadRgbaSurface(resources.filename().c_str());
  }
protected:
  void onSizeHint(SizeHintEvent& ev) override
  {
    ev.setSizeHint(gfx::Size(0, 110 * guiscale()));
  }
  void onPaint(PaintEvent& ev) override
  {
    ev.graphics()->fillRect(skin::SkinTheme::get(this)->colors.face(), clientBounds());
    if (!m_logo)
      return;
    const auto bounds = clientBounds();
    // Display only the logo inside the supplied image's white margins.
    // Integer magnification and explicit nearest sampling keep its pixels crisp.
    const int zoom = std::max(1, std::min(2 * guiscale(), bounds.w / 197));
    const gfx::Size size(197 * zoom, 51 * zoom);
    ev.graphics()->drawSurface(m_logo.get(), gfx::Rect(62, 95, 197, 51),
      gfx::Rect((bounds.w-size.w)/2, (bounds.h-size.h)/2, size.w, size.h),
      os::Sampling(os::Sampling::Filter::Nearest), nullptr);
  }
private:
  os::SurfaceRef m_logo;
};
}

class AboutKarnModsCommand : public Command {
public:
  AboutKarnModsCommand() : Command(CommandId::AboutKarnMods()) {}
protected:
  bool onEnabled(Context* context) override { return context->isUIAvailable(); }
  void onExecute(Context*) override
  {
    Window window(Window::WithTitleBar, "ABOUT KARN MODS");
    window.setSizeable(true);
    auto* contents = new VBox;
    contents->setChildSpacing(8 * guiscale());
    window.addChild(contents);
    contents->addChild(new KarnLogo);
    auto* credit = new Label("Authored by Justin Schwartzbauer and ChatGPT");
    credit->setAlign(CENTER);
    contents->addChild(credit);
    auto* chapters = new ComboBox;
    const char* sections[] = { "Welcome", "Quick start", "Controlled shading",
      "Tool presets", "Palette banks", "VERA colors and KARN rules", "Full color Reference",
      "Converting RGB artwork", "Export layers as 4bpp BMPs", "Export to Karnedit",
      "Saving and compatibility", "Troubleshooting" };
    for (auto section : sections)
      chapters->addItem(section);
    contents->addChild(chapters);
    auto* manual = new BrowserView(true);
    manual->setExpansive(true);
    contents->addChild(manual);
    manual->loadFile("docs/karn-mods.md");
    chapters->Change.connect([=] {
      manual->loadFile("docs/karn-mods.md", sections[chapters->getSelectedItemIndex()]);
      manual->onWorkspaceViewSelected();
    });
    auto* close = new Button("Close");
    close->Click.connect([&] { window.closeWindow(close); });
    contents->addChild(close);
    const auto available = Manager::getDefault()->bounds();
    window.setMinSize(gfx::Size(std::min(420 * guiscale(), available.w),
                                std::min(340 * guiscale(), available.h)));
    window.remapWindow();
    window.expandWindow(gfx::Size(std::min(760 * guiscale(), available.w - 16),
                                  std::min(680 * guiscale(), available.h - 16)));
    window.centerWindow();
    load_window_pos(&window, "AboutKarnMods");
    window.Open.connect([manual](Event&) {
      manual->loadFile("docs/karn-mods.md", "Welcome");
      manual->onWorkspaceViewSelected();
    });
    window.openWindowInForeground();
    save_window_pos(&window, "AboutKarnMods");
  }
};

Command* CommandFactory::createAboutKarnModsCommand()
{
  return new AboutKarnModsCommand;
}

} // namespace app
