#ifndef SORAC_VT_H26X_VIDEO_ENCODER_HPP_
#define SORAC_VT_H26X_VIDEO_ENCODER_HPP_

#include <memory>
#include <string>

#include "video_encoder.hpp"

namespace sorac {

enum class VTH26xVideoEncoderType {
  kH264,
  kH265,
};

std::shared_ptr<VideoEncoder> CreateVTH26xVideoEncoder(
    VTH26xVideoEncoderType type);

}  // namespace sorac

#endif
