// Aseprite
// Copyright (C) 2019-2025  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/cmd/flatten_layers.h"
#include "app/cmd/set_pixel_format.h"
#include "app/commands/command.h"
#include "app/commands/new_params.h"
#include "app/commands/params.h"
#include "app/console.h"
#include "app/context_access.h"
#include "app/extensions.h"
#include "app/i18n/strings.h"
#include "app/load_matrix.h"
#include "app/modules/gui.h"
#include "app/sprite_job.h"
#include "app/ui/best_fit_criteria_selector.h"
#include "app/ui/dithering_selector.h"
#include "app/ui/editor/editor.h"
#include "app/ui/editor/editor_render.h"
#include "app/ui_context.h"
#include "app/ui/rgbmap_algorithm_selector.h"
#include "app/ui/skin/skin_theme.h"
#include "app/util/indexed_conversion.h"
#include "app/util/conversion_preview_task.h"
#include "app/util/palette_bank.h"
#include "doc/cel.h"
#include "doc/mask.h"
#include "doc/color_mode.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/sprite.h"
#include "fmt/format.h"
#include "render/dithering.h"
#include "render/dithering_algorithm.h"
#include "render/quantization.h"
#include "render/render.h"
#include "render/task_delegate.h"
#include "ui/listitem.h"
#include "ui/alert.h"
#include "ui/size_hint_event.h"
#include "ui/scale.h"
#include "ui/message.h"

#include "color_mode.xml.h"
#include "palette_bank_conversion.xml.h"

#include <string>
#include <thread>
#include <atomic>

namespace app {

using namespace ui;

namespace {

struct ConversionSource {
  doc::ImageRef image;
  doc::Palette palette;
  doc::Layer* layer;
  gfx::Point position;
  int transparent;
  bool paste;
};

rgba_to_graya_func get_gray_func(gen::ToGrayAlgorithm toGray)
{
  switch (toGray) {
    case gen::ToGrayAlgorithm::LUMA: return &rgba_to_graya_using_luma;
    case gen::ToGrayAlgorithm::HSV:  return &rgba_to_graya_using_hsv;
    case gen::ToGrayAlgorithm::HSL:  return &rgba_to_graya_using_hsl;
  }
  return nullptr;
}

class ConvertThread : public ConversionPreviewTask {
public:
  ConvertThread(const doc::ImageRef& dstImage, const doc::Sprite* sprite,
                doc::frame_t frame, const gfx::Point& pos,
                const ConversionSource& source, const IndexedConversionOptions& options)
    : ConversionPreviewTask([dstImage, sprite, frame, pos, source, options](render::TaskDelegate& task) {
      auto indexed = convert_to_indexed(source.image.get(), source.palette,
                                        source.transparent, options, &task);
      if (task.continueTask()) {
        doc::ImageRef rgba(render::convert_pixel_format(indexed.get(), nullptr, IMAGE_RGB,
          render::Dithering(), nullptr, &source.palette, source.transparent < 0, 0));
        render::Render renderer;
        renderer.setNewBlend(Preferences::instance().experimental.newBlend());
        renderer.setComposeGroups(true);
        gfx::Point origin = source.position;
        auto* cel = source.layer->cel(frame);
        // Paste replaces nontransparent indexed pixels, preserving the existing
        // cel underneath. Compose before applying layer opacity/blending.
        if (source.paste && cel) {
          const gfx::Rect bounds = cel->bounds() |
            gfx::Rect(origin, rgba->size());
          doc::ImageRef combined(Image::create(IMAGE_RGB, bounds.w, bounds.h));
          combined->clear(0);
          renderer.renderCel(combined.get(), cel, sprite, cel->image(), source.layer->tileset(),
            sprite->palette(frame), cel->bounds(),
            gfx::Clip(0, 0, bounds), 255, doc::BlendMode::SRC);
          for (int y = 0; y < indexed->height(); ++y)
            for (int x = 0; x < indexed->width(); ++x)
              if (indexed->getPixel(x, y) != source.transparent)
                combined->putPixel(origin.x - bounds.x + x, origin.y - bounds.y + y,
                                   rgba->getPixel(x, y));
          rgba = combined;
          origin = bounds.origin();
        }
        doc::Cel extra(frame, rgba);
        extra.setPosition(origin);
        if (cel)
          renderer.setPreviewImage(source.layer, frame, rgba.get(), nullptr, origin,
                                   doc::BlendMode::SRC);
        else {
          extra.setOpacity(source.layer->opacity());
          renderer.setExtraImage(render::ExtraType::OVER_COMPOSITE, &extra, rgba.get(),
                                 source.layer->blendMode(), source.layer, frame);
        }
        renderer.renderSprite(dstImage.get(), sprite, frame,
          gfx::Clip(0, 0, pos.x, pos.y, dstImage->width(), dstImage->height()));
      }
    })
  {
  }

