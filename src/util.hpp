#ifndef SORAC_UTIL_HPP_
#define SORAC_UTIL_HPP_

#include <string>
#include <vector>

namespace sorac {

uint32_t generate_random_number(uint32_t max = UINT32_MAX);
std::string generate_random_string(int length);
std::string generate_random_string(int length, std::string pattern);
std::vector<std::string> split_with(const std::string& str,
                                    const std::string& token);
bool starts_with(const std::string& str, const std::string& s);
std::string trim(const std::string& str, const std::string& trim_chars);

}  // namespace sorac

#endif
