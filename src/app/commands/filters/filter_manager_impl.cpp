// Aseprite
// Copyright (C) 2019-2025  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/commands/filters/filter_manager_impl.h"

#include "app/app.h"
#include "app/cmd/copy_region.h"
#include "app/cmd/patch_cel.h"
#include "app/cmd/set_palette.h"
#include "app/util/palette_edit.h"
#include "app/cmd/unlink_cel.h"
#include "app/context_access.h"
#include "app/doc.h"
#include "app/ini_file.h"
#include "app/modules/palettes.h"
#include "app/site.h"
#include "app/transaction.h"
#include "app/ui/color_bar.h"
#include "app/ui/editor/editor.h"
#include "app/ui/palette_view.h"
#include "app/ui/timeline/timeline.h"
#include "app/ui_context.h"
#include "app/util/cel_ops.h"
#include "doc/algorithm/shrink_bounds.h"
#include "doc/cel.h"
#include "doc/cels_range.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/layer_list.h"
#include "doc/mask.h"
#include "doc/palette.h"
#include "doc/palette_bank.h"
#include "doc/rgbmap.h"
#include "doc/sprite.h"
#include "filters/filter.h"
#include "ui/manager.h"
#include "ui/view.h"
#include "ui/widget.h"
#include "view/cels.h"

#include <cstdlib>
#include <cstring>
#include <set>

