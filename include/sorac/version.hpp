#ifndef SORAC_VERSION_HPP_
#define SORAC_VERSION_HPP_

#include <string>

namespace sorac {

class Version {
 public:
  static std::string GetClientName();
  static std::string GetEnvironment();
};

}  // namespace sorac

#endif
