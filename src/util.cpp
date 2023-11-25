#include "util.hpp"

#include <array>
#include <functional>
#include <random>

// zlib
#include <zlib.h>

namespace sorac {

uint32_t generate_random_number(uint32_t max) {
  // std::random_device random;
  // std::array<std::uint32_t, std::mt19937::state_size> seq;
  // std::generate(seq.begin(), seq.end(), std::ref(random));
  // std::seed_seq seed_seq(seq.begin(), seq.end());
  // std::mt19937 engine(seed_seq);
  // std::uniform_int_distribution<uint32_t> dist(0, max);
  // return dist(engine);
  std::random_device random;
  return random() % max;
}
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

std::string zlib_compress(const uint8_t* input_buf, size_t input_size) {
  std::string output;
  output.resize(16 * 1024);
  uLongf output_size;
  while (true) {
    output_size = output.size();
    int ret = compress2((Bytef*)output.data(), &output_size, input_buf,
                        input_size, Z_DEFAULT_COMPRESSION);
    if (ret == Z_BUF_ERROR) {
      output.resize(output.size() * 2);
      continue;
    }
    if (ret != Z_OK) {
      throw std::exception();
    }
    break;
  }
  output.resize(output_size);
  return output;
}

std::string zlib_uncompress(const uint8_t* input_buf, size_t input_size) {
  std::string output;
  output.resize(16 * 1024);
  uLongf output_size;
  while (true) {
    output_size = output.size();
    int ret =
        uncompress((Bytef*)output.data(), &output_size, input_buf, input_size);
    if (ret == Z_BUF_ERROR) {
      output.resize(output.size() * 2);
      continue;
    }
    if (ret != Z_OK) {
      throw std::exception();
    }
    break;
  }
  output.resize(output_size);
  return output;
}

}  // namespace sorac