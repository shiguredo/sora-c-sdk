#include "macos_recorder.h"

#include <functional>
#include <thread>

// Mac
#include <AudioToolbox/AudioConverter.h>
#include <CoreAudio/CoreAudio.h>

// Sora C SDK
#include <sorac/current_time.hpp>
#include <sorac/types.hpp>

namespace sumomo {

static const int RECORDING_SAMPLE_RATE = 48000;
static const int RECORDING_CHANNELS = 1;
static const int RECORDING_FRAME_TIME_MS = 20;

class MacosRecorder : public SumomoRecorder {
 public:
  MacosRecorder() {
    this->destroy = [](SumomoRecorder* p) { delete (sumomo::MacosRecorder*)p; };
    this->set_frame_callback = [](SumomoRecorder* p,
                                  sumomo_recorder_on_frame_func on_frame,
                                  void* userdata) {
      ((sumomo::MacosRecorder*)p)
          ->SetFrameCallback(
              [on_frame, userdata](const sorac::AudioFrame& frame) {
                on_frame((SoracAudioFrameRef*)&frame, userdata);
              });
    };
    this->start = [](SumomoRecorder* p) {
      return ((sumomo::MacosRecorder*)p)->Start();
    };
    this->stop = [](SumomoRecorder* p) { ((sumomo::MacosRecorder*)p)->Stop(); };
  }
  ~MacosRecorder() { Stop(); }

