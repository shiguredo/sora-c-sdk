#include "sorac/current_time.hpp"

// Linux
#include <sys/time.h>

namespace sorac {

std::chrono::microseconds get_current_time() {
  struct timeval time;
  gettimeofday(&time, NULL);
  return std::chrono::microseconds(uint64_t(time.tv_sec) * 1000 * 1000 +
                                   time.tv_usec);
}

}  // namespace sorac
