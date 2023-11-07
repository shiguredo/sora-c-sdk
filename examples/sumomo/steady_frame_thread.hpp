#ifndef SUMOMO_STEADY_FRAME_THREAD_HPP_
#define SUMOMO_STEADY_FRAME_THREAD_HPP_

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace sumomo {

class SteadyFrameThread {
 public:
  SteadyFrameThread();
  ~SteadyFrameThread();

  void SetOnPrepare(std::function<std::function<void()>()> on_prepare);

  void Start(int fps,
             std::function<void(std::chrono::microseconds,
                                std::chrono::microseconds)> on_frame);
  void Stop();

 private:
  std::function<std::function<void()>()> on_prepare_;
  std::shared_ptr<std::thread> th_;
  std::atomic<bool> stop_;
};

}  // namespace sumomo

#endif