// Aseprite
// Copyright (C) 2022-2023  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_COMMANDS_FILTERS_FILTER_PREVIEW_H_INCLUDED
#define APP_COMMANDS_FILTERS_FILTER_PREVIEW_H_INCLUDED
#pragma once

#include "app/task.h"
#include "ui/timer.h"
#include "ui/widget.h"

#include <exception>
#include <memory>
#include <mutex>

namespace app {

class FilterManagerImpl;
class Context;

// Invisible widget to control a effect-preview in the current editor.
class FilterPreview : public ui::Widget {
public:
  FilterPreview(FilterManagerImpl* filterMgr);
  ~FilterPreview();

  void stop();
  void restartPreview();
  void discardDocument();

protected:
  bool onProcessMessage(ui::Message* msg) override;

private:
  void onFilterTask(base::task_token& token);
  void onDelayedStartPreview();

  FilterManagerImpl* m_filterMgr;
  ui::Timer m_timer;
  ui::Timer m_restartPreviewTimer;
  std::mutex m_filterMgrMutex;
  app::Task m_filterTask;
  bool m_started = false;
  std::unique_ptr<Context> m_previewContext;
  std::unique_ptr<FilterManagerImpl> m_allLayers;
  std::exception_ptr m_error;
};

} // namespace app

#endif
