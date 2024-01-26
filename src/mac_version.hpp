#ifndef SORAC_MAC_VERSION_HPP_
#define SORAC_MAC_VERSION_HPP_

#include <string>

namespace sorac {

class MacVersion {
 public:
  static std::string GetOSName();
  static std::string GetOSVersion();
};

}  // namespace sorac

#endif
