#include "sorac/vt_h26x_video_encoder.hpp"

#include <string.h>
#include <atomic>
#include <exception>

// mac
#include <VideoToolbox/VideoToolbox.h>

// plog
#include <plog/Log.h>

namespace sorac {

// デストラクタで指定した関数を呼ぶだけのクラス
class Resource {
 public:
  Resource(std::function<void()> release) : release_(release) {}
  ~Resource() { release_(); }

 private:
  std::function<void()> release_;
};

class VTH26xVideoEncoder : public VideoEncoder {
 public:
  VTH26xVideoEncoder(VTH26xVideoEncoderType type) : type_(type) {}
  ~VTH26xVideoEncoder() override { Release(); }

  void ForceIntraNextFrame() override { next_iframe_ = true; }

  bool InitEncode(const Settings& settings) override {
    Release();

    OSType pixel_format_value = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
    CFNumberRef pixel_format = CFNumberCreate(
        kCFAllocatorDefault, kCFNumberSInt32Type, &pixel_format_value);
    Resource pixel_format_resource(
        [pixel_format]() { CFRelease(pixel_format); });
    CFDictionaryRef empty_dict = CFDictionaryCreate(
        kCFAllocatorDefault, nullptr, nullptr, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    Resource empty_dict_resource([empty_dict]() { CFRelease(empty_dict); });
    CFStringRef keys[] = {kCVPixelBufferIOSurfacePropertiesKey,
                          kCVPixelBufferPixelFormatTypeKey};
    void* values[] = {(void*)empty_dict, (void*)pixel_format};
    CFDictionaryRef source_attr = CFDictionaryCreate(
        nullptr, (const void**)keys, (const void**)values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    Resource source_attr_resource([source_attr]() { CFRelease(source_attr); });

    CFDictionaryRef encoder_specs = CFDictionaryCreate(
        nullptr,
        (const void**)
            kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder,
        (const void**)&kCFBooleanTrue, 1, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    Resource encoder_specs_resource(
        [encoder_specs]() { CFRelease(encoder_specs); });

    OSStatus err = VTCompressionSessionCreate(
        nullptr,  // use default allocator
        settings.width, settings.height,
        type_ == VTH26xVideoEncoderType::kH264 ? kCMVideoCodecType_H264
                                               : kCMVideoCodecType_HEVC,
        encoder_specs,  // use hardware accelerated encoder if available
        source_attr,
        nullptr,  // use default compressed data allocator
        OnEncodeStatic, nullptr, &vtref_);
    if (err != noErr) {
      PLOG_ERROR << "Failed to create compression session: err=" << err;
      return false;
    }

    if (OSStatus err = VTSessionSetProperty(
            vtref_, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
        err != noErr) {
      PLOG_ERROR << "Failed to set real-time property: err=" << err;
      return false;
    }
    if (type_ == VTH26xVideoEncoderType::kH264) {
      if (OSStatus err = VTSessionSetProperty(
              vtref_, kVTCompressionPropertyKey_ProfileLevel,
              kVTProfileLevel_H264_Baseline_3_1);
          err != noErr) {
        PLOG_ERROR << "Failed to set profile-level property: err=" << err;
        return false;
      }
    }
    if (OSStatus err = VTSessionSetProperty(
            vtref_, kVTCompressionPropertyKey_AllowFrameReordering,
            kCFBooleanFalse);
        err != noErr) {
      PLOG_ERROR << "Failed to set allow-frame-reordering property: err="
                 << err;
      return false;
    }

    // ビットレート
    {
      int value = settings.bitrate_kbps * 1000;
      CFNumberRef cfnum =
          CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
      Resource cfnum_resource([cfnum]() { CFRelease(cfnum); });
      OSStatus err = VTSessionSetProperty(
          vtref_, kVTCompressionPropertyKey_AverageBitRate, cfnum);
      if (err != noErr) {
        PLOG_ERROR << "Failed to set average-bitrate property: err=" << err;
        return false;
      }
    }

    // キーフレーム間隔 (7200 フレームまたは 4 分間)
    {
      int value = 7200;
      CFNumberRef cfnum =
          CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
      Resource cfnum_resource([cfnum]() { CFRelease(cfnum); });
      OSStatus err = VTSessionSetProperty(
          vtref_, kVTCompressionPropertyKey_MaxKeyFrameInterval, cfnum);
      if (err != noErr) {
        PLOG_ERROR << "Failed to set max-keyframe-interval property: err="
                   << err;
        return false;
      }
    }
    {
      int value = 240;
      CFNumberRef cfnum =
          CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
      Resource cfnum_resource([cfnum]() { CFRelease(cfnum); });
      OSStatus err = VTSessionSetProperty(
          vtref_, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, cfnum);
      if (err != noErr) {
        PLOG_ERROR
            << "Failed to set max-keyframe-interval-duration property: err="
            << err;
        return false;
      }
    }

    next_iframe_ = true;

    return true;
  }

  void SetEncodeCallback(
      std::function<void(const EncodedImage&)> callback) override {
    callback_ = callback;
  }

  void Encode(const VideoFrame& frame) override {
    if (frame.nv12_buffer == nullptr) {
      PLOG_ERROR << "VTH26xVideoEncoder only support NV12 buffer.";
      return;
    }

    CVPixelBufferPoolRef pixel_buffer_pool =
        VTCompressionSessionGetPixelBufferPool(vtref_);
    if (!pixel_buffer_pool) {
      PLOG_ERROR << "Failed to get pixel buffer pool.";
      return;
    }
    CVPixelBufferRef pixel_buffer = nullptr;
    Resource pixel_buffer_resource([&pixel_buffer]() {
      if (pixel_buffer != nullptr) {
        CFRelease(pixel_buffer);
      }
    });
    if (CVReturn ret = CVPixelBufferPoolCreatePixelBuffer(
            nullptr, pixel_buffer_pool, &pixel_buffer);
        ret != kCVReturnSuccess) {
      PLOG_ERROR << "Failed to create pixel buffer: " << ret;
      return;
    }

    if (CVReturn r = CVPixelBufferLockBaseAddress(pixel_buffer, 0);
        r != kCVReturnSuccess) {
      PLOG_ERROR << "Failed to lock base address: " << r;
      return;
    }
    uint8_t* dst_y =
        (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0);
    int dst_stride_y = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0);
    uint8_t* dst_uv =
        (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 1);

    // NV12 の内容をコピーする
    int dst_stride_uv = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1);
    const uint8_t* src_y = frame.nv12_buffer->y.get();
    int src_stride_y = frame.nv12_buffer->stride_y;
    const uint8_t* src_uv = frame.nv12_buffer->uv.get();
    int src_stride_uv = frame.nv12_buffer->stride_uv;
    int width = frame.nv12_buffer->width;
    int height = frame.nv12_buffer->height;
    int chroma_width = (width + 1) / 2 * 2;
    int chroma_height = (height + 1) / 2;
    for (int i = 0; i < height; ++i) {
      memcpy(dst_y, src_y, width);
      dst_y += dst_stride_y;
      src_y += src_stride_y;
    }
    for (int i = 0; i < chroma_height; ++i) {
      memcpy(dst_uv, src_uv, chroma_width);
      dst_uv += dst_stride_uv;
      src_uv += src_stride_uv;
    }

    if (CVReturn ret = CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);
        ret != 0) {
      PLOG_ERROR << "Failed to unlock base address: " << ret;
      return;
    }

    CFDictionaryRef frame_properties = nullptr;
    Resource frame_properties_resource([&frame_properties]() {
      if (frame_properties != nullptr) {
        CFRelease(frame_properties);
      }
    });
    bool send_key_frame = next_iframe_.exchange(false);
    if (send_key_frame) {
      PLOG_DEBUG << "KeyFrame generated";
      CFTypeRef keys[] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
      CFTypeRef values[] = {kCFBooleanTrue};
      frame_properties = CFDictionaryCreate(
          nullptr, (const void**)keys, (const void**)values, 1,
          &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    }

    CMTime timestamp = CMTimeMake(frame.timestamp.count(), 1000000);

    std::unique_ptr<EncodeParams> params(new EncodeParams());
    params->encoder = this;
    params->timestamp = frame.timestamp;

    if (OSStatus err = VTCompressionSessionEncodeFrame(
            vtref_, pixel_buffer, timestamp, kCMTimeInvalid, frame_properties,
            params.release(), nullptr);
        err != noErr) {
      PLOG_ERROR << "Failed to encode frame: err=" << err;
      return;
    }
  }

  void Release() override {
    if (vtref_ != nullptr) {
      VTCompressionSessionInvalidate(vtref_);
      CFRelease(vtref_);
      vtref_ = nullptr;
    }
  }

 private:
  static void OnEncodeStatic(void* encoder,
                             void* params,
                             OSStatus status,
                             VTEncodeInfoFlags infoFlags,
                             CMSampleBufferRef sampleBuffer) {
    if (!params) {
      // If there are pending callbacks when the encoder is destroyed, this can happen.
      return;
    }
    std::unique_ptr<EncodeParams> p((EncodeParams*)params);
    p->encoder->OnEncode(status, infoFlags, sampleBuffer, p->timestamp);
  }
  void OnEncode(OSStatus status,
                VTEncodeInfoFlags flags,
                CMSampleBufferRef buffer,
                std::chrono::microseconds timestamp) {
    if (status != noErr) {
      PLOG_ERROR << "H26x encode failed with code: " << status;
      return;
    }
    if (flags & kVTEncodeInfo_FrameDropped) {
      PLOG_INFO << "H26x encode dropped frame.";
      return;
    }

    bool key_frame = false;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(buffer, 0);
    if (attachments != nullptr && CFArrayGetCount(attachments) != 0) {
      CFDictionaryRef attachment =
          (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
      key_frame =
          !CFDictionaryContainsKey(attachment, kCMSampleAttachmentKey_NotSync);
    }

    EncodedImage encoded;
    encoded.timestamp = timestamp;
    // CMSampleBufferRef を encoded.buf に詰める
    {
      const char NAL_BYTES[4] = {0, 0, 0, 1};
      const size_t NAL_SIZE = sizeof(NAL_BYTES);

      CMBlockBufferRef buf = CMSampleBufferGetDataBuffer(buffer);
      size_t block_buffer_size = CMBlockBufferGetDataLength(buf);
      uint8_t* dst;

      if (key_frame) {
        CMVideoFormatDescriptionRef description =
            CMSampleBufferGetFormatDescription(buffer);
        if (description == nullptr) {
          PLOG_ERROR << "Failed to get sample buffer's description.";
          return;
        }
        int nalu_header_size = 0;
        size_t param_set_count = 0;
        if (OSStatus status =
                type_ == VTH26xVideoEncoderType::kH264
                    ? CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                          description, 0, nullptr, nullptr, &param_set_count,
                          &nalu_header_size)
                    : CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
                          description, 0, nullptr, nullptr, &param_set_count,
                          &nalu_header_size);
            status != noErr) {
          PLOG_ERROR << "Failed to get parameter set.";
          return;
        }
        assert(nalu_header_size == NAL_SIZE);
        if (type_ == VTH26xVideoEncoderType::kH264) {
          assert(param_set_count == 2);
        } else {
          assert(param_set_count == 3);
        }

        size_t header_size = 0;
        // まずサイズだけ計算
        for (size_t i = 0; i < param_set_count; ++i) {
          size_t param_set_size = 0;
          const uint8_t* param_set = nullptr;
          if (OSStatus status =
                  type_ == VTH26xVideoEncoderType::kH264
                      ? CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                            description, i, &param_set, &param_set_size,
                            nullptr, nullptr)
                      : CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
                            description, i, &param_set, &param_set_size,
                            nullptr, nullptr);
              status != noErr) {
            PLOG_ERROR << "Failed to get parameter set.";
            return;
          }
          header_size += NAL_SIZE + param_set_size;
        }
        // 実際にヘッダーをコピーする
        encoded.buf.reset(new uint8_t[header_size + block_buffer_size]);
        encoded.size = header_size + block_buffer_size;
        dst = encoded.buf.get();
        for (size_t i = 0; i < param_set_count; ++i) {
          size_t param_set_size = 0;
          const uint8_t* param_set = nullptr;
          if (OSStatus status =
                  type_ == VTH26xVideoEncoderType::kH264
                      ? CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                            description, i, &param_set, &param_set_size,
                            nullptr, nullptr)
                      : CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
                            description, i, &param_set, &param_set_size,
                            nullptr, nullptr);
              status != noErr) {
            PLOG_ERROR << "Failed to get parameter set.";
            return;
          }

          memcpy(dst, NAL_BYTES, NAL_SIZE);
          dst += NAL_SIZE;

          memcpy(dst, param_set, param_set_size);
          dst += param_set_size;
        }
      } else {
        encoded.buf.reset(new uint8_t[block_buffer_size]);
        encoded.size = block_buffer_size;
        dst = encoded.buf.get();
      }