  ConvertThread(const doc::ImageRef& dstImage, const doc::Sprite* sprite,
                doc::frame_t frame, doc::ColorMode colorMode,
                const render::Dithering& dithering, gen::ToGrayAlgorithm toGray,
                const gfx::Point& pos, bool newBlend)
    : ConversionPreviewTask([=](render::TaskDelegate& task) {
        doc::ImageRef tmp(Image::create(sprite->pixelFormat(), dstImage->width(), dstImage->height()));
        render::Render renderer;
        renderer.setNewBlend(newBlend);
        renderer.renderSprite(tmp.get(), sprite, frame,
          gfx::Clip(0, 0, pos.x, pos.y, dstImage->width(), dstImage->height()));
        render::convert_pixel_format(tmp.get(), dstImage.get(), PixelFormat(colorMode),
          dithering, sprite->rgbMap(frame), sprite->palette(frame),
          sprite->backgroundLayer() != nullptr, 0, get_gray_func(toGray), &task);
      })
  {
  }
};

class PaletteBankWindow : public app::gen::PaletteBankConversion {
public:
  PaletteBankWindow(Editor* editor, doc::Layer* layer, int bank)
    : m_editor(editor), m_layer(layer), m_bank(bank), m_delay(40), m_monitor(30)
    , m_palette(doc::make_bank_palette(layer->sprite(), editor->frame(), bank))
  {
    const auto bankName = doc::palette_bank_name(layer->sprite(), bank);
    destination()->setText(layer->name() + " -> Bank " + std::to_string(bank) +
                            (bankName.empty() ? "" : " (" + bankName + ")"));
    setMinSize(gfx::Size(360 * ui::guiscale(), 0));
    gfx::Size descriptionSize;
    for (const auto& text : {Strings::palette_bank_conversion_keep_description(),
                             Strings::palette_bank_conversion_map_description(),
                             Strings::palette_bank_conversion_copy_description()}) {
      explanation()->setText(text);
      descriptionSize = descriptionSize.createUnion(explanation()->sizeHint());
    }
    explanation()->setMinSize(descriptionSize);
    if (!doc::palette_bank_used_by_other_layer(layer, bank))
      operation()->addItem(Strings::palette_bank_conversion_copy_colors());
    operation()->setSelectedItemIndex(0);
    matching()->setSelectedItemIndex(0);
    factor()->setValue(100);
    preview()->setSelected(true);
    m_dither = new DitheringSelector(DitheringSelector::SelectBoth, &m_palette);
    // The canvas is the preview. Keep the closed selector's method name clear,
    // including when disabled; pattern thumbnails remain in the dropdown.
    m_dither->setUseCustomWidget(false);
    m_dither->setExpansive(true);
    m_dither->setSelectedItemIndex(0);
    ditheringPlaceholder()->addChild(m_dither);
    operation()->Change.connect([this] { changed(); });
    matching()->Change.connect([this] { changed(); });
    m_dither->Change.connect([this] { changed(); });
    factor()->Change.connect([this] { changed(); });
    preview()->Click.connect([this] { changed(); });
    m_delay.Tick.connect([this] { startPreview(); });
    m_monitor.Tick.connect([this] {
      if (!m_task || m_task->isRunning()) return;
      m_monitor.stop();
      m_task->stop();
      if (m_task->error()) {
        m_error = m_task->error();
        failPreview();
        return;
      }
      Editor::renderEngine().setPreviewSprite(m_layer->sprite(), m_copy->sprite());
      m_published = true;
      invalidateEditors();
    });
    changed();
  }
  ~PaletteBankWindow() { stopPreview(); }

