// KARN scoped conversion uses the ordinary quantizer with a local bank palette.
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gtest/gtest.h>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "app/util/indexed_conversion.h"
#include "app/util/palette_bank.h"
#include "app/color_picker.h"
#include "app/commands/cmd_eyedropper.h"
#include "app/cli/app_options.h"
#include "app/doc_undo.h"
#include "app/test_context.h"
#include "app/commands/commands.h"
#include "app/commands/params.h"
#include "app/commands/filters/cels_target.h"
#include "app/modules/palettes.h"
#include "app/pref/preferences.h"
#include "app/ui_context.h"
#include "app/ui/color_bar.h"
#include "app/ui/palette_view.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui/editor/editor.h"
#include "app/ui/editor/editor_render.h"
#include "app/ui/editor/standby_state.h"
#include "app/tools/active_tool.h"
#include "app/tools/ink.h"
#include "app/tools/tool_box.h"
#include "ui/cursor.h"
#include "ui/system.h"
#include "ui/alert.h"
#include "ui/combobox.h"
#include "ui/slider.h"
#include "ui/button.h"
#include "app/ui/dithering_selector.h"
#include "ui/manager.h"
#include "ui/timer.h"
#include "os/system.h"
#include "os/surface.h"
#include "doc/mask.h"
#include "doc/image.h"
#include "doc/palette_bank.h"
#include "doc/tilesets.h"
#include "doc/primitives.h"
#include "render/task_delegate.h"
#include "render/render.h"

using namespace doc;
using namespace app;
static const char* g_exeName;
static void check_bank_color_picker();

static void disable_test_update_check()
{
  // app.run(false) still starts the updater in GUI tests. Keep these tests
  // offline and avoid racing a newly started HTTP request during teardown.
  Preferences::instance().updater.waitDays(365);
  Preferences::instance().updater.lastCheck(std::time(nullptr)-60);
}

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

TEST(IndexedConversion, BankMatchingAndFloydSteinbergStrength)
{
  Palette choices(0, 3);
  choices.setEntry(1, rgba(230, 70, 40, 255));
  choices.setEntry(2, rgba(130, 100, 70, 255));
  PaletteMatchMap closest(PaletteMatch::ClosestColor), lightness(PaletteMatch::Lightness),
                  hue(PaletteMatch::Hue);
  const auto source = rgba(180, 60, 40, 255);
  closest.regenerateMap(&choices, 0);
  lightness.regenerateMap(&choices, 0);
  EXPECT_EQ(1, closest.mapColor(source));
  EXPECT_EQ(2, lightness.mapColor(source));
  choices.setEntry(1, rgba(180, 40, 80, 255));
  choices.setEntry(2, rgba(140, 20, 10, 255));
  closest.regenerateMap(&choices, 0);
  hue.regenerateMap(&choices, 0);
  EXPECT_EQ(1, closest.mapColor(source));
  EXPECT_EQ(2, hue.mapColor(source));
  Palette palette(0, 16);
  for (int i = 0; i < 16; ++i) palette.setEntry(i, rgba(0, 0, 0, 255));
  palette.setEntry(1, rgba(255, 255, 255, 255));
  ImageRef rgb(Image::create(IMAGE_RGB, 32, 8));
  rgb->clear(rgba(128, 128, 128, 255));
  // Include transparent holes surrounded by opaque pixels, not just a corner.
  for (int y = 0; y < 8; ++y) rgb->putPixel(7, y, 0);
  for (auto priority : {PaletteMatch::ClosestColor, PaletteMatch::Lightness, PaletteMatch::Hue}) {
    IndexedConversionOptions options;
    options.match = priority;
    auto plain = convert_to_indexed(rgb.get(), palette, 0, options);
    options.dithering.algorithm(render::DitheringAlgorithm::ErrorDiffusion);
    options.dithering.factor(0.0);
    auto zero = convert_to_indexed(rgb.get(), palette, 0, options);
    EXPECT_EQ(0, count_diff_between_images(plain.get(), zero.get()));
    options.dithering.factor(1.0);
    auto full = convert_to_indexed(rgb.get(), palette, 0, options);
    EXPECT_GT(count_diff_between_images(zero.get(), full.get()), 0);
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 32; ++x) {
        EXPECT_EQ(x == 7, full->getPixel(x, y) == 0);
        EXPECT_LT(full->getPixel(x, y), 16);
      }
  }
}