  void SetFrameCallback(
      std::function<void(const sorac::AudioFrame& frame)> callback) {
    callback_ = callback;
  }
  int Start() {
    // デバイスの初期化
    {
      OSStatus err = noErr;
      UInt32 size;
      AudioObjectPropertyAddress pa = {kAudioHardwarePropertyDefaultInputDevice,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};

      size = sizeof(AudioDeviceID);
      if (OSStatus err = AudioObjectGetPropertyData(
              kAudioObjectSystemObject, &pa, 0, NULL, &size, &rec_device_id_);
          err != noErr) {
        fprintf(stderr,
                "AudioObjectGetPropertyData("
                "kAudioHardwarePropertyDefaultInputDevice) failed: %d\n",
                err);
        return -1;
      }
      if (rec_device_id_ == kAudioDeviceUnknown) {
        fprintf(stderr, "No default device exists\n");
        return -1;
      }

      char name[128];
      char manf[128];
      memset(name, 0, sizeof(name));
      memset(manf, 0, sizeof(manf));

      pa.mSelector = kAudioDevicePropertyDeviceName;
      pa.mScope = kAudioDevicePropertyScopeInput;
      pa.mElement = 0;
      size = sizeof(name);
      if (OSStatus err = AudioObjectGetPropertyData(rec_device_id_, &pa, 0,
                                                    NULL, &size, name);
          err != noErr) {
        fprintf(stderr,
                "AudioObjectGetPropertyData(kAudioDevicePropertyDeviceName) "
                "failed: %d\n",
                err);
        return -1;
      }

      pa.mSelector = kAudioDevicePropertyDeviceManufacturer;
      size = sizeof(manf);
      if (OSStatus err = AudioObjectGetPropertyData(rec_device_id_, &pa, 0,
                                                    NULL, &size, manf);
          err != noErr) {
        fprintf(stderr,
                "AudioObjectGetPropertyData("
                "kAudioDevicePropertyDeviceManufacturer) failed: %d\n",
                err);
        return -1;
      }

      printf("Input device: manf=%s name=%s\n", manf, name);
    }
    // 録音デバイスの初期化
    {
      UInt32 size = 0;
      AudioObjectPropertyAddress pa = {kAudioDevicePropertyStreamFormat,
                                       kAudioDevicePropertyScopeInput, 0};
      memset(&rec_stream_format_, 0, sizeof(rec_stream_format_));
      size = sizeof(rec_stream_format_);
      if (OSStatus err = AudioObjectGetPropertyData(
              rec_device_id_, &pa, 0, NULL, &size, &rec_stream_format_);
          err != noErr) {
        fprintf(stderr,
                "AudioObjectGetPropertyData(kAudioDevicePropertyStreamFormat) "
                "failed: %d\n",
                err);
        return -1;
      }

      if (rec_stream_format_.mFormatID != kAudioFormatLinearPCM) {
        fprintf(stderr, "Unacceptable input stream format -> mFormatID=%s\n",
                (const char*)&rec_stream_format_.mFormatID);
        return -1;
      }

      printf("Input stream format:\n");
      printf("  mSampleRate=%d, mChannelsPerFrame=%d\n",
             (int)rec_stream_format_.mSampleRate,
             rec_stream_format_.mChannelsPerFrame);
      printf("  mBytesPerPacket=%d, mFramesPerPacket=%d\n",
             rec_stream_format_.mBytesPerPacket,
             rec_stream_format_.mFramesPerPacket);
      printf("  mBytesPerFrame=%d, mBitsPerChannel=%d\n",
             rec_stream_format_.mBytesPerFrame,
             rec_stream_format_.mBitsPerChannel);
      printf("  mFormatFlags=%d, mFormatID=%s\n",
             (unsigned int)rec_stream_format_.mFormatFlags,
             (const char*)&rec_stream_format_.mFormatID);

      rec_desired_format_.mChannelsPerFrame = RECORDING_CHANNELS;
      rec_desired_format_.mSampleRate = RECORDING_SAMPLE_RATE;
      rec_desired_format_.mBytesPerPacket =
          rec_desired_format_.mChannelsPerFrame * sizeof(float);
      rec_desired_format_.mFramesPerPacket = 1;
      rec_desired_format_.mBytesPerFrame =
          rec_desired_format_.mChannelsPerFrame * sizeof(float);
      rec_desired_format_.mBitsPerChannel = sizeof(float) * 8;

      rec_desired_format_.mFormatFlags =
          kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked;
      rec_desired_format_.mFormatID = kAudioFormatLinearPCM;

      if (OSStatus err = AudioConverterNew(
              &rec_stream_format_, &rec_desired_format_, &rec_converter_);
          err != noErr) {
        fprintf(stderr, "AudioConverterNew failed: %d\n", err);
        return -1;
      }

      pa.mSelector = kAudioDevicePropertyBufferSizeRange;
      AudioValueRange range;
      size = sizeof(range);
      if (OSStatus err = AudioObjectGetPropertyData(rec_device_id_, &pa, 0,
                                                    NULL, &size, &range);
          err != noErr) {
        fprintf(stderr,
                "AudioObjectGetPropertyData("
                "kAudioDevicePropertyBufferSizeRange) failed: %d\n",
                err);
        return -1;
      }

      pa.mSelector = kAudioDevicePropertyBufferSize;
      size = sizeof(UInt32);
      UInt32 buffer_size = (UInt32)range.mMaximum;
      if (OSStatus err = AudioObjectSetPropertyData(rec_device_id_, &pa, 0,
                                                    NULL, size, &buffer_size);
          err != noErr) {
        fprintf(stderr,
                "AudioObjectSetPropertyData(kAudioDevicePropertyBufferSize) "
                "failed: %d\n",
                err);
        return -1;
      }
      printf("Buffer Size: %d\n", buffer_size);

      pa.mSelector = kAudioDevicePropertyStreamFormat;
      if (OSStatus err = AudioObjectAddPropertyListener(
              rec_device_id_, &pa, &OnListenerProcStatic, this);
          err != noErr) {
        fprintf(stderr,
                "AudioObjectAddPropertyListener("
                "kAudioDevicePropertyStreamFormat) failed: %d\n",
                err);
        return -1;
      }

      pa.mSelector = kAudioDeviceProcessorOverload;
      if (OSStatus err = AudioObjectAddPropertyListener(
              rec_device_id_, &pa, &OnListenerProcStatic, this);
          err != noErr) {
        fprintf(stderr,
                "AudioObjectAddPropertyListener(kAudioDeviceProcessorOverload) "
                "failed: %d\n",
                err);
        return -1;
      }

      if (OSStatus err = AudioDeviceCreateIOProcID(
              rec_device_id_, OnIOProcStatic, this, &rec_ioproc_id_);
          err != noErr) {
        fprintf(stderr, "AudioDeviceCreateIOProcID failed: %d\n", err);
        return -1;
      }
    }
    // 録音開始
    {
      th_.reset(new std::thread([this] {
        std::optional<std::chrono::microseconds> timestamp;
        while (true) {
          // RECORDING_FRAME_TIME_MS 分のバッファを用意する
          UInt32 samples = RECORDING_SAMPLE_RATE * RECORDING_CHANNELS *
                           RECORDING_FRAME_TIME_MS / 1000;
          size_t buffer_count = samples * rec_desired_format_.mChannelsPerFrame;
          std::unique_ptr<float[]> buffer(new float[buffer_count]);

          AudioBufferList bufs;
          bufs.mNumberBuffers = 1;
          bufs.mBuffers->mNumberChannels =
              rec_desired_format_.mChannelsPerFrame;
          bufs.mBuffers->mDataByteSize =
              samples * rec_desired_format_.mBytesPerPacket;
          bufs.mBuffers->mData = buffer.get();

          OSStatus err = AudioConverterFillComplexBuffer(
              rec_converter_, OnConverterProcStatic, this, &samples, &bufs,
              NULL);

          sorac::AudioFrame frame;
          frame.pcm = std::move(buffer);
          frame.samples = samples;
          frame.sample_rate = RECORDING_SAMPLE_RATE;
          frame.channels = RECORDING_CHANNELS;
          if (timestamp == std::nullopt) {
            timestamp = sorac::get_current_time();
          } else {
            *timestamp += std::chrono::milliseconds(RECORDING_FRAME_TIME_MS);
          }
          frame.timestamp = *timestamp;
          callback_(frame);
        }
      }));

      if (OSStatus err = AudioDeviceStart(rec_device_id_, rec_ioproc_id_);
          err != noErr) {
        fprintf(stderr, "AudioDeviceStart failed: %d\n", err);
        return -1;
      }
    }

    return 0;
  }
  void Stop() {
    if (rec_device_id_ == kAudioDeviceUnknown) {
      return;
    }

    quit_ = true;
    rec_buf_cond_.notify_all();
    if (th_) {
      th_->join();
      th_.reset();
    }
    if (rec_device_id_ != 0 && rec_ioproc_id_ != 0) {
      AudioDeviceStop(rec_device_id_, rec_ioproc_id_);
      AudioDeviceDestroyIOProcID(rec_device_id_, rec_ioproc_id_);
      rec_ioproc_id_ = 0;
    }
    if (rec_converter_ != 0) {
      AudioConverterDispose(rec_converter_);
      rec_converter_ = 0;
    }

    AudioObjectPropertyAddress pa = {kAudioDevicePropertyStreamFormat,
                                     kAudioDevicePropertyScopeInput, 0};
    if (OSStatus err = AudioObjectRemovePropertyListener(
            rec_device_id_, &pa, &OnListenerProcStatic, this);
        err != noErr) {
      fprintf(stderr,
              "AudioObjectRemovePropertyListener("
              "kAudioDevicePropertyStreamFormat) failed: %d\n",
              err);
    }

    pa.mSelector = kAudioDeviceProcessorOverload;
    if (OSStatus err = AudioObjectRemovePropertyListener(
            rec_device_id_, &pa, &OnListenerProcStatic, this);
        err != noErr) {
      fprintf(
          stderr,
          "AudioObjectRemovePropertyListener(kAudioDeviceProcessorOverload) "
          "failed: %d\n",
          err);
    }

    quit_ = false;
  }

