#ifndef SORAC_OPEN_H264_VIDEO_ENCODER_HPP_
#define SORAC_OPEN_H264_VIDEO_ENCODER_HPP_

#include <memory>
#include <string>

#include "video_encoder.hpp"

namespace sorac {

std::shared_ptr<VideoEncoder> CreateOpenH264VideoEncoder(
    const std::string& openh264);

}

#endif
