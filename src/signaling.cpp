#include "sorac/signaling.hpp"

#include <optional>
#include <random>
#include <vector>

// libdatachannel
#include <rtc/rtc.hpp>

// nlohmann::json
#include <nlohmann/json.hpp>

// plog
#include <plog/Log.h>

#include "sorac/current_time.hpp"
#include "sorac/open_h264_video_encoder.hpp"
#include "sorac/opus_audio_encoder.hpp"
#include "sorac/simulcast_encoder_adapter.hpp"
#include "sorac/simulcast_media_handler.hpp"
#include "sorac/version.hpp"

#if defined(__APPLE__)
#include "sorac/vt_h26x_video_encoder.hpp"
#endif

#include "sorac/bitrate.hpp"
#include "util.hpp"

namespace sorac {

static const int ENCODING_SAMPLE_RATE = 48000;
static const int ENCODING_CHANNELS = 2;
static const int ENCODING_FRAME_DURATION_MS = 20;
static const int ENCODING_BITRATE_KBPS = 128;

struct Track {
  std::shared_ptr<rtc::Track> track;
  std::map<std::optional<std::string>, std::shared_ptr<rtc::RtcpSrReporter>>
      senders;
  std::shared_ptr<SimulcastMediaHandler> simulcast_handler;
};

struct Client {
  std::shared_ptr<rtc::PeerConnection> pc;

  std::shared_ptr<Track> video;
  std::shared_ptr<VideoEncoder> video_encoder;
  std::optional<VideoEncoder::Settings> video_encoder_settings;

  std::shared_ptr<Track> audio;
  std::shared_ptr<OpusAudioEncoder> opus_encoder;

  nlohmann::json data_channel_metadata;
  std::map<std::string, std::shared_ptr<sorac::DataChannel>> dcs;
};

class SignalingImpl : public Signaling {
 public:
  SignalingImpl(const soracp::SignalingConfig& config) : config_(config) {}

  void Connect(const soracp::SoraConnectConfig& sora_config) override {
    sora_config_ = sora_config;

    // ランダムに並び替える
    auto urls = config_.signaling_url_candidates;
    {
      std::random_device seed_gen;
      std::mt19937 engine(seed_gen());
      std::shuffle(urls.begin(), urls.end(), engine);
    }

    for (const auto& url : urls) {
      // TODO(melpon): Proxy 対応

      rtc::WebSocket::Configuration ws_config;
      if (!config_.ca_certificate.empty()) {
        ws_config.caCertificatePemFile = config_.ca_certificate;
      }
      auto ws = std::make_shared<rtc::WebSocket>(ws_config);
      ws->onOpen([this, url, wws = std::weak_ptr<rtc::WebSocket>(ws)]() {
        PLOG_DEBUG << "onOpen: url=" << url;
        {
          std::unique_lock<std::mutex> lock(ws_mutex_, std::try_to_lock);
          if (!lock) {
            PLOG_DEBUG << "Maybe WebSocket is already connected";
            return;
          }
          {
            auto ws = wws.lock();
            if (ws_ != nullptr || ws == nullptr) {
              PLOG_DEBUG << "WebSocket is already connected";
              return;
            }
            ws_ = ws;
          }
          //for (auto& ws : connecting_wss_) {
          //  if (ws == ws_) {
          //    continue;
          //  }
          //  ws->close();
          //}
          connecting_wss_.clear();
        }
        OnOpen(false);
      });
      ws->onError([this](std::string s) {
        PLOG_DEBUG << "WebSocket error: " << s;
        OnError(s);
      });
      ws->onClosed([this]() {
        PLOG_DEBUG << "WebSocket closed";
        OnClosed();
      });
      ws->onMessage([this](rtc::message_variant data) { OnMessage(data); });

      {
        std::lock_guard<std::mutex> lock(ws_mutex_);
        connecting_wss_.push_back(ws);
      }
    }

    for (int i = 0; i < urls.size(); i++) {
      PLOG_DEBUG << "Connect to: " << urls[i];
      std::shared_ptr<rtc::WebSocket> ws;
      {
        std::lock_guard<std::mutex> lock(ws_mutex_);
        ws = connecting_wss_[i];
      }

      ws->open(urls[i]);
    }
  }

