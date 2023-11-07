#include "v4l2_capturer.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

// Posix
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

// Linux
#include <linux/videodev2.h>

// libyuv
#include <libyuv.h>

// Sora C SDK
#include <sorac/current_time.hpp>
#include <sorac/types.hpp>

namespace sumomo {

class V4L2Capturer : public SumomoCapturer {
 public:
  V4L2Capturer(const char* device, int width, int height) {
    this->device_ = device;
    this->width_ = width;
    this->height_ = height;
    this->destroy = [](SumomoCapturer* p) { delete (sumomo::V4L2Capturer*)p; };
    this->set_frame_callback = [](SumomoCapturer* p,
                                  sumomo_capturer_on_frame_func on_frame,
                                  void* userdata) {
      ((sumomo::V4L2Capturer*)p)
          ->SetFrameCallback(
              [on_frame, userdata](const sorac::VideoFrame& frame) {
                on_frame((SoracVideoFrameRef*)&frame, userdata);
              });
    };
    this->start = [](SumomoCapturer* p) {
      auto q = (sumomo::V4L2Capturer*)p;
      return q->Start(q->device_.c_str(), q->width_, q->height_);
    };
    this->stop = [](SumomoCapturer* p) { ((sumomo::V4L2Capturer*)p)->Stop(); };
  }

  void SetFrameCallback(
      std::function<void(const sorac::VideoFrame& frame)> callback) {
    callback_ = callback;
  }

  int Start(const char* device, int width, int height) {
    Stop();

    device_fd_ = open(device, O_RDWR | O_NONBLOCK, 0);
    if (device_fd_ < 0) {
      fprintf(stderr, "Failed to open: %s: %s\n", device, strerror(errno));
      return -1;
    }

    // デバイスに MJPEG フォーマットがあるか確認
    struct v4l2_fmtdesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.index = 0;
    desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bool found = false;
    while (ioctl(device_fd_, VIDIOC_ENUM_FMT, &desc) == 0) {
      printf("desc: %s\n", desc.description);
      if (desc.pixelformat == V4L2_PIX_FMT_MJPEG) {
        found = true;
        break;
      }
      desc.index++;
    }
    if (!found) {
      fprintf(stderr, "Failed to find V4L2_PIX_FMT_MJPEG\n");
      return -1;
    }

    // MJPEG フォーマットの設定
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.sizeimage = 0;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    if (ioctl(device_fd_, VIDIOC_S_FMT, &fmt) < 0) {
      fprintf(stderr, "Failed to VIDIOC_S_FMT: %s\n", strerror(errno));
      return -1;
    }
    width_ = fmt.fmt.pix.width;
    height_ = fmt.fmt.pix.height;

    // ビデオバッファの設定
    const int V4L2_BUFFER_COUNT = 4;
    {
      struct v4l2_requestbuffers req;
      memset(&req, 0, sizeof(req));

      req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      req.memory = V4L2_MEMORY_MMAP;
      req.count = V4L2_BUFFER_COUNT;

      if (ioctl(device_fd_, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "Failed to VIDIOC_REQBUFS: %s\n", strerror(errno));
        return -1;
      }

      for (int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(device_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
          fprintf(stderr, "Failed to VIDIOC_QUERYBUF: %s\n", strerror(errno));
          return -1;
        }

        Buffer b;
        b.start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                       device_fd_, buf.m.offset);
        if (b.start == MAP_FAILED) {
          fprintf(stderr, "Failed to mmap: %s\n", strerror(errno));
          return -1;
        }
        b.length = buf.length;

        if (ioctl(device_fd_, VIDIOC_QBUF, &buf) < 0) {
          fprintf(stderr, "Failed to VIDIOC_QBUF: %s\n", strerror(errno));
          return -1;
        }

        pool_.push_back(b);
      }
    }

    // キャプチャ開始
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(device_fd_, VIDIOC_STREAMON, &type) < 0) {
      fprintf(stderr, "Failed to VIDIOC_STREAMON: %s\n", strerror(errno));
      return -1;
    }

    quit_ = false;
    capture_thread_.reset(new std::thread([this]() {
      while (true) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(device_fd_, &fds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int r = select(device_fd_ + 1, &fds, NULL, NULL, &timeout);

        if (quit_) {
          break;
        }

        if (r < 0 && errno != EINTR) {
          fprintf(stderr, "Failed to select: %s\n", strerror(errno));
          break;
        } else if (r == 0) {
          // timeout
          continue;
        } else if (!FD_ISSET(device_fd_, &fds)) {
          // spurious failure
          continue;
        }

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        bool dequeued = true;
        while (ioctl(device_fd_, VIDIOC_DQBUF, &buf) < 0) {
          if (errno != EINTR) {
            dequeued = false;
            break;
          }
        }
        if (!dequeued) {
          continue;
        }

        auto fb = sorac::VideoFrameBuffer::Create(width_, height_);
        auto p = (uint8_t*)pool_[buf.index].start;
        auto chroma_height = (fb->height + 1) / 2;
        libyuv::ConvertToI420(p, buf.bytesused, fb->y.get(), fb->stride_y,
                              fb->u.get(), fb->stride_u, fb->v.get(),
                              fb->stride_v, 0, 0, width_, height_, width_,
                              height_, libyuv::kRotate0, libyuv::FOURCC_MJPG);

        sorac::VideoFrame frame;
        frame.video_frame_buffer = fb;
        frame.timestamp = sorac::get_current_time();
        callback_(frame);

        if (ioctl(device_fd_, VIDIOC_QBUF, &buf) < 0) {
          fprintf(stderr, "Failed to VIDIOC_QBUF: %s\n", strerror(errno));
        }

        std::this_thread::yield();
      }
    }));

    return 0;
  }

  void Stop() {
    if (capture_thread_) {
      quit_ = true;
      capture_thread_->join();
      capture_thread_.reset();
      quit_ = false;
    }

    for (auto& b : pool_) {
      munmap(b.start, b.length);
    }
    pool_.clear();

    if (device_fd_ >= 0) {
      close(device_fd_);
      device_fd_ = -1;
    }
  }

 private:
  std::string device_;
  std::function<void(const sorac::VideoFrame& frame)> callback_;
  int width_;
  int height_;

  int device_fd_ = -1;
  std::atomic<bool> quit_;
  std::unique_ptr<std::thread> capture_thread_;

  struct Buffer {
    void* start;
    size_t length;
  };
  std::vector<Buffer> pool_;
};

}  // namespace sumomo

extern "C" {

SumomoCapturer* sumomo_v4l2_capturer_create(const char* device,
                                            int width,
                                            int height) {
  return new sumomo::V4L2Capturer(device, width, height);
}
}
