#include "fake_recorder.h"

#include <math.h>
#include <chrono>
#include <functional>
#include <vector>

// Sora C SDK
#include <sorac/types.hpp>

#include "recorder.h"
#include "steady_frame_thread.hpp"

namespace sumomo {

static const int RECORDING_SAMPLE_RATE = 48000;
static const int RECORDING_CHANNELS = 1;

class FakeRecorder : public SumomoRecorder {
 public:
  FakeRecorder() {
    this->destroy = [](SumomoRecorder* p) { delete (sumomo::FakeRecorder*)p; };
    this->set_frame_callback = [](SumomoRecorder* p,
                                  sumomo_recorder_on_frame_func on_frame,
                                  void* userdata) {
      ((sumomo::FakeRecorder*)p)
          ->SetFrameCallback(
              [on_frame, userdata](const sorac::AudioFrame& frame) {
                on_frame((SoracAudioFrameRef*)&frame, userdata);
              });
    };
    this->start = [](SumomoRecorder* p) {
      return ((sumomo::FakeRecorder*)p)->Start();
    };
    this->stop = [](SumomoRecorder* p) { ((sumomo::FakeRecorder*)p)->Stop(); };
  }

  void SetFrameCallback(
      std::function<void(const sorac::AudioFrame& frame)> callback) {
    callback_ = callback;
  }
  int Start() {
    th_.SetOnPrepare([this]() {
      di_ = 0;
      // ダミーデータを作る
      {
        static const int SAMPLE_RATE = RECORDING_SAMPLE_RATE;
        static const double BIPBOP_DURATION = 0.07;
        static const double BIPBOP_VOLUME = 0.5;
        static const double BIP_FREQUENCY = 1500;
        static const double BOP_FREQUENCY = 500;
        static const double HUM_FREQUENCY = 150;
        static const double HUM_VOLUME = 0.1;
        static const double NOISE_FREQUENCY = 3000;
        static const double NOISE_VOLUME = 0.05;

        auto add_hum = [](float volume, float frequency, float sample_rate,
                          int start, float* p, int count) {
          float hum_period = sample_rate / frequency;
          for (int i = start; i < start + count; ++i) {
            float a = 0.1 * volume * sin(i * 2 * M_PI / hum_period);
            *p += a;
            ++p;
          }
        };
        dummy_.resize(SAMPLE_RATE * 2);

        int bipbop_sample_count = (int)std::ceil(BIPBOP_DURATION * SAMPLE_RATE);

        add_hum(BIPBOP_VOLUME, BIP_FREQUENCY, SAMPLE_RATE, 0, dummy_.data(),
                bipbop_sample_count);
        add_hum(BIPBOP_VOLUME, BOP_FREQUENCY, SAMPLE_RATE, 0,
                dummy_.data() + SAMPLE_RATE, bipbop_sample_count);
        add_hum(NOISE_VOLUME, NOISE_FREQUENCY, SAMPLE_RATE, 0, dummy_.data(),
                2 * SAMPLE_RATE);
        add_hum(HUM_VOLUME, HUM_FREQUENCY, SAMPLE_RATE, 0, dummy_.data(),
                2 * SAMPLE_RATE);
      }
      return nullptr;
    });

    th_.Start(50, [this](std::chrono::microseconds timestamp,
                         std::chrono::microseconds prev) {
      auto d = timestamp - prev;

      // 1000 * 1000 [microsec] なら 16000 [Hz] = 16000 [samples]
      // なので d [microsec] なら 16000 * (d / (1000 * 1000)) [samples] になる
      auto samples = int64_t(RECORDING_SAMPLE_RATE) * d.count() / (1000 * 1000);
      sorac::AudioFrame frame;
      frame.sample_rate = RECORDING_SAMPLE_RATE;
      frame.channels = RECORDING_CHANNELS;
      frame.timestamp = timestamp;
      frame.samples = samples;
      frame.pcm.reset(new float[frame.samples * frame.channels]());
      // ダミーのデータを適当に詰める
      for (int i = 0; i < frame.samples; i++) {
        for (int c = 0; c < frame.channels; c++) {
          frame.pcm[i * frame.channels + c] = dummy_[di_];
        }
        di_ = (di_ + 1) % dummy_.size();
      }

      callback_(frame);
    });
    return 0;
  }
  void Stop() { th_.Stop(); }

 private:
  std::function<void(const sorac::AudioFrame& frame)> callback_;
  SteadyFrameThread th_;
  std::vector<float> dummy_;
  int di_;
};

}  // namespace sumomo

extern "C" {

SumomoRecorder* sumomo_fake_recorder_create() {
  return new sumomo::FakeRecorder();
}
}
