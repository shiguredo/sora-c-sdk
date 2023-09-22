#include "sorac/types.hpp"

namespace sorac {

std::shared_ptr<VideoFrameBuffer> VideoFrameBuffer::Create(int width,
                                                           int height) {
  auto p = std::make_shared<VideoFrameBuffer>();
  p->width = width;
  p->height = height;
  p->stride_y = width;
  p->stride_u = (width + 1) / 2;
  p->stride_v = (width + 1) / 2;
  int chroma_height = (height + 1) / 2;
  p->y.reset(new uint8_t[p->stride_y * height]());
  p->u.reset(new uint8_t[p->stride_u * chroma_height]());
  p->v.reset(new uint8_t[p->stride_v * chroma_height]());
  return p;
}

}  // namespace sorac
