#include "sorac/signaling.hpp"

#include <random>

// libdatachannel
#include <rtc/rtc.hpp>

// nlohmann::json
#include <nlohmann/json.hpp>

// plog
#include <plog/Log.h>

#include "sorac/current_time.hpp"
#include "sorac/open_h264_video_encoder.hpp"
#include "sorac/opus_audio_encoder.hpp"

#if defined(__APPLE__)
#include "sorac/vt_h26x_video_encoder.hpp"
#endif

// https://github.com/paullouisageneau/libdatachannel/issues/990
namespace rtc {
using ::operator<<;
}

namespace sorac {

static const int ENCODING_SAMPLE_RATE = 48000;
static const int ENCODING_CHANNELS = 2;
static const int ENCODING_FRAME_DURATION_MS = 20;
static const int ENCODING_BITRATE_KBPS = 128;

struct Track {
  std::shared_ptr<rtc::Track> track;
  std::shared_ptr<rtc::RtcpSrReporter> sender;
};

struct Client {
  std::shared_ptr<rtc::PeerConnection> pc;

  std::shared_ptr<Track> video;
  std::shared_ptr<VideoEncoder> video_encoder;

  std::shared_ptr<Track> audio;
  std::shared_ptr<OpusAudioEncoder> opus_encoder;