TEST(IndexedConversion, RemapBankPreservesAnimatedColorsAndUndo)
{
  auto system = os::System::make();
  const char* argv[] = {g_exeName, "-b"};
  AppOptions args(2, argv);
  App app;
  app.initialize(args);
  TestContext context;
  auto* document = context.documents().add(2, 1, ColorMode::INDEXED);
  auto* sprite = document->sprite();
  auto* layer = static_cast<LayerImage*>(sprite->firstLayer());
  layer->userData().properties(kPaletteBankProperties)["bank"] = uint8_t(0);
  sprite->userData().properties(kPaletteBankProperties)["initialized"] = int32_t(3);
  sprite->setTotalFrames(3);
  layer->cel(0)->image()->putPixel(0, 0, 1);
  layer->addCel(Cel::MakeLink(1, layer->cel(0)));
  layer->addCel(Cel::MakeLink(2, layer->cel(0)));
  auto* other = new LayerImage(sprite);
  sprite->root()->addLayer(other);
  other->userData().properties(kPaletteBankProperties)["bank"] = uint8_t(0);
  other->addCel(Cel::MakeLink(0, layer->cel(0)));
  auto* palette = sprite->palette(0);
  palette->resize(32);
  for (int i = 0; i < 32; ++i) palette->setEntry(i, rgba(0, 0, 0, 255));
  palette->setEntry(1, rgba(255, 0, 0, 255));
  palette->setEntry(17, rgba(0, 0, 255, 255));
  palette->setEntry(18, rgba(255, 0, 0, 255));
  Palette later(*palette);
  later.setFrame(2);
  later.setEntry(17, rgba(255, 0, 0, 255));
  later.setEntry(18, rgba(0, 0, 255, 255));
  sprite->setPalette(&later, true);
  const Palette before(*palette);
  IndexedConversionOptions options;
  options.remapBankColors = true;
  options.match = PaletteMatch::ClosestColor;
  {
    Tx tx(sprite);
    set_layer_palette_bank(tx, layer, 1, options);
    tx.commit();
  }
  EXPECT_EQ(2, layer->cel(0)->image()->getPixel(0, 0));
  EXPECT_EQ(2, layer->cel(1)->image()->getPixel(0, 0));
  EXPECT_EQ(1, layer->cel(2)->image()->getPixel(0, 0));
  EXPECT_EQ(layer->cel(0)->data(), layer->cel(1)->data());
  EXPECT_NE(layer->cel(0)->data(), layer->cel(2)->data());
  EXPECT_EQ(1, other->cel(0)->image()->getPixel(0, 0));
  EXPECT_NE(layer->cel(0)->data(), other->cel(0)->data());
  for (auto* cel : layer->cels()) EXPECT_EQ(0, cel->image()->getPixel(1, 0));
  EXPECT_EQ(before, *sprite->palette(0));
  EXPECT_EQ(later, *sprite->palette(2));
  document->undoHistory()->undo();
  EXPECT_EQ(0, layer->paletteBank());
  EXPECT_EQ(layer->cel(0)->data(), layer->cel(2)->data());
  EXPECT_EQ(layer->cel(0)->data(), other->cel(0)->data());
  EXPECT_EQ(1, layer->cel(0)->image()->getPixel(0, 0));
  document->undoHistory()->redo();
  EXPECT_EQ(1, layer->paletteBank());
  EXPECT_EQ(2, layer->cel(0)->image()->getPixel(0, 0));
  EXPECT_EQ(1, layer->cel(2)->image()->getPixel(0, 0));
  EXPECT_EQ(1, other->cel(0)->image()->getPixel(0, 0));
  document->close();
  delete document;
  app.run(false);
}

