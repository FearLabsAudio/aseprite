// KARN scoped conversion uses the ordinary quantizer with a local bank palette.
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gtest/gtest.h>
#include "app/util/indexed_conversion.h"
#include "app/util/palette_bank.h"
#include "app/cli/app_options.h"
#include "app/doc_undo.h"
#include "app/test_context.h"
#include "app/commands/commands.h"
#include "app/commands/params.h"
#include "app/modules/palettes.h"
#include "app/pref/preferences.h"
#include "app/ui_context.h"
#include "app/ui/color_bar.h"
#include "app/ui/palette_view.h"
#include "ui/alert.h"
#include "ui/manager.h"
#include "ui/timer.h"
#include "os/system.h"
#include "doc/image.h"
#include "doc/palette_bank.h"
#include "doc/primitives.h"
#include "render/task_delegate.h"

using namespace doc;
using namespace app;
static const char* g_exeName;

TEST(IndexedConversion, DitheringUsesOnlyTheDestinationBank)
{
  Palette master(0, 48);
  for (int i = 0; i < 48; ++i)
    master.setEntry(i, rgba(128, 128, 128, 255)); // Exact matches OUTSIDE bank 2.
  for (int i = 32; i < 48; ++i)
    master.setEntry(i, rgba(0, 0, 0, 255));
  master.setEntry(33, rgba(255, 255, 255, 255));
  const auto palette = make_bank_palette(master, 2);
  ImageRef rgb(Image::create(IMAGE_RGB, 32, 16));
  rgb->clear(rgba(128, 128, 128, 255));
  rgb->putPixel(0, 0, 0);
  const auto original = ImageRef(Image::createCopy(rgb.get()));
  for (const auto mapping : { RgbMapAlgorithm::RGB5A3, RgbMapAlgorithm::OCTREE }) {
    for (const auto algorithm : { render::DitheringAlgorithm::None,
                                 render::DitheringAlgorithm::Ordered,
                                 render::DitheringAlgorithm::Old,
                                 render::DitheringAlgorithm::ErrorDiffusion }) {
      IndexedConversionOptions options;
      options.mapping = mapping;
      options.fit = FitCriteria::RGB;
      options.dithering = render::Dithering(algorithm, render::BayerMatrix::make(8), 1.0);
      auto result = convert_to_indexed(rgb.get(), palette, 0, options);
      ASSERT_TRUE(result);
      EXPECT_EQ(IMAGE_INDEXED, result->pixelFormat());
      EXPECT_EQ(0, result->maskColor());
      EXPECT_EQ(0, result->getPixel(0, 0));
      bool black = false, white = false;
      for (int y = 0; y < result->height(); ++y) {
        for (int x = 0; x < result->width(); ++x) {
          const int index = result->getPixel(x, y);
          EXPECT_GE(index, 0);
          EXPECT_LT(index, 16);
          if (!x && !y) continue;
          EXPECT_NE(0, index);
          const auto color = palette.getEntry(index);
          black |= color == rgba(0, 0, 0, 255);
          white |= color == rgba(255, 255, 255, 255);
        }
      }
      if (algorithm != render::DitheringAlgorithm::None)
        EXPECT_TRUE(black && white);
      EXPECT_EQ(0, count_diff_between_images(rgb.get(), original.get()));
    }
  }
}

TEST(IndexedConversion, UnbankedTransparencyAndOpaqueIndexZero)
{
  Palette palette(0, 16);
  for (int i = 0; i < 16; ++i)
    palette.setEntry(i, rgba(i * 17, i * 17, i * 17, 255));
  ImageRef rgb(Image::create(IMAGE_RGB, 2, 1));
  rgb->putPixel(0, 0, 0);
  rgb->putPixel(1, 0, rgba(0, 0, 0, 255));
  auto transparent = convert_to_indexed(rgb.get(), palette, 7, {});
  EXPECT_EQ(7, transparent->maskColor());
  EXPECT_EQ(7, transparent->getPixel(0, 0));
  EXPECT_EQ(0, transparent->getPixel(1, 0));
  auto opaque = convert_to_indexed(rgb.get(), palette, -1, {});
  EXPECT_EQ(0, opaque->getPixel(1, 0));
}

