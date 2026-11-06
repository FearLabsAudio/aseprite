#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "app/app.h"
#include "app/cli/app_options.h"
#include "app/commands/commands.h"
#include "app/commands/params.h"
#include "app/doc_undo.h"
#include "app/modules/palettes.h"
#include "app/pref/preferences.h"
#include "app/ui/color_bar.h"
#include "app/ui_context.h"
#include "app/util/palette_bank.h"
#include "doc/cel.h"
#include "doc/palette_picks.h"
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
    ("aseprite-bank-operations-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
    App app; app.initialize(options);
    Preferences::instance().updater.waitDays(365);
    Preferences::instance().updater.lastCheck(std::time(nullptr)-60);
    Palette::initBestfit();
    auto* context = UIContext::instance();
    auto* bar = ColorBar::instance();
    auto* button = bar->findChildT<ui::Button>("remap_palette");
    check(button != nullptr, "Missing Remap Palette button");
    for (bool resize : {false, true}) {
      auto* document = context->documents().add(2,2,ColorMode::INDEXED);
      context->setActiveDocument(document);
      auto* sprite = document->sprite();
      auto* target = static_cast<LayerImage*>(sprite->firstLayer());
      auto* master = sprite->palette(0);
      for (int i=0; i<256; ++i) master->setEntry(i, rgba(0,255,0,255));
      master->setEntry(1,rgba(255,0,0,255)); master->setEntry(2,rgba(0,0,255,255));
      master->setEntry(200,rgba(136,0,136,255));
      master->setEntry(17,rgba(0,0,0,255)); master->setEntry(18,rgba(255,255,255,255));
      master->setEntry(20,rgba(136,136,136,255));
      Palette local(0,16);
      for (int i=0; i<16; ++i) local.setEntry(i,rgba(0,255,0,255));
      local.setEntry(1,rgba(0,0,0,255)); local.setEntry(2,rgba(255,255,255,255));
      local.setEntry(3,rgba(136,136,136,255));
      auto& properties = sprite->userData().properties(kPaletteBankProperties);
      properties["initialized"] = int32_t(65535);
      properties["bank0Palettes"] = UserData::Properties{{"1",bank_palette_colors(local)}};
      auto* other = new LayerImage(sprite); sprite->root()->addLayer(other);
      { Tx tx(sprite); set_layer_palette_bank(tx,target,0); set_layer_palette_bank(tx,other,1); tx.commit(); }
      auto* image = target->cel(0)->image();
      image->putPixel(0,0,resize ? 1 : 3); image->putPixel(1,0,2);
      image->putPixel(0,1,1); image->putPixel(1,1,2);
      other->addCel(resize ? Cel::MakeLink(0,target->cel(0)) : new Cel(0,ImageRef(Image::createCopy(image))));
      context->setActiveLayer(target); set_current_palette(target->palette(0),false);
      if (resize) {
        check(target->cel(0)->data()==other->cel(0)->data(), "Fixture did not share cel data across banks");
        Params params; params.set("ui","false"); params.set("width","3"); params.set("height","3"); params.set("method","bilinear");
        context->executeCommand(Commands::instance()->byId("SpriteSize"),params);
        const auto verify = [&] {
          check(sprite->width()==3, "Smooth resize failed");
          check(target->cel(0)->image()->getPixel(1,1)==3, "Bank 00 resized through the wrong palette");
          check(other->cel(0)->image()->getPixel(1,1)==4, "Cross-bank linked data was not separated for resize");
        };
        verify(); document->undoHistory()->undo();
        check(sprite->width()==2 && target->cel(0)->data()==other->cel(0)->data(), "Resize undo failed to restore cross-bank links");
        document->undoHistory()->redo(); verify();
      }
      else {
        const Palette before(*master);
        PalettePicks picks(16); picks[3]=true; context->setSelectedColors(picks);
        Params params; params.set("before","8");
        context->executeCommand(Commands::instance()->byId("MoveColors"),params);
        check(*master==before, "Move Colors changed the Document Palette");
        check(target->palette(0)->getEntry(7)==local.getEntry(3), "Move Colors missed the selected bank");
        check(button->isVisible(), "Move Colors did not offer bank-aware remapping");
        button->Click();
        check(target->cel(0)->image()->getPixel(0,0)==7, "Move Colors remap failed");
        check(other->cel(0)->image()->getPixel(0,0)==3, "Move Colors remapped another bank");
        picks.clear(); picks[7]=true; context->setSelectedColors(picks);
        context->executeCommand(Commands::instance()->byId("CopyColors"),params);
        check(button->isVisible(), "Copy Colors did not offer bank-aware remapping");
        check(target->palette(0)->size()==16 && target->palette(0)->getEntry(8)==local.getEntry(3), "Copy Colors did not replace bank swatches");
        check(*master==before, "Copy Colors changed the Document Palette");
      }
      document->close(); delete document;
    }
    app.run(false);
    std::cout << "Bank operations UI: move/copy palette scope, remap button and cross-bank resize PASS\n";
    return 0;
  }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