  std::map<std::string, std::shared_ptr<rtc::DataChannel>> dcs;
};

static std::string generate_random_string(int length, std::string pattern) {
  if (pattern.size() == 0) {
    return "";
  }

  std::random_device random;
  // % を計算する時にマイナス値があると危険なので unsigned 型であることを保証する
  typedef std::make_unsigned<std::random_device::result_type>::type
      unsigned_type;
  std::string r;
  for (int i = 0; i < length; i++) {
    r += pattern[(unsigned_type)random() % pattern.size()];
  }
  return r;
}

static std::string generate_random_string(int length) {
  return generate_random_string(
      length, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
}

static std::vector<std::string> split_with(const std::string& str,
                                           const std::string& token) {
  int sp = 0;
  std::vector<std::string> lines;
  while (true) {
    auto ep = str.find(token, sp);
    if (ep == std::string::npos) {
      if (str.size() - sp > 0) {
        lines.push_back(str.substr(sp));
      }
      break;
    }
    lines.push_back(str.substr(sp, ep - sp));
    sp = ep + token.size();
  }
  return lines;
}

static bool starts_with(const std::string& str, const std::string& s) {
  return str.substr(0, s.size()) == s;
}

class SignalingImpl : public Signaling {
 public:
  SignalingImpl(const soracp::SignalingConfig& config) : config_(config) {}

  void Connect(const soracp::SoraConnectConfig& sora_config) override {
    sora_config_ = sora_config;

    // TODO(melpon): Proxy 対応
    rtc::WebSocket::Configuration ws_config;
    if (!config_.ca_certificate.empty()) {
      ws_config.caCertificatePemFile = config_.ca_certificate;
    }
    ws_ = std::make_shared<rtc::WebSocket>(ws_config);
    ws_->onOpen([this]() {
      PLOG_DEBUG << "onOpen";
      OnOpen();
    });
    ws_->onError([this](std::string s) {
      PLOG_DEBUG << "WebSocket error: " << s;
      OnError(s);
    });
    ws_->onClosed([this]() {
      PLOG_DEBUG << "WebSocket closed";
      OnClosed();
    });
    ws_->onMessage([this](rtc::message_variant data) { OnMessage(data); });
    // TODO(melpon): リダイレクト、複数URL同時接続+接続順序ランダム化、あたりを実装する
    ws_->open(config_.signaling_url_candidates[0]);
  }

  void SendVideoFrame(const VideoFrame& frame) override {
    client_.video_encoder->Encode(frame);
  }

  void SendAudioFrame(const AudioFrame& frame) override {
    client_.opus_encoder->Encode(frame);
  }

  // TODO(melpon): GetCandidateSignalingURLs, GetSelectedSignalingURL, GetConnectedSignalingURL あたりを実装する

  void SetOnTrack(
      std::function<void(std::shared_ptr<rtc::Track>)> on_track) override {
    on_track_ = on_track;
  }

  void SetOnDataChannel(std::function<void(std::shared_ptr<rtc::DataChannel>)>
                            on_data_channel) override {
    on_data_channel_ = on_data_channel;
  }

 private:
  void OnMessage(rtc::message_variant data) {
    if (!std::holds_alternative<std::string>(data)) {
      return;
    }
    std::string message = std::get<std::string>(data);
    PLOG_DEBUG << "onMessage: " << message;

    nlohmann::json js = nlohmann::json::parse(message);

    if (js["type"] == "offer") {
      rtc::Configuration config;
      for (auto& ice_server : js["config"]["iceServers"]) {
        rtc::IceServer s(ice_server["urls"][0].get<std::string>());
        s.username = ice_server["username"].get<std::string>();
        s.password = ice_server["credential"].get<std::string>();
        config.iceServers.push_back(s);
      }
      client_.pc = std::make_shared<rtc::PeerConnection>(config);
      client_.pc->onLocalDescription([this](rtc::Description desc) {
        auto sdp = desc.generateSdp();
        PLOG_DEBUG << "answer sdp:" << sdp;
        nlohmann::json js = {
            {"type", desc.typeString()},
            {"sdp", sdp},
        };
        PLOG_DEBUG << "onLocalDescription: send=" << js.dump();
        ws_->send(js.dump());
      });
      client_.pc->onLocalCandidate([this](rtc::Candidate candidate) {
        nlohmann::json js = {
            {"type", "candidate"},
            {"candidate", candidate.candidate()},
        };
        PLOG_DEBUG << "onLocalCandidate: send=" << js.dump();
        ws_->send(js.dump());
      });
      client_.pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
        PLOG_DEBUG << "onDataChannel: label=" << dc->label();
        if (dc->label()[0] == '#') {
          // ユーザー定義ラベルなのでコールバックを呼ぶ
          if (on_data_channel_) {
            on_data_channel_(dc);
          }
          return;
        }

        client_.dcs[dc->label()] = dc;
      });
      client_.pc->onGatheringStateChange(
          [](rtc::PeerConnection::GatheringState state) {
            PLOG_DEBUG << "onGatheringStateChange: " << state;
          });
      client_.pc->onIceStateChange([](rtc::PeerConnection::IceState state) {
        PLOG_DEBUG << "onIceStateChange: " << state;
      });
      client_.pc->onSignalingStateChange(
          [](rtc::PeerConnection::SignalingState state) {
            PLOG_DEBUG << "onSignalingStateChange: " << state;
          });
      client_.pc->onStateChange([](rtc::PeerConnection::State state) {
        PLOG_DEBUG << "onStateChange: " << state;
      });
      client_.pc->onTrack([](std::shared_ptr<rtc::Track> track) {
        PLOG_DEBUG << "onTrack: " << track->mid();
      });

      auto sdp = js["sdp"].get<std::string>();

      PLOG_DEBUG << "---------- offer sdp ----------";
      PLOG_DEBUG << sdp;
      PLOG_DEBUG << "-------------------------------";
      auto lines = split_with(sdp, "\r\n");
      auto cname = "cname-" + generate_random_string(24);
      auto msid = "msid-" + generate_random_string(24);
      auto track_id = "trackid-" + generate_random_string(24);
      // video
      {
        int ssrc = 1;
        // m=video から他の m= が出てくるまでの間のデータを取得する
        std::vector<std::string> video_lines;
        {
          auto it = std::find_if(
              lines.begin(), lines.end(),
              [](const std::string& s) { return starts_with(s, "m=video"); });
          if (it != lines.end()) {
            auto it2 = std::find_if(
                it + 1, lines.end(),
                [](const std::string& s) { return starts_with(s, "m="); });
            video_lines.assign(it, it2);
          }
        }
        // mid, payload_type, codec
        std::string mid;
        int payload_type;
        std::string codec;
        {
          auto get_value =
              [&video_lines](const std::string& search) -> std::string {
            auto it = std::find_if(video_lines.begin(), video_lines.end(),
                                   [&search](const std::string& s) {
                                     return starts_with(s, search);
                                   });
            if (it == video_lines.end()) {
              return "";
            }
            return it->substr(search.size());
          };
          mid = get_value("a=mid:");
          PLOG_DEBUG << "mid=" << mid;
          auto xs = split_with(get_value("a=msid:"), " ");
          auto rtpmap = get_value("a=rtpmap:");
          auto ys = split_with(rtpmap, " ");
          payload_type = std::stoi(ys[0]);
          codec = split_with(ys[1], "/")[0];
          PLOG_DEBUG << "payload_type=" << payload_type << ", codec=" << codec;
        }

        std::shared_ptr<rtc::Track> track;
        std::shared_ptr<rtc::RtcpSrReporter> sr_reporter;
        if (codec == "H264") {
          auto video = rtc::Description::Video(mid);
          video.addH264Codec(payload_type);
          video.addSSRC(ssrc, cname, msid, track_id);
          track = client_.pc->addTrack(video);
          auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(
              ssrc, cname, payload_type,
              rtc::H264RtpPacketizer::defaultClockRate);
          auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
              rtc::NalUnit::Separator::LongStartSequence, rtp_config);
          sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
          packetizer->addToChain(sr_reporter);
          auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
          packetizer->addToChain(nack_responder);
          auto pli_handler = std::make_shared<rtc::PliHandler>([this]() {
            PLOG_DEBUG << "PLI or FIR";
            client_.video_encoder->ForceIntraNextFrame();
          });
          packetizer->addToChain(pli_handler);
          track->setMediaHandler(packetizer);
        } else {
          auto video = rtc::Description::Video(mid);
          video.addH265Codec(payload_type);
          video.addSSRC(ssrc, cname, msid, track_id);
          track = client_.pc->addTrack(video);
          auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(
              ssrc, cname, payload_type,
              rtc::H265RtpPacketizer::defaultClockRate);
          auto packetizer = std::make_shared<rtc::H265RtpPacketizer>(
              rtc::NalUnit::Separator::LongStartSequence, rtp_config);
          sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
          packetizer->addToChain(sr_reporter);
          auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
          packetizer->addToChain(nack_responder);
          auto pli_handler = std::make_shared<rtc::PliHandler>([this]() {
            PLOG_DEBUG << "PLI or FIR";
            client_.video_encoder->ForceIntraNextFrame();
          });
          packetizer->addToChain(pli_handler);
          track->setMediaHandler(packetizer);
        }

        track->onOpen([this, wtrack = std::weak_ptr<rtc::Track>(track),
                       codec]() {
          PLOG_DEBUG << "Video Track Opened";
          auto track = wtrack.lock();
          if (track == nullptr) {
            return;
          }

          if (codec == "H264") {
            if (config_.h264_encoder_type ==
                soracp::H264_ENCODER_TYPE_OPEN_H264) {
              client_.video_encoder =
                  CreateOpenH264VideoEncoder(config_.openh264);
            } else if (config_.h264_encoder_type ==
                       soracp::H264_ENCODER_TYPE_VIDEO_TOOLBOX) {
#if defined(__APPLE__)
              client_.video_encoder =
                  CreateVTH26xVideoEncoder(VTH26xVideoEncoderType::kH264);
#else
              PLOG_ERROR << "VideoToolbox is only supported on macOS/iOS";
              return;
#endif
            } else {
              PLOG_ERROR << "Unknown H264EncoderType";
              return;
            }
          } else if (codec == "H265") {
            if (config_.h265_encoder_type ==
                soracp::H265_ENCODER_TYPE_VIDEO_TOOLBOX) {
#if defined(__APPLE__)
              client_.video_encoder =
                  CreateVTH26xVideoEncoder(VTH26xVideoEncoderType::kH265);
#else
              PLOG_ERROR << "VideoToolbox is only supported on macOS/iOS";
              return;
#endif
            } else {
              PLOG_ERROR << "Unknown H265EncoderType";
              return;
            }
          }

          if (!client_.video_encoder->InitEncode()) {
            PLOG_ERROR << "Failed to InitEncode()";
            return;
          }
          client_.video_encoder->SetEncodeCallback(
              [this, initial_timestamp =
                         get_current_time()](const EncodedImage& image) {
                auto rtp_config = client_.video->sender->rtpConfig;
                auto elapsed_seconds =
                    double((image.timestamp - initial_timestamp).count()) /
                    (1000 * 1000);
                rtp_config->timestamp =
                    rtp_config->startTimestamp +
                    rtp_config->secondsToTimestamp(elapsed_seconds);
                auto report_elapsed_timestamp =
                    rtp_config->timestamp -
                    client_.video->sender->lastReportedTimestamp();
                if (rtp_config->timestampToSeconds(report_elapsed_timestamp) >
                    0.2) {
                  client_.video->sender->setNeedsToReport();
                }
                std::vector<std::byte> buf(
                    (std::byte*)image.buf.get(),
                    (std::byte*)image.buf.get() + image.size);
                client_.video->track->send(buf);
              });
          on_track_(track);
        });
        client_.video = std::make_shared<Track>();
        client_.video->track = track;
        client_.video->sender = sr_reporter;
      }
      // audio
      {
        int ssrc = 2;
        // m=audio から他の m= が出てくるまでの間のデータを取得する
        std::vector<std::string> audio_lines;
        {
          auto it = std::find_if(
              lines.begin(), lines.end(),
              [](const std::string& s) { return starts_with(s, "m=audio"); });
          if (it != lines.end()) {
            auto it2 = std::find_if(
                it + 1, lines.end(),
                [](const std::string& s) { return starts_with(s, "m="); });
            audio_lines.assign(it, it2);
          }
        }
        // mid, payload_type
        std::string mid;
        int payload_type;
        {
          auto get_value =
              [&audio_lines](const std::string& search) -> std::string {
            auto it = std::find_if(audio_lines.begin(), audio_lines.end(),
                                   [&search](const std::string& s) {
                                     return starts_with(s, search);
                                   });
            if (it == audio_lines.end()) {
              return "";
            }
            return it->substr(search.size());
          };
          mid = get_value("a=mid:");
          PLOG_DEBUG << "mid=" << mid;
          auto xs = split_with(get_value("a=msid:"), " ");
          auto rtpmap = get_value("a=rtpmap:");
          payload_type = std::stoi(split_with(rtpmap, " ")[0]);
          PLOG_DEBUG << "payload_type=" << payload_type;
        }

        auto audio = rtc::Description::Audio(mid);
        audio.addOpusCodec(payload_type);
        audio.addSSRC(ssrc, cname, msid, track_id);
        auto track = client_.pc->addTrack(audio);
        auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(
            ssrc, cname, payload_type,
            rtc::OpusRtpPacketizer::DefaultClockRate);
        auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtp_config);
        auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
        packetizer->addToChain(sr_reporter);
        auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
        packetizer->addToChain(nack_responder);
        track->setMediaHandler(packetizer);
        track->onOpen([this, wtrack = std::weak_ptr<rtc::Track>(track)]() {
          PLOG_DEBUG << "Audio Track Opened";
          auto track = wtrack.lock();
          if (track == nullptr) {
            return;
          }

          client_.opus_encoder = CreateOpusAudioEncoder();
          if (!client_.opus_encoder->InitEncode(
                  ENCODING_SAMPLE_RATE, ENCODING_CHANNELS,
                  ENCODING_FRAME_DURATION_MS, ENCODING_BITRATE_KBPS)) {
            PLOG_ERROR << "Failed to InitEncode()";
            return;
          }
          client_.opus_encoder->SetEncodeCallback(
              [this, initial_timestamp =
                         get_current_time()](const EncodedAudio& audio) {
                auto rtp_config = client_.audio->sender->rtpConfig;
                auto elapsed_seconds =
                    double((audio.timestamp - initial_timestamp).count()) /
                    (1000 * 1000);
                rtp_config->timestamp =
                    rtp_config->startTimestamp +
                    rtp_config->secondsToTimestamp(elapsed_seconds);
                auto report_elapsed_timestamp =
                    rtp_config->timestamp -
                    client_.audio->sender->lastReportedTimestamp();
                if (rtp_config->timestampToSeconds(report_elapsed_timestamp) >
                    5) {
                  client_.audio->sender->setNeedsToReport();
                }
                std::vector<std::byte> buf(
                    (std::byte*)audio.buf.get(),
                    (std::byte*)audio.buf.get() + audio.size);
                client_.audio->track->send(buf);
              });
          on_track_(track);
        });
        client_.audio = std::make_shared<Track>();
        client_.audio->track = track;
        client_.audio->sender = sr_reporter;
      }

