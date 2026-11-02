// Failures must stay inside their operation, not terminate the editor.
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gtest/gtest.h>
#include "app/app.h"
#include "app/cli/app_options.h"
#include "app/commands/commands.h"
#include "app/commands/params.h"
#include "app/doc_undo.h"
#include "app/doc.h"
#include "app/doc_observer.h"
#include "app/file/file.h"
#include "app/site.h"
#include "app/ini_file.h"
#include "app/pref/preferences.h"
#include "app/tool_presets.h"
#include "app/tools/tool_box.h"
#include "app/tools/tool.h"
#include "app/ui/color_bar.h"
#include "app/ui/editor/drawing_state.h"
#include "app/ui/editor/editor.h"
#include "app/ui/editor/editor_render.h"
#include "app/ui/editor/tool_loop_impl.h"
#include "app/ui/main_window.h"
#include "app/ui/tool_presets_dock.h"
#include "app/ui_context.h"
#include "app/util/conversion_preview_task.h"
#include "app/util/indexed_conversion.h"
#include "doc/cel.h"
#include "doc/layer.h"
#include "doc/palette.h"
#include "doc/primitives.h"
#include "os/system.h"
#include "ui/alert.h"
#include "ui/manager.h"
#include "ui/message.h"
#include "ui/timer.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>

using namespace app;
using namespace doc;
static const char* g_exeName;

TEST(CrashSafety, ConversionWorkerCapturesEveryExceptionAndJoins)
{
  ImageRef source(Image::create(IMAGE_RGB, 8, 8));
  source->clear(rgba(255, 0, 0, 255));
  ImageRef original(Image::createCopy(source.get()));
  for (int fault : {0, 1, 2}) {
    std::promise<void> started;
    auto ready = started.get_future();
    ConversionPreviewTask task([&](render::TaskDelegate& delegate) {
      delegate.notifyTaskProgress(0.25);
      started.set_value();
      if (fault == 0) throw std::runtime_error("Injected preview rendering failure");
      if (fault == 1) throw std::bad_alloc();
      throw 42;
    });
    ASSERT_EQ(std::future_status::ready, ready.wait_for(std::chrono::seconds(5)));
    task.stop();
    EXPECT_FALSE(task.isRunning());
    ASSERT_TRUE(task.error());
    if (fault == 0) EXPECT_THROW(std::rethrow_exception(task.error()), std::runtime_error);
    if (fault == 1) EXPECT_THROW(std::rethrow_exception(task.error()), std::bad_alloc);
    if (fault == 2) EXPECT_THROW(std::rethrow_exception(task.error()), int);
    task.stop(); // Joining twice must also be safe.
    EXPECT_EQ(0, count_diff_between_images(source.get(), original.get()));
  }
  { // Destruction without polling/joining must not terminate the process either.
    ConversionPreviewTask task([](render::TaskDelegate&) { throw std::bad_alloc(); });
  }
  Palette palette(0, 16);
  palette.setEntry(1, rgba(255, 0, 0, 255));
  ImageRef result;
  std::promise<void> completed;
  auto ready = completed.get_future();
  ConversionPreviewTask healthy([&](render::TaskDelegate& delegate) {
    result = convert_to_indexed(source.get(), palette, 0, {}, &delegate);
    completed.set_value();
  });
  ASSERT_EQ(std::future_status::ready, ready.wait_for(std::chrono::seconds(5)));
  healthy.stop();
  EXPECT_FALSE(healthy.error());
  ASSERT_TRUE(result);
  EXPECT_EQ(1, result->getPixel(4, 4));
  EXPECT_EQ(0, count_diff_between_images(source.get(), original.get()));
}