 private:
  static OSStatus OnIOProcStatic(AudioDeviceID device,
                                 const AudioTimeStamp* now,
                                 const AudioBufferList* inputData,
                                 const AudioTimeStamp* inputTime,
                                 AudioBufferList* outputData,
                                 const AudioTimeStamp* outputTime,
                                 void* clientData) {
    return static_cast<MacosRecorder*>(clientData)
        ->OnIOProc(inputData, inputTime);
  }
  OSStatus OnIOProc(const AudioBufferList* rec_data,
                    const AudioTimeStamp* rec_time) {
    std::lock_guard<std::mutex> lock(rec_buf_mutex_);
    float* p = (float*)rec_data->mBuffers->mData;
    int samples =
        rec_data->mBuffers->mDataByteSize / rec_stream_format_.mBytesPerPacket;
    int size = samples * rec_stream_format_.mChannelsPerFrame;
    for (int i = 0; i < size; i++) {
      rec_buf_.push_back(p[i]);
    }
    // printf("write buffer: size=%d max=%d\n", (int)size,
    //        (int)rec_buf_.size());
    rec_buf_cond_.notify_all();

    return noErr;
  }

  static OSStatus OnListenerProcStatic(
      AudioObjectID objectId,
      UInt32 numberAddresses,
      const AudioObjectPropertyAddress addresses[],
      void* clientData) {
    return static_cast<MacosRecorder*>(clientData)
        ->OnListenerProc(objectId, numberAddresses, addresses);
  }
  OSStatus OnListenerProc(AudioObjectID object_id,
                          UInt32 addresses_count,
                          const AudioObjectPropertyAddress addresses[]) {
    for (UInt32 i = 0; i < addresses_count; i++) {
      if (addresses[i].mSelector == kAudioDevicePropertyStreamFormat) {
        printf("kAudioDevicePropertyStreamFormat\n");
      } else if (addresses[i].mSelector == kAudioDeviceProcessorOverload) {
        printf("kAudioDeviceProcessorOverload\n");
      }
    }
    return noErr;
  }

