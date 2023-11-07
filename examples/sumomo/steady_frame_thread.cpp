#include "steady_frame_thread.hpp"

// Linux
#include <time.h>

// Sora C SDK
#include <sorac/current_time.hpp>

namespace sumomo {

SteadyFrameThread::SteadyFrameThread() : stop_(false) {}
SteadyFrameThread::~SteadyFrameThread() {
  Stop();
}

void SteadyFrameThread::SetOnPrepare(
    std::function<std::function<void()>()> on_prepare) {
  on_prepare_ = on_prepare;
}

void SteadyFrameThread::Start(
    int fps,
    std::function<void(std::chrono::microseconds, std::chrono::microseconds)>
        on_frame) {
  Stop();
  th_.reset(new std::thread([this, fps, on_frame]() {
    std::function<void()> finish;
    if (on_prepare_) {
      finish = on_prepare_();
      on_prepare_ = nullptr;
    }

    auto frame_duration =
        std::chrono::nanoseconds(std::chrono::seconds(1)) / fps;

    auto prev = sorac::get_current_time();

    while (!stop_) {
      auto timestamp = sorac::get_current_time();
      auto d = timestamp - prev;
      // ENCODING_FRAME_DURATION_MS 秒分のデータが溜まるまで continue
      if (d < frame_duration - std::chrono::milliseconds(1)) {
        // d = [0, frame_duration - 1)

        // まだ時間があるので sleep する
        // 予定時間の 1 ミリ秒前に起きる
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = std::chrono::nanoseconds(frame_duration -
                                              std::chrono::milliseconds(1) - d)
                         .count();
        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
        continue;
      } else if (d < frame_duration) {
        // d = [frame_duration - 1, frame_duration)
        // もうすぐなのでスピンロック
        continue;
      }

      on_frame(timestamp, prev);

      prev = timestamp;
    }

    if (finish) {
      finish();
    }
  }));
}

void SteadyFrameThread::Stop() {
  if (th_ != nullptr) {
    stop_ = true;
    th_->join();
    th_ = nullptr;
    stop_ = false;
  }
}

}  // namespace sumomo
