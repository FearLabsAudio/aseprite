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
#include "app/cmd/set_pixel_format.h"
#include "app/cmd/flatten_layers.h"
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
#include "ui/message.h"
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

TEST(IndexedConversion, AdjustsRgbBeforeDitheringWithoutChangingSourceOrAlpha)
{
  Palette palette(0, 16);
  for (int i = 0; i < 16; ++i) palette.setEntry(i, rgba(0, 0, 0, 255));
  palette.setEntry(1, rgba(255, 255, 255, 255));
  ImageRef source(Image::create(IMAGE_RGB, 32, 8));
  source->clear(rgba(100, 100, 100, 255));
  source->putPixel(7, 3, rgba(91, 22, 33, 0));
  const ImageRef before(Image::createCopy(source.get()));
  ImageRef expectedSource(Image::createCopy(source.get()));
  expectedSource->clear(rgba(200, 200, 200, 255));
  expectedSource->putPixel(7, 3, source->getPixel(7, 3));
  const Palette paletteBefore(palette);
  for (auto algorithm : {render::DitheringAlgorithm::None, render::DitheringAlgorithm::Ordered,
                         render::DitheringAlgorithm::ErrorDiffusion}) {
    IndexedConversionOptions options;
    options.fit = FitCriteria::RGB;
    options.dithering = render::Dithering(algorithm, render::BayerMatrix::make(8), 0.65);
    auto expected = convert_to_indexed(expectedSource.get(), palette, 0, options);
    auto unadjusted = convert_to_indexed(source.get(), palette, 0, options);
    options.adjustment.brightness = 1.0;
    auto result = convert_to_indexed(source.get(), palette, 0, options);
    EXPECT_EQ(0, count_diff_between_images(expected.get(), result.get()));
    EXPECT_GT(count_diff_between_images(unadjusted.get(), result.get()), 0);
    EXPECT_EQ(0, result->getPixel(7, 3));
    EXPECT_EQ(0, count_diff_between_images(before.get(), source.get()));
    EXPECT_EQ(paletteBefore, palette);
  }
  source->putPixel(0, 0, rgba(64, 128, 192, 117));
  auto adjusted = RgbConversionAdjustment{0.5, 0.5}.makeAdjustedCopy(source.get());
  EXPECT_EQ(rgba(48, 192, 255, 117), adjusted->getPixel(0, 0));
  EXPECT_EQ(source->getPixel(7, 3), adjusted->getPixel(7, 3));
  EXPECT_FALSE(RgbConversionAdjustment{}.makeAdjustedCopy(source.get()));
}