TEST(IndexedConversion, BankDialogPreviewsDefaultMappingAndStrengthWithoutEditing)
{
  auto system = os::System::make();
  const char* argv[] = {g_exeName};
  AppOptions args(1, argv);
  App app;
  app.initialize(args);
  disable_test_update_check();
  auto* context = UIContext::instance();
  auto* document = context->documents().add(32, 8, ColorMode::INDEXED);
  auto* sprite = document->sprite();
  auto* layer = sprite->firstLayer();
  layer->userData().properties(kPaletteBankProperties)["bank"] = uint8_t(0);
  sprite->userData().properties(kPaletteBankProperties)["initialized"] = int32_t(3);
  auto* palette = sprite->palette(0);
  palette->resize(32);
  for (int i = 0; i < 32; ++i) palette->setEntry(i, rgba(0, 0, 0, 255));
  palette->setEntry(1, rgba(128, 128, 128, 255));
  palette->setEntry(17, rgba(255, 255, 255, 255));
  layer->cel(0)->image()->clear(1);
  auto* other = new LayerImage(sprite);
  other->userData().properties(kPaletteBankProperties)["bank"] = uint8_t(1);
  sprite->root()->addLayer(other);
  ImageRef otherImage(Image::create(IMAGE_INDEXED, 1, 1));
  otherImage->clear(1);
  other->addCel(new Cel(0, otherImage));
  context->setActiveLayer(layer);
  const auto history = document->undoHistory()->currentState();
  const Palette before(*palette);
  auto surface = system->makeRgbaSurface(32, 8);
  auto draw = [&] {
    auto& engine = Editor::renderEngine();
    engine.setProjection(render::Projection());
    engine.setNonactiveLayersOpacity(255);
    engine.setSelectedLayer(layer);
    engine.renderSprite(surface.get(), sprite, 0, gfx::Clip(0, 0, sprite->bounds()));
  };
  std::vector<gfx::Color> finalPreview;
  for (bool accept : {false, true}) {
    int stage = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    ui::Timer poll(20);
    poll.Tick.connect([&] {
      auto* window = ui::Manager::getDefault()->getTopWindow();
      auto* operation = dynamic_cast<ui::ComboBox*>(window->findChild("operation"));
      if (!operation) return;
      if (std::chrono::steady_clock::now() > deadline) {
        ADD_FAILURE() << "Bank preview timed out at stage " << stage;
        poll.stop();
        window->closeWindow(nullptr);
        return;
      }
      auto* factor = static_cast<ui::Slider*>(window->findChild("factor"));
      auto* preview = static_cast<ui::CheckBox*>(window->findChild("preview"));
      draw();
      EXPECT_EQ(history, document->undoHistory()->currentState());
      EXPECT_EQ(0, layer->paletteBank());
      EXPECT_EQ(before, *sprite->palette(0));
      EXPECT_EQ(1, layer->cel(0)->image()->getPixel(1, 0));
      EXPECT_EQ(gfx::rgba(255, 255, 255), surface->getPixel(0, 0)); // Other layer.
      if (stage == 0) {
        if (surface->getPixel(1, 0) != gfx::rgba(255, 255, 255)) return;
        EXPECT_EQ(0, operation->getSelectedItemIndex());
        EXPECT_EQ(2, operation->getItemCount()); // Shared bank cannot be overwritten.
        EXPECT_FALSE(factor->isEnabled());
        operation->setSelectedItemIndex(1);
        auto* dither = static_cast<DitheringSelector*>(window->findChild("dithering_selector"));
        dither->setSelectedItemIndex(dither->getItemCount()-1);
        dither->Change();
        EXPECT_EQ(render::DitheringAlgorithm::ErrorDiffusion, dither->ditheringAlgorithm());
        EXPECT_TRUE(factor->isEnabled());
        ++stage;
      }
      else if (stage == 1) {
        bool black = false, white = false;
        for (int x = 1; x < 32; ++x) {
          black |= surface->getPixel(x, 0) == gfx::rgba(0, 0, 0);
          white |= surface->getPixel(x, 0) == gfx::rgba(255, 255, 255);
        }
        if (!black || !white) return;
        factor->setValue(0);
        factor->Change();
        ++stage;
      }
      else if (stage == 2) {
        for (int x = 1; x < 32; ++x)
          if (surface->getPixel(x, 0) != gfx::rgba(255, 255, 255)) return;
        preview->setSelected(false);
        preview->Click();
        draw();
        EXPECT_EQ(gfx::rgba(128, 128, 128), surface->getPixel(1, 0));
        factor->setValue(65);
        factor->Change();
        preview->setSelected(true);
        preview->Click();
        ++stage;
      }
      else {
        bool black = false;
        for (int x = 1; x < 32; ++x)
          black |= surface->getPixel(x, 0) == gfx::rgba(0, 0, 0);
        if (!black) return;
        finalPreview.clear();
        for (int y = 0; y < 8; ++y)
          for (int x = 0; x < 32; ++x) finalPreview.push_back(surface->getPixel(x, y));
        poll.stop();
        window->closeWindow(window->findChild(accept ? "ok" : "cancel"));
      }
    });
    IndexedConversionOptions options;
    poll.start();
    EXPECT_EQ(accept, choose_palette_bank_conversion(layer, 1, options));
    poll.stop();
    EXPECT_EQ(3, stage);
    draw();
    EXPECT_EQ(gfx::rgba(128, 128, 128), surface->getPixel(1, 0));
    EXPECT_EQ(history, document->undoHistory()->currentState());
    if (accept) {
      EXPECT_TRUE(options.remapBankColors);
      EXPECT_DOUBLE_EQ(0.65, options.dithering.factor());
      { Tx tx(sprite); set_layer_palette_bank(tx, layer, 1, options); tx.commit(); }
      draw();
      ASSERT_EQ(256, finalPreview.size());
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 32; ++x)
          EXPECT_EQ(finalPreview[y*32+x], surface->getPixel(x, y));
      document->undoHistory()->undo();
      EXPECT_EQ(history, document->undoHistory()->currentState());
    }
  }
  document->close();
  delete document;
  app.run(false);
}

