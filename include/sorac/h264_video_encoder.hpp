#ifndef SORAC_H264_VIDEO_ENCODER_HPP_
#define SORAC_H264_VIDEO_ENCODER_HPP_

#include <functional>
#include <string>

#include "types.hpp"

namespace sorac {

class H264VideoEncoder {
 public:
  virtual ~H264VideoEncoder() {}
  virtual void ForceIntraNextFrame() = 0;
  virtual bool InitEncode() = 0;
  virtual void SetEncodeCallback(
      std::function<void(const EncodedImage&)> callback) = 0;
  virtual void Encode(const VideoFrame& frame) = 0;
  virtual void Release() = 0;
};

std::shared_ptr<H264VideoEncoder> CreateOpenH264VideoEncoder(
    const std::string& openh264);

}  // namespace sorac

#endif
