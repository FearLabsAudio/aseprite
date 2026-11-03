// Aseprite
// Copyright (C) 2019-2025  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/commands/filters/filter_preview.h"

#include "app/commands/filters/filter_manager_impl.h"
#include "app/console.h"
#include "app/modules/palettes.h"
#include "app/ui/editor/editor.h"
#include "app/ui/editor/editor_render.h"
#include "app/ui_context.h"
#include "base/thread.h"
#include "doc/layer.h"
#include "doc/sprite.h"
#include "ui/manager.h"
#include "ui/message.h"
#include "ui/widget.h"

namespace app {

using namespace ui;
using namespace filters;

FilterPreview::FilterPreview(FilterManagerImpl* filterMgr)
  : Widget(kGenericWidget)
  , m_filterMgr(filterMgr)
  , m_timer(1, this)
  , m_restartPreviewTimer(10)
{
  setVisible(false);

  m_restartPreviewTimer.Tick.connect([this] {
    m_restartPreviewTimer.stop();
    onDelayedStartPreview();
  });
}

FilterPreview::~FilterPreview()
{
  stop();
}

void FilterPreview::discardDocument()
{
  stop();
  m_previewContext.reset();
}

void FilterPreview::stop()
{
  m_restartPreviewTimer.stop();
  m_timer.stop();
  if (!m_started)
    return;

  m_filterTask.cancel();
  // An enqueued job also owns references to the filter and preview document.
  if (!m_filterTask.completed())
    m_filterTask.wait();

  auto& render = Editor::renderEngine();
  if (m_allLayers) {
    render.setPreviewSprite(nullptr, nullptr);
    m_allLayers->updateWriterThread();
    m_allLayers.reset(); // Roll back the private document for the next slider value.
    set_current_palette(m_filterMgr->layer()->palette(m_filterMgr->frame()), false);
  }
  else {
    render.removePreviewImage();
    m_filterMgr->end();
  }
  m_filterMgr->disablePreview();
  m_error = nullptr;
  m_started = false;
}

void FilterPreview::restartPreview()
{
  stop();

  // Start the timer to re-launch the filter preview in the next 10
  // milliseconds. This is necessary to avoid restarting the preview
  // immediately when a lot of mouse events are received at the same
  // time, so we can group several events in one restart.
  //
  // E.g. this can happen when the user scrolls the preview or when
  // changes a filter parameter (e.g. tolerance) and the operating
  // system (e.g. Linux) creates a lot of mouse events/change filter
  // param events.
  m_restartPreviewTimer.start();
}

void FilterPreview::onDelayedStartPreview()
{
  try {
    if (m_filterMgr->celsTarget() == CelsTarget::All) {
      if (!m_previewContext)
        m_previewContext = std::make_unique<Context>();
      m_allLayers = m_filterMgr->makeAllLayersPreview(*m_previewContext);
    }
    else {
      m_filterMgr->beginForPreview();
      Editor::renderEngine().setPreviewImage(
        m_filterMgr->layer(), m_filterMgr->frame(), m_filterMgr->destinationImage(),
        nullptr, m_filterMgr->position(), m_filterMgr->layer()->blendMode());
    }
    m_filterTask.run([this](base::task_token& token) {
      try {
        if (m_allLayers) {
          m_allLayers->updateWriterThread();
          m_allLayers->applyToTarget(&token);
        }
        else {
          m_filterMgr->setTaskToken(token);
          onFilterTask(token);
        }
      }
      catch (...) {
        m_error = std::current_exception();
      }
    });
    m_started = true;
    m_timer.start();
  }
  catch (const std::exception& e) {
    m_allLayers.reset();
    m_previewContext.reset();
    Editor::renderEngine().removePreviewImage();
    Console::showException(e);
  }
}

bool FilterPreview::onProcessMessage(Message* msg)
{
  switch (msg->type()) {
    case kCloseMessage: stop(); break;

    case kTimerMessage: {
      if (!m_started)
        break;
      if (!m_allLayers) {
        std::scoped_lock lock(m_filterMgrMutex);
        m_filterMgr->flush();
      }
      if (m_filterTask.completed()) {
        m_timer.stop();
        if (m_error) {
          auto error = m_error;
          discardDocument();
          try {
            std::rethrow_exception(error);
          }
          catch (const std::exception& e) {
            Console::showException(e);
          }
          catch (...) {
            Console().printf("Unable to preview this filter.\n");
          }
        }
        else if (m_allLayers) {
          // Publish only a completed result. Editors of other sprites keep
          // rendering their own documents.
          Editor::renderEngine().setPreviewSprite(m_filterMgr->sprite(), m_allLayers->sprite());
          set_current_palette(m_allLayers->layer()->palette(m_allLayers->frame()), false);
          for (auto* editor :
               UIContext::instance()->getAllEditorsIncludingPreview(m_filterMgr->document()))
            editor->invalidate();
        }
      }
      break;
    }
  }

  return Widget::onProcessMessage(msg);
}

// This is executed in other thread.
void FilterPreview::onFilterTask(base::task_token& token)
{
  while (!token.canceled()) {
    {
      std::scoped_lock lock(m_filterMgrMutex);
      if (!m_filterMgr->applyStep())
        token.cancel();
    }
    base::this_thread::yield();
  }
}

} // namespace app
