#include "sorac/simulcast_encoder_adapter.hpp"

#include <string.h>
#include <atomic>
#include <exception>

// plog
#include <plog/Log.h>

#include "sorac/bitrate.hpp"

namespace sorac {

struct SimulcastFormat {
  int width;
  int height;
  Bps max_bitrate;
};
// この値は
// https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/video/config/simulcast.cc;l=76-103;drc=8e78783dc1f7007bad46d657c9f332614e240fd8
// から持ってきている
const SimulcastFormat kSimulcastFormats[] = {
    // clang-format off
    {1920, 1080, Kbps(5000)},
    {1280, 720, Kbps(2500)},
    {960, 540, Kbps(1200)},
    {640, 360, Kbps(700)},
    {480, 270, Kbps(450)},
    {320, 180, Kbps(200)},
    {0, 0, Kbps(0)},
    // clang-format on
};
static Bps GetMaxBitrate(int width, int height) {
  int n;
  for (n = 0; n < sizeof(kSimulcastFormats) / sizeof(SimulcastFormat); n++) {
    const auto& fmt = kSimulcastFormats[n];
    if (width * height >= fmt.width * fmt.height) {
      break;
    }
  }
  if (n == 0) {
    return kSimulcastFormats[n].max_bitrate;
  }
  // kSimulcastFormats[n] と kSimulcastFormats[n-1] の間で max_bitrate を線形補間する
  const auto& a = kSimulcastFormats[n];
  const auto& b = kSimulcastFormats[n - 1];
  double t = (double)(width * height - a.width * a.height) /
             (b.width * b.height - a.width * a.height);
  return Bps((b.max_bitrate.count() - a.max_bitrate.count()) * t +
             a.max_bitrate.count());
}

class SimulcastEncoderAdapter : public VideoEncoder {
 public:
  SimulcastEncoderAdapter(
      const soracp::RtpParameters& params,
      std::function<std::shared_ptr<VideoEncoder>(std::string)> create_encoder)
      : create_encoder_(create_encoder) {
    if (params.encodings.empty()) {
      encoders_.resize(1);
      encoders_[0].encoding.active = true;
      encoders_[0].codec = params.codecs[0];
      simulcast_ = false;
    } else {
      for (const auto& encoding : params.encodings) {
        auto it = std::find_if(params.rids.begin(), params.rids.end(),
                               [&encoding](const soracp::RidDescription& rd) {
                                 return rd.rid == encoding.rid;
                               });
        if (it == params.rids.end()) {
          PLOG_ERROR << "Rid not found: rid=" << encoding.rid;
          continue;
        }
        if (!it->has_payload_type()) {
          encoders_.push_back({nullptr, encoding, params.codecs[0]});
        } else {
          auto it2 =
              std::find_if(params.codecs.begin(), params.codecs.end(),
                           [it](const soracp::RtpCodecParameters& codec) {
                             return codec.payload_type == it->payload_type;
                           });
          if (it2 == params.codecs.end()) {
            PLOG_ERROR << "Codec not found: payload_type=" << it->payload_type;
            continue;
          }
          encoders_.push_back({nullptr, encoding, *it2});
        }
      }
      simulcast_ = true;
    }
  }
  ~SimulcastEncoderAdapter() override { Release(); }

  void ForceIntraNextFrame() override {
    for (auto& e : encoders_) {
      if (e.encoder != nullptr) {
        e.encoder->ForceIntraNextFrame();
      }
    }
  }

  bool InitEncode(const Settings& settings) override {
    Release();

    PLOG_INFO << "InitEncode: width=" << settings.width
              << " height=" << settings.height
              << " bitrate=" << settings.bitrate.count();
    // 各サイズの最大ビットレートを計算して、その割合でビットレートを分配する
    Bps sum_bitrate;
    for (const auto& e : encoders_) {
      const auto& p = e.encoding;
      if (!p.active) {
        continue;
      }
      int width = settings.width;
      int height = settings.height;
      if (p.has_scale_resolution_down_by()) {
        width = (int)(settings.width / p.scale_resolution_down_by);
        height = (int)(settings.height / p.scale_resolution_down_by);
      }
      sum_bitrate += GetMaxBitrate(width, height);
    }

    for (auto& e : encoders_) {
      if (!e.encoding.active) {
        continue;
      }
      Settings s = settings;
      if (e.encoding.has_scale_resolution_down_by()) {
        s.width = (int)(settings.width / e.encoding.scale_resolution_down_by);
        s.height = (int)(settings.height / e.encoding.scale_resolution_down_by);
      }
      double rate = (double)GetMaxBitrate(s.width, s.height).count() /
                    sum_bitrate.count();
      s.bitrate = Bps((int64_t)(settings.bitrate.count() * rate));
      e.encoder = create_encoder_(e.codec.name);
      PLOG_INFO << "InitEncode(Layerd): rid=" << e.encoding.rid
                << ", codec=" << e.codec.name << ", width=" << s.width
                << ", height=" << s.height << ", bitrate=" << s.bitrate.count();
      if (!e.encoder->InitEncode(s)) {
        return false;
      }
      e.settings = s;
    }

    return true;
  }

  void SetEncodeCallback(
      std::function<void(const EncodedImage&)> callback) override {
    for (auto& e : encoders_) {
      if (e.encoder != nullptr) {
        std::optional<std::string> rid;
        if (simulcast_) {
          rid = e.encoding.rid;
        }
        e.encoder->SetEncodeCallback(
            [rid, callback](const sorac::EncodedImage& image) {
              sorac::EncodedImage img = image;
              img.rid = rid;
              callback(img);
            });
      }
    }
  }

  void Encode(const VideoFrame& frame) override {
    // 適切なエンコーダーに送る
    if (!simulcast_) {
      // 非サイマルキャストなのに rid が設定されてる
      if (frame.rid != std::nullopt) {
        return;
      }
      encoders_[0].encoder->Encode(frame);
    } else {
      // サイマルキャストなのに rid が設定されてない
      if (frame.rid == std::nullopt) {
        return;
      }
      for (auto& e : encoders_) {
        if (e.encoder != nullptr) {
          if (e.encoding.rid == *frame.rid) {
            e.encoder->Encode(frame);
            break;
          }
        }
      }
    }
  }

  void Release() override {
    for (auto& e : encoders_) {
      if (e.encoder != nullptr) {
        e.encoder->Release();
        e.encoder = nullptr;
      }
    }
  }

 private:
  struct Encoder {
    std::shared_ptr<VideoEncoder> encoder;
    soracp::RtpEncodingParameters encoding;
    soracp::RtpCodecParameters codec;
    Settings settings;
  };
  std::vector<Encoder> encoders_;
  bool simulcast_;
  std::function<std::shared_ptr<VideoEncoder>(std::string)> create_encoder_;
};

std::shared_ptr<VideoEncoder> CreateSimulcastEncoderAdapter(
    const soracp::RtpParameters& params,
    std::function<std::shared_ptr<VideoEncoder>(std::string)> create_encoder) {
  return std::make_shared<SimulcastEncoderAdapter>(params, create_encoder);
}

}  // namespace sorac