  void SendVideoFrame(const VideoFrame& frame) override {
    if (!client_.video_encoder_settings ||
        frame.base_width != client_.video_encoder_settings->width ||
        frame.base_height != client_.video_encoder_settings->height) {
      client_.video_encoder->Release();
      VideoEncoder::Settings settings;
      settings.width = frame.base_width;
      settings.height = frame.base_height;
      settings.bitrate = Kbps(config_.video_encoder_initial_bitrate_kbps);
      if (!client_.video_encoder->InitEncode(settings)) {
        PLOG_ERROR << "Failed to InitEncode()";
        return;
      }
      client_.video_encoder_settings = settings;
      client_.video_encoder->SetEncodeCallback([this, initial_timestamp =
                                                          get_current_time()](
                                                   const EncodedImage& image) {
        auto sender = client_.video->senders[image.rid];
        auto rtp_config = sender->rtpConfig;
        auto elapsed_seconds =
            double((image.timestamp - initial_timestamp).count()) /
            (1000 * 1000);
        rtp_config->timestamp = rtp_config->startTimestamp +
                                rtp_config->secondsToTimestamp(elapsed_seconds);
        auto report_elapsed_timestamp =
            rtp_config->timestamp - sender->lastReportedTimestamp();
        if (rtp_config->timestampToSeconds(report_elapsed_timestamp) > 0.2) {
          sender->setNeedsToReport();
        }
        std::vector<std::byte> buf((std::byte*)image.buf.get(),
                                   (std::byte*)image.buf.get() + image.size);
        client_.video->simulcast_handler->config()->rid = image.rid;
        client_.video->track->send(buf);
      });
    }
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

  void SetOnDataChannel(std::function<void(std::shared_ptr<sorac::DataChannel>)>
                            on_data_channel) override {
    on_data_channel_ = on_data_channel;
  }

  void SetOnNotify(std::function<void(const std::string&)> on_notify) override {
    on_notify_ = on_notify;
  }

  void SetOnPush(std::function<void(const std::string&)> on_push) override {
    on_push_ = on_push;
  }

  soracp::RtpParameters GetRtpParameters() const override {
    return rtp_params_;
  }

 private:
  void OnMessage(rtc::message_variant data) {
    if (!std::holds_alternative<std::string>(data)) {
      return;
    }
    std::string message = std::get<std::string>(data);
    PLOG_DEBUG << "onMessage: " << message;

    nlohmann::json js = nlohmann::json::parse(message);

    if (js["type"] == "redirect") {
      const std::string location = js["location"].get<std::string>();
      // location に繋ぎ直す
      GetWebSocket()->close();
      rtc::WebSocket::Configuration ws_config;
      if (!config_.ca_certificate.empty()) {
        ws_config.caCertificatePemFile = config_.ca_certificate;
      }
      auto ws = std::make_shared<rtc::WebSocket>(ws_config);
      ws->onOpen([this, ws, location]() {
        PLOG_DEBUG << "onOpen (redirected): url=" << location;
        OnOpen(true);
      });
      ws->onError([this](std::string s) {
        PLOG_DEBUG << "WebSocket error: " << s;
        OnError(s);
      });
      ws->onClosed([this]() {
        PLOG_DEBUG << "WebSocket closed";
        OnClosed();
      });
      ws->onMessage([this](rtc::message_variant data) { OnMessage(data); });
      ws->open(location);
      ws_ = ws;
    } else if (js["type"] == "offer") {
      rtc::Configuration config;
      for (auto& ice_server : js["config"]["iceServers"]) {
        rtc::IceServer s(ice_server["urls"][0].get<std::string>());
        s.username = ice_server["username"].get<std::string>();
        s.password = ice_server["credential"].get<std::string>();
        config.iceServers.push_back(s);
      }

      if (js["simulcast"].get<bool>()) {
        for (auto& enc : js["encodings"]) {
          soracp::RtpEncodingParameters p;
          p.rid = enc["rid"].get<std::string>();
          p.active = true;
          if (enc.contains("active")) {
            p.active = enc["active"].get<bool>();
          }
          if (enc.contains("scaleResolutionDownBy")) {
            p.set_scale_resolution_down_by(
                enc["scaleResolutionDownBy"].get<double>());
          }
          if (enc.contains("maxBitrate")) {
            p.set_max_bitrate_bps(enc["maxBitrate"].get<int>());
          }
          if (enc.contains("maxFramerate")) {
            p.set_max_framerate(enc["maxFramerate"].get<double>());
          }
          if (enc.contains("adaptivePtime")) {
            p.adaptive_ptime = enc["adaptivePtime"].get<bool>();
          }
          if (enc.contains("scalabilityMode")) {
            p.set_scalability_mode(enc["scalabilityMode"].get<std::string>());
          }
          rtp_params_.encodings.push_back(p);
        }
      }

      client_.data_channel_metadata = js["data_channels"];
      client_.pc = std::make_shared<rtc::PeerConnection>(config);
      client_.pc->onLocalDescription([this](rtc::Description desc) {
        auto sdp = desc.generateSdp();
        if (IsSimulcast()) {
          for (const auto& rd : rtp_params_.rids) {
            sdp += "a=rid:" + rd.rid + " send";
            if (rd.has_payload_type()) {
              sdp += " pt=" + std::to_string(rd.payload_type);
            }
            sdp += "\r\n";
          }
          sdp += "a=simulcast:send r0;r1;r2\r\n";
        }
        PLOG_DEBUG << "answer sdp:" << sdp;
        nlohmann::json js = {
            {"type", desc.typeString()},
            {"sdp", sdp},
        };
        PLOG_DEBUG << "onLocalDescription: send=" << js.dump();

        GetWebSocket()->send(js.dump());
      });
      client_.pc->onLocalCandidate([this](rtc::Candidate candidate) {
        nlohmann::json js = {
            {"type", "candidate"},
            {"candidate", candidate.candidate()},
        };
        PLOG_DEBUG << "onLocalCandidate: send=" << js.dump();

        GetWebSocket()->send(js.dump());
      });
      client_.pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> rdc) {
        auto label = rdc->label();
        PLOG_DEBUG << "onDataChannel: label=" << label;
        bool compress = false;
        for (const auto& d : client_.data_channel_metadata) {
          if (d["label"] == label) {
            compress = d["compress"].get<bool>();
            break;
          }
        }
        std::shared_ptr<DataChannel> dc = CreateDataChannel(rdc, compress);

        if (label[0] == '#') {
          // ユーザー定義ラベルなのでコールバックを呼ぶ
          if (on_data_channel_) {
            on_data_channel_(dc);
          }
          return;
        }

        auto wdc = std::weak_ptr<sorac::DataChannel>(dc);
        if (label == "stats") {
          dc->SetOnMessage([this, wdc](const uint8_t* buf, size_t size) {
            auto dc = wdc.lock();
            if (dc == nullptr) {
              return;
            }
            nlohmann::json js = nlohmann::json::parse(buf, buf + size);
            if (js["type"] == "stats-req") {
              nlohmann::json js = {{"type", "stats"},
                                   {"reports", nlohmann::json::array()}};
              PLOG_DEBUG << "stats: " << js.dump();
              std::string str = js.dump();
              dc->Send((const uint8_t*)str.data(), str.size());
            }
          });
        } else if (label == "notify") {
          dc->SetOnMessage([this, wdc](const uint8_t* buf, size_t size) {
            auto dc = wdc.lock();
            if (dc == nullptr) {
              return;
            }
            auto message = std::string((const char*)buf, size);
            PLOG_DEBUG << "onMessage: label=" << dc->GetLabel()
                       << ", message=" << message;

            if (on_notify_) {
              on_notify_(message);
            }
          });
        } else if (label == "push") {
          dc->SetOnMessage([this, wdc](const uint8_t* buf, size_t size) {
            auto dc = wdc.lock();
            if (dc == nullptr) {
              return;
            }
            auto message = std::string((const char*)buf, size);
            PLOG_DEBUG << "onMessage: label=" << dc->GetLabel()
                       << ", message=" << message;

            if (on_push_) {
              on_push_(message);
            }
          });
        }
        client_.dcs[label] = dc;
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
        for (const auto& line : video_lines) {
          if (auto s = std::string("a=mid:"); starts_with(line, s)) {
            auto mid = line.substr(s.size());
            PLOG_DEBUG << "mid=" << mid;
            rtp_params_.mid = mid;
          } else if (auto s = std::string("a=rtpmap:"); starts_with(line, s)) {
            auto rtpmap = line.substr(s.size());
            auto ys = split_with(rtpmap, " ");
            auto payload_type = std::stoi(ys[0]);
            auto codec = split_with(ys[1], "/")[0];
            if (codec == "H264" || codec == "H265") {
              PLOG_DEBUG << "payload_type=" << payload_type
                         << ", codec=" << codec;
              soracp::RtpCodecParameters cp;
              cp.payload_type = payload_type;
              cp.kind = "video";
              cp.name = codec;
              rtp_params_.codecs.push_back(cp);
            }
          }
        }
        // サイマルキャストの場合、拡張ヘッダーのどの ID を使えば良いか調べる
        if (IsSimulcast()) {
          auto it = std::find_if(
              video_lines.begin(), video_lines.end(), [](const std::string& s) {
                return starts_with(s, "a=extmap:") &&
                       s.find(
                           "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id") !=
                           std::string::npos;
              });
          auto xs = split_with(*it, " ");
          auto ys = split_with(xs[0], ":");
          rtp_stream_id_ = std::stoi(ys[1]);
          PLOG_DEBUG << "rtp_stream_id=" << rtp_stream_id_;
        }
        // rid が参照するべき payload_type の対応を作る
        if (IsSimulcast()) {
          for (const auto& line : video_lines) {
            // 以下のような感じの行を探して値を設定する
            // a=rid:r0 send
            // a=rid:r0 recv pt=37

            auto s = std::string("a=rid:");
            if (!starts_with(line, s)) {
              continue;
            }
            auto xs = split_with(line, " ");
            if (xs.size() < 2) {
              continue;
            }
            soracp::RidDescription rd;
            rd.rid = xs[0].substr(s.size());
            rd.direction = xs[1];
            s = "pt=";
            if (xs.size() >= 3 && starts_with(xs[2], s)) {
              rd.set_payload_type(std::stoi(xs[2].substr(s.size())));
            }
            rtp_params_.rids.push_back(rd);
            PLOG_DEBUG << "rid=" << rd.rid << ", direction=" << rd.direction
                       << ", payload_type="
                       << (rd.has_payload_type()
                               ? std::to_string(rd.payload_type)
                               : "(none)");
          }
        }

        std::shared_ptr<rtc::Track> track;
        std::map<std::optional<std::string>,
                 std::shared_ptr<rtc::RtcpSrReporter>>
            sr_reporters;

        auto video = rtc::Description::Video(rtp_params_.mid);
        for (const auto& codec : rtp_params_.codecs) {
          if (codec.name == "H264") {
            video.addH264Codec(codec.payload_type);
          } else if (codec.name == "H265") {
            video.addH265Codec(codec.payload_type);
          }
        }
        std::map<std::optional<std::string>, uint32_t> ssrcs;
        if (!IsSimulcast()) {
          uint32_t ssrc = generate_random_number();
          video.addSSRC(ssrc, cname, msid, track_id);
          ssrcs.insert(std::make_pair(std::nullopt, ssrc));
        } else {
          for (const auto& p : rtp_params_.encodings) {
            uint32_t ssrc = generate_random_number();
            video.addSSRC(ssrc, cname, msid, track_id);
            ssrcs.insert(std::make_pair(p.rid, ssrc));
          }
        }
        track = client_.pc->addTrack(video);

        auto simulcast_config = std::make_shared<SimulcastMediaHandlerConfig>();
        auto simulcast_handler =
            std::make_shared<SimulcastMediaHandler>(simulcast_config);
        for (int i = 0; i < (!IsSimulcast() ? 1 : rtp_params_.encodings.size());
             i++) {
          std::optional<std::string> rid;
          if (IsSimulcast()) {
            rid = rtp_params_.encodings[i].rid;
          }
          uint32_t ssrc = ssrcs[rid];

          int payload_type;
          std::string codec;
          if (IsSimulcast()) {
            // この rid が参照するべき payload_type と codec を探す
            auto it =
                std::find_if(rtp_params_.rids.begin(), rtp_params_.rids.end(),
                             [rid](const soracp::RidDescription& rd) {
                               return rd.rid == *rid;
                             });
            if (it == rtp_params_.rids.end() || !it->has_payload_type()) {
              payload_type = rtp_params_.codecs[0].payload_type;
              codec = rtp_params_.codecs[0].name;
            } else {
              payload_type = it->payload_type;
              codec =
                  std::find_if(
                      rtp_params_.codecs.begin(), rtp_params_.codecs.end(),
                      [payload_type](const soracp::RtpCodecParameters& codec) {
                        return codec.payload_type == payload_type;
                      })
                      ->name;
            }
          } else {
            payload_type = rtp_params_.codecs[0].payload_type;
            codec = rtp_params_.codecs[0].name;
          }

          auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(
              ssrc, cname, payload_type,
              codec == "H264" ? rtc::H264RtpPacketizer::defaultClockRate
                              : rtc::H265RtpPacketizer::defaultClockRate);
          std::shared_ptr<rtc::RtpPacketizer> packetizer;
          if (codec == "H264") {
            packetizer = std::make_shared<rtc::H264RtpPacketizer>(
                rtc::NalUnit::Separator::LongStartSequence, rtp_config);
          } else {
            packetizer = std::make_shared<rtc::H265RtpPacketizer>(
                rtc::NalUnit::Separator::LongStartSequence, rtp_config);
          }
          auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
          packetizer->addToChain(sr_reporter);
          auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
          packetizer->addToChain(nack_responder);
          auto pli_handler = std::make_shared<rtc::PliHandler>([this]() {
            PLOG_DEBUG << "PLI or FIR received";
            client_.video_encoder->ForceIntraNextFrame();
          });
          packetizer->addToChain(pli_handler);

          if (!IsSimulcast()) {
            simulcast_handler->addToChain(packetizer);
          } else {
            simulcast_handler->addToChainWithRid(
                *rid, packetizer, [this, rtp_config](std::string rid) {
                  rtp_config->rid = rid;
                  rtp_config->ridId = rtp_stream_id_;
                  return nullptr;
                });
          }

          sr_reporters[rid] = sr_reporter;
        }
        track->setMediaHandler(simulcast_handler);

        track->onOpen([this, wtrack = std::weak_ptr<rtc::Track>(track)]() {
          PLOG_DEBUG << "Video Track Opened";
          auto track = wtrack.lock();
          if (track == nullptr) {
            return;
          }

          std::function<std::shared_ptr<VideoEncoder>(std::string)>
              create_encoder =
                  [this](std::string codec) -> std::shared_ptr<VideoEncoder> {
            if (codec == "H264") {
              if (config_.h264_encoder_type ==
                  soracp::H264_ENCODER_TYPE_OPEN_H264) {
                return CreateOpenH264VideoEncoder(config_.openh264);
              } else if (config_.h264_encoder_type ==
                         soracp::H264_ENCODER_TYPE_VIDEO_TOOLBOX) {
#if defined(__APPLE__)
                return CreateVTH26xVideoEncoder(VTH26xVideoEncoderType::kH264);
#else
                PLOG_ERROR << "VideoToolbox is only supported on macOS/iOS";
#endif
              } else {
                PLOG_ERROR << "Unknown H264EncoderType";
              }
            } else if (codec == "H265") {
              if (config_.h265_encoder_type ==
                  soracp::H265_ENCODER_TYPE_VIDEO_TOOLBOX) {
#if defined(__APPLE__)
                return CreateVTH26xVideoEncoder(VTH26xVideoEncoderType::kH265);
#else
                PLOG_ERROR << "VideoToolbox is only supported on macOS/iOS";
#endif
              } else {
                PLOG_ERROR << "Unknown H265EncoderType";
              }
            }
            return nullptr;
          };

          client_.video_encoder =
              CreateSimulcastEncoderAdapter(rtp_params_, create_encoder);

          on_track_(track);
        });
        client_.video = std::make_shared<Track>();
        client_.video->track = track;
        client_.video->senders = sr_reporters;
        client_.video->simulcast_handler = simulcast_handler;
      }
      // audio
      {
        uint32_t ssrc = generate_random_number();
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
                auto sender = client_.audio->senders[std::nullopt];
                auto rtp_config = sender->rtpConfig;
                auto elapsed_seconds =
                    double((audio.timestamp - initial_timestamp).count()) /
                    (1000 * 1000);
                rtp_config->timestamp =
                    rtp_config->startTimestamp +
                    rtp_config->secondsToTimestamp(elapsed_seconds);
                auto report_elapsed_timestamp =
                    rtp_config->timestamp - sender->lastReportedTimestamp();
                if (rtp_config->timestampToSeconds(report_elapsed_timestamp) >
                    5) {
                  sender->setNeedsToReport();
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
        std::map<std::optional<std::string>,
                 std::shared_ptr<rtc::RtcpSrReporter>>
            sr_reporters;
        sr_reporters[std::nullopt] = sr_reporter;
        client_.audio->senders = sr_reporters;
      }

      client_.pc->setRemoteDescription(rtc::Description(sdp, "offer"));
    } else if (js["type"] == "switched") {
      auto v = js["ignore_disconnect_websocket"];
      if (v.is_boolean() && v.get<bool>()) {
        GetWebSocket()->close();
        ws_ = nullptr;
      }
    } else if (js["type"] == "stats-req") {
      nlohmann::json js = {{"type", "stats"},
                           {"reports", nlohmann::json::array()}};
      PLOG_DEBUG << "stats: " << js.dump();
      GetWebSocket()->send(js.dump());
    } else if (js["type"] == "ping") {
      nlohmann::json js = {{"type", "pong"},
                           {"stats", nlohmann::json::array()}};
      PLOG_DEBUG << "pong: " << js.dump();
      GetWebSocket()->send(js.dump());
    } else if (js["type"] == "notify") {
      if (on_notify_) {
        on_notify_(message);
      }
    } else if (js["type"] == "push") {
      if (on_push_) {
        on_push_(message);
      }
    }
  }

  void OnOpen(bool redirect) {
    const auto& sc = sora_config_;
    nlohmann::json js = {
        {"type", "connect"},
        {"role", sc.role},
        {"channel_id", sc.channel_id},
        {"sora_client", Version::GetClientName()},
        {"environment", Version::GetEnvironment()},
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

    set_if(js, "redirect", true, redirect);
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
             d.has_max_packet_life_time());
      set_if(dc, "max_retransmits", d.max_retransmits, d.has_max_retransmits());
      set_if(dc, "protocol", d.protocol, d.has_protocol());
      set_optional_bool(dc, "compress", d.compress);
      js["data_channels"].push_back(dc);
    }

    if (sc.has_forwarding_filter()) {
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
    GetWebSocket()->send(js.dump());
  }

  void OnError(const std::string& s) {
    // client_ = Client();
    // ws_ = nullptr;
  }

  void OnClosed() {
    // ws_ = nullptr;
  }

  bool IsSimulcast() const { return !rtp_params_.encodings.empty(); }

  std::shared_ptr<rtc::WebSocket> GetWebSocket() const {
    std::lock_guard<std::mutex> lock(ws_mutex_);
    return ws_;
  }

 private:
  std::shared_ptr<rtc::WebSocket> ws_;
  std::vector<std::shared_ptr<rtc::WebSocket>> connecting_wss_;
  mutable std::mutex ws_mutex_;
  Client client_;
  soracp::SignalingConfig config_;
  soracp::SoraConnectConfig sora_config_;
  soracp::RtpParameters rtp_params_;
  int rtp_stream_id_ = 0;
  int video_ssrc_ = 0;
  std::function<void(std::shared_ptr<rtc::Track>)> on_track_;
  std::function<void(std::shared_ptr<sorac::DataChannel>)> on_data_channel_;
  std::function<void(const std::string&)> on_notify_;
  std::function<void(const std::string&)> on_push_;
};

std::shared_ptr<Signaling> CreateSignaling(
    const soracp::SignalingConfig& config) {
  return std::make_shared<SignalingImpl>(config);
}

}  // namespace sorac