TEST(IndexedConversion, BankChangeConvertsAllFramesAndUndoesAsOneEdit)
{
  os::SystemRef system = os::System::make();
  const char* argv[] = { g_exeName, "-b" };
  AppOptions arguments(2, argv);
  App app;
  app.initialize(arguments);
  TestContext context;
  auto* document = context.documents().add(32, 16, ColorMode::INDEXED);
  auto* sprite = document->sprite();
  auto* layer = static_cast<LayerImage*>(sprite->firstLayer());
  auto* palette = sprite->palette(0);
  palette->resize(32);
  for (int i = 0; i < 32; ++i)
    palette->setEntry(i, rgba(0, 0, 0, 255));
  palette->setEntry(17, rgba(255, 255, 255, 255));
  sprite->userData().properties(kPaletteBankProperties)["initialized"] = int32_t(2);
  const Palette before(*palette);
  {
    Tx tx(sprite);
    set_layer_palette_bank(tx, layer, -2);
    tx.commit();
  }
  layer->cel(0)->image()->clear(rgba(128, 128, 128, 255));
  layer->cel(0)->image()->putPixel(0, 0, 0);
  ImageRef original(Image::createCopy(layer->cel(0)->image()));
  sprite->setTotalFrames(3);
  layer->addCel(Cel::MakeLink(1, layer->cel(0)));
  layer->addCel(Cel::MakeCopy(2, layer->cel(0)));
  auto* untouched = new LayerImage(sprite);
  sprite->root()->addLayer(untouched);
  ImageRef other(Image::create(IMAGE_INDEXED, 32, 16));
  other->clear(3);
  untouched->addCel(new Cel(0, other));
  IndexedConversionOptions options;
  options.dithering.algorithm(render::DitheringAlgorithm::ErrorDiffusion);
  {
    Tx tx(sprite);
    set_layer_palette_bank(tx, layer, 1, options);
    tx.commit();
  }
  EXPECT_EQ(1, layer->paletteBank());
  EXPECT_EQ(layer->cel(0)->data(), layer->cel(1)->data());
  EXPECT_NE(layer->cel(0)->data(), layer->cel(2)->data());
  for (auto* cel : layer->cels()) {
    EXPECT_TRUE(is_local_bank_image(cel->image()));
    EXPECT_EQ(0, cel->image()->getPixel(0, 0));
  }
  const ImageRef converted(Image::createCopy(layer->cel(0)->image()));
  EXPECT_EQ(0, count_diff_between_images(converted.get(), layer->cel(2)->image()));
  EXPECT_EQ(0, palette->countDiff(&before, nullptr, nullptr));
  EXPECT_EQ(3, other->getPixel(0, 0));
  document->undoHistory()->undo();
  EXPECT_EQ(-2, layer->paletteBank());
  for (auto* cel : layer->cels())
    EXPECT_EQ(0, count_diff_between_images(original.get(), cel->image()));
  EXPECT_EQ(layer->cel(0)->data(), layer->cel(1)->data());
  document->undoHistory()->redo();
  EXPECT_EQ(1, layer->paletteBank());
  for (auto* cel : layer->cels())
    EXPECT_EQ(0, count_diff_between_images(converted.get(), cel->image()));
  document->close();
  delete document;
  app.run(false);
}

