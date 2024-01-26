#ifndef SORAC_SIMULCAST_ENCODER_ADAPTER_HPP_
#define SORAC_SIMULCAST_ENCODER_ADAPTER_HPP_

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "soracp.json.h"
#include "types.hpp"
#include "video_encoder.hpp"

namespace sorac {

std::shared_ptr<VideoEncoder> CreateSimulcastEncoderAdapter(
    const soracp::RtpEncodingParameters& params,
    std::function<std::shared_ptr<VideoEncoder>()> create_encoder);

}

#endif