  IndexedConversionOptions options() const
  {
    IndexedConversionOptions result;
    result.remapBankColors = operation()->getSelectedItemIndex() == 1;
    result.copyBankColors = operation()->getSelectedItemIndex() == 2;
    result.match = static_cast<PaletteMatch>(matching()->getSelectedItemIndex() + 1);
    result.dithering = render::Dithering(m_dither->ditheringAlgorithm(),
      m_dither->ditheringMatrix(), factor()->getValue() / 100.0);
    return result;
  }
  void finishPreview()
  {
    stopPreview();
    if (m_error) std::rethrow_exception(m_error);
  }

protected:
  bool onProcessMessage(ui::Message* message) override
  {
    if (message->type() == ui::kCloseMessage) stopPreview();
    return app::gen::PaletteBankConversion::onProcessMessage(message);
  }

private:
  void invalidateEditors()
  {
    for (auto* editor : UIContext::instance()->getAllEditorsIncludingPreview(m_editor->document()))
      editor->invalidate();
  }
  void stopPreview()
  {
    m_delay.stop();
    m_monitor.stop();
    if (m_task) {
      m_task->stop();
      if (m_task->error()) m_error = m_task->error();
      m_task.reset();
    }
    if (m_published) {
      Editor::renderEngine().setPreviewSprite(nullptr, nullptr);
      m_published = false;
      invalidateEditors();
    }
    if (m_tx) {
      m_copy->updateWriterThread();
      m_tx.reset(); // Roll back only the private preview document.
    }
  }
  void failPreview()
  {
    stopPreview();
    ok()->setEnabled(false);
    closeWindow(nullptr);
  }
  void changed()
  {
    stopPreview();
    if (m_error) { failPreview(); return; }
    const int mode = operation()->getSelectedItemIndex();
    explanation()->setText(mode == 0 ? Strings::palette_bank_conversion_keep_description() :
                           mode == 1 ? Strings::palette_bank_conversion_map_description() :
                                       Strings::palette_bank_conversion_copy_description());
    matching()->setEnabled(mode == 1);
    m_dither->setEnabled(mode == 1);
    // Floyd-Steinberg diffuses this percentage of quantization error. The
    // ordered algorithms use their matrix and do not support this factor.
    factor()->setEnabled(mode == 1 && m_dither->ditheringAlgorithm() ==
                                      render::DitheringAlgorithm::ErrorDiffusion);
    if (preview()->isSelected()) m_delay.start();
  }
  void startPreview()
  {
    m_delay.stop();
    try {
      if (!m_copy) {
        m_copy = m_context.documents().add(m_editor->document()->duplicate(DuplicateExactCopy));
        const auto layers = m_layer->sprite()->allLayers();
        const auto index = std::find(layers.begin(), layers.end(), m_layer) - layers.begin();
        m_copyLayer = m_copy->sprite()->allLayers()[index];
        m_context.setActiveLayer(m_copyLayer);
        m_context.setActiveFrame(m_editor->frame());
      }
      m_tx = std::make_unique<Tx>(m_copy, "Palette Bank Preview");
      const auto settings = options();
      m_task = std::make_unique<ConversionPreviewTask>([this, settings](render::TaskDelegate& task) {
        m_copy->updateWriterThread();
        set_layer_palette_bank(*m_tx, m_copyLayer, m_bank, settings, &task);
      });
      m_monitor.start();
    }
    catch (...) {
      m_error = std::current_exception();
      failPreview();
    }
  }
  Editor* m_editor;
  doc::Layer* m_layer;
  int m_bank;
  ui::Timer m_delay, m_monitor;
  doc::Palette m_palette;
  DitheringSelector* m_dither = nullptr;
  Context m_context;
  Doc* m_copy = nullptr;
  doc::Layer* m_copyLayer = nullptr;
  std::unique_ptr<Tx> m_tx;
  std::unique_ptr<ConversionPreviewTask> m_task;
  std::exception_ptr m_error;
  bool m_published = false;
};

class ConversionItem : public ListItem {
public:
  ConversionItem(const doc::ColorMode colorMode) : m_colorMode(colorMode)
  {
    std::string toMode;
    switch (colorMode) {
      case doc::ColorMode::RGB: toMode = Strings::commands_ChangePixelFormat_RGB(); break;
      case doc::ColorMode::GRAYSCALE:
        toMode = Strings::commands_ChangePixelFormat_Grayscale();
        break;
      case doc::ColorMode::INDEXED: toMode = Strings::commands_ChangePixelFormat_Indexed(); break;
    }
    setText(fmt::format("-> {}", toMode));
  }
  doc::ColorMode colorMode() const { return m_colorMode; }

private:
  doc::ColorMode m_colorMode;
};

class ColorModeWindow : public app::gen::ColorMode {
public:
  ColorModeWindow(Editor* editor, const ConversionSource* source = nullptr)
    : m_timer(100)
    , m_editor(editor)
    , m_image(nullptr)
    , m_imageBuffer(new doc::ImageBuffer)
    , m_selectedItem(nullptr)
    , m_ditheringSelector(nullptr)
    , m_mapAlgorithmSelector(nullptr)
    , m_bestFitCriteriaSelector(nullptr)
    , m_imageJustCreated(true)
    , m_source(source)
  {
    const auto& pref = Preferences::instance();
    const doc::ColorMode from = source ? doc::ColorMode::RGB : m_editor->sprite()->colorMode();

    // Add the color mode in the window title
    std::string fromMode;
    switch (from) {
      case doc::ColorMode::RGB: fromMode = Strings::commands_ChangePixelFormat_RGB(); break;
      case doc::ColorMode::GRAYSCALE:
        fromMode = Strings::commands_ChangePixelFormat_Grayscale();
        break;
      case doc::ColorMode::INDEXED: fromMode = Strings::commands_ChangePixelFormat_Indexed(); break;
    }
    setText(fmt::format("{}: {}", text(), fromMode));
    if (source) {
      setText(text() + (source->paste ? " - Paste" : " - " + source->layer->name()));
      setMinSize(gfx::Size(320 * ui::guiscale(), 0));
      flatten()->setVisible(false);
    }

    // Add conversion items
    if (from != doc::ColorMode::RGB)
      colorMode()->addChild(new ConversionItem(doc::ColorMode::RGB));
    if (from != doc::ColorMode::INDEXED) {
      colorMode()->addChild(new ConversionItem(doc::ColorMode::INDEXED));

      m_ditheringSelector = new DitheringSelector(DitheringSelector::SelectBoth,
                                                 source ? &source->palette : nullptr);
      m_ditheringSelector->setExpansive(true);

      m_mapAlgorithmSelector = new RgbMapAlgorithmSelector;
      m_mapAlgorithmSelector->setExpansive(true);

      m_bestFitCriteriaSelector = new BestFitCriteriaSelector;
      m_bestFitCriteriaSelector->setExpansive(true);

      // Select default dithering method
      {
        int index = m_ditheringSelector->findItemIndex(pref.quantization.ditheringAlgorithm());
        if (index >= 0)
          m_ditheringSelector->setSelectedItemIndex(index);
      }

      // Select default RgbMap algorithm
      m_mapAlgorithmSelector->algorithm(pref.quantization.rgbmapAlgorithm());

      // Select default best fit criteria
      m_bestFitCriteriaSelector->criteria(pref.quantization.fitCriteria());

      ditheringPlaceholder()->addChild(m_ditheringSelector);
      rgbmapAlgorithmPlaceholder()->addChild(m_mapAlgorithmSelector);
      bestFitCriteriaPlaceholder()->addChild(m_bestFitCriteriaSelector);

      const bool adv = pref.quantization.advanced();
      advancedCheck()->setSelected(adv);
      advanced()->setVisible(adv);

      // Signals
      m_ditheringSelector->Change.connect([this] { onIndexParamChange(); });
      m_mapAlgorithmSelector->Change.connect([this] { onIndexParamChange(); });
      m_bestFitCriteriaSelector->Change.connect([this] { onIndexParamChange(); });
      factor()->Change.connect([this] { onIndexParamChange(); });

      advancedCheck()->Click.connect([this]() {
        advanced()->setVisible(advancedCheck()->isSelected());
        expandWindow(sizeHint());
      });
    }
    else {
      amount()->setVisible(false);
      advancedCheck()->setVisible(false);
      advanced()->setVisible(false);
    }
    if (!source && from != doc::ColorMode::GRAYSCALE) {
      colorMode()->addChild(new ConversionItem(doc::ColorMode::GRAYSCALE));

      toGrayCombobox()->Change.connect([this] { onToGrayChange(); });
    }

    colorModeView()->setMinSize(colorModeView()->sizeHint() + colorMode()->sizeHint());

    colorMode()->Change.connect([this] { onChangeColorMode(); });
    m_timer.Tick.connect([this] { onMonitorProgress(); });

    progress()->setReadOnly(true);

    // Default dithering factor
    factor()->setValue(pref.quantization.ditheringFactor());

    // Select first option
    colorMode()->selectIndex(0);
  }

