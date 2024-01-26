#ifndef SORAC_OPUS_AUDIO_ENCODER_HPP_
#define SORAC_OPUS_AUDIO_ENCODER_HPP_

#include <functional>

#include "types.hpp"

namespace sorac {

class OpusAudioEncoder {
 public:
  virtual ~OpusAudioEncoder() {}

  virtual bool InitEncode(int sample_rate,
                          int channels,
                          int frame_duration_ms,
                          int bitrate_kbps) = 0;
  virtual void Release() = 0;

  virtual void Encode(const AudioFrame& frame) = 0;
  virtual void SetEncodeCallback(
      std::function<void(const EncodedAudio&)> callback) = 0;
};

std::shared_ptr<OpusAudioEncoder> CreateOpusAudioEncoder();

}  // namespace sorac

#endif