namespace app {

using namespace std;
using namespace ui;

FilterManagerImpl::FilterManagerImpl(Context* context, Filter* filter)
  : m_reader(context)
  , m_site(const_cast<Site&>(m_reader.site()))
  , m_filter(filter)
  , m_cel(nullptr)
  , m_src(nullptr)
  , m_dst(nullptr)
  , m_row(0)
  , m_mask(nullptr)
  , m_previewMask(nullptr)
  , m_targetOrig(TARGET_ALL_CHANNELS)
  , m_target(TARGET_ALL_CHANNELS)
  , m_celsTarget(CelsTarget::Selected)
  , m_oldPalette(nullptr)
  , m_taskToken(&m_noToken)
  , m_progressDelegate(nullptr)
{
  int x, y;
  Image* image = m_site.image(&x, &y);
  if (!image || (m_site.layer() && m_site.layer()->isReference()))
    throw NoImageException();

  init(m_site.cel());
}

FilterManagerImpl::~FilterManagerImpl()
{
  if (m_oldPalette) {
    restoreSpritePalette();
    if (m_reader.context()->isUIAvailable())
      set_current_palette(m_oldPalette.get(), false);
  }
}

Doc* FilterManagerImpl::document()
{
  return static_cast<Doc*>(m_site.document());
}

void FilterManagerImpl::setProgressDelegate(IProgressDelegate* progressDelegate)
{
  m_progressDelegate = progressDelegate;
}

PixelFormat FilterManagerImpl::pixelFormat() const
{
  return m_cel ? m_cel->layer()->pixelFormat() : m_site.pixelFormat();
}

void FilterManagerImpl::setTarget(int target)
{
  m_targetOrig = target;
  m_target = target;

  // The alpha channel of the background layer can't be modified.
  if (m_site.layer() && m_site.layer()->isBackground())
    m_target &= ~TARGET_ALPHA_CHANNEL;
}

void FilterManagerImpl::setCelsTarget(CelsTarget celsTarget)
{
  m_celsTarget = celsTarget;
}

std::unique_ptr<FilterManagerImpl> FilterManagerImpl::makeAllLayersPreview(Context& context)
{
  // Keep the copy for the lifetime of this edit. Each preview runs in an
  // uncommitted transaction which restores the copy when the preview stops.
  if (!context.activeDocument()) {
    auto* copy = context.documents().add(document()->duplicate(DuplicateExactCopy));
    if (sprite()->pixelFormat() == IMAGE_INDEXED) {
      // A document can use a non-default nearest-color algorithm and metric.
      const auto* map = sprite()->rgbMap(frame());
      copy->sprite()->rgbMap(frame(), sprite()->rgbMapForSprite(),
                             map->rgbmapAlgorithm(), map->fitCriteria());
    }
    const auto index = find_layer_index(sprite()->allLayers(), layer());
    context.setActiveLayer(copy->sprite()->allLayers()[index]);
    context.setActiveFrame(frame());
    context.setSelectedColors(m_site.selectedColors());
  }
  auto preview = std::make_unique<FilterManagerImpl>(&context, m_filter);
  preview->m_site.tilesetMode(m_site.tilesetMode());
  preview->setTarget(m_targetOrig);
  preview->setCelsTarget(CelsTarget::All);
  preview->initTransaction();
  return preview;
}

void FilterManagerImpl::begin()
{
  Doc* document = m_site.document();

  m_row = 0;
  m_mask = (document->isMaskVisible() ? document->mask() : nullptr);
  updateBounds(m_mask);
}

void FilterManagerImpl::beginForPreview()
{
  Doc* document = m_site.document();

  if (document->isMaskVisible())
    m_previewMask.reset(new Mask(*document->mask()));
  else {
    m_previewMask.reset(new Mask());
    m_previewMask->replace(m_site.sprite()->bounds());
  }

  m_row = m_nextRowToFlush = 0;
  m_mask = m_previewMask.get();

  // If we have a tiled mode enabled, we'll apply the filter to the whole areaes
  Editor* activeEditor = UIContext::instance()->activeEditor();
  if (activeEditor->docPref().tiled.mode() == filters::TiledMode::NONE) {
    gfx::Rect vp;
    for (Editor* editor : UIContext::instance()->getAllEditorsIncludingPreview(document)) {
      vp |= editor->screenToEditor(View::getView(editor)->viewportBounds());
    }
    vp &= m_site.sprite()->bounds();
    if (vp.isEmpty()) {
      m_previewMask.reset(nullptr);
      m_row = -1;
      return;
    }
    m_previewMask->intersect(vp);
  }

  if (!updateBounds(m_mask)) {
    m_previewMask.reset(nullptr);
    m_row = -1;
    return;
  }
}

void FilterManagerImpl::end()
{
  m_maskBits.unlock();
}

bool FilterManagerImpl::applyStep()
{
  if (m_row < 0 || m_row >= m_bounds.h)
    return false;

  if (m_mask && m_mask->bitmap()) {
    int x = m_bounds.x - m_mask->bounds().x;
    int y = m_bounds.y - m_mask->bounds().y + m_row;
    if ((x >= m_bounds.w) || (y >= m_bounds.h))
      return false;

    m_maskBits = m_mask->bitmap()->lockBits<BitmapTraits>(
      Image::ReadLock,
      gfx::Rect(x, y, m_bounds.w - x, m_bounds.h - y));

    m_maskIterator = m_maskBits.begin();
  }

  if (m_row == 0) {
    applyToPaletteIfNeeded();
  }

  switch (pixelFormat()) {
    case IMAGE_RGB:       m_filter->applyToRgba(this); break;
    case IMAGE_GRAYSCALE: m_filter->applyToGrayscale(this); break;
    case IMAGE_INDEXED:   m_filter->applyToIndexed(this); break;
  }
  ++m_row;

  return true;
}

void FilterManagerImpl::apply()
{
  CommandResult result;
  bool cancelled = false;

  begin();
  while (!cancelled && !m_taskToken->canceled() && applyStep()) {
    if (m_progressDelegate) {
      // Report progress.
      m_progressDelegate->reportProgress(m_progressBase +
                                         m_progressWidth * (m_row + 1) / m_bounds.h);

      // Does the user cancelled the whole process?
      cancelled = m_progressDelegate->isCancelled();
    }
  }

  if (m_taskToken->canceled())
    return;

  if (!cancelled) {
    gfx::Rect output;
    if (algorithm::shrink_bounds2(m_src.get(), m_dst.get(), m_bounds, output)) {
      if (m_cel->layer()->isTilemap()) {
        modify_tilemap_cel_region(*m_tx,
                                  m_cel,
                                  nullptr,
                                  gfx::Region(output),
                                  m_site.tilesetMode(),
                                  [this](const doc::ImageRef& origTile,
                                         const gfx::Rect& tileBoundsInCanvas) -> doc::ImageRef {
                                    return ImageRef(crop_image(m_dst.get(),
                                                               tileBoundsInCanvas.x,
                                                               tileBoundsInCanvas.y,
                                                               tileBoundsInCanvas.w,
                                                               tileBoundsInCanvas.h,
                                                               m_dst->maskColor()));
                                  });
      }
      else if (m_cel->layer()->isBackground()) {
        (*m_tx)(new cmd::CopyRegion(m_cel->image(), m_dst.get(), gfx::Region(output), position()));
      }
      else {
        // Patch "m_cel"
        (*m_tx)(new cmd::PatchCel(m_cel, m_dst.get(), gfx::Region(output), position()));
      }
    }

    result = CommandResult(CommandResult::kOk);
  }
  else {
    result = CommandResult(CommandResult::kCanceled);

    // Rollback transaction
    m_tx.reset();
  }

  ASSERT(m_reader.context());
  m_reader.context()->setCommandResult(result);
}

void FilterManagerImpl::applyToTarget(base::task_token* token)
{
  m_taskToken = token ? token : &m_noToken;
  applyToPaletteIfNeeded();

  const bool paletteChange = paletteHasChanged();
  bool cancelled = false;

  CelList cels;

  switch (m_celsTarget) {
    case CelsTarget::Selected: {
      cels = m_site.selectedUniqueCelsToEditPixels();
      break;
    }

    case CelsTarget::All: {
      for (Cel* cel : m_site.sprite()->uniqueCels()) {
        if (cel->layer()->canEditPixels())
          cels.push_back(cel);
      }
      break;
    }
  }

  if (cels.empty() && !paletteChange) {
    // We don't have images/palette changes to do (there will not be a
    // transaction).
    return;
  }

  m_progressBase = 0.0f;
  m_progressWidth = (cels.size() > 0 ? 1.0f / cels.size() : 1.0f);

  std::set<ObjectId> visited;

  // Palette change
  if (paletteChange) {
    Palette newPalette(*getNewPalette());
    restoreSpritePalette();
    if (auto* edit = make_palette_edit(m_site.layer(), m_site.frame(), newPalette)) (*m_tx)(edit);
  }

  // For each target image
  for (auto it = cels.begin(); it != cels.end() && !cancelled && !m_taskToken->canceled(); ++it) {
    Image* image = (*it)->image();

    // Avoid applying the filter two times to the same image
    if (visited.find(image->id()) == visited.end()) {
      visited.insert(image->id());
      applyToCel(*it);
    }

    // Is there a delegate to know if the process was cancelled by the user?
    if (m_progressDelegate)
      cancelled = m_progressDelegate->isCancelled();

    // Make progress
    m_progressBase += m_progressWidth;
  }

  // Reset m_oldPalette to avoid restoring the color palette
  m_oldPalette.reset(nullptr);
  m_bankNewPalette.reset();
  // Only the real manager is reused for another edit after Apply. Avoid
  // rebuilding the active cel's canvas after every layer and frame.
  if (!token && m_site.cel())
    init(m_site.cel());
}

void FilterManagerImpl::initTransaction()
{
  ASSERT(!m_tx);
  m_writer = std::make_unique<ContextWriter>(m_reader);
  m_tx.reset(new Tx(*m_writer, m_filter->getName(), ModifyDocument));
}

void FilterManagerImpl::updateWriterThread()
{
  document()->updateWriterThread();
}

bool FilterManagerImpl::isTransaction() const
{
  return (m_tx != nullptr);
}

// This must be executed in the main UI thread.
// Check Transaction::commit() comments.
void FilterManagerImpl::commitTransaction()
{
  ASSERT(m_tx);
  m_tx->commit();
  m_writer.reset();
  m_tx.reset();
}

void FilterManagerImpl::flush()
{
  int h = m_row - m_nextRowToFlush;

  if (m_row >= 0 && h > 0) {
    // Redraw the color palette
    if (m_nextRowToFlush == 0 && paletteHasChanged())
      redrawColorPalette();

    for (Editor* editor : UIContext::instance()->getAllEditorsIncludingPreview(document())) {
      // We expand the region one pixel at the top and bottom of the
      // region [m_row,m_nextRowToFlush) to be updated on the screen to
      // avoid screen artifacts when we apply filters like convolution
      // matrices.
      gfx::Rect rect(
        editor->editorToScreen(gfx::Point(m_bounds.x, m_bounds.y + m_nextRowToFlush - 1)),
        gfx::Size(editor->projection().applyX(m_bounds.w),
                  (editor->projection().scaleY() >= 1 ? editor->projection().applyY(h + 2) :
                                                        editor->projection().removeY(h + 2))));

      gfx::Region reg1(rect);
      editor->expandRegionByTiledMode(reg1, true);

      gfx::Region reg2;
      editor->getDrawableRegion(reg2, Widget::kCutTopWindows);
      reg1.createIntersection(reg1, reg2);

      editor->invalidateRegion(reg1);
    }

    m_nextRowToFlush = m_row + 1;
  }
}

void FilterManagerImpl::disablePreview()
{
  for (Editor* editor : UIContext::instance()->getAllEditorsIncludingPreview(document()))
    editor->invalidate();

  // Redraw the color bar in case the filter modified the palette.
  if (m_oldPalette) {
    restoreSpritePalette();
    set_current_palette(m_oldPalette.get(), false);
    m_oldPalette.reset();
    m_bankNewPalette.reset();
  }
}

const void* FilterManagerImpl::getSourceAddress()
{
  return m_src->getPixelAddress(m_bounds.x, m_bounds.y + m_row);
}

void* FilterManagerImpl::getDestinationAddress()
{
  return m_dst->getPixelAddress(m_bounds.x, m_bounds.y + m_row);
}

bool FilterManagerImpl::skipPixel()
{
  bool skip = false;

  if ((m_mask) && (m_mask->bitmap())) {
    if (!*m_maskIterator)
      skip = true;

    ++m_maskIterator;
  }

  return skip;
}

const Palette* FilterManagerImpl::getPalette() const
{
  if (m_oldPalette)
    return m_oldPalette.get();
  else
    return m_cel ? m_cel->layer()->palette(m_site.frame()) : m_site.palette();
}

const RgbMap* FilterManagerImpl::getRgbMap() const
{
  return m_cel ? m_cel->layer()->rgbMap(m_site.frame()) : m_site.rgbMap();
}

int FilterManagerImpl::getTransparentIndex() const
{
  return m_cel->layer()->isBackground() ? -1 : int(m_cel->sprite()->transparentColor());
}

Palette* FilterManagerImpl::getNewPalette()
{
  if (!m_oldPalette)
    m_oldPalette.reset(new Palette(*getPalette()));
  if (doc::uses_local_palette(m_site.layer())) {
    if (!m_bankNewPalette)
      m_bankNewPalette.reset(new Palette(*m_oldPalette));
    return m_bankNewPalette.get();
  }
  return m_site.sprite()->palette(m_site.frame());
}

doc::PalettePicks FilterManagerImpl::getPalettePicks()
{
  return m_site.selectedColors();
}

void FilterManagerImpl::init(Cel* cel)
{
  ASSERT(cel);

  Doc* doc = m_site.document();
  if (!updateBounds(doc->isMaskVisible() ? doc->mask() : nullptr))
    throw InvalidAreaException();

  m_cel = cel;
  const color_t padding = (pixelFormat() == IMAGE_INDEXED ? cel->sprite()->transparentColor() : 0);
  m_src = crop_cel_image(cel, padding);
  m_dst.reset(Image::createCopy(m_src.get()));

  m_row = -1;
  m_mask = nullptr;
  m_previewMask.reset(nullptr);

  m_target = m_targetOrig;

  // The alpha channel of the background layer can't be modified
  if (cel->layer()->isBackground())
    m_target &= ~TARGET_ALPHA_CHANNEL;
}

void FilterManagerImpl::applyToCel(Cel* cel)
{
  init(cel);
  apply();
}

bool FilterManagerImpl::updateBounds(doc::Mask* mask)
{
  gfx::Rect bounds;
  if (mask && mask->bitmap() && !mask->bounds().isEmpty()) {
    bounds = mask->bounds();
    bounds &= m_site.sprite()->bounds();
  }
  else {
    bounds = m_site.sprite()->bounds();
  }
  m_bounds = bounds;
  return !m_bounds.isEmpty();
}

bool FilterManagerImpl::paletteHasChanged()
{
  return (m_oldPalette && getPalette()->countDiff(getNewPalette(), nullptr, nullptr));
}

void FilterManagerImpl::restoreSpritePalette()
{
  // Restore the original palette to save the undoable "cmd"
  if (m_oldPalette && !m_bankNewPalette)
    m_site.sprite()->setPalette(m_oldPalette.get(), false);
}

void FilterManagerImpl::applyToPaletteIfNeeded()
{
  m_filter->applyToPalette(this);
}

void FilterManagerImpl::redrawColorPalette()
{
  set_current_palette(getNewPalette(), false);
  ColorBar::instance()->invalidate();
}

bool FilterManagerImpl::isMaskActive() const
{
  return m_site.document()->isMaskVisible();
}

base::task_token& FilterManagerImpl::taskToken() const
{
  ASSERT(m_taskToken); // It's always pointing to a token, m_noToken by default
  return *m_taskToken;
}

void FilterManagerImpl::setTaskToken(base::task_token& token)
{
  m_taskToken = &token;
}

} // namespace app
