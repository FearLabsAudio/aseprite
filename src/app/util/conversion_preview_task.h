// Owns a conversion preview worker. Failures are reported by the UI after joining.
#pragma once

#include "render/task_delegate.h"
#include <atomic>
#include <exception>
#include <thread>
#include <utility>

namespace app {
class ConversionPreviewTask : public render::TaskDelegate {
public:
  template<typename Work>
  explicit ConversionPreviewTask(Work work)
    : m_thread([this, work = std::move(work)]() mutable {
        try {
          work(*this);
        }
        catch (...) {
          if (!m_stop) m_error = std::current_exception();
        }
        m_running = false;
      })
  {
  }

  ~ConversionPreviewTask() { stop(); }
  void stop()
  {
    m_stop = true;
    if (m_thread.joinable())
      m_thread.join();
  }
  bool isRunning() const { return m_running; }
  double progress() const { return m_progress; }
  // Only read after stop() has joined the worker.
  std::exception_ptr error() const { return m_error; }
  bool continueTask() override { return !m_stop; }
  void notifyTaskProgress(double value) override { m_progress = value; }

private:
  std::atomic<bool> m_running{ true };
  std::atomic<bool> m_stop{ false };
  std::atomic<double> m_progress{ 0.0 };
  std::exception_ptr m_error;
  // Last: all shared state must exist before the worker starts.
  std::thread m_thread;
};
} // namespace app
