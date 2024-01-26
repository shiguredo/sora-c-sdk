#include "pulse_recorder.h"

#include <math.h>
#include <string.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

// Linux
#include <pulse/pulseaudio.h>

// Sora C SDK
#include <sorac/current_time.hpp>
#include <sorac/types.hpp>

#include "dyn.hpp"
#include "recorder.h"
#include "steady_frame_thread.hpp"

namespace dyn {

static const char PULSE_SO[] = "libpulse.so.0";
DYN_REGISTER(PULSE_SO, pa_bytes_per_second);
DYN_REGISTER(PULSE_SO, pa_context_connect);
DYN_REGISTER(PULSE_SO, pa_context_disconnect);
DYN_REGISTER(PULSE_SO, pa_context_errno);
DYN_REGISTER(PULSE_SO, pa_context_get_protocol_version);
DYN_REGISTER(PULSE_SO, pa_context_get_server_info);
DYN_REGISTER(PULSE_SO, pa_context_get_sink_info_list);
DYN_REGISTER(PULSE_SO, pa_context_get_sink_info_by_index);
DYN_REGISTER(PULSE_SO, pa_context_get_sink_info_by_name);
DYN_REGISTER(PULSE_SO, pa_context_get_sink_input_info);
DYN_REGISTER(PULSE_SO, pa_context_get_source_info_by_index);
DYN_REGISTER(PULSE_SO, pa_context_get_source_info_by_name);
DYN_REGISTER(PULSE_SO, pa_context_get_source_info_list);
DYN_REGISTER(PULSE_SO, pa_context_get_state);
DYN_REGISTER(PULSE_SO, pa_context_new);
DYN_REGISTER(PULSE_SO, pa_context_set_sink_input_volume);
DYN_REGISTER(PULSE_SO, pa_context_set_sink_input_mute);
DYN_REGISTER(PULSE_SO, pa_context_set_source_volume_by_index);
DYN_REGISTER(PULSE_SO, pa_context_set_source_mute_by_index);
DYN_REGISTER(PULSE_SO, pa_context_set_state_callback);
DYN_REGISTER(PULSE_SO, pa_context_unref);
DYN_REGISTER(PULSE_SO, pa_cvolume_set);
DYN_REGISTER(PULSE_SO, pa_operation_get_state);
DYN_REGISTER(PULSE_SO, pa_operation_unref);
DYN_REGISTER(PULSE_SO, pa_stream_connect_playback);
DYN_REGISTER(PULSE_SO, pa_stream_connect_record);
DYN_REGISTER(PULSE_SO, pa_stream_disconnect);
DYN_REGISTER(PULSE_SO, pa_stream_drop);
DYN_REGISTER(PULSE_SO, pa_stream_get_device_index);
DYN_REGISTER(PULSE_SO, pa_stream_get_index);
DYN_REGISTER(PULSE_SO, pa_stream_get_latency);
DYN_REGISTER(PULSE_SO, pa_stream_get_sample_spec);
DYN_REGISTER(PULSE_SO, pa_stream_get_state);
DYN_REGISTER(PULSE_SO, pa_stream_new);
DYN_REGISTER(PULSE_SO, pa_stream_peek);
DYN_REGISTER(PULSE_SO, pa_stream_readable_size);
DYN_REGISTER(PULSE_SO, pa_stream_set_buffer_attr);
DYN_REGISTER(PULSE_SO, pa_stream_set_overflow_callback);
DYN_REGISTER(PULSE_SO, pa_stream_set_read_callback);
DYN_REGISTER(PULSE_SO, pa_stream_set_state_callback);
DYN_REGISTER(PULSE_SO, pa_stream_set_underflow_callback);
DYN_REGISTER(PULSE_SO, pa_stream_set_write_callback);
DYN_REGISTER(PULSE_SO, pa_stream_unref);
DYN_REGISTER(PULSE_SO, pa_stream_writable_size);
DYN_REGISTER(PULSE_SO, pa_stream_write);
DYN_REGISTER(PULSE_SO, pa_strerror);
DYN_REGISTER(PULSE_SO, pa_threaded_mainloop_free);
DYN_REGISTER(PULSE_SO, pa_threaded_mainloop_get_api);
DYN_REGISTER(PULSE_SO, pa_threaded_mainloop_lock);
DYN_REGISTER(PULSE_SO, pa_threaded_mainloop_new);
DYN_REGISTER(PULSE_SO, pa_threaded_mainloop_signal);
DYN_REGISTER(PULSE_SO, pa_threaded_mainloop_start);
DYN_REGISTER(PULSE_SO, pa_threaded_mainloop_stop);
DYN_REGISTER(PULSE_SO, pa_threaded_mainloop_unlock);
DYN_REGISTER(PULSE_SO, pa_threaded_mainloop_wait);

}  // namespace dyn