TEST(IndexedConversion, BankPaletteUIAndReuse)
{
  os::SystemRef system = os::System::make();
  const char* argv[] = { g_exeName };
  AppOptions arguments(1, argv);
  App app;
  app.initialize(arguments);
  Preferences::instance().experimental.useNativeClipboard(false);
  auto* context = UIContext::instance();
  auto* document = context->documents().add(8, 8, ColorMode::INDEXED);
  context->setActiveDocument(document);
  auto* sprite = document->sprite();
  auto* a = sprite->firstLayer();
  auto* b = new LayerImage(sprite);
  sprite->root()->addLayer(b);
  sprite->palette(0)->setEntry(3, rgba(17, 34, 51, 255));
  const auto assign = [&](Layer* layer, int bank, bool copy = false) {
    IndexedConversionOptions options;
    options.copyBankColors = copy;
    Tx tx(sprite);
    set_layer_palette_bank(tx, layer, bank, options);
    tx.commit();
  };
  assign(a, 2);
  assign(b, 5);
  context->setActiveLayer(a);
  EXPECT_EQ(16, get_current_palette()->size());
  sprite->setTotalFrames(2);
  context->setActiveFrame(1);
  EXPECT_EQ(16, get_current_palette()->size());
  context->setActiveFrame(0);
  auto* view = ColorBar::instance()->getPaletteView();
  view->selectRange(1, 15);
  view->copyToClipboard();
  context->setActiveLayer(b);
  // The clipboard must retain the source colors even after a layer change.
  const auto expected = make_bank_palette(*sprite->palette(0), 2);
  sprite->palette(0)->setEntry(83, rgba(255, 0, 0, 255));
  app_refresh_screen();
  const Palette before(*sprite->palette(0));
  view->selectRange(1, 15);
  view->pasteFromClipboard();
  EXPECT_EQ(expected, *b->palette(0));
  for (int i = 0; i < before.size(); ++i)
    if (i <= 80 || i >= 96)
      EXPECT_EQ(before.getEntry(i), sprite->palette(0)->getEntry(i));
  EXPECT_EQ(16, get_current_palette()->size());
  document->undoHistory()->undo();
  EXPECT_EQ(before, *sprite->palette(0));
  document->undoHistory()->redo();
  EXPECT_EQ(expected, *b->palette(0));

  // An unused, previously assigned bank supports both choices. A shared bank
  // must never be overwritten, even if a caller requests copying its colors.
  assign(a, 3);
  EXPECT_EQ(16, get_current_palette()->size());
  sprite->palette(0)->setEntry(35, rgba(255, 0, 0, 255));
  // Exercise the actual unused-bank dialog: Cancel, Keep, and Copy. Merely
  // choosing an option must not mutate anything before the transaction starts.
  for (int button : {3, 2, 1}) {
    const Palette snapshot(*sprite->palette(0));
    bool shown = false;
    ui::Timer answer(10);
    answer.Tick.connect([&] {
      auto* alert = dynamic_cast<ui::Alert*>(ui::Manager::getDefault()->getTopWindow());
      if (alert) {
        EXPECT_EQ("Unused Palette Bank", alert->text());
        shown = true;
        answer.stop();
        alert->closeWindow(alert->findChild(("button-" + std::to_string(button)).c_str()));
      }
    });
    answer.start();
    IndexedConversionOptions options;
    EXPECT_EQ(button != 3, choose_palette_bank_conversion(a, 2, options));
    answer.stop();
    EXPECT_TRUE(shown);
    EXPECT_EQ(button == 1, options.copyBankColors);
    EXPECT_EQ(snapshot, *sprite->palette(0));
    EXPECT_EQ(3, a->paletteBank());
  }
  assign(a, 2, false);
  EXPECT_EQ(rgba(255, 0, 0, 255), a->palette(0)->getEntry(3));
  assign(a, 3);
  assign(a, 2, true);
  EXPECT_EQ(expected.getEntry(3), a->palette(0)->getEntry(3));
  assign(a, 5, true);
  EXPECT_EQ(expected, *b->palette(0));
  IndexedConversionOptions noPrompt;
  EXPECT_TRUE(choose_palette_bank_conversion(a, 7, noPrompt)); // NEW
  EXPECT_TRUE(choose_palette_bank_conversion(a, 5, noPrompt)); // Already assigned

  Params force;
  force.set("enabled", "true"); force.set("ui", "false");
  context->executeCommand(Commands::instance()->byId("ForceKarnRules"), force);
  Palette resized(*get_current_palette()); resized.resize(17);
  view->delegate()->onPaletteViewModification(&resized, PaletteViewModification::RESIZE);
  EXPECT_EQ(16, get_current_palette()->size());
  EXPECT_EQ(before.size(), sprite->palette(0)->size());
  assign(b, -2);
  EXPECT_EQ(16, get_current_palette()->size());
  assign(b, -1);
  EXPECT_EQ(16, get_current_palette()->size());
  document->close();
  delete document;
  app.run(false);
}

int app_main(int argc, char* argv[])
{
  Palette::initBestfit();
  g_exeName = argv[0];
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