TEST(CrashSafety, RejectedStrokeRollsBackOnceAndTheNextStrokeWorks)
{
  os::SystemRef system = os::System::make();
  const char* argv[] = {g_exeName};
  AppOptions options(1, argv);
  App app;
  app.initialize(options);
  auto* ctx = UIContext::instance();
  auto* document = ctx->documents().add(8, 8, ColorMode::INDEXED);
  ctx->setActiveDocument(document);
  auto* sprite = document->sprite();
  auto* layer = sprite->firstLayer();
  layer->cel(0)->image()->putPixel(0, 0, 3); // Existing unsaved artwork.
  Params force;
  force.set("enabled", "true"); force.set("ui", "false");
  ctx->executeCommand(Commands::instance()->byId("ForceKarnRules"), force);
  auto* editor = ctx->activeEditor();
  ASSERT_TRUE(editor);
  Preferences::instance().editor.straightLinePreview(false);
  const ImageRef original(Image::createCopy(layer->cel(0)->image()));
  const auto* history = document->undoHistory()->currentState();
  struct CommitCounter : DocObserver {
    int attempts = 0;
    void onBeforeCommitTransaction(DocEvent&) override { ++attempts; }
  } commits;
  document->add_observer(&commits);
  const auto grid = sprite->gridBounds();
  auto* tool = app.toolBox()->getToolById("pencil");
  const auto stroke = [&](bool reject, bool duringDestruction) {
    ToolLoopParams params;
    params.tool = tool;
    params.ink = app.toolBox()->getInkById("paint");
    ASSERT_TRUE(params.ink);
    params.inkType = tools::InkType::SIMPLE;
    params.fg = Color::fromIndex(5); params.bg = Color::fromIndex(0);
    params.brush = std::make_shared<Brush>(kCircleBrushType, 1, 0);
    params.controller = tool->getController(params.button);
    auto* loop = create_tool_loop_for_script(ctx, ctx->activeSite(), params);
    ASSERT_TRUE(loop);
    auto state = std::make_shared<DrawingState>(editor, loop, StandbyState::DrawingType::Regular);
    if (!duringDestruction)
      editor->setState(state);
    const gfx::Point point(4, 4);
    state->initToolLoop(editor, nullptr,
      tools::Pointer(point, tools::Vec2(0, 0), tools::Pointer::Left, tools::Pointer::Type::Mouse, 1.0f));
    // Simulate a document/rule mismatch discovered by the real commit validator.
    if (reject)
      sprite->setGridBounds(gfx::Rect(0, 0, 32, 32));
    if (duringDestruction) {
      editor->renderEngine().removePreviewImage();
      editor->releaseMouse();
      state.reset(); // Previously an exception escaping here called terminate().
    }
    else {
      ui::MouseMessage up(ui::kMouseUpMessage, ui::PointerType::Mouse, ui::kButtonLeft,
                          ui::kKeyNoneModifier, editor->editorToScreen(point));
      EXPECT_NO_THROW(state->onMouseUp(editor, &up));
      EXPECT_FALSE(editor->hasCapture());
      state.reset(); // Must not retry the rejected commit.
    }
    sprite->setGridBounds(grid);
    EXPECT_EQ(nullptr, document->transaction());
  };
  for (bool destruction : {false, true}) {
    const int previousAttempts = commits.attempts;
    stroke(true, destruction);
    EXPECT_EQ(previousAttempts + 1, commits.attempts);
    EXPECT_EQ(history, document->undoHistory()->currentState());
    EXPECT_EQ(0, count_diff_between_images(original.get(), layer->cel(0)->image()));
  }
  stroke(false, false);
  EXPECT_EQ(5, layer->cel(0)->image()->getPixel(4, 4));
  EXPECT_EQ(3, layer->cel(0)->image()->getPixel(0, 0));
  document->undoHistory()->undo();
  EXPECT_EQ(0, count_diff_between_images(original.get(), layer->cel(0)->image()));
  document->undoHistory()->redo();
  EXPECT_EQ(5, layer->cel(0)->image()->getPixel(4, 4));
  document->remove_observer(&commits);
  document->setFilename((std::filesystem::path(std::getenv("ASEPRITE_USER_FOLDER")) /
                         "after-rejected-stroke.aseprite").string());
  ASSERT_EQ(0, save_document(ctx, document));
  std::unique_ptr<Doc> reopened(load_document(nullptr, document->filename()));
  ASSERT_TRUE(reopened);
  EXPECT_EQ(0, count_diff_between_images(layer->cel(0)->image(),
    reopened->sprite()->firstLayer()->cel(0)->image()));
  reopened.reset();
  document->close(); delete document;
  app.run(false);
}

TEST(CrashSafety, BrokenPresetsDoNotBlockRestoreOrOverwriteTheFile)
{
  os::SystemRef system = os::System::make();
  const char* argv[] = {g_exeName};
  AppOptions options(1, argv);
  App app;
  app.initialize(options);
  const auto filename = tool_presets_filename();
  const std::string broken = "{ intentionally incomplete preset data";
  { std::ofstream file(filename, std::ios::binary); file << broken; }
  auto* dock = app.mainWindow()->toolPresetsDock();
  set_config_bool("ToolPresets", "Visible", true);
  int errors = 0;
  ui::Timer dismiss(10);
  dismiss.Tick.connect([&] {
    if (auto* alert = dynamic_cast<ui::Alert*>(ui::Manager::getDefault()->getTopWindow())) {
      EXPECT_EQ("Tool Presets", alert->text());
      ++errors;
      alert->closeWindow(alert->findChild("button-1"));
    }
  });
  dismiss.start();
  EXPECT_NO_THROW(dock->restore());
  EXPECT_FALSE(dock->isPanelVisible());
  EXPECT_EQ(1, errors);
  EXPECT_FALSE(get_config_bool("ToolPresets", "Visible", true));
  EXPECT_NO_THROW(dock->toggle()); // Reopening another time must also be safe.
  EXPECT_EQ(2, errors);
  dismiss.stop();
  { std::ifstream file(filename, std::ios::binary);
    EXPECT_EQ(broken, std::string(std::istreambuf_iterator<char>(file), {})); }
  // Repairing the file allows reopening without restarting the editor.
  save_tool_presets(filename, {});
  EXPECT_NO_THROW(dock->toggle());
  EXPECT_TRUE(dock->isPanelVisible());
  auto* ctx = UIContext::instance();
  auto* document = ctx->documents().add(8, 8, ColorMode::INDEXED);
  ctx->setActiveDocument(document);
  Params params; params.set("name", "Still usable after preset failure");
  ctx->executeCommand(Commands::instance()->byId("NewLayer"), params);
  EXPECT_EQ("Still usable after preset failure", ctx->activeSite().layer()->name());
  document->undoHistory()->undo();
  document->undoHistory()->redo();
  document->close(); delete document;
  app.run(false);
}

int app_main(int argc, char* argv[])
{
  // Never touch the user's presets or configuration, including in local runs.
  const auto profile = std::filesystem::temp_directory_path() /
    ("aseprite-crash-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(profile);
  // Fixtures delete documents directly, without the UI's recovery-owned
  // deferred destruction. Keep the background backup writer out of this suite.
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