namespace sumomo {

static const int RECORDING_SAMPLE_RATE = 48000;
static const int RECORDING_CHANNELS = 1;

class PulseRecorder : public SumomoRecorder {
 public:
  PulseRecorder() {
    this->destroy = [](SumomoRecorder* p) { delete (sumomo::PulseRecorder*)p; };
    this->set_frame_callback = [](SumomoRecorder* p,
                                  sumomo_recorder_on_frame_func on_frame,
                                  void* userdata) {
      ((sumomo::PulseRecorder*)p)
          ->SetFrameCallback(
              [on_frame, userdata](const sorac::AudioFrame& frame) {
                on_frame((SoracAudioFrameRef*)&frame, userdata);
              });
    };
    this->start = [](SumomoRecorder* p) {
      return ((sumomo::PulseRecorder*)p)->Start();
    };
    this->stop = [](SumomoRecorder* p) { ((sumomo::PulseRecorder*)p)->Stop(); };
  }
  ~PulseRecorder() { Stop(); }

  void SetFrameCallback(
      std::function<void(const sorac::AudioFrame& frame)> callback) {
    callback_ = callback;
  }
  int Start() {
    pa_mainloop_ = dyn::pa_threaded_mainloop_new();
    if (pa_mainloop_ == nullptr) {
      fprintf(stderr, "Failed to create pa_mainloop\n");
      return -1;
    }
    if (int r = dyn::pa_threaded_mainloop_start(pa_mainloop_); r != 0) {
      fprintf(stderr, "Failed to start pa_mainloop: error=%d\n", r);
      return -1;
    }

    // pa_context 周りの作成
    {
      dyn::pa_threaded_mainloop_lock(pa_mainloop_);
      std::shared_ptr<int> unlocker(nullptr, [this](int*) {
        dyn::pa_threaded_mainloop_unlock(pa_mainloop_);
      });

      pa_mainloop_api_ = dyn::pa_threaded_mainloop_get_api(pa_mainloop_);
      if (pa_mainloop_api_ == nullptr) {
        fprintf(stderr, "Failed to create pa_mainloop_api\n");
        return -1;
      }
      pa_context_ = dyn::pa_context_new(pa_mainloop_api_, "Recorder");
      if (pa_context_ == nullptr) {
        fprintf(stderr, "Failed to create pa_context\n");
        return -1;
      }
      dyn::pa_context_set_state_callback(
          pa_context_,
          [](pa_context* c, void* userdata) {
            auto p = (sumomo::PulseRecorder*)userdata;
            pa_context_state_t state = dyn::pa_context_get_state(c);
            switch (state) {
              case PA_CONTEXT_FAILED:
              case PA_CONTEXT_TERMINATED:
                p->pa_state_changed_ = true;
                dyn::pa_threaded_mainloop_signal(p->pa_mainloop_, 0);
              case PA_CONTEXT_READY:
                p->pa_state_changed_ = true;
                dyn::pa_threaded_mainloop_signal(p->pa_mainloop_, 0);
              default:
                break;
            }
          },
          this);

      pa_state_changed_ = false;
      if (auto r = dyn::pa_context_connect(pa_context_, (const char*)NULL,
                                           PA_CONTEXT_NOAUTOSPAWN,
                                           (const pa_spawn_api*)NULL);
          r != PA_OK) {
        fprintf(stderr, "Failed to connect pa_context: error=%d\n", r);
        return -1;
      }

      while (!pa_state_changed_) {
        dyn::pa_threaded_mainloop_wait(pa_mainloop_);
      }

      pa_context_state_t state = dyn::pa_context_get_state(pa_context_);
      if (state != PA_CONTEXT_READY) {
        fprintf(stderr, "Failed to connect pa_context: state=%d\n", state);
        return -1;
      }
    }

    // pa_server_info の取得
    {
      dyn::pa_threaded_mainloop_lock(pa_mainloop_);
      std::shared_ptr<int> unlocker(nullptr, [this](int*) {
        dyn::pa_threaded_mainloop_unlock(pa_mainloop_);
      });
      pa_operation* pa_op = dyn::pa_context_get_server_info(
          pa_context_,
          [](pa_context* c, const pa_server_info* i, void* userdata) {
            auto p = (sumomo::PulseRecorder*)userdata;
            p->sample_rate_ = i->sample_spec.rate;
            p->rec_device_name_ = i->default_source_name;
            printf("sample_rate: %d\n", p->sample_rate_);
            printf("rec_device_name: %s\n", p->rec_device_name_.c_str());
            dyn::pa_threaded_mainloop_signal(p->pa_mainloop_, 0);
          },
          this);

      while (dyn::pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING) {
        dyn::pa_threaded_mainloop_wait(pa_mainloop_);
      }
      dyn::pa_operation_unref(pa_op);
    }

    // 録音周りの初期化
    {
      rec_sample_rate_ = 48000;

      pa_sample_spec spec;
      spec.channels = 1;
      spec.format = PA_SAMPLE_FLOAT32LE;
      spec.rate = rec_sample_rate_;
      rec_stream_ = dyn::pa_stream_new(pa_context_, "recStream", &spec,
                                       (const pa_channel_map*)NULL);
      if (rec_stream_ == nullptr) {
        fprintf(stderr, "Failed to create rec_stream: err=%d\n",
                dyn::pa_context_errno(pa_context_));
        return -1;
      }

      dyn::pa_stream_set_state_callback(
          rec_stream_,
          [](pa_stream*, void* userdata) {
            auto p = (sumomo::PulseRecorder*)userdata;
            dyn::pa_threaded_mainloop_signal(p->pa_mainloop_, 0);
          },
          this);
    }

    quit_ = false;
    th_.reset(new std::thread([this]() {
      {
        dyn::pa_threaded_mainloop_lock(pa_mainloop_);
        std::shared_ptr<int> unlocker(nullptr, [this](int*) {
          dyn::pa_threaded_mainloop_unlock(pa_mainloop_);
        });

        const pa_sample_spec* spec =
            dyn::pa_stream_get_sample_spec(rec_stream_);
        if (spec == nullptr) {
          fprintf(stderr, "Failed to pa_stream_get_sample_spec\n");
          return;
        }

        auto bytes_per_sec = dyn::pa_bytes_per_second(spec);
        // 通常は 10ms のレイテンシにする
        auto fragsize = bytes_per_sec * 10 / 1000;
        // 追加で 750ms まで許容する
        auto maxlength = fragsize + bytes_per_sec * 750 / 1000;

        pa_buffer_attr attr;
        attr.fragsize = fragsize;
        attr.maxlength = maxlength;

        if (auto r = dyn::pa_stream_connect_record(
                rec_stream_, rec_device_name_.c_str(), &attr,
                (pa_stream_flags_t)(PA_STREAM_AUTO_TIMING_UPDATE |
                                    PA_STREAM_INTERPOLATE_TIMING |
                                    PA_STREAM_ADJUST_LATENCY));
            r != PA_OK) {
          fprintf(stderr, "Failed to connect rec_stream: err=%d\n", r);
          return;
        }

        while (dyn::pa_stream_get_state(rec_stream_) != PA_STREAM_READY) {
          dyn::pa_threaded_mainloop_wait(pa_mainloop_);
        }

        dyn::pa_stream_set_read_callback(rec_stream_, ReadCallback, this);
      }

      std::vector<sorac::AudioFrame> frames;
      std::optional<std::chrono::microseconds> last_timestamp;
      while (true) {
        {
          std::unique_lock<std::mutex> lock(rec_mutex_);
          rec_cv_.wait(lock, [this]() { return rec_size_ > 0 || quit_; });
          if (quit_) {
            break;
          }
          while (true) {
            sorac::AudioFrame frame;
            frame.sample_rate = rec_sample_rate_;
            frame.channels = 1;
            frame.samples = rec_size_ / sizeof(float);
            if (last_timestamp == std::nullopt) {
              last_timestamp = frame.timestamp;
            } else {
              *last_timestamp += std::chrono::microseconds(
                  std::chrono::microseconds(std::chrono::seconds(1)).count() *
                  frame.samples / frame.sample_rate);
            }
            frame.timestamp = *last_timestamp;
            frame.pcm.reset(new float[frame.samples * frame.channels]());
            memcpy(frame.pcm.get(), rec_data_, rec_size_);
            frames.push_back(std::move(frame));
            rec_size_ = 0;
            rec_data_ = nullptr;

            dyn::pa_threaded_mainloop_lock(pa_mainloop_);
            std::shared_ptr<int> unlocker(nullptr, [this](int*) {
              dyn::pa_threaded_mainloop_unlock(pa_mainloop_);
            });
            dyn::pa_stream_drop(rec_stream_);
            if (dyn::pa_stream_readable_size(rec_stream_) <= 0) {
              break;
            }
            if (auto r =
                    dyn::pa_stream_peek(rec_stream_, &rec_data_, &rec_size_);
                r != PA_OK) {
              printf("Failed to pa_stream_peek: err=%d\n", r);
              break;
            }
          }
        }
        {
          dyn::pa_threaded_mainloop_lock(pa_mainloop_);
          std::shared_ptr<int> unlocker(nullptr, [this](int*) {
            dyn::pa_threaded_mainloop_unlock(pa_mainloop_);
          });
          dyn::pa_stream_set_read_callback(rec_stream_, ReadCallback, this);
        }
        for (const auto& frame : frames) {
          callback_(frame);
        }
        frames.clear();
      }
    }));

    return 0;
  }
  void Stop() {
    if (th_ != nullptr) {
      quit_ = true;
      rec_cv_.notify_one();
      th_->join();
      th_.reset();
    }
  }