  ~ColorModeWindow() { stop(); }

  void checkPreviewError() const
  {
    if (m_previewError) {
      try {
        std::rethrow_exception(m_previewError);
      }
      catch (const std::exception&) {
        throw;
      }
      catch (...) {
        throw std::runtime_error("The color conversion preview failed. Your artwork has not been changed.");
      }
    }
  }

  void finishPreview()
  {
    // Also check errors when OK/Cancel beats the progress timer.
    stop();
    checkPreviewError();
  }

  doc::ColorMode selectedColorMode() const
  {
    ASSERT(m_selectedItem);
    return m_selectedItem->colorMode();
  }

  render::Dithering dithering() const
  {
    render::Dithering d;
    if (m_ditheringSelector) {
      d.algorithm(m_ditheringSelector->ditheringAlgorithm());
      d.matrix(m_ditheringSelector->ditheringMatrix());
    }
    d.factor(double(factor()->getValue()) / 100.0);
    return d;
  }

  doc::RgbMapAlgorithm rgbMapAlgorithm() const
  {
    if (m_mapAlgorithmSelector)
      return m_mapAlgorithmSelector->algorithm();
    else
      return doc::RgbMapAlgorithm::DEFAULT;
  }

  doc::FitCriteria fitCriteria() const
  {
    if (m_bestFitCriteriaSelector)
      return m_bestFitCriteriaSelector->criteria();
    else
      return doc::FitCriteria::DEFAULT;
  }

