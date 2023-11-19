#ifndef SORAC_SRTP_KEYING_MATERIAL_EXPORTER_H_
#define SORAC_SRTP_KEYING_MATERIAL_EXPORTER_H_

#include <memory>
#include <optional>
#include <vector>

// libdatachannel
#include <rtc/rtc.hpp>

namespace sorac {

struct KeyingMaterial {
  std::vector<uint8_t> client_write_key;
  std::vector<uint8_t> server_write_key;
  std::vector<uint8_t> client_write_salt;
  std::vector<uint8_t> server_write_salt;
};

std::optional<KeyingMaterial> ExportSrtpKeyingMaterial(
    std::shared_ptr<rtc::PeerConnection> pc);

}  // namespace sorac

#endif