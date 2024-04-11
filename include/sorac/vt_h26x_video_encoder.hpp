#ifndef SORAC_VT_H26X_VIDEO_ENCODER_HPP_
#define SORAC_VT_H26X_VIDEO_ENCODER_HPP_

#include <memory>
#include <optional>
#include <string>

#include "h264_profile_level_id.hpp"
#include "video_encoder.hpp"

namespace sorac {

enum class VTH26xVideoEncoderType {
  kH264,
  kH265,
};

std::shared_ptr<VideoEncoder> CreateVTH26xVideoEncoder(
    VTH26xVideoEncoderType type,
    std::optional<H264ProfileLevelId> profile);

}  // namespace sorac

#endif