      size_t buf_pos = 0;
      while (buf_pos < block_buffer_size) {
        uint32_t size_buf = 0;
        if (OSStatus err = CMBlockBufferCopyDataBytes(
                buf, buf_pos, sizeof(uint32_t), &size_buf);
            err != noErr) {
          PLOG_ERROR << "Failed to copy data bytes: err=" << err;
          return;
        }
        buf_pos += sizeof(uint32_t);

        uint32_t size = CFSwapInt32BigToHost(size_buf);

        memcpy(dst, NAL_BYTES, NAL_SIZE);
        dst += NAL_SIZE;

        if (OSStatus err = CMBlockBufferCopyDataBytes(buf, buf_pos, size, dst);
            err != noErr) {
          PLOG_ERROR << "Failed to copy data bytes: err=" << err;
          return;
        }
        buf_pos += size;
        dst += size;
      }
    }

    callback_(encoded);
  }

 private:
  struct EncodeParams {
    VTH26xVideoEncoder* encoder;
    std::chrono::microseconds timestamp;
  };

  VTH26xVideoEncoderType type_;

  VTCompressionSessionRef vtref_ = nullptr;
  std::function<void(const EncodedImage&)> callback_;

  std::atomic<bool> next_iframe_;
};

std::shared_ptr<VideoEncoder> CreateVTH26xVideoEncoder(
    VTH26xVideoEncoderType type) {
  return std::make_shared<VTH26xVideoEncoder>(type);
}

}  // namespace sorac
