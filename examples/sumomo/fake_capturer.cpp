#include "fake_capturer.h"

#include <memory>
#include <random>

// Sora C SDK
#include <sorac/types.hpp>

#include "capturer.h"
#include "steady_frame_thread.hpp"

namespace sumomo {

class FakeCapturer : public SumomoCapturer {
 public:
  FakeCapturer() {
    this->destroy = [](SumomoCapturer* p) { delete (sumomo::FakeCapturer*)p; };
    this->set_frame_callback = [](SumomoCapturer* p,
                                  sumomo_capturer_on_frame_func on_frame,
                                  void* userdata) {
      ((sumomo::FakeCapturer*)p)
          ->SetFrameCallback(
              [on_frame, userdata](const sorac::VideoFrame& frame) {
                sorac::VideoFrame f = frame;
                on_frame((SoracVideoFrameRef*)&f, userdata);
              });
    };
    this->start = [](SumomoCapturer* p) {
      return ((sumomo::FakeCapturer*)p)->Start();
    };
    this->stop = [](SumomoCapturer* p) { ((sumomo::FakeCapturer*)p)->Stop(); };
  }

  void SetFrameCallback(
      std::function<void(const sorac::VideoFrame& frame)> callback) {
    callback_ = callback;
  }

  int Start() {
    th_.SetOnPrepare([this]() {
      std::random_device seed_gen;
      engine_ = std::make_unique<std::mt19937>(seed_gen());
      return nullptr;
    });
    th_.Start(30, [this](std::chrono::microseconds timestamp,
                         std::chrono::microseconds prev) {
      std::uniform_int_distribution<int> dist(0, 640 * 480 - 1);
      sorac::VideoFrame frame;
      frame.timestamp = timestamp;
      frame.i420_buffer = sorac::VideoFrameBufferI420::Create(640, 480);
      frame.i420_buffer->y[dist(*engine_)] = 0xff;
      frame.i420_buffer->y[dist(*engine_)] = 0xff;
      frame.i420_buffer->y[dist(*engine_)] = 0xff;
      frame.i420_buffer->y[dist(*engine_)] = 0xff;
      frame.i420_buffer->y[dist(*engine_)] = 0xff;
      frame.base_width = 640;
      frame.base_height = 480;
      callback_(frame);
    });
    return 0;
  }
  void Stop() { th_.Stop(); }

 private:
  std::function<void(const sorac::VideoFrame& frame)> callback_;
  SteadyFrameThread th_;
  std::unique_ptr<std::mt19937> engine_;
};

}  // namespace sumomo

extern "C" {

SumomoCapturer* sumomo_fake_capturer_create() {
  return new sumomo::FakeCapturer();
}
}