  static OSStatus OnConverterProcStatic(
      AudioConverterRef,
      UInt32* numberDataPackets,
      AudioBufferList* data,
      AudioStreamPacketDescription** /*dataPacketDescription*/,
      void* userData) {
    return static_cast<MacosRecorder*>(userData)->OnConverterProc(
        numberDataPackets, data);
  }
  OSStatus OnConverterProc(UInt32* samples, AudioBufferList* data) {
    std::unique_lock<std::mutex> lock(rec_buf_mutex_);
    size_t size = *samples * rec_stream_format_.mChannelsPerFrame;
    size_t byte_size = *samples * rec_stream_format_.mBytesPerPacket;
    rec_buf_cond_.wait(
        lock, [this, size] { return rec_buf_.size() >= size || quit_; });
    if (quit_) {
      return -1;
    }
    // printf("read buffer: size=%d max=%d\n", (int)size, (int)rec_buf_.size());
    std::memcpy(data->mBuffers->mData, rec_buf_.data(), byte_size);
    rec_buf_.erase(rec_buf_.begin(), rec_buf_.begin() + size);
    data->mBuffers->mDataByteSize = byte_size;

    return noErr;
  }

 private:
  std::function<void(const sorac::AudioFrame& frame)> callback_;
  std::shared_ptr<std::thread> th_;
  std::atomic<bool> quit_;

  AudioDeviceID rec_device_id_ = kAudioDeviceUnknown;
  AudioConverterRef rec_converter_;
  AudioStreamBasicDescription rec_stream_format_;
  AudioStreamBasicDescription rec_desired_format_;
  AudioDeviceIOProcID rec_ioproc_id_;
  std::vector<float> rec_buf_;
  std::mutex rec_buf_mutex_;
  std::condition_variable rec_buf_cond_;
};

}  // namespace sumomo

extern "C" {

SumomoRecorder* sumomo_macos_recorder_create() {
  return new sumomo::MacosRecorder();
}
}