TEST(IndexedConversion, RgbAdjustmentDialogsPreviewApplyCancelAndUndo)
{
  auto system = os::System::make();
  const char* argv[] = {g_exeName};
  AppOptions args(1, argv);
  App app;
  app.initialize(args);
  disable_test_update_check();
  auto* context = UIContext::instance();
  for (int mode : {0, 1, 2}) { // Color Mode, Color Mode + flatten, RGB -> bank.
    const bool bank = mode == 2;
    auto* document = context->documents().add(20, 8, bank ? ColorMode::INDEXED : ColorMode::RGB);
    auto* sprite = document->sprite();
    auto* bottom = static_cast<LayerImage*>(sprite->firstLayer());
    auto* layer = new LayerImage(sprite);
    sprite->root()->addLayer(layer);
    auto* palette = sprite->palette(0);
    palette->resize(bank ? 32 : 16);
    for (int i = 0; i < palette->size(); ++i) palette->setEntry(i, rgba(0, 0, 0, 255));
    palette->setEntry(1, rgba(255, 255, 255, 255));
    if (bank) {
      palette->setEntry(17, rgba(255, 255, 255, 255));
      sprite->userData().properties(kPaletteBankProperties)["initialized"] = int32_t(3);
      bottom->userData().properties(kPaletteBankProperties)["bank"] = uint8_t(1);
      layer->userData().properties(kPaletteBankProperties)["bank"] = int8_t(-2);
    }
    bottom->cel(0)->image()->clear(bank ? 2 : rgba(20, 40, 70, 255));
    ImageRef source(Image::create(IMAGE_RGB, 16, 4));
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 16; ++x)
        source->putPixel(x, y, rgba(40 + x*10, 40 + x*10, 40 + x*10, 255));
    source->putPixel(7, 2, rgba(45, 67, 89, 0));
    auto* cel = new Cel(0, source);
    cel->setPosition(2, 2);
    layer->addCel(cel);
    sprite->setTotalFrames(2);
    bottom->addCel(Cel::MakeLink(1, bottom->cel(0)));
    layer->addCel(Cel::MakeLink(1, cel));
    context->setActiveLayer(layer);
    context->setActiveFrame(1);
    const ImageRef original(Image::createCopy(source.get()));
    const Palette originalPalette(*palette);
    const auto history = document->undoHistory()->currentState();

    const auto expected = [&](double brightness) {
      Context privateContext;
      auto* copy = privateContext.documents().add(document->duplicate(DuplicateExactCopy));
      IndexedConversionOptions options;
      options.fit = FitCriteria::RGB;
      options.dithering = render::Dithering(render::DitheringAlgorithm::ErrorDiffusion,
                                           render::DitheringMatrix(), 0.65);
      options.adjustment = {brightness, -0.3};
      Tx tx(copy);
      if (bank)
        set_layer_palette_bank(tx, copy->sprite()->allLayers().back(), 1, options);
      else {
        if (mode == 1) {
          SelectedLayers layers;
          for (auto* l : copy->sprite()->root()->layers()) layers.insert(l);
          cmd::FlattenLayers::Options flattenOptions;
          flattenOptions.newBlendMethod = Preferences::instance().experimental.newBlend();
          tx(new cmd::FlattenLayers(copy->sprite(), layers, flattenOptions));
        }
        tx(new cmd::SetPixelFormat(copy->sprite(), IMAGE_INDEXED, options.dithering,
                                   options.mapping, nullptr, nullptr, options.fit, options.adjustment));
      }
      ImageRef image(Image::create(IMAGE_RGB, 20, 8));
      render::Render renderer;
      renderer.setNewBlend(Preferences::instance().experimental.newBlend());
      renderer.setComposeGroups(true);
      renderer.renderSprite(image.get(), copy->sprite(), 1);
      return image;
    };
    const auto expectedNormal = expected(0.4);
    const auto expectedBlack = expected(-1.0);
    ASSERT_GT(count_diff_between_images(expectedNormal.get(), expectedBlack.get()), 0);
    auto surface = system->makeRgbaSurface(20, 8);
    const auto draw = [&] {
      auto& engine = Editor::renderEngine();
      engine.setProjection(render::Projection());
      engine.setNonactiveLayersOpacity(255);
      engine.setSelectedLayer(layer);
      engine.renderSprite(surface.get(), sprite, 1, gfx::Clip(0, 0, sprite->bounds()));
    };
    const auto matches = [&](const ImageRef& expectedImage) {
      draw();
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 20; ++x) {
          const auto c = expectedImage->getPixel(x, y);
          if (surface->getPixel(x, y) != gfx::rgba(rgba_getr(c), rgba_getg(c), rgba_getb(c)))
            return false;
        }
      return true;
    };
    for (bool accept : {false, true}) {
      int stage = 0;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      ui::Timer poll(30);
      poll.Tick.connect([&] {
        auto* window = ui::Manager::getDefault()->getTopWindow();
        auto* brightness = dynamic_cast<ui::Slider*>(window->findChild("brightness"));
        if (!brightness) return;
        if (std::chrono::steady_clock::now() > deadline) {
          ADD_FAILURE() << "RGB adjustment preview timed out: mode " << mode << " stage " << stage;
          poll.stop(); window->closeWindow(nullptr); return;
        }
        EXPECT_EQ(history, document->undoHistory()->currentState());
        EXPECT_EQ(originalPalette, *sprite->palette(0));
        EXPECT_EQ(0, count_diff_between_images(original.get(), layer->cel(0)->image()));
        if (stage == 0) {
          EXPECT_TRUE(brightness->isVisible());
          EXPECT_EQ(0, brightness->getValue());
          auto* contrast = static_cast<ui::Slider*>(window->findChild("contrast"));
          EXPECT_EQ(0, contrast->getValue());
          auto* fit = static_cast<ui::ComboBox*>(window->findChild("best_fit_criteria_selector"));
          fit->setSelectedItemIndex(1); // RGB distance, deterministic fixture.
          auto* dither = static_cast<DitheringSelector*>(window->findChild("dithering_selector"));
          dither->setSelectedItemIndex(dither->getItemCount()-1);
          auto* factor = static_cast<ui::Slider*>(window->findChild("factor"));
          factor->setValue(65); factor->Change();
          brightness->setValue(40); brightness->Change();
          contrast->setValue(-30); contrast->Change();
          auto* flatten = static_cast<ui::CheckBox*>(window->findChild("flatten"));
          flatten->setSelected(mode == 1); flatten->Click();
          ++stage;
        }
        else if (stage == 1 && matches(expectedNormal)) {
          // Exercise the slider's input path, not just its public Change signal.
          brightness->requestFocus();
          ui::KeyMessage home(ui::kKeyDownMessage, ui::kKeyHome, ui::kKeyNoneModifier, 0, 0);
          brightness->sendMessage(&home);
          EXPECT_EQ(-100, brightness->getValue());
          EXPECT_TRUE(matches(expectedNormal)); // Keep the published image during recalculation.
          ++stage;
        }
        else if (stage == 2 && matches(expectedBlack)) {
          brightness->setValue(40); brightness->Change();
          EXPECT_TRUE(matches(expectedBlack));
          ++stage;
        }
        else if (stage == 3 && matches(expectedNormal)) {
          ++stage;
          poll.stop();
          window->closeWindow(accept ? window->findChild("ok") : nullptr);
        }
      });
      poll.start();
      if (bank) {
        IndexedConversionOptions options;
        EXPECT_EQ(accept, choose_palette_bank_conversion(layer, 1, options));
        if (accept) {
          EXPECT_DOUBLE_EQ(0.4, options.adjustment.brightness);
          EXPECT_DOUBLE_EQ(-0.3, options.adjustment.contrast);
          Tx tx(sprite); set_layer_palette_bank(tx, layer, 1, options); tx.commit();
        }
      }
      else {
        Params params; params.set("ui", "true");
        context->executeCommand(Commands::instance()->byId("ChangePixelFormat"), params);
      }
      poll.stop();
      ASSERT_EQ(4, stage);
      if (accept) {
        ASSERT_NE(history, document->undoHistory()->currentState());
        // Flattening suspends the old layer, so use the surviving active layer.
        auto* savedLayer = layer;
        if (mode == 1) layer = static_cast<LayerImage*>(sprite->firstLayer());
        EXPECT_TRUE(matches(expectedNormal));
        document->undoHistory()->undo();
        layer = savedLayer;
      }
      EXPECT_EQ(history, document->undoHistory()->currentState());
      EXPECT_EQ(0, count_diff_between_images(original.get(), layer->cel(0)->image()));
      EXPECT_EQ(originalPalette, *sprite->palette(0));
    }
    document->close(); delete document;
  }
  app.run(false);
}

