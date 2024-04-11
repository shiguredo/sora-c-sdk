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
  FakeCapturer(int width, int height, int fps, SumomoFakeCapturerFormat format)
      : width_(width), height_(height), fps_(fps), format_(format) {
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
    th_.Start(fps_, [this](std::chrono::microseconds timestamp,
                           std::chrono::microseconds prev) {
      std::uniform_int_distribution<int> dist(0, width_ * height_ - 1);
      sorac::VideoFrame frame;
      frame.timestamp = timestamp;
      if (format_ == SUMOMO_FAKE_CAPTURER_FORMAT_I420) {
        frame.i420_buffer =
            sorac::VideoFrameBufferI420::Create(width_, height_);
        for (int i = 0; i < width_ / 100; i++) {
          frame.i420_buffer->y[dist(*engine_)] = 0xff;
        }
      } else if (format_ == SUMOMO_FAKE_CAPTURER_FORMAT_NV12) {
        frame.nv12_buffer =
            sorac::VideoFrameBufferNV12::Create(width_, height_);
        for (int i = 0; i < width_ / 100; i++) {
          frame.nv12_buffer->y[dist(*engine_)] = 0xff;
        }
      }
      frame.base_width = width_;
      frame.base_height = height_;
      callback_(frame);
    });
    return 0;
  }
  void Stop() { th_.Stop(); }

 private:
  int width_;
  int height_;
  int fps_;
  SumomoFakeCapturerFormat format_;
  std::function<void(const sorac::VideoFrame& frame)> callback_;
  SteadyFrameThread th_;
  std::unique_ptr<std::mt19937> engine_;
};

}  // namespace sumomo

extern "C" {

SumomoCapturer* sumomo_fake_capturer_create(int width,
                                            int height,
                                            int fps,
                                            SumomoFakeCapturerFormat format) {
  return new sumomo::FakeCapturer(width, height, fps, format);
}
}
