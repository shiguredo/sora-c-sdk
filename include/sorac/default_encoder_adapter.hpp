#ifndef SORAC_DEFAULT_ENCODER_ADAPTER_HPP_
#define SORAC_DEFAULT_ENCODER_ADAPTER_HPP_

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "soracp.json.h"
#include "types.hpp"
#include "video_encoder.hpp"

namespace sorac {

std::shared_ptr<VideoEncoder> CreateDefaultEncoderAdapter(
    std::shared_ptr<VideoEncoder> encoder);

}

#endif
