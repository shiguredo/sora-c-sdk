#include "sorac/default_encoder_adapter.hpp"

#include <string.h>
#include <atomic>
#include <exception>

// plog
#include <plog/Log.h>

#include "sorac/bitrate.hpp"

namespace sorac {

// FPS の計測区間
static const std::chrono::seconds kFpsCalcInterval(2);

// 全てのエンコーダに適用するアダプタ。
//
// 機能ごとにアダプタを分けるのが面倒なので一緒にしてしまう。
// 今のところ、以下の機能がある。
// - エンコードする映像を 16 の倍数にアライメントする
// - FPS を計測して、指定した FPS を超えた場合はエンコードをスキップする
class DefaultEncoderAdapter : public VideoEncoder {
 public:
  DefaultEncoderAdapter(std::shared_ptr<VideoEncoder> encoder)
      : encoder_(encoder) {}
  ~DefaultEncoderAdapter() override { Release(); }

  void ForceIntraNextFrame() override { encoder_->ForceIntraNextFrame(); }

  bool InitEncode(const Settings& settings) override {
    Release();

    // 16の倍数にアライメントする
    settings_ = settings;
    settings_.width = settings.width / 16 * 16;
    settings_.height = settings.height / 16 * 16;
    if (settings.width != settings_.width ||
        settings.height != settings_.height) {
      PLOG_INFO << "InitEncode adjusted: width=" << settings_.width
                << " height=" << settings_.height;
    }
    return encoder_->InitEncode(settings_);
  }

  void SetEncodeCallback(
      std::function<void(const EncodedImage&)> callback) override {
    encoder_->SetEncodeCallback(callback);
  }

  void Encode(const VideoFrame& frame) override {
    // フレームレートによってはエンコードをスキップする
    auto now = std::chrono::steady_clock::now();
    if (!start_timestamp_) {
      start_timestamp_ = now;
    } else {
      auto from = std::max(now - kFpsCalcInterval, *start_timestamp_);
      // from 未満のフレームを削除する
      encode_timestamps_.erase(
          std::remove_if(encode_timestamps_.begin(), encode_timestamps_.end(),
                         [from](const auto& t) { return t < from; }),
          encode_timestamps_.end());
      auto fps =
          ((double)encode_timestamps_.size() * 1000000 /
           std::chrono::duration_cast<std::chrono::microseconds>(now - from)
               .count());
      if (fps > settings_.fps) {
        return;
      }
    }
    encode_timestamps_.push_back(now);

    VideoFrame frame2 = frame;
    if (frame2.i420_buffer != nullptr) {
      frame2.i420_buffer->width = settings_.width;
      frame2.i420_buffer->height = settings_.height;
    }
    if (frame2.nv12_buffer != nullptr) {
      frame2.nv12_buffer->width = settings_.width;
      frame2.nv12_buffer->height = settings_.height;
    }
    encoder_->Encode(frame2);
  }

  void Release() override { encoder_->Release(); }

 private:
  std::shared_ptr<VideoEncoder> encoder_;
  Settings settings_;
  std::vector<std::chrono::steady_clock::time_point> encode_timestamps_;
  std::optional<std::chrono::steady_clock::time_point> start_timestamp_;
};

std::shared_ptr<VideoEncoder> CreateDefaultEncoderAdapter(
    std::shared_ptr<VideoEncoder> encoder) {
  return std::make_shared<DefaultEncoderAdapter>(encoder);
}

}  // namespace sorac
