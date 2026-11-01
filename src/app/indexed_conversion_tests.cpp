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

int app_main(int argc, char* argv[])
{
  Palette::initBestfit();
  g_exeName = argv[0];
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