 private:
  static void ReadCallback(pa_stream*, size_t, void* userdata) {
    auto p = (sumomo::PulseRecorder*)userdata;
    std::lock_guard<std::mutex> lock(p->rec_mutex_);
    if (auto r =
            dyn::pa_stream_peek(p->rec_stream_, &p->rec_data_, &p->rec_size_);
        r != PA_OK) {
      fprintf(stderr, "Failed to pa_stream_peek: err=%d\n", r);
      return;
    }
    dyn::pa_stream_set_read_callback(p->rec_stream_, nullptr, nullptr);
    p->rec_cv_.notify_one();
  }

 private:
  std::function<void(const sorac::AudioFrame& frame)> callback_;
  std::shared_ptr<std::thread> th_;
  std::atomic<bool> quit_;
  std::vector<float> dummy_;
  int di_;

  pa_threaded_mainloop* pa_mainloop_;
  pa_mainloop_api* pa_mainloop_api_;
  pa_context* pa_context_;
  bool pa_state_changed_ = false;
  std::string rec_device_name_;
  pa_stream* rec_stream_;
  int sample_rate_;
  int rec_sample_rate_;

  std::condition_variable rec_cv_;
  std::mutex rec_mutex_;
  const void* rec_data_;
  size_t rec_size_;
};

}  // namespace sumomo

extern "C" {

SumomoRecorder* sumomo_pulse_recorder_create() {
  return new sumomo::PulseRecorder();
}
}