TEST(IndexedConversion, RemapTilemapIsolatesSharedTilesAndAnimatedPalettes)
{
  auto system = os::System::make();
  const char* argv[] = {g_exeName, "-b"};
  AppOptions args(2, argv);
  App app;
  app.initialize(args);
  TestContext context;
  auto* document = context.documents().add(2, 1, ColorMode::INDEXED);
  auto* sprite = document->sprite();
  auto* tileset = new Tileset(sprite, Grid(gfx::Size(2, 1)), 2);
  const auto tsi = sprite->tilesets()->add(tileset);
  ImageRef tileImage(Image::create(IMAGE_INDEXED, 2, 1));
  tileImage->putPixel(0, 0, 1);
  tileset->set(1, tileImage);
  auto* layer = new LayerTilemap(sprite, tsi);
  auto* other = new LayerTilemap(sprite, tsi);
  sprite->root()->addLayer(layer);
  sprite->root()->addLayer(other);
  for (auto* target : {layer, other}) {
    target->userData().properties(kPaletteBankProperties)["bank"] = uint8_t(0);
    ImageRef image(Image::create(IMAGE_TILEMAP, 1, 1));
    image->putPixel(0, 0, tile(1, tile_f_xflip));
    target->addCel(new Cel(0, image));
  }
  sprite->setTotalFrames(2);
  layer->addCel(Cel::MakeLink(1, layer->cel(0)));
  sprite->userData().properties(kPaletteBankProperties)["initialized"] = int32_t(3);
  auto* palette = sprite->palette(0);
  palette->resize(32);
  for (int i = 0; i < 32; ++i) palette->setEntry(i, rgba(0, 0, 0, 255));
  palette->setEntry(1, rgba(255, 0, 0, 255));
  palette->setEntry(17, rgba(0, 0, 255, 255));
  palette->setEntry(18, rgba(255, 0, 0, 255));
  Palette later(*palette);
  later.setFrame(1);
  later.setEntry(17, rgba(255, 0, 0, 255));
  later.setEntry(18, rgba(0, 0, 255, 255));
  sprite->setPalette(&later, true);
  IndexedConversionOptions options;
  options.remapBankColors = true;
  options.match = PaletteMatch::ClosestColor;
  { Tx tx(sprite); set_layer_palette_bank(tx, layer, 1, options); tx.commit(); }
  EXPECT_NE(tsi, layer->tilesetIndex());
  EXPECT_EQ(tsi, other->tilesetIndex());
  EXPECT_EQ(1, other->tileset()->get(1)->getPixel(0, 0));
  for (auto* cel : layer->cels()) {
    const auto t = cel->image()->getPixel(0, 0);
    EXPECT_EQ(tile_f_xflip, tile_getf(t));
    auto image = layer->tileset()->get(tile_geti(t));
    EXPECT_EQ(cel->frame() == 0 ? 2 : 1, image->getPixel(0, 0));
    EXPECT_EQ(0, image->getPixel(1, 0));
  }
  document->undoHistory()->undo();
  EXPECT_EQ(tsi, layer->tilesetIndex());
  EXPECT_EQ(layer->cel(0)->data(), layer->cel(1)->data());
  EXPECT_EQ(tile(1, tile_f_xflip), layer->cel(1)->image()->getPixel(0, 0));
  document->undoHistory()->redo();
  EXPECT_NE(tsi, layer->tilesetIndex());
  EXPECT_EQ(tsi, other->tilesetIndex());
  document->close();
  delete document;
  app.run(false);
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

TEST(FilterPreview, AllShowsBothPaletteBanksBeforeApply)
{
  auto system = os::System::make();
  const char* argv[] = { g_exeName };
  AppOptions options(1, argv);
  App app;
  app.initialize(options);
  disable_test_update_check();
  auto* context = UIContext::instance();
  auto* document = context->documents().add(3, 1, ColorMode::INDEXED);
  auto* sprite = document->sprite();
  auto* lower = sprite->firstLayer();
  auto* upper = new LayerImage(sprite);
  sprite->root()->addLayer(upper);
  lower->userData().properties(kPaletteBankProperties)["bank"] = uint8_t(0);
  upper->userData().properties(kPaletteBankProperties)["bank"] = uint8_t(1);
  auto* palette = sprite->palette(0);
  palette->resize(32);
  for (int i = 0; i < 32; ++i)
    palette->setEntry(i, rgba(0, 0, 0, 255));
  palette->setEntry(1, rgba(0, 68, 0, 255));
  palette->setEntry(2, rgba(0, 102, 0, 255));
  palette->setEntry(17, rgba(68, 0, 0, 255));
  palette->setEntry(19, rgba(102, 0, 0, 255));
  lower->cel(0)->image()->clear(1);
  ImageRef top(Image::create(IMAGE_INDEXED, 3, 1));
  top->putPixel(1, 0, 1);
  upper->addCel(new Cel(0, top));
  Mask mask;
  mask.replace(gfx::Rect(0, 0, 2, 1));
  document->setMask(&mask);
  context->setActiveLayer(upper);
  auto& target = Preferences::instance().filters.celsTarget;
  const auto previousTarget = target();
  target(CelsTarget::All);

  auto surface = system->makeRgbaSurface(3, 1);
  auto draw = [&] {
    auto& engine = Editor::renderEngine();
    engine.setProjection(render::Projection());
    engine.setNonactiveLayersOpacity(255);
    engine.setSelectedLayer(upper);
    engine.renderSprite(surface.get(), sprite, 0, gfx::Clip(0, 0, sprite->bounds()));
  };
  const auto history = document->undoHistory()->currentState();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  ui::Timer poll(10);
  poll.Tick.connect([&] {
    draw();
    if (surface->getPixel(0, 0) != gfx::rgba(0, 102, 0) &&
        std::chrono::steady_clock::now() < deadline)
      return;
    poll.stop();
    EXPECT_EQ(gfx::rgba(0, 102, 0), surface->getPixel(0, 0));
    EXPECT_EQ(gfx::rgba(102, 0, 0), surface->getPixel(1, 0));
    EXPECT_EQ(gfx::rgba(0, 68, 0), surface->getPixel(2, 0)); // Outside the marquee.
    EXPECT_EQ(1, lower->cel(0)->image()->getPixel(0, 0));
    EXPECT_EQ(1, upper->cel(0)->image()->getPixel(1, 0));
    EXPECT_EQ(history, document->undoHistory()->currentState());
    ui::Manager::getDefault()->getTopWindow()->closeWindow(nullptr);
  });
  Params params;
  params.set("brightness", "50");
  poll.start();
  context->executeCommand(Commands::instance()->byId("BrightnessContrast"), params);
  EXPECT_FALSE(poll.isRunning());
  poll.stop();
  draw();
  EXPECT_EQ(gfx::rgba(0, 68, 0), surface->getPixel(0, 0));
  EXPECT_EQ(gfx::rgba(68, 0, 0), surface->getPixel(1, 0));
  EXPECT_EQ(history, document->undoHistory()->currentState());
  target(previousTarget);
  document->close();
  delete document;
  app.run(false);
}

static void check_document_palette_paste();

TEST(IndexedConversion, BankPaletteUIAndReuse)
{
  os::SystemRef system = os::System::make();
  const char* argv[] = { g_exeName };
  AppOptions arguments(1, argv);
  App app;
  app.initialize(arguments);
  Preferences::instance().experimental.useNativeClipboard(false);
  disable_test_update_check();
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
  for (int mode : {-1, 0, 2}) {
    const Palette snapshot(*sprite->palette(0));
    bool shown = false;
    ui::Timer answer(10);
    answer.Tick.connect([&] {
      auto* window = ui::Manager::getDefault()->getTopWindow();
      if (window && window->findChild("operation")) {
        EXPECT_EQ("Change Palette Bank", window->text());
        shown = true;
        answer.stop();
        auto* operation = static_cast<ui::ComboBox*>(window->findChild("operation"));
        EXPECT_EQ(0, operation->getSelectedItemIndex());
        if (mode >= 0) operation->setSelectedItemIndex(mode);
        window->closeWindow(window->findChild(mode < 0 ? "cancel" : "ok"));
      }
    });
    answer.start();
    IndexedConversionOptions options;
    EXPECT_EQ(mode >= 0, choose_palette_bank_conversion(a, 2, options));
    answer.stop();
    EXPECT_TRUE(shown);
    EXPECT_EQ(mode == 2, options.copyBankColors);
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
  check_document_palette_paste();
  check_bank_color_picker();
  app.run(false);
}

static void check_document_palette_paste()
{
  auto* context = UIContext::instance();
  auto* document = context->documents().add(8, 8, ColorMode::INDEXED);
  context->setActiveDocument(document);
  auto* sprite = document->sprite();
  auto* unbanked = sprite->firstLayer();
  auto* zero = new LayerImage(sprite);
  auto* one = new LayerImage(sprite);
  sprite->root()->addLayer(zero);
  sprite->root()->addLayer(one);
  // Simulate an older file whose bank 00 still aliases document slots 0-15.
  zero->userData().properties(kPaletteBankProperties)["bank"] = int8_t(0);
  one->userData().properties(kPaletteBankProperties)["bank"] = int8_t(1);
  for (int i = 0; i < 256; ++i)
    sprite->palette(0)->setEntry(i, rgba(i%16*17, i/16*17, 68, 255));
  sprite->setTotalFrames(2);
  Palette later(*sprite->palette(0));
  later.setFrame(1);
  later.setEntry(3, rgba(0, 255, 0, 255));
  sprite->setPalette(&later, true);
  Params force;
  force.set("enabled", "true"); force.set("ui", "false");
  context->executeCommand(Commands::instance()->byId("ForceKarnRules"), force);
  const Palette laterZero(*zero->palette(1));
  const Palette zeroBefore(*zero->palette(0));
  const Palette source(*one->palette(0));
  const Palette documentBefore(*unbanked->palette(0));
  auto* view = ColorBar::instance()->getPaletteView();
  context->setActiveLayer(one);
  view->selectRange(0, 15);
  view->copyToClipboard();
  context->setActiveLayer(unbanked);
  view->selectRange(0, 15);
  view->pasteFromClipboard();
  EXPECT_EQ(source, *unbanked->palette(0));
  EXPECT_EQ(zeroBefore, *zero->palette(0));
  EXPECT_EQ(source, *one->palette(0));
  EXPECT_EQ(laterZero, *zero->palette(1));
  document->undoHistory()->undo();
  EXPECT_EQ(documentBefore, *unbanked->palette(0));
  EXPECT_EQ(zeroBefore, *zero->palette(0));
  document->undoHistory()->redo();
  EXPECT_EQ(source, *unbanked->palette(0));
  EXPECT_EQ(zeroBefore, *zero->palette(0));
  context->setActiveLayer(zero);
  view->selectRange(0, 15);
  view->pasteFromClipboard();
  EXPECT_EQ(source, *zero->palette(0));
  EXPECT_EQ(source, *unbanked->palette(0));
  const Palette masterBefore(*sprite->palette(0));
  Palette changed(*zero->palette(0));
  changed.setEntry(3, rgba(255, 0, 255, 255));
  view->delegate()->onPaletteViewModification(&changed, PaletteViewModification::CLEAR);
  EXPECT_EQ(changed, *zero->palette(0));
  EXPECT_EQ(masterBefore, *sprite->palette(0));
  EXPECT_EQ(laterZero, *zero->palette(1));
  document->undoHistory()->undo();
  EXPECT_EQ(source, *zero->palette(0));
  document->undoHistory()->redo();
  EXPECT_EQ(changed, *zero->palette(0));
  EXPECT_EQ(masterBefore, *sprite->palette(0));
  ImageRef pixels(Image::create(IMAGE_INDEXED, 1, 1)); pixels->clear(3);
  zero->addCel(new Cel(0, pixels));
  ImageRef preview(Image::create(IMAGE_RGB, 8, 8));
  render::Render().renderSprite(preview.get(), sprite, 0);
  EXPECT_EQ(changed.getEntry(3), preview->getPixel(0, 0));
  // Even slot zero has opaque RGB in KARN, but it remains the mask index.
  changed.setEntry(0, rgba(255, 0, 255, 0));
  changed.setEntry(1, rgba(17, 34, 51, 0));
  view->delegate()->onPaletteViewModification(&changed, PaletteViewModification::CLEAR);
  EXPECT_EQ(rgba(255, 0, 255, 255), zero->palette(0)->getEntry(0));
  EXPECT_EQ(rgba(17, 34, 51, 255), zero->palette(0)->getEntry(1));
  EXPECT_EQ(masterBefore, *sprite->palette(0));
  pixels->putPixel(0, 0, 0);
  preview->clear(0);
  render::Render().renderSprite(preview.get(), sprite, 0);
  EXPECT_EQ(0, rgba_geta(preview->getPixel(0, 0)));
  // Clearing a numbered bank covers every native animation palette.
  const Palette firstBefore(*sprite->palette(0));
  const Palette secondBefore(*sprite->palette(1));
  { Tx tx(document, "Clear unused bank"); clear_palette_bank(tx, sprite, 3); tx.commit(); }
  for (int f = 0; f < 2; ++f)
    for (int i = 48; i < 64; ++i)
      EXPECT_EQ(rgba(0, 0, 0, 255), sprite->palette(f)->getEntry(i));
  document->undoHistory()->undo();
  EXPECT_EQ(firstBefore, *sprite->palette(0));
  EXPECT_EQ(secondBefore, *sprite->palette(1));
  document->close();
  delete document;
}

static void check_bank_color_picker()
{
  auto* context = UIContext::instance();
  auto* document = context->documents().add(8, 8, ColorMode::INDEXED);
  context->setActiveDocument(document);
  auto* sprite = document->sprite();
  auto* active = sprite->firstLayer();
  auto* top = new LayerImage(sprite);
  sprite->root()->addLayer(top);
  active->userData().properties(kPaletteBankProperties)["bank"] = int8_t(1);
  top->userData().properties(kPaletteBankProperties)["bank"] = int8_t(2);
  auto* palette = sprite->palette(0);
  for (int i = 0; i < 256; ++i) palette->setEntry(i, rgba(0, 0, 0, 255));
  palette->setEntry(18, rgba(255, 0, 0, 255));
  palette->setEntry(37, rgba(255, 0, 0, 255));
  palette->setEntry(38, rgba(0, 255, 0, 255));
  ImageRef pixels(Image::create(IMAGE_INDEXED, 8, 8));
  pixels->clear(0); pixels->putPixel(0, 0, 5); pixels->putPixel(1, 0, 6);
  top->addCel(new Cel(0, pixels));
  context->setActiveLayer(active); // No cel on the destination layer.
  const Site site = context->activeSite();
  auto& pref = Preferences::instance();
  const auto oldSample = pref.eyedropper.sample();
  const auto oldChannel = pref.eyedropper.channel();
  EyedropperCommand command;
  for (auto sample : { app::gen::EyedropperSample::ALL_LAYERS,
                       app::gen::EyedropperSample::CURRENT_LAYER,
                       app::gen::EyedropperSample::FIRST_REFERENCE_LAYER }) {
    pref.eyedropper.sample(sample);
    for (auto channel : { app::gen::EyedropperChannel::COLOR_ALPHA,
                          app::gen::EyedropperChannel::RGB,
                          app::gen::EyedropperChannel::INDEX }) {
      pref.eyedropper.channel(channel);
      app::Color selected = app::Color::fromIndex(7);
      tile_t tile = notile;
      EXPECT_TRUE(command.pickSample(site, gfx::PointF(0, 0), render::Projection(), selected, tile));
      EXPECT_EQ(app::Color::fromIndex(2), selected);
      EXPECT_FALSE(command.pickSample(site, gfx::PointF(1, 0), render::Projection(), selected, tile));
      EXPECT_EQ(app::Color::fromIndex(2), selected); // A failure is a no-op.
    }
  }
  ColorPicker picker;
  picker.pickColor(site, gfx::PointF(1, 0), render::Projection(), ColorPicker::FromComposition);
  EXPECT_FALSE(picker.available());
  EXPECT_EQ(top, picker.layer()); // Auto-select-layer still sees the topmost cel.
  picker.pickColor(site, gfx::PointF(0, 0), render::Projection(), ColorPicker::FromComposition);
  EXPECT_TRUE(picker.available());
  EXPECT_EQ(app::Color::fromIndex(2), picker.color());
  auto* theme = app::skin::SkinTheme::instance();
  const auto* cursor = theme->getUnavailableColorCursor();
  ASSERT_TRUE(cursor && cursor->surface());
  EXPECT_EQ(cursor, theme->getUnavailableColorCursor());
  EXPECT_EQ(gfx::Point(5 * ui::guiscale(), 5 * ui::guiscale()), cursor->focus());
  EXPECT_EQ(gfx::rgba(230, 32, 32, 255), cursor->surface()->getPixel(cursor->focus().x, cursor->focus().y));
  EXPECT_EQ(0, gfx::geta(cursor->surface()->getPixel(0, 0)));
  // The toolbar eyedropper and Alt's temporary quick tool must use the same
  // command/cursor path, for both foreground and background picks.
  auto* editor = Editor::activeEditor();
  ASSERT_TRUE(editor);
  auto* tools = App::instance()->activeToolManager();
  auto* oldTool = tools->selectedTool();
  auto* dropper = App::instance()->toolBox()->getToolById("eyedropper");
  auto* pencil = App::instance()->toolBox()->getToolById("pencil");
  auto* spray = App::instance()->toolBox()->getToolById("spray");
  StandbyState standby;
  for (auto* selected : { dropper, pencil, spray }) {
    tools->newToolSelectedInToolBar(selected);
    if (selected != dropper) tools->newQuickToolSelectedFromEditor(dropper);
    EXPECT_TRUE(editor->getCurrentEditorInk()->isEyedropper());
    const gfx::Point available = editor->editorToScreen(gfx::Point(0, 0));
    const gfx::Point unavailable = editor->editorToScreen(gfx::Point(1, 0));
    EXPECT_TRUE(standby.onSetCursor(editor, unavailable));
    pref.colorBar.fgColor(app::Color::fromIndex(7));
    pref.colorBar.bgColor(app::Color::fromIndex(8));
    command.executeOnMousePos(context, editor, unavailable, true);
    command.executeOnMousePos(context, editor, unavailable, false);
    EXPECT_EQ(app::Color::fromIndex(7), pref.colorBar.fgColor());
    EXPECT_EQ(app::Color::fromIndex(8), pref.colorBar.bgColor());
    command.executeOnMousePos(context, editor, available, true);
    command.executeOnMousePos(context, editor, available, false);
    EXPECT_EQ(app::Color::fromIndex(2), pref.colorBar.fgColor());
    EXPECT_EQ(app::Color::fromIndex(2), pref.colorBar.bgColor());
    tools->newQuickToolSelectedFromEditor(nullptr);
  }
  tools->newToolSelectedInToolBar(oldTool);
  ui::set_mouse_cursor(ui::kArrowCursor);
  pref.eyedropper.sample(oldSample);
  pref.eyedropper.channel(oldChannel);
  document->close();
  delete document;
}

int app_main(int argc, char* argv[])
{
  // These fixtures directly create/delete documents instead of following the
  // UI's deferred recovery-owned destruction. Do not back them up in parallel.
  const auto profile = std::filesystem::temp_directory_path() /
    ("aseprite-palette-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(profile);
  { std::ofstream config(profile / "aseprite.ini"); config << "[general]\ndata_recovery = false\n"; }
#ifdef _WIN32
  _putenv_s("ASEPRITE_USER_FOLDER", profile.string().c_str());
#else
  setenv("ASEPRITE_USER_FOLDER", profile.string().c_str(), 1);
#endif
  Palette::initBestfit();
  g_exeName = argv[0];
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
