// Aseprite
// Copyright (C) 2024  Igara Studio S.A.
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <gtest/gtest.h>

#include "app/app.h"
#include "app/app_brushes.h"
#include "app/cli/app_options.h"
#include "app/commands/commands.h"
#include "app/context.h"
#include "app/doc.h"
#include "app/doc_undo.h"
#include "app/modules/palettes.h"
#include "app/pref/preferences.h"
#include "app/site.h"
#include "app/test_context.h"
#include "app/tools/tool_box.h"
#include "app/tools/tool_loop_manager.h"
#include "app/ui/context_bar.h"
#include "app/ui/editor/tool_loop_impl.h"
#include "app/ui/main_window.h"
#include "doc/cel.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/palette.h"
#include "doc/sprite.h"
#include "os/system.h"
#include "os/window.h"
#include "ui/display.h"
#include "ui/manager.h"
#include "ui/system.h"
#include "ui/window.h"

using namespace app;

const char* g_exeName = nullptr;

TEST(App, ShadingOncePerStroke)
{
  os::SystemRef system = os::System::make();
  const char* argv[] = { g_exeName };
  app::AppOptions options(sizeof(argv) / sizeof(argv[0]), argv);
  app::App app;
  app.initialize(options);

  // Use real UI ink/ramp settings without creating editor tabs for test documents.
  struct ShadingContext : TestContext {
    bool isUIAvailable() const override { return true; }
  } context;
  auto* ctx = &context;
  auto* tool = app.toolBox()->getToolById("pencil");
  auto& pref = Preferences::instance();
  auto& toolPref = pref.tool(tool);
  EXPECT_FALSE(toolPref.shadingOncePerStroke());
  pref.shared.shareDynamics(true);
  pref.colorBar.bgColor(Color::fromIndex(1));

  // Equal RGB values deliberately distinguish index remapping from color matching.
  for (auto sensor : { tools::DynamicSensor::Static,
                       tools::DynamicSensor::Pressure,
                       tools::DynamicSensor::Velocity }) {
    pref.tool(nullptr).dynamics.gradient(sensor);
    for (int mode : { 0, 1, 2 }) {
      const bool once = (mode == 1);
      const bool reentry = (mode == 2);
      toolPref.shadingOnReentry(reentry);
      toolPref.shadingOncePerStroke(once);
      for (bool left : { false, true }) {
        SCOPED_TRACE(::testing::Message() << "sensor=" << int(sensor)
                                         << " once=" << once << " left=" << left);
        auto* document = ctx->documents().add(24, 24, doc::ColorMode::INDEXED);
        auto* sprite = document->sprite();
        auto* layer = static_cast<doc::LayerImage*>(sprite->root()->firstLayer());
        for (int i = 1; i <= 12; ++i)
          sprite->palette(0)->setEntry(i, doc::rgba(128, 128, 128, 255));
        set_current_palette(sprite->palette(0), true);
        Shade ramp;
        for (int i = 1; i <= 12; ++i)
          ramp.push_back(Color::fromIndex(i));
        const auto slot = app.brushes().addBrushSlot(
          BrushSlot(BrushSlot::Flags::Shade, nullptr, Color::fromMask(), Color::fromMask(),
                    tools::InkType::SHADING, 255, ramp));
        app.contextBar()->setActiveBrushBySlot(tool, slot);
        app.brushes().removeBrushSlot(slot);
        ASSERT_EQ(12, app.contextBar()->getShade().size());

        Site site;
        site.document(document);
        site.sprite(sprite);
        site.layer(layer);
        const int initial = left ? 10 : 2;
        layer->cel(0)->image()->clear(initial);
        auto pixel = [&](int x = 8, int y = 8) {
          const auto* cel = layer->cel(0);
          return cel ? cel->image()->getPixel(x - cel->x(), y - cel->y()) : 0;
        };
        int nominalSize = 7;
        bool clickOnly = false;
        bool dynamicSize = false;
        bool imageBrush = false;
        bool squareBrush = false;
        bool pixelPerfect = false;
        bool promoteEdge = false;
        int visits = 1;
        auto stroke = [&](bool cancel = false) {
          ToolLoopParams params;
          params.tool = tool;
          params.ink = app.toolBox()->getInkById("shading");
          params.inkType = tools::InkType::SHADING;
          params.fg = Color::fromIndex(1);
          params.bg = Color::fromIndex(1);
          params.button = left ? tools::ToolLoop::Left : tools::ToolLoop::Right;
          params.controller = tool->getController(params.button);
          params.freehandAlgorithm = pixelPerfect ? tools::FreehandAlgorithm::PIXEL_PERFECT :
                                                   tools::FreehandAlgorithm::DEFAULT;
          params.brush.reset(new doc::Brush(squareBrush ? doc::kSquareBrushType : doc::kCircleBrushType, nominalSize, 0));
          if (imageBrush) {
            doc::ImageRef stamp(doc::Image::create(IMAGE_INDEXED, 7, 7));
            stamp->clear(1);
            params.brush->setImage(stamp.get(), nullptr);
          }
          std::unique_ptr<tools::ToolLoop> loop(create_tool_loop_for_script(ctx, site, params));
          ASSERT_TRUE(loop);
          EXPECT_EQ(imageBrush || !reentry || nominalSize < 3 ? nominalSize : nominalSize + 2,
                    loop->getBrush()->size());
          tools::ToolLoopManager manager(loop.get());
          auto pointer = [&](int x, int y = 8, float pressure = 1.0f) {
            return tools::Pointer(gfx::Point(x, y), tools::Vec2(1, 0),
                                  left ? tools::Pointer::Left : tools::Pointer::Right,
                                  tools::Pointer::Type::Pen, pressure);
          };
          manager.prepareLoop(pointer(8));
          manager.pressButton(pointer(8));
          if (!clickOnly) {
            manager.movement(pointer(9, 8, dynamicSize ? 0.0f : 1.0f));
            manager.movement(pointer(8));
          }
          if (promoteEdge) {
            manager.movement(pointer(8, 9));
            manager.movement(pointer(8));
          }
          for (int visit = 1; visit < visits; ++visit) {
            manager.movement(pointer(15));
            manager.movement(pointer(8));
            manager.movement(pointer(9));
            manager.movement(pointer(8));
          }
          manager.releaseButton(pointer(8));
          if (cancel)
            manager.cancel();
          manager.end();
          EXPECT_EQ(nominalSize, params.brush->size());
        };

        const int step = left ? -1 : 1;
        const int steps = (once || reentry || sensor == tools::DynamicSensor::Static) ? 1 : 3;
        stroke();
        EXPECT_EQ(initial + step * steps, pixel());
        if (once || reentry) {
          stroke();
          EXPECT_EQ(initial + 2 * step, pixel());
          document->undoHistory()->undo();
          EXPECT_EQ(initial + step, pixel());
          document->undoHistory()->redo();
          EXPECT_EQ(initial + 2 * step, pixel());
          stroke(true);
          EXPECT_EQ(initial + 2 * step, pixel());
        }
        imageBrush = true;
        layer->cel(0)->image()->clear(initial);
        stroke();
        EXPECT_EQ(initial + step * steps, pixel());
        if (once || reentry) {
          visits = 5;
          for (bool stamp : { false, true }) {
            imageBrush = stamp;
            layer->cel(0)->image()->clear(initial);
            stroke();
            EXPECT_EQ(initial + step * (reentry ? 5 : 1), pixel());
            document->undoHistory()->undo();
            EXPECT_EQ(initial, pixel());
            document->undoHistory()->redo();
            EXPECT_EQ(initial + step * (reentry ? 5 : 1), pixel());
          }
          if (reentry) {
            // The mirrored brush takes over coverage as the main brush leaves.
            // Neither reversing direction nor changing which copy covers the
            // pixel should count as a new visit.
            pref.symmetryMode.enabled(true);
            pref.document(document).symmetry.mode(gen::SymmetryMode::HORIZONTAL);
            pref.document(document).symmetry.xAxis(12);
            imageBrush = false;
            layer->cel(0)->image()->clear(initial);
            stroke();
            EXPECT_EQ(initial + step, pixel());
            pref.symmetryMode.enabled(false);
          }
          visits = 1;
        }
        for (int unchanged : { 0, 13, left ? 1 : 12 }) {
          layer->cel(0)->image()->clear(unchanged == 0 ? 13 : unchanged);
          layer->cel(0)->image()->putPixel(8, 8, unchanged);
          stroke();
          EXPECT_EQ(unchanged, pixel());
        }
        if (reentry) {
          squareBrush = true;
          for (bool perfect : { false, true }) {
            pixelPerfect = perfect;
            for (bool stamp : { false, true }) {
              imageBrush = stamp;
              const int edgeY = stamp ? 11 : 12;
              for (int passes : { 1, 2, 5 }) {
                visits = passes;
                layer->cel(0)->image()->clear(initial);
                stroke();
                EXPECT_EQ(initial + step * (passes / 2), pixel(8, edgeY));
                EXPECT_EQ(initial + step * passes, pixel(8, edgeY - 1));
                EXPECT_EQ(initial + step * passes, pixel());
                document->undoHistory()->undo();
                EXPECT_EQ(initial, pixel(8, edgeY));
                document->undoHistory()->redo();
                EXPECT_EQ(initial + step * (passes / 2), pixel(8, edgeY));
              }
              visits = 1;
              promoteEdge = true;
              layer->cel(0)->image()->clear(initial);
              stroke();
              EXPECT_EQ(initial + step, pixel(8, edgeY));
              promoteEdge = false;
            }
          }
        }
        imageBrush = false;
        squareBrush = true;
        pixelPerfect = false;
        visits = 1;
        clickOnly = true;
        for (int size : { 1, 2, 3, 4, doc::Brush::kMaxBrushSize }) {
          nominalSize = size;
          layer->cel(0)->image()->clear(initial);
          stroke();
          EXPECT_EQ(initial + step, pixel());
          if (reentry && (size == 3 || size == 4)) {
            EXPECT_EQ(initial, pixel(10, 8)); // Extra outer ring is a half-pass.
            EXPECT_EQ(initial + step, pixel(9, 8)); // Nominal core is fully shaded.
          }
        }
        if (reentry) {
          clickOnly = false;
          nominalSize = 3;
          dynamicSize = true;
          pref.tool(nullptr).dynamics.size(tools::DynamicSensor::Pressure);
          pref.tool(nullptr).dynamics.minSize(1);
          layer->cel(0)->image()->clear(initial);
          stroke(); // Physical sizes 5 -> 1 -> 5; core leaves and returns.
          EXPECT_EQ(initial + 2 * step, pixel());
          EXPECT_EQ(initial + step, pixel(10, 8));
          pref.tool(nullptr).dynamics.size(tools::DynamicSensor::Static);
        }
        // TestContext does not perform UIContext preference cleanup.
        pref.removeDocument(document);
        document->close();
        delete document;
      }
    }
  }
  toolPref.shadingOncePerStroke(false);
  toolPref.shadingOnReentry(false);
  pref.tool(nullptr).dynamics.gradient(tools::DynamicSensor::Static);
  app.run(false);
}

int app_main(int argc, char* argv[])
{
  g_exeName = argv[0];
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