  gen::ToGrayAlgorithm toGray() const
  {
    static_assert(
      int(gen::ToGrayAlgorithm::LUMA) == 0 && int(gen::ToGrayAlgorithm::HSV) == 1 &&
        int(gen::ToGrayAlgorithm::HSL) == 2,
      "Check that 'to_gray_combobox' combobox items matches these indexes in color_mode.xml");
    return (gen::ToGrayAlgorithm)toGrayCombobox()->getSelectedItemIndex();
  }

  bool flattenEnabled() const { return flatten()->isSelected(); }

  void saveOptions()
  {
    auto& pref = Preferences::instance();

    // Save the dithering method used for the future
    if (m_ditheringSelector) {
      if (auto item = m_ditheringSelector->getSelectedItem()) {
        pref.quantization.ditheringAlgorithm(item->text());

        if (m_ditheringSelector->ditheringAlgorithm() ==
            render::DitheringAlgorithm::ErrorDiffusion) {
          pref.quantization.ditheringFactor(factor()->getValue());
        }
      }
    }

    if (m_mapAlgorithmSelector || m_bestFitCriteriaSelector)
      pref.quantization.advanced(advancedCheck()->isSelected());
  }

private:
  void stop()
  {
    m_editor->renderEngine().removePreviewImage();
    m_editor->invalidate();

    m_timer.stop();
    if (m_bgThread) {
      m_bgThread->stop();
      if (m_bgThread->error())
        m_previewError = m_bgThread->error();
      m_bgThread.reset(nullptr);
    }
  }

  void onChangeColorMode()
  {
    try {
      updatePreview();
    }
    catch (...) {
      previewFailed(std::current_exception());
    }
  }

  void previewFailed(std::exception_ptr error)
  {
    m_previewError = error;
    stop();
    m_image.reset();
    m_imageBuffer.reset();
    ok()->setEnabled(false);
    closeWindow(nullptr);
    // The caller reports the error after the modal window has unwound.
  }

  void updatePreview()
  {
    ConversionItem* item = static_cast<ConversionItem*>(colorMode()->getSelectedChild());
    if (item == m_selectedItem) // Avoid restarting the conversion process for the same option
      return;
    m_selectedItem = item;

    stop();
    checkPreviewError();

    gfx::Rect visibleBounds = m_editor->getVisibleSpriteBounds();
    if (visibleBounds.isEmpty())
      return;

    doc::ColorMode dstColorMode = item->colorMode();

    if (m_ditheringSelector) {
      const bool toIndexed = (dstColorMode == doc::ColorMode::INDEXED);
      m_ditheringSelector->setVisible(toIndexed);

      const bool errorDiff = (m_ditheringSelector->ditheringAlgorithm() ==
                              render::DitheringAlgorithm::ErrorDiffusion);
      amount()->setVisible(toIndexed && errorDiff);
    }

    {
      const bool toGray = (dstColorMode == doc::ColorMode::GRAYSCALE);
      toGrayCombobox()->setVisible(toGray);
    }

    if (m_source) {
      m_previewPosition = visibleBounds.origin();
      // Publish only completed buffers; the worker never writes a displayed image.
      m_image.reset(Image::create(IMAGE_RGB, visibleBounds.w, visibleBounds.h));
      m_image->clear(0);
      progress()->setValue(0);
      progress()->setVisible(false);
      expandWindow(sizeHint());
      layout();
      IndexedConversionOptions options{ dithering(), rgbMapAlgorithm(), fitCriteria() };
      m_bgThread.reset(new ConvertThread(m_image, m_editor->sprite(), m_editor->frame(),
                                        m_previewPosition, *m_source, options));
      m_timer.start();
      return;
    }

    m_image.reset(
      Image::create(ImageSpec(dstColorMode, visibleBounds.w, visibleBounds.h, 0), m_imageBuffer));
    if (m_imageJustCreated) {
      m_imageJustCreated = false;
      m_image->clear(0);
    }

    m_editor->renderEngine().setPreviewImage(nullptr,
                                             m_editor->frame(),
                                             m_image.get(),
                                             nullptr,
                                             visibleBounds.origin(),
                                             doc::BlendMode::SRC);

    m_editor->sprite()->rgbMap(0,
                               m_editor->sprite()->rgbMapForSprite(),
                               rgbMapAlgorithm(),
                               fitCriteria());

    m_editor->invalidate();
    progress()->setValue(0);
    progress()->setVisible(false);
    layout();

    m_bgThread.reset(new ConvertThread(m_image,
                                       m_editor->sprite(),
                                       m_editor->frame(),
                                       dstColorMode,
                                       dithering(),
                                       toGray(),
                                       visibleBounds.origin(),
                                       Preferences::instance().experimental.newBlend()));

    m_timer.start();
  }

