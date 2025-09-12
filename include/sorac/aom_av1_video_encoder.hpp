#ifndef SORAC_AOM_AV1_VIDEO_ENCODER_HPP_
#define SORAC_AOM_AV1_VIDEO_ENCODER_HPP_

#include <memory>
#include <string>

#include "video_encoder.hpp"

namespace sorac {

std::shared_ptr<VideoEncoder> CreateAomAv1VideoEncoder(const std::string& aom);

}

#endif
