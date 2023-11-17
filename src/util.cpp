#include "util.hpp"

#include <random>

namespace sorac {

std::string generate_random_string(int length) {
  return generate_random_string(
      length, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
}

std::string generate_random_string(int length, std::string pattern) {
  if (pattern.size() == 0) {
    return "";
  }

  std::random_device random;
  // % を計算する時にマイナス値があると危険なので unsigned 型であることを保証する
  typedef std::make_unsigned<std::random_device::result_type>::type
      unsigned_type;
  std::string r;
  for (int i = 0; i < length; i++) {
    r += pattern[(unsigned_type)random() % pattern.size()];
  }
  return r;
}

std::vector<std::string> split_with(const std::string& str,
                                    const std::string& token) {
  int sp = 0;
  std::vector<std::string> lines;
  while (true) {
    auto ep = str.find(token, sp);
    if (ep == std::string::npos) {
      if (str.size() - sp > 0) {
        lines.push_back(str.substr(sp));
      }
      break;
    }
    lines.push_back(str.substr(sp, ep - sp));
    sp = ep + token.size();
  }
  return lines;
}

bool starts_with(const std::string& str, const std::string& s) {
  return str.substr(0, s.size()) == s;
}

std::string trim(const std::string& str, const std::string& trim_chars) {
  auto sp = str.find_first_not_of(trim_chars);
  if (sp == std::string::npos) {
    return "";
  }
  auto ep = str.find_last_not_of(trim_chars);
  return str.substr(sp, ep - sp + 1);
}

}  // namespace sorac