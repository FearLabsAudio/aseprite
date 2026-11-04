// Exercise the real color-bar snapshot and button without synthetic mouse input.
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "app/app.h"
#include "app/cli/app_options.h"
#include "app/commands/commands.h"
#include "app/doc_undo.h"
#include "app/modules/palettes.h"
#include "app/pref/preferences.h"
#include "app/ui/color_bar.h"
#include "app/ui_context.h"
#include "app/util/palette_bank.h"
#include "doc/cel.h"
#include "os/system.h"
#include "ui/button.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace app;
using namespace doc;
static void check(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}

int app_main(int argc, char* argv[])
{
  const auto profile = std::filesystem::temp_directory_path() /
    ("aseprite-remap-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(profile);
  { std::ofstream config(profile / "aseprite.ini"); config << "[general]\ndata_recovery = false\n"; }
#ifdef _WIN32
  _putenv_s("ASEPRITE_USER_FOLDER", profile.string().c_str());
#else
  setenv("ASEPRITE_USER_FOLDER", profile.string().c_str(), 1);
#endif
  try {
    auto system = os::System::make();
    const char* arguments[] = {argv[0]};
    AppOptions options(1, arguments);
    App app;
    app.initialize(options);
    Preferences::instance().updater.waitDays(365);
    Preferences::instance().updater.lastCheck(std::time(nullptr)-60);
    Palette::initBestfit();
    auto* context = UIContext::instance();
    auto* bar = ColorBar::instance();
    auto* button = bar->findChildT<ui::Button>("remap_palette");
    check(button != nullptr, "Remap Palette button is missing");
    const auto assign = [](Layer* layer, int bank) {
      Tx tx(layer->sprite());
      set_layer_palette_bank(tx, layer, bank);
      tx.commit();
    };
    const auto arm = [&](Layer* layer) {
      context->setActiveLayer(layer);
      set_current_palette(layer->palette(0), false);
      Palette edited(*layer->palette(0));
      const auto a = edited.getEntry(3), b = edited.getEntry(7);
      edited.setEntry(3, b); edited.setEntry(7, a);
      bar->getPaletteView()->delegate()->onPaletteViewModification(&edited, PaletteViewModification::DRAGANDDROP);
      check(button->isVisible(), "Palette edit did not offer Remap Palette");
    };
    for (int bank : {-1, 0, 1, 15}) {
      auto* document = context->documents().add(8, 8, ColorMode::INDEXED);
      context->setActiveDocument(document);
      auto* sprite = document->sprite();
      auto* target = static_cast<LayerImage*>(sprite->firstLayer());
      for (int i=0; i<256; ++i)
        sprite->palette(0)->setEntry(i, rgba(i%16*17, i/16*17, 68, 255));
      if (bank >= 0) assign(target, bank);
      target->cel(0)->image()->putPixel(0,0,3);
      sprite->setTotalFrames(2);
      target->addCel(Cel::MakeLink(1, target->cel(0)));
      auto* other = new LayerImage(sprite);
      sprite->root()->addLayer(other);
      assign(other, bank == 1 ? 2 : 1);
      ImageRef image(Image::create(IMAGE_INDEXED, 8, 8)); image->clear(3);
      other->addCel(bank == 15 ? Cel::MakeLink(0, target->cel(0)) : new Cel(0, image));
      arm(target);
      check(target->cel(0)->image()->getPixel(0,0)==3, "Palette edit unexpectedly remapped pixels");
      button->Click();
      check(!button->isVisible(), "Remap button remained pending after remapping");
      check(target->cel(0)->image()->getPixel(0,0)==7, "Button failed to remap the selected scope");
      check(other->cel(0)->image()->getPixel(0,0)==3, "Button remapped another bank");
      document->undoHistory()->undo();
      check(target->cel(0)->image()->getPixel(0,0)==3, "Undo failed to restore pixel indices");
      document->undoHistory()->redo();
      check(target->cel(0)->image()->getPixel(0,0)==7, "Redo failed to restore remapping");
      arm(target);
      context->setActiveLayer(other);
      check(!button->isVisible(), "Pending remap crossed palette scopes");
      context->setActiveLayer(target);
      arm(target);
      context->setActiveFrame(1);
      check(!button->isVisible(), "Pending remap crossed palette frames");
      context->setActiveFrame(0);
      arm(target);
      context->executeCommand(Commands::instance()->byId("Undo"));
      check(!button->isVisible(), "Undo retained a stale palette snapshot");
      context->executeCommand(Commands::instance()->byId("Redo"));
      check(!button->isVisible(), "Redo revived a stale palette snapshot");
      arm(target);
      assign(target, -2);
      check(!button->isVisible(), "RGB reference offered indexed remapping");
      document->close(); delete document;
    }
    // Ordinary indexed documents still follow the stock global path.
    auto* document = context->documents().add(8,8,ColorMode::INDEXED);
    context->setActiveDocument(document);
    auto* sprite = document->sprite();
    sprite->palette(0)->setEntry(3, rgba(255,0,0,255));
    sprite->palette(0)->setEntry(7, rgba(0,255,0,255));
    auto* layer = sprite->firstLayer();
    layer->cel(0)->image()->putPixel(0,0,3);
    arm(layer); button->Click();
    check(layer->cel(0)->image()->getPixel(0,0)==7, "Stock indexed remapping regressed");
    document->close(); delete document;
    app.run(false);
    std::cout << "Palette remap UI: bank/document scope, snapshot lifetime, RGB exclusion and stock path PASS\n";
    return 0;
  }
  catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
