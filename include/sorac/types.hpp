#ifndef SORAC_TYPES_HPP_
#define SORAC_TYPES_HPP_

#include <chrono>
#include <memory>

namespace sorac {

struct VideoFrameBufferI420 {
  int width;
  int height;
  std::unique_ptr<uint8_t[]> y;
  int stride_y;
  std::unique_ptr<uint8_t[]> u;
  int stride_u;
  std::unique_ptr<uint8_t[]> v;
  int stride_v;

  static std::shared_ptr<VideoFrameBufferI420> Create(int width, int height);
};

struct VideoFrameBufferNV12 {
  int width;
  int height;
  std::unique_ptr<uint8_t[]> y;
  int stride_y;
  std::unique_ptr<uint8_t[]> uv;
  int stride_uv;

  static std::shared_ptr<VideoFrameBufferNV12> Create(int width, int height);
};

struct VideoFrame {
  std::shared_ptr<VideoFrameBufferI420> i420_buffer;
  std::shared_ptr<VideoFrameBufferNV12> nv12_buffer;
  std::chrono::microseconds timestamp;
};

struct EncodedImage {
  std::unique_ptr<uint8_t[]> buf;
  int size;
  std::chrono::microseconds timestamp;
};

struct AudioFrame {
  int sample_rate;
  int channels;
  std::unique_ptr<float[]> pcm;
  int samples;
  std::chrono::microseconds timestamp;
};

struct EncodedAudio {
  std::unique_ptr<uint8_t> buf;
  int size = 0;
  int cap = 0;
  std::chrono::microseconds timestamp;
};

}  // namespace sorac

#endif
