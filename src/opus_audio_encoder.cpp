#include "sorac/opus_audio_encoder.hpp"

#include <chrono>
#include <functional>
#include <vector>

// opus
#include <opus.h>

// plog
#include <plog/Log.h>

#include "sorac/current_time.hpp"

namespace sorac {

class OpusAudioEncoderImpl : public OpusAudioEncoder {
 public:
  OpusAudioEncoderImpl() {}
  ~OpusAudioEncoderImpl() override { Release(); }

  bool InitEncode(int sample_rate,
                  int channels,
                  int frame_duration_ms,
                  int bitrate_kbps) override {
    Release();
    sample_rate_ = sample_rate;
    channels_ = channels;
    frame_duration_ms_ = frame_duration_ms;
    bitrate_kbps_ = bitrate_kbps;

    int error = 0;
    // maxplaybackrate=48000;stereo=1;sprop-stereo=1;minptime=10;ptime=20;useinbandfec=1;usedtx=0
    encoder_ = opus_encoder_create(sample_rate_, channels_,
                                   OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK) {
      PLOG_ERROR << "Failed to create opus encoder";
      return false;
    }
    int r = opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(1));
    if (r != OPUS_OK) {
      PLOG_ERROR << "Failed to OPUS_SET_INBAND_FEC";
      return false;
    }
    r = opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate_kbps_ * 1000));
    if (r != OPUS_OK) {
      PLOG_ERROR << "Failed to OPUS_SET_BITRATE";
      return false;
    }
    return true;
  }
  void Release() override {
    if (encoder_ != nullptr) {
      opus_encoder_destroy(encoder_);
      encoder_ = nullptr;
    }
  }

  void Encode(const AudioFrame& frame) override {
    EncodedAudio encoded;
    encoded.size = 0;
    encoded.cap = 1024;
    encoded.buf.reset(new uint8_t[encoded.cap]);
    //if (timestamp_.count() == 0) {
    //  timestamp_ = frame.timestamp;
    //}
    timestamp_ = frame.timestamp;
    encoded.timestamp = frame.timestamp;

    if (frame.sample_rate != sample_rate_) {
      // TODO(melpon): リサンプリングが必要
      PLOG_WARNING << "Needs resampling: " << frame.sample_rate << " Hz to "
                   << sample_rate_ << " Hz";
      return;
    }

    for (int i = 0; i < frame.samples; i++) {
      pcm_buf_.push_back(frame.pcm[i]);
      if (frame.channels == 1 && channels_ == 2) {
        // 入力はモノラルだけど、エンコーダに渡すのはステレオなので同じデータを詰めておく
        pcm_buf_.push_back(frame.pcm[i]);
      }

      // 20ms ごとにエンコードする
      // 16000 [Hz] * 2 [channels] * (20 [ms] / 1000 [ms])
      if (pcm_buf_.size() ==
          sample_rate_ * channels_ * frame_duration_ms_ / 1000) {
        while (true) {
          int n = opus_encode_float(
              encoder_, pcm_buf_.data(), pcm_buf_.size() / channels_,
              encoded.buf.get() + encoded.size, encoded.cap - encoded.size);
          if (n >= 0) {
            // 無事エンコードできた
            encoded.size += n;
            encoded.timestamp = timestamp_;
            encoded_buf_.push_back(std::move(encoded));
            encoded.size = 0;
            encoded.cap = 1024;
            encoded.buf.reset(new uint8_t[encoded.cap]);
            break;
          }
          // バッファが足りないので増やす
          if (n == OPUS_BUFFER_TOO_SMALL) {
            encoded.cap *= 2;
            encoded.buf.reset(new uint8_t[encoded.cap]);
          }
          // 何かエラーが起きた
          PLOG_ERROR << "Failed to opus_encode_float: result=" << n;
          return;
        }
        // PLOG_DEBUG << "encode frame: samples=" << pcm_buf_.size() << " encoded=" << encoded.size << " [0]=" << pcm_buf_[0] << " [1]=" << pcm_buf_[1];
        pcm_buf_.clear();
        timestamp_ += std::chrono::milliseconds(frame_duration_ms_);
      }
    }

    auto now = get_current_time();
    // 100ミリ秒以上経過したやつから送っていく
    auto end = std::find_if(
        encoded_buf_.begin(), encoded_buf_.end(), [now](const EncodedAudio& a) {
          return (now - a.timestamp) < std::chrono::milliseconds(100);
        });
    for (auto it = encoded_buf_.begin(); it != end; ++it) {
      callback_(*it);
    }
    encoded_buf_.erase(encoded_buf_.begin(), end);
  }

  void SetEncodeCallback(
      std::function<void(const EncodedAudio&)> callback) override {
    callback_ = callback;
  }

 private:
  int sample_rate_;
  int channels_;
  int frame_duration_ms_;
  int bitrate_kbps_;

  OpusEncoder* encoder_ = nullptr;
  std::function<void(const EncodedAudio&)> callback_;
  std::vector<float> pcm_buf_;
  std::chrono::microseconds timestamp_;
  std::vector<EncodedAudio> encoded_buf_;
};

std::shared_ptr<OpusAudioEncoder> CreateOpusAudioEncoder() {
  return std::make_shared<OpusAudioEncoderImpl>();
}

}  // namespace sorac
