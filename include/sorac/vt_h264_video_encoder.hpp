#ifndef SORAC_VT_H264_VIDEO_ENCODER_HPP_
#define SORAC_VT_H264_VIDEO_ENCODER_HPP_

#include <memory>
#include <string>

#include "h264_video_encoder.hpp"

namespace sorac {

std::shared_ptr<H264VideoEncoder> CreateVTH264VideoEncoder();

}

#endif
