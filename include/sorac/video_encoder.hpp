#ifndef SORAC_VIDEO_ENCODER_HPP_
#define SORAC_VIDEO_ENCODER_HPP_

#include <functional>
#include <string>

#include "types.hpp"

namespace sorac {

class VideoEncoder {
 public:
  struct Settings {
    int width;
    int height;
    int bitrate_kbps;
  };

  virtual ~VideoEncoder() {}
  virtual void ForceIntraNextFrame() = 0;
  virtual bool InitEncode(const Settings& settings) = 0;
  virtual void SetEncodeCallback(
      std::function<void(const EncodedImage&)> callback) = 0;
  virtual void Encode(const VideoFrame& frame) = 0;
  virtual void Release() = 0;
};

}  // namespace sorac

#endif
