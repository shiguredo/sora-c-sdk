#ifndef SORAC_SIGNALING_HPP_
#define SORAC_SIGNALING_HPP_

#include <functional>
#include <memory>

// libdatachannel
#include <rtc/rtc.hpp>

#include "soracp.json.c.hpp"
#include "types.hpp"
#include "data_channel.hpp"

namespace sorac {

class Signaling {
 public:
  virtual ~Signaling() {}
  virtual void Connect(const soracp::SoraConnectConfig& sora_config) = 0;
  virtual void SendVideoFrame(const VideoFrame& frame) = 0;
  virtual void SendAudioFrame(const AudioFrame& frame) = 0;

  virtual void SetOnTrack(
      std::function<void(std::shared_ptr<rtc::Track>)> on_track) = 0;
  virtual void SetOnDataChannel(
      std::function<void(std::shared_ptr<sorac::DataChannel>)>
          on_data_channel) = 0;

  virtual void SetOnNotify(
      std::function<void(const std::string&)> on_notify) = 0;
  virtual void SetOnPush(std::function<void(const std::string&)> on_push) = 0;
};

std::shared_ptr<Signaling> CreateSignaling(
    const soracp::SignalingConfig& config);

}  // namespace sorac

#endif