TEST(IndexedConversion, PastePreviewPreservesExistingHolesAndLastCompletedImage)
{
  auto system = os::System::make();
  const char* argv[] = {g_exeName};
  AppOptions args(1, argv);
  App app;
  app.initialize(args);
  disable_test_update_check();
  auto* context = UIContext::instance();
  for (bool tilemap : {false, true}) {
    auto* document = context->documents().add(20, 12, ColorMode::INDEXED);
    auto* sprite = document->sprite();
    auto* palette = sprite->palette(0);
    palette->resize(32);
    for (int i = 0; i < 32; ++i) palette->setEntry(i, rgba(255, 0, 255, 255));
    palette->setEntry(1, rgba(20, 60, 100, 255));
    palette->setEntry(17, rgba(0, 0, 0, 255));
    palette->setEntry(18, rgba(255, 255, 255, 255));
    sprite->firstLayer()->cel(0)->image()->clear(1);
    sprite->userData().properties(kPaletteBankProperties)["initialized"] = int32_t(3);
    ImageRef pixels(Image::create(IMAGE_INDEXED, 4, 4));
    pixels->clear(2);
    pixels->putPixel(3, 0, 0);
    pixels->putPixel(3, 3, 0);
    LayerImage* layer;
    ImageRef celImage;
    if (tilemap) {
      auto* tileset = new Tileset(sprite, Grid(gfx::Size(4, 4)), 2);
      tileset->set(1, pixels);
      layer = new LayerTilemap(sprite, sprite->tilesets()->add(tileset));
      celImage.reset(Image::create(IMAGE_TILEMAP, 2, 1));
      celImage->putPixel(0, 0, tile(1, 0));
      celImage->putPixel(1, 0, tile(1, tile_f_xflip));
    }
    else {
      layer = new LayerImage(sprite);
      celImage = pixels;
    }
    sprite->root()->addLayer(layer);
    layer->userData().properties(kPaletteBankProperties)["bank"] = uint8_t(1);
    auto* cel = new Cel(0, celImage);
    cel->setPosition(8, 4);
    layer->addCel(cel);
    context->setActiveLayer(layer);
    const auto history = document->undoHistory()->currentState();
    const ImageRef original(Image::createCopy(celImage.get()));
    ImageRef source(Image::create(IMAGE_RGB, 5, 3));
    source->clear(rgba(180, 180, 180, 255));
    source->putPixel(2, 1, 0);
    const gfx::Point position(6, 5); // Extends beyond the original cel and overlaps it.
    const auto bankPalette = make_bank_palette(sprite, 0, 1);
    ImageRef base(Image::create(IMAGE_RGB, 20, 12));
    render::Render renderer;
    renderer.renderSprite(base.get(), sprite, 0);
    const auto expected = [&](double brightness) {
      IndexedConversionOptions options;
      options.fit = FitCriteria::RGB;
      options.dithering = render::Dithering(render::DitheringAlgorithm::ErrorDiffusion,
                                           render::DitheringMatrix(), 0.44);
      options.adjustment.brightness = brightness;
      auto converted = convert_to_indexed(source.get(), bankPalette, 0, options);
      ImageRef result(Image::createCopy(base.get()));
      for (int y = 0; y < source->height(); ++y)
        for (int x = 0; x < source->width(); ++x) {
          const int index = converted->getPixel(x, y);
          if (index) result->putPixel(position.x+x, position.y+y, bankPalette.getEntry(index));
        }
      return result;
    };
    const auto normal = expected(0.0);
    const auto black = expected(-1.0);
    ASSERT_GT(count_diff_between_images(normal.get(), black.get()), 0);
    auto surface = system->makeRgbaSurface(20, 12);
    const auto matches = [&](const ImageRef& image) {
      auto& engine = Editor::renderEngine();
      engine.setProjection(render::Projection());
      engine.setNonactiveLayersOpacity(255);
      engine.setSelectedLayer(layer);
      engine.renderSprite(surface.get(), sprite, 0, gfx::Clip(0, 0, sprite->bounds()));
      for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 20; ++x) {
          const auto c = image->getPixel(x, y);
          if (surface->getPixel(x, y) != gfx::rgba(rgba_getr(c), rgba_getg(c), rgba_getb(c)))
            return false;
        }
      return true;
    };
    for (bool accept : {false, true}) {
      int stage = 0;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      ui::Timer poll(20);
      poll.Tick.connect([&] {
        auto* window = ui::Manager::getDefault()->getTopWindow();
        auto* brightness = dynamic_cast<ui::Slider*>(window->findChild("brightness"));
        if (!brightness) return;
        if (std::chrono::steady_clock::now() > deadline) {
          ADD_FAILURE() << "Paste preview timed out: tilemap " << tilemap << " stage " << stage;
          poll.stop(); window->closeWindow(nullptr); return;
        }
        if (stage == 0) {
          auto* fit = static_cast<ui::ComboBox*>(window->findChild("best_fit_criteria_selector"));
          fit->setSelectedItemIndex(1);
          auto* dither = static_cast<DitheringSelector*>(window->findChild("dithering_selector"));
          dither->setSelectedItemIndex(dither->getItemCount()-1);
          auto* factor = static_cast<ui::Slider*>(window->findChild("factor"));
          factor->setValue(44); factor->Change();
          ++stage;
        }
        else if (stage == 1 && matches(normal)) {
          // Several changes before any completion is published must retain the old image.
          for (int value : {-20, 35, -100}) {
            brightness->setValue(value); brightness->Change();
            EXPECT_TRUE(matches(normal));
          }
          ++stage;
        }
        else if (stage == 2) {
          const bool completed = matches(black);
          EXPECT_TRUE(completed || matches(normal));
          if (completed) {
            brightness->setValue(0); brightness->Change();
            EXPECT_TRUE(matches(black));
            ++stage;
          }
        }
        else if (stage == 3) {
          const bool completed = matches(normal);
          EXPECT_TRUE(completed || matches(black));
          if (completed) {
            ++stage;
            poll.stop();
            window->closeWindow(accept ? window->findChild("ok") : nullptr);
          }
        }
      });
      poll.start();
      IndexedConversionOptions options;
      EXPECT_EQ(accept, choose_indexed_conversion(Editor::activeEditor(), layer, source,
                  position, bankPalette, 0, true, options));
      poll.stop();
      ASSERT_EQ(4, stage);
      EXPECT_TRUE(matches(base)); // Both OK and Cancel remove the overlay.
      EXPECT_EQ(history, document->undoHistory()->currentState());
      EXPECT_EQ(0, count_diff_between_images(original.get(), cel->image()));
    }
    document->close(); delete document;
  }
  app.run(false);
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
  palette->setEntry(2, rgba(64, 64, 64, 255));
  palette->setEntry(17, rgba(255, 255, 255, 255));
  layer->cel(0)->image()->clear(1);
  for (int x = 0; x < 32; ++x) layer->cel(0)->image()->putPixel(x, 7, 2);
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
  const auto snapshot = [&] {
    draw();
    std::vector<gfx::Color> pixels;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 32; ++x) pixels.push_back(surface->getPixel(x, y));
    return pixels;
  };
  // Independent pre-adjusted RGB values verify that the controls feed the
  // quantizer before Floyd-Steinberg, rather than adjusting its output.
  const auto adjustedPreview = [&](int mid, int dark) {
    ImageRef rgb(Image::create(IMAGE_RGB, 32, 8));
    rgb->clear(rgba(mid, mid, mid, 255));
    for (int x = 0; x < 32; ++x) rgb->putPixel(x, 7, rgba(dark, dark, dark, 255));
    const auto destination = make_bank_palette(sprite, 0, 1);
    IndexedConversionOptions settings;
    settings.match = PaletteMatch::ClosestColor;
    settings.dithering = render::Dithering(render::DitheringAlgorithm::ErrorDiffusion,
                                         render::DitheringMatrix(), 0.65);
    auto indexed = convert_to_indexed(rgb.get(), destination, 0, settings);
    std::vector<gfx::Color> pixels;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 32; ++x) {
        const auto c = destination.getEntry(indexed->getPixel(x, y));
        pixels.push_back(gfx::rgba(rgba_getr(c), rgba_getg(c), rgba_getb(c)));
      }
    pixels[0] = gfx::rgba(255, 255, 255); // Unaffected foreground layer.
    return pixels;
  };
  const auto brightPreview = adjustedPreview(192, 96); // Brightness +50%, contrast 0.
  const auto contrastPreview = adjustedPreview(193, 0); // Brightness +50%, contrast +100%.
  ASSERT_NE(brightPreview, contrastPreview);
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
      auto* brightness = static_cast<ui::Slider*>(window->findChild("brightness"));
      auto* contrast = static_cast<ui::Slider*>(window->findChild("contrast"));
      auto* preview = static_cast<ui::CheckBox*>(window->findChild("preview"));
      draw();
      EXPECT_EQ(history, document->undoHistory()->currentState());
      EXPECT_EQ(0, layer->paletteBank());
      EXPECT_EQ(before, *sprite->palette(0));
      EXPECT_EQ(1, layer->cel(0)->image()->getPixel(1, 0));
      EXPECT_EQ(gfx::rgba(255, 255, 255), surface->getPixel(0, 0)); // Other layer.
      if (stage == 1 || stage == 2) {
        // No return to the original palette during debounce or calculation.
        for (int x = 1; x < 32; ++x)
          EXPECT_NE(gfx::rgba(128, 128, 128), surface->getPixel(x, 0));
      }
      if (stage == 0) {
        if (surface->getPixel(1, 0) != gfx::rgba(255, 255, 255)) return;
        EXPECT_EQ(0, operation->getSelectedItemIndex());
        EXPECT_EQ(2, operation->getItemCount()); // Shared bank cannot be overwritten.
        EXPECT_FALSE(factor->isEnabled());
        EXPECT_EQ(0, brightness->getValue());
        EXPECT_EQ(0, contrast->getValue());
        EXPECT_FALSE(brightness->isEnabled());
        EXPECT_FALSE(contrast->isEnabled());
        const auto previous = snapshot();
        operation->setSelectedItemIndex(1);
        auto* dither = static_cast<DitheringSelector*>(window->findChild("dithering_selector"));
        dither->setSelectedItemIndex(dither->getItemCount()-1);
        dither->Change();
        EXPECT_EQ(render::DitheringAlgorithm::ErrorDiffusion, dither->ditheringAlgorithm());
        EXPECT_TRUE(factor->isEnabled());
        EXPECT_TRUE(brightness->isEnabled());
        EXPECT_TRUE(contrast->isEnabled());
        EXPECT_EQ(previous, snapshot());
        ++stage;
      }
      else if (stage == 1) {
        bool black = false, white = false;
        for (int x = 1; x < 32; ++x) {
          black |= surface->getPixel(x, 0) == gfx::rgba(0, 0, 0);
          white |= surface->getPixel(x, 0) == gfx::rgba(255, 255, 255);
        }
        if (!black || !white) return;
        const auto previous = snapshot();
        for (int strength : {44, 85, 0}) {
          factor->setValue(strength);
          factor->Change();
          EXPECT_EQ(previous, snapshot());
        }
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
      else if (stage == 3) {
        bool black = false;
        for (int x = 1; x < 32; ++x)
          black |= surface->getPixel(x, 0) == gfx::rgba(0, 0, 0);
        if (!black) return;
        const auto previous = snapshot();
        brightness->setValue(50); brightness->Change();
        EXPECT_EQ(previous, snapshot());
        ++stage;
      }
      else if (stage == 4) {
        if (snapshot() != brightPreview) return;
        contrast->setValue(100); contrast->Change();
        EXPECT_EQ(brightPreview, snapshot());
        ++stage;
      }
      else {
        if (snapshot() != contrastPreview) return;
        finalPreview = snapshot();
        poll.stop();
        window->closeWindow(window->findChild(accept ? "ok" : "cancel"));
      }
    });
    IndexedConversionOptions options;
    poll.start();
    EXPECT_EQ(accept, choose_palette_bank_conversion(layer, 1, options));
    poll.stop();
    ASSERT_EQ(5, stage);
    draw();
    EXPECT_EQ(gfx::rgba(128, 128, 128), surface->getPixel(1, 0));
    EXPECT_EQ(history, document->undoHistory()->currentState());
    if (accept) {
      EXPECT_TRUE(options.remapBankColors);
      EXPECT_DOUBLE_EQ(0.65, options.dithering.factor());
      EXPECT_DOUBLE_EQ(0.5, options.adjustment.brightness);
      EXPECT_DOUBLE_EQ(1.0, options.adjustment.contrast);
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
  options.adjustment = {0.35, -0.4};
  const auto expectedPalette = make_bank_palette(sprite, 0, 1);
  const auto expectedImage = convert_to_indexed(original.get(), expectedPalette, 0, options);
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
  EXPECT_EQ(0, count_diff_between_images(expectedImage.get(), converted.get()));
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
