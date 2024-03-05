#include "util.h"

#include <sorac/types.hpp>

// libyuv
#include <libyuv.h>

extern "C" {

void sumomo_util_scale_simulcast(const char* rids[],
                                 const float scales[],
                                 int len,
                                 SoracVideoFrameRef* frame,
                                 void (*scaled)(SoracVideoFrameRef* frame,
                                                void* userdata),
                                 void* userdata) {
  for (int i = 0; i < len; i++) {
    sorac::VideoFrame f = *(sorac::VideoFrame*)frame;
    f.rid = rids[i];
    int width = (int)(f.width() / scales[i]);
    int height = (int)(f.height() / scales[i]);
    if (f.width() != width || f.height() != height) {
      if (f.i420_buffer) {
        auto fb = sorac::VideoFrameBufferI420::Create(width, height);
        libyuv::I420Scale(f.i420_buffer->y.get(), f.i420_buffer->stride_y,
                          f.i420_buffer->u.get(), f.i420_buffer->stride_u,
                          f.i420_buffer->v.get(), f.i420_buffer->stride_v,
                          f.width(), f.height(), fb->y.get(), fb->stride_y,
                          fb->u.get(), fb->stride_u, fb->v.get(), fb->stride_v,
                          fb->width, fb->height, libyuv::kFilterBox);
        f.i420_buffer = fb;
        scaled((SoracVideoFrameRef*)&f, userdata);
      } else {
        auto fb = sorac::VideoFrameBufferNV12::Create(width, height);
        libyuv::NV12Scale(f.nv12_buffer->y.get(), f.nv12_buffer->stride_y,
                          f.nv12_buffer->uv.get(), f.nv12_buffer->stride_uv,
                          f.width(), f.height(), fb->y.get(), fb->stride_y,
                          fb->uv.get(), fb->stride_uv, fb->width, fb->height,
                          libyuv::kFilterBox);
        f.nv12_buffer = fb;
        scaled((SoracVideoFrameRef*)&f, userdata);
      }
    } else {
      scaled((SoracVideoFrameRef*)&f, userdata);
    }
  }
}
}
