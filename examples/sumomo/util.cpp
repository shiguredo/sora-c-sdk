#include "util.h"

#include <sorac/types.hpp>

// libyuv
#include <libyuv.h>

extern "C" {

void sumomo_util_scale_simulcast(const char* rids[],
                                 int num_rids,
                                 SoracVideoFrameRef* frame,
                                 void (*scaled)(SoracVideoFrameRef* frame,
                                                void* userdata),
                                 void* userdata) {
  for (int i = 0; i < num_rids; i++) {
    sorac::VideoFrame f = *(sorac::VideoFrame*)frame;
    f.rid = rids[i];
    int width;
    int height;
    if (*f.rid == "r0") {
      width = f.width() / 4;
      height = f.height() / 4;
    } else if (*f.rid == "r1") {
      width = f.width() / 2;
      height = f.height() / 2;
    } else {
      width = f.width();
      height = f.height();
    }
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