  void onIndexParamChange()
  {
    stop();
    m_selectedItem = nullptr;
    onChangeColorMode();
  }

  void onToGrayChange()
  {
    stop();
    m_selectedItem = nullptr;
    onChangeColorMode();
  }

  void onMonitorProgress()
  {
    ASSERT(m_bgThread);
    if (!m_bgThread)
      return;

    if (!m_bgThread->isRunning()) {
      m_timer.stop();
      m_bgThread->stop();
      const auto error = m_bgThread->error();
      m_bgThread.reset(nullptr);
      if (error) {
        previewFailed(error);
        return;
      }

      if (m_source)
        m_editor->renderEngine().setPreviewImage(nullptr, m_editor->frame(), m_image.get(),
          nullptr, m_previewPosition, doc::BlendMode::SRC);

      progress()->setVisible(false);
      layout();
    }
    else {
      int v = int(100 * m_bgThread->progress());
      if (v > 0) {
        progress()->setValue(v);
        if (!progress()->isVisible()) {
          progress()->setVisible(true);
          layout();
        }
      }
    }

    m_editor->invalidate();
  }

  Timer m_timer;
  Editor* m_editor;
  doc::ImageRef m_image;
  doc::ImageBufferPtr m_imageBuffer;
  std::unique_ptr<ConvertThread> m_bgThread;
  std::exception_ptr m_previewError;
  ConversionItem* m_selectedItem;
  DitheringSelector* m_ditheringSelector;
  RgbMapAlgorithmSelector* m_mapAlgorithmSelector;
  BestFitCriteriaSelector* m_bestFitCriteriaSelector;
  bool m_imageJustCreated;
  const ConversionSource* m_source;
  gfx::Point m_previewPosition;
};

} // anonymous namespace

bool choose_indexed_conversion(Editor* editor, doc::Layer* layer,
                               const doc::ImageRef& source, const gfx::Point& position,
                               const doc::Palette& palette, int transparent,
                               bool paste, IndexedConversionOptions& options,
                               const doc::Mask* mask)
{
  if (!editor || !layer || editor->sprite() != layer->sprite())
    return true;
  ConversionSource input{ doc::ImageRef(Image::createCopy(source.get())), palette,
                          layer, position, transparent, paste };
  if (mask && !mask->isEmpty()) {
    for (int y = 0; y < input.image->height(); ++y)
      for (int x = 0; x < input.image->width(); ++x)
        if (!mask->containsPoint(position.x + x, position.y + y))
          input.image->putPixel(x, y, 0);
  }
  ColorModeWindow window(editor, &input);
  window.checkPreviewError();
  window.remapWindow();
  window.centerWindow();
  load_window_pos(&window, "ChangePixelFormat");
  window.openWindowInForeground();
  window.finishPreview();
  save_window_pos(&window, "ChangePixelFormat");
  if (window.closer() != window.ok())
    return false;
  options = { window.dithering(), window.rgbMapAlgorithm(), window.fitCriteria() };
  window.saveOptions();
  return true;
}

bool choose_palette_bank_conversion(doc::Layer* layer, int bank,
                                    IndexedConversionOptions& options)
{
  if (bank >= 0 && bank != layer->paletteBank() && !layer->isRgbReference() &&
      App::instance()->isGui() &&
      ((doc::initialized_palette_banks(layer->sprite()) & (1 << bank)) ||
       doc::palette_bank_used_by_other_layer(layer, bank))) {
    auto* editor = Editor::activeEditor();
    if (editor && editor->sprite() == layer->sprite()) {
      PaletteBankWindow window(editor, layer, bank);
      window.remapWindow();
      window.centerWindow();
      load_window_pos(&window, "PaletteBankConversion");
      window.openWindowInForeground();
      window.finishPreview();
      save_window_pos(&window, "PaletteBankConversion");
      if (window.closer() != window.ok()) return false;
      options = window.options();
      return true;
    }
  }
  if (bank >= 0 && bank != layer->paletteBank() && App::instance()->isGui() &&
      !doc::palette_bank_used_by_other_layer(layer, bank) &&
      (doc::initialized_palette_banks(layer->sprite()) & (1 << bank))) {
    const int answer = ui::Alert::show(
      "Unused Palette Bank<<No other layer uses this bank."
      "<<Copy the layer's previous palette colors into it,"
      "<<or keep the bank's existing colors?"
      "||&Copy Colors||&Keep Bank||&Cancel");
    if (answer != 1 && answer != 2)
      return false;
    options.copyBankColors = answer == 1;
  }
  if (!layer->isRgbReference() || bank == -2 || !App::instance()->isGui())
    return true;
  auto* editor = Editor::activeEditor();
  if (!editor || editor->sprite() != layer->sprite())
    return true;
  auto* cel = layer->cel(editor->frame());
  doc::ImageRef image = cel ? cel->imageRef() : doc::ImageRef(Image::create(IMAGE_RGB, 1, 1));
  if (!cel)
    image->clear(0);
  const auto palette = palette_for_bank_conversion(layer, bank, editor->frame(), options);
  const bool copyBankColors = options.copyBankColors;
  const bool accepted = choose_indexed_conversion(editor, layer, image, cel ? cel->position() : gfx::Point(),
    palette, bank >= 0 ? 0 : layer->sprite()->transparentColor(), false, options);
  options.copyBankColors = copyBankColors;
  return accepted;
}

struct ChangePixelFormatParams : public NewParams {
  Param<bool> ui{ this, false, "ui" };
  Param<ColorMode> colorMode{
    this,
    ColorMode::RGB,
    { "colorMode", "format" }
  };
  Param<render::DitheringAlgorithm> dithering{ this,
                                               render::DitheringAlgorithm::None,
                                               "dithering" };
  Param<std::string> matrix{
    this,
    "",
    { "ditheringMatrix", "dithering-matrix" }
  };
  Param<double> factor{
    this,
    1.0,
    { "ditheringFactor", "dithering-factor" }
  };
  Param<doc::RgbMapAlgorithm> rgbmap{ this, RgbMapAlgorithm::DEFAULT, "rgbmap" };
  Param<gen::ToGrayAlgorithm> toGray{ this, gen::ToGrayAlgorithm::DEFAULT, "toGray" };
  Param<doc::FitCriteria> fitCriteria{ this, doc::FitCriteria::DEFAULT, "fitCriteria" };
};

class ChangePixelFormatCommand : public CommandWithNewParams<ChangePixelFormatParams> {
public:
  ChangePixelFormatCommand();

protected:
  bool onEnabled(Context* ctx) override;
  bool onChecked(Context* ctx) override;
  void onExecute(Context* ctx) override;
  std::string onGetFriendlyName() const override;

private:
  static render::DitheringMatrix getDitheringMatrix(const std::string& ditheringMatrixId);
};

ChangePixelFormatCommand::ChangePixelFormatCommand()
  : CommandWithNewParams(CommandId::ChangePixelFormat())
{
}

bool ChangePixelFormatCommand::onEnabled(Context* ctx)
{
  if (!ctx->checkFlags(ContextFlags::ActiveDocumentIsWritable | ContextFlags::HasActiveSprite))
    return false;

  const ContextReader reader(ctx);
  const Sprite* sprite(reader.sprite());

  if (!sprite)
    return false;

  if (params().ui())
    return true;

  if (sprite->pixelFormat() == IMAGE_INDEXED && params().colorMode() == ColorMode::INDEXED &&
      params().dithering() != render::DitheringAlgorithm::None)
    return false;

  return true;
}

bool ChangePixelFormatCommand::onChecked(Context* ctx)
{
  if (params().ui() || (!params().ui.isSet() && !params().colorMode.isSet()))
    return false;

  const ContextReader reader(ctx);
  const Sprite* sprite = reader.sprite();

  if (sprite && sprite->pixelFormat() == IMAGE_INDEXED &&
      params().colorMode() == ColorMode::INDEXED &&
      params().dithering() != render::DitheringAlgorithm::None)
    return false;

  return (sprite && sprite->colorMode() == params().colorMode());
}

void ChangePixelFormatCommand::onExecute(Context* ctx)
{
  const bool ui = ((params().ui() || (!params().ui.isSet() && !params().colorMode.isSet())) &&
                   ctx->isUIAvailable());
  bool flatten = false;
  render::DitheringMatrix matrix = getDitheringMatrix(params().matrix());

  if (ui) {
    ColorModeWindow window(Editor::activeEditor());
    window.checkPreviewError();

    window.remapWindow();
    window.centerWindow();

    load_window_pos(&window, "ChangePixelFormat");
    window.openWindowInForeground();
    window.finishPreview();
    save_window_pos(&window, "ChangePixelFormat");

    if (window.closer() != window.ok())
      return;

    params().colorMode(window.selectedColorMode());
    params().dithering(window.dithering().algorithm());
    matrix = window.dithering().matrix();
    params().factor(window.dithering().factor());
    params().rgbmap(window.rgbMapAlgorithm());
    params().fitCriteria(window.fitCriteria());
    params().toGray(window.toGray());
    flatten = window.flattenEnabled();

    window.saveOptions();
  }
  else if (params().colorMode() == ColorMode::INDEXED) {
    if (!params().rgbmap.isSet())
      params().rgbmap(Preferences::instance().quantization.rgbmapAlgorithm());
    if (!params().fitCriteria.isSet())
      params().fitCriteria(Preferences::instance().quantization.fitCriteria());
  }

  // No conversion needed
  Doc* doc = ctx->activeDocument();
  if (doc->sprite()->pixelFormat() == (PixelFormat)params().colorMode())
    return;

  {
    SpriteJob job(ctx, doc, Strings::color_mode_title(), ui);
    Sprite* sprite(job.sprite());

    // TODO this was moved in the main UI thread because
    //      cmd::FlattenLayers() generates a EditorObserver::onAfterLayerChanged()
    //      event, and that event is an UI event.
    //      We should refactor the whole app to separate doc changes <-> UI changes,
    //      but that is for the future:
    //      https://github.com/aseprite/aseprite/issues/509
    //      https://github.com/aseprite/aseprite/issues/378
    if (flatten) {
      Tx tx(Tx::LockDoc, ctx, doc);
      const bool newBlend = Preferences::instance().experimental.newBlend();
      cmd::FlattenLayers::Options options;
      options.newBlendMethod = newBlend;

      SelectedLayers selLayers;
      for (auto layer : sprite->root()->layers())
        selLayers.insert(layer);
      tx(new cmd::FlattenLayers(sprite, selLayers, options));
    }

    job.startJobWithCallback([this, &job, sprite, &matrix](Tx& tx) {
      tx(new cmd::SetPixelFormat(sprite,
                                 (PixelFormat)params().colorMode(),
                                 render::Dithering(params().dithering(), matrix, params().factor()),
                                 params().rgbmap(),
                                 get_gray_func(params().toGray()),
                                 &job, // SpriteJob is a render::TaskDelegate
                                 params().fitCriteria()));
    });
    job.waitJob();
  }

  if (ctx->isUIAvailable())
    app_refresh_screen();
}

std::string ChangePixelFormatCommand::onGetFriendlyName() const
{
  std::string conversion;

  if (!params().ui()) {
    switch (params().colorMode()) {
      case ColorMode::RGB: conversion = Strings::commands_ChangePixelFormat_RGB(); break;
      case ColorMode::GRAYSCALE:
        conversion = Strings::commands_ChangePixelFormat_Grayscale();
        break;
      case ColorMode::INDEXED:
        switch (params().dithering()) {
          case render::DitheringAlgorithm::None:
            conversion = Strings::commands_ChangePixelFormat_Indexed();
            break;
          case render::DitheringAlgorithm::Ordered:
            conversion = Strings::commands_ChangePixelFormat_Indexed_OrderedDithering();
            break;
          case render::DitheringAlgorithm::Old:
            conversion = Strings::commands_ChangePixelFormat_Indexed_OldDithering();
            break;
          case render::DitheringAlgorithm::ErrorDiffusion:
            conversion = Strings::commands_ChangePixelFormat_Indexed_ErrorDiffusion();
            break;
        }
        break;
    }
  }
  else
    conversion = Strings::commands_ChangePixelFormat_MoreOptions();

  return Strings::commands_ChangePixelFormat(conversion);
}

render::DitheringMatrix ChangePixelFormatCommand::getDitheringMatrix(
  const std::string& ditheringMatrix)
{
  if (!ditheringMatrix.empty()) {
    // Try to get the matrix from the extensions
    const render::DitheringMatrix* knownMatrix = App::instance()->extensions().ditheringMatrix(
      ditheringMatrix);
    if (knownMatrix) {
      return *knownMatrix;
    }

    // If the matrix doesn't exist we try to load it from a file
    try {
      render::DitheringMatrix matrix;
      load_dithering_matrix_from_sprite(ditheringMatrix, matrix);
      return matrix;
    }
    catch (const std::exception& e) {
      LOG(ERROR, "%s\n", e.what());
      Console::showException(e);
    }
  }

  // Default dithering matrix is BayerMatrix(8)
  return render::BayerMatrix::make(8);
}

Command* CommandFactory::createChangePixelFormatCommand()
{
  return new ChangePixelFormatCommand;
}

} // namespace app