      client_.pc->setRemoteDescription(rtc::Description(sdp, "offer"));
    } else if (js["type"] == "ping") {
      nlohmann::json js = {{"type", "pong"}};
      PLOG_DEBUG << "pong: " << js.dump();
      ws_->send(js.dump());
    }
  }

  void OnOpen() {
    const auto& sc = sora_config_;
    nlohmann::json js = {
        {"type", "connect"},
        {"role", sc.role},
        {"channel_id", sc.channel_id},
    };
    auto set_if = [](nlohmann::json& js, const std::string& key, auto value,
                     bool cond) {
      if (cond) {
        js[key] = value;
      }
    };
    auto set_string = [](nlohmann::json& js, const std::string& key,
                         const std::string& value) {
      if (!value.empty()) {
        js[key] = value;
      }
    };
    auto set_optional_bool = [](nlohmann::json& js, const std::string& key,
                                soracp::OptionalBool value) {
      if (value != soracp::OPTIONAL_BOOL_NONE) {
        js[key] = value == soracp::OPTIONAL_BOOL_TRUE ? true : false;
      }
    };
    auto set_json = [](nlohmann::json& js, const std::string& key,
                       const std::string& value) {
      if (!value.empty()) {
        js[key] = nlohmann::json::parse(value);
      }
    };

    set_string(js, "client_id", sc.client_id);
    set_string(js, "bundle_id", sc.bundle_id);
    set_optional_bool(js, "multistream", sc.multistream);
    set_optional_bool(js, "simulcast", sc.simulcast);
    set_string(js, "simulcast_rid", sc.simulcast_rid);
    set_optional_bool(js, "spotlight", sc.spotlight);
    set_if(js, "spotlight_number", sc.spotlight_number,
           sc.spotlight_number > 0);
    set_string(js, "spotlight_focus_rid", sc.spotlight_focus_rid);
    set_string(js, "spotlight_unfocus_rid", sc.spotlight_unfocus_rid);
    set_json(js, "metadata", sc.metadata);
    set_json(js, "signaling_notify_metadata", sc.signaling_notify_metadata);

    if (!sc.video) {
      // video: false の場合はそのまま設定
      js["video"] = false;
    } else {
      // video: true の場合は、ちゃんとオプションを設定する
      set_string(js["video"], "codec_type", sc.video_codec_type);
      set_if(js["video"], "bit_rate", sc.video_bit_rate,
             sc.video_bit_rate != 0);
      set_string(js["video"], "vp9_params", sc.video_vp9_params);
      set_string(js["video"], "av1_params", sc.video_av1_params);
      set_string(js["video"], "h264_params", sc.video_h264_params);

      // オプションの設定が行われてなければ単に true を設定
      if (js["video"].is_null()) {
        js["video"] = true;
      }
    }

    if (!sc.audio) {
      js["audio"] = false;
    } else {
      set_string(js["audio"], "codec_type", sc.audio_codec_type);
      set_if(js["audio"], "bit_rate", sc.audio_bit_rate,
             sc.audio_bit_rate != 0);

      // オプションの設定が行われてなければ単に true を設定
      if (js["audio"].is_null()) {
        js["audio"] = true;
      }
    }

    set_string(js, "audio_streaming_language_code",
               sc.audio_streaming_language_code);
    set_optional_bool(js, "data_channel_signaling", sc.data_channel_signaling);
    set_optional_bool(js, "ignore_disconnect_websocket",
                      sc.ignore_disconnect_websocket);

    for (const auto& d : sc.data_channels) {
      nlohmann::json dc;
      set_string(dc, "label", d.label);
      set_string(dc, "direction", d.direction);
      set_if(dc, "max_packet_life_time", d.max_packet_life_time,
             d.enable_max_packet_life_time);
      set_if(dc, "max_retransmits", d.max_retransmits,
             d.enable_max_retransmits);
      set_if(dc, "protocol", d.protocol, d.enable_protocol);
      set_optional_bool(dc, "compress", d.compress);
      js["data_channels"].push_back(dc);
    }

    if (sc.enable_forwarding_filter) {
      nlohmann::json obj;
      const auto& f = sc.forwarding_filter;
      obj["action"] = f.action;
      for (const auto& rules : f.rules) {
        nlohmann::json ar;
        for (const auto& r : rules.rules) {
          nlohmann::json rule;
          rule["field"] = r.field;
          rule["operator"] = r.op;
          for (const auto& v : r.values) {
            rule["values"].push_back(v);
          }
          ar.push_back(rule);
        }
        obj["rules"].push_back(ar);
      }
      js["forwarding_filter"] = obj;
    }

    PLOG_DEBUG << "connect: " << js.dump();
    ws_->send(js.dump());
  }

  void OnError(const std::string& s) {
    client_ = Client();
    ws_ = nullptr;
  }

  void OnClosed() {
    client_ = Client();
    ws_ = nullptr;
  }

 private:
  std::shared_ptr<rtc::WebSocket> ws_;
  Client client_;
  soracp::SignalingConfig config_;
  soracp::SoraConnectConfig sora_config_;
  std::function<void(std::shared_ptr<rtc::Track>)> on_track_;
  std::function<void(std::shared_ptr<rtc::DataChannel>)> on_data_channel_;
};

std::shared_ptr<Signaling> CreateSignaling(
    const soracp::SignalingConfig& config) {
  return std::make_shared<SignalingImpl>(config);
}

}  // namespace sorac
