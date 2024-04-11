// https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/api/video_codecs/h264_profile_level_id.cpp
// から必要な部分だけ抜き出して修正したもの。

/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sorac/h264_profile_level_id.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace sorac {

namespace {

// For level_idc=11 and profile_idc=0x42, 0x4D, or 0x58, the constraint set3
// flag specifies if level 1b or level 1.1 is used.
const uint8_t kConstraintSet3Flag = 0x10;

// Convert a string of 8 characters into a byte where the positions containing
// character c will have their bit set. For example, c = 'x', str = "x1xx0000"
// will return 0b10110000. constexpr is used so that the pattern table in
// kProfilePatterns is statically initialized.
constexpr uint8_t ByteMaskString(char c, const char (&str)[9]) {
  return (str[0] == c) << 7 | (str[1] == c) << 6 | (str[2] == c) << 5 |
         (str[3] == c) << 4 | (str[4] == c) << 3 | (str[5] == c) << 2 |
         (str[6] == c) << 1 | (str[7] == c) << 0;
}

// Class for matching bit patterns such as "x1xx0000" where 'x' is allowed to be
// either 0 or 1.
class BitPattern {
 public:
  explicit constexpr BitPattern(const char (&str)[9])
      : mask_(~ByteMaskString('x', str)),
        masked_value_(ByteMaskString('1', str)) {}

  bool IsMatch(uint8_t value) const { return masked_value_ == (value & mask_); }

 private:
  const uint8_t mask_;
  const uint8_t masked_value_;
};

// Table for converting between profile_idc/profile_iop to H264Profile.
struct ProfilePattern {
  const uint8_t profile_idc;
  const BitPattern profile_iop;
  const H264Profile profile;
};

// This is from https://tools.ietf.org/html/rfc6184#section-8.1.
constexpr ProfilePattern kProfilePatterns[] = {
    {0x42, BitPattern("x1xx0000"), H264Profile::kProfileConstrainedBaseline},
    {0x4D, BitPattern("1xxx0000"), H264Profile::kProfileConstrainedBaseline},
    {0x58, BitPattern("11xx0000"), H264Profile::kProfileConstrainedBaseline},
    {0x42, BitPattern("x0xx0000"), H264Profile::kProfileBaseline},
    {0x58, BitPattern("10xx0000"), H264Profile::kProfileBaseline},
    {0x4D, BitPattern("0x0x0000"), H264Profile::kProfileMain},
    {0x64, BitPattern("00000000"), H264Profile::kProfileHigh},
    {0x64, BitPattern("00001100"), H264Profile::kProfileConstrainedHigh},
    {0xF4, BitPattern("00000000"), H264Profile::kProfilePredictiveHigh444}};

}  // anonymous namespace

std::optional<H264ProfileLevelId> ParseH264ProfileLevelId(const char* str) {
  // The string should consist of 3 bytes in hexadecimal format.
  if (std::strlen(str) != 6u)
    return std::nullopt;
  const uint32_t profile_level_id_numeric = strtol(str, nullptr, 16);
  if (profile_level_id_numeric == 0)
    return std::nullopt;

  // Separate into three bytes.
  const uint8_t level_idc =
      static_cast<uint8_t>(profile_level_id_numeric & 0xFF);
  const uint8_t profile_iop =
      static_cast<uint8_t>((profile_level_id_numeric >> 8) & 0xFF);
  const uint8_t profile_idc =
      static_cast<uint8_t>((profile_level_id_numeric >> 16) & 0xFF);

  // Parse level based on level_idc and constraint set 3 flag.
  H264Level level_casted = static_cast<H264Level>(level_idc);
  H264Level level;

  switch (level_casted) {
    case H264Level::kLevel1_1:
      level = (profile_iop & kConstraintSet3Flag) != 0 ? H264Level::kLevel1_b
                                                       : H264Level::kLevel1_1;
      break;
    case H264Level::kLevel1:
    case H264Level::kLevel1_2:
    case H264Level::kLevel1_3:
    case H264Level::kLevel2:
    case H264Level::kLevel2_1:
    case H264Level::kLevel2_2:
    case H264Level::kLevel3:
    case H264Level::kLevel3_1:
    case H264Level::kLevel3_2:
    case H264Level::kLevel4:
    case H264Level::kLevel4_1:
    case H264Level::kLevel4_2:
    case H264Level::kLevel5:
    case H264Level::kLevel5_1:
    case H264Level::kLevel5_2:
      level = level_casted;
      break;
    default:
      // Unrecognized level_idc.
      return std::nullopt;
  }

  // Parse profile_idc/profile_iop into a Profile enum.
  for (const ProfilePattern& pattern : kProfilePatterns) {
    if (profile_idc == pattern.profile_idc &&
        pattern.profile_iop.IsMatch(profile_iop)) {
      return H264ProfileLevelId(pattern.profile, level);
    }
  }

  // Unrecognized profile_idc/profile_iop combination.
  return std::nullopt;
}

}  // namespace sorac