#include "sorac/open_h264_video_encoder.hpp"

#include <string.h>
#include <atomic>
#include <exception>

// Linux
#include <dlfcn.h>

// plog
#include <plog/Log.h>

// OpenH264
#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>
#include <wels/codec_ver.h>

namespace sorac {

class OpenH264VideoEncoder : public H264VideoEncoder {
 public:
  OpenH264VideoEncoder(const std::string& openh264) {
    bool result = InitOpenH264(openh264);
    if (!result) {
      throw std::runtime_error("Failed to load OpenH264");
    }
  }
  ~OpenH264VideoEncoder() override {
    Release();
    ReleaseOpenH264();
  }

  void ForceIntraNextFrame() override { next_iframe_ = true; }

  bool InitEncode() override {
    Release();

    if (create_encoder_(&encoder_) != 0) {
      return false;
    }

    // https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/video_coding/codecs/h264/h264_encoder_impl.cc
    // から必要な部分を持ってきている
    SEncParamExt encoder_params;
    encoder_->GetDefaultParams(&encoder_params);
    encoder_params.iUsageType = CAMERA_VIDEO_REAL_TIME;
    encoder_params.iPicWidth = 640;
    encoder_params.iPicHeight = 480;
    encoder_params.iTargetBitrate = 100 * 1000;
    // Keep unspecified. WebRTC's max codec bitrate is not the same setting
    // as OpenH264's iMaxBitrate. More details in https://crbug.com/webrtc/11543
    encoder_params.iMaxBitrate = UNSPECIFIED_BIT_RATE;
    // Rate Control mode
    encoder_params.iRCMode = RC_BITRATE_MODE;
    encoder_params.fMaxFrameRate = 30;

    // The following parameters are extension parameters (they're in SEncParamExt,
    // not in SEncParamBase).
    encoder_params.bEnableFrameSkip = true;
    // |uiIntraPeriod|    - multiple of GOP size
    // |keyFrameInterval| - number of frames
    encoder_params.uiIntraPeriod = 10 * 60;
    // Reuse SPS id if possible. This helps to avoid reset of chromium HW decoder
    // on each key-frame.
    // Note that WebRTC resets encoder on resolution change which makes all
    // EParameterSetStrategy modes except INCREASING_ID (default) essentially
    // equivalent to CONSTANT_ID.
    encoder_params.eSpsPpsIdStrategy = SPS_LISTING;
    encoder_params.uiMaxNalSize = 0;
    // Threading model: use auto.
    //  0: auto (dynamic imp. internal encoder)
    //  1: single thread (default value)
    // >1: number of threads
    encoder_params.iMultipleThreadIdc = 1;
    // The base spatial layer 0 is the only one we use.
    encoder_params.sSpatialLayers[0].iVideoWidth = encoder_params.iPicWidth;
    encoder_params.sSpatialLayers[0].iVideoHeight = encoder_params.iPicHeight;
    encoder_params.sSpatialLayers[0].fFrameRate = encoder_params.fMaxFrameRate;
    encoder_params.sSpatialLayers[0].iSpatialBitrate =
        encoder_params.iTargetBitrate;
    encoder_params.sSpatialLayers[0].iMaxSpatialBitrate =
        encoder_params.iMaxBitrate;
    encoder_params.iTemporalLayerNum =
        1;  // configurations_[i].num_temporal_layers;
    if (encoder_params.iTemporalLayerNum > 1) {
      encoder_params.iNumRefFrame = 1;
    }
    //RTC_LOG(LS_INFO) << "OpenH264 version is " << OPENH264_MAJOR << "."
    //                 << OPENH264_MINOR;
    // When uiSliceMode = SM_FIXEDSLCNUM_SLICE, uiSliceNum = 0 means auto
    // design it with cpu core number.
    // TODO(sprang): Set to 0 when we understand why the rate controller borks
    //               when uiSliceNum > 1.
    encoder_params.sSpatialLayers[0].sSliceArgument.uiSliceNum = 1;
    encoder_params.sSpatialLayers[0].sSliceArgument.uiSliceMode =
        SM_FIXEDSLCNUM_SLICE;

    // Initialize.
    if (encoder_->InitializeExt(&encoder_params) != 0) {
      //RTC_LOG(LS_ERROR) << "Failed to initialize OpenH264 encoder";
      Release();
      //ReportError();
      return false;
    }
    // TODO(pbos): Base init params on these values before submitting.
    int video_format = EVideoFormatType::videoFormatI420;
    encoder_->SetOption(ENCODER_OPTION_DATAFORMAT, &video_format);

    return true;
  }

  void SetEncodeCallback(
      std::function<void(const EncodedImage&)> callback) override {
    callback_ = callback;
  }

  void Encode(const VideoFrame& frame) override {
    SSourcePicture pic = {};
    pic.iPicWidth = 640;
    pic.iPicHeight = 480;
    pic.iColorFormat = EVideoFormatType::videoFormatI420;
    pic.uiTimeStamp = frame.timestamp.count() / 1000;
    pic.iStride[0] = frame.i420_buffer->stride_y;
    pic.iStride[1] = frame.i420_buffer->stride_u;
    pic.iStride[2] = frame.i420_buffer->stride_v;
    pic.pData[0] = frame.i420_buffer->y.get();
    pic.pData[1] = frame.i420_buffer->u.get();
    pic.pData[2] = frame.i420_buffer->v.get();

    bool send_key_frame = next_iframe_;
    next_iframe_ = false;
    if (send_key_frame) {
      encoder_->ForceIntraFrame(true);
    }

    SFrameBSInfo info;
    memset(&info, 0, sizeof(SFrameBSInfo));

    int enc_ret = encoder_->EncodeFrame(&pic, &info);
    if (enc_ret != 0) {
      PLOG_ERROR << "OpenH264 frame encoding failed, EncodeFrame returned "
                 << enc_ret << ".";
      return;
    }

    // SFrameBSInfo から EncodedImage にコピーする
    EncodedImage encoded;
    int size = 0;
    for (int i = 0; i < info.iLayerNum; ++i) {
      const SLayerBSInfo& layer = info.sLayerInfo[i];
      for (int j = 0; j < layer.iNalCount; ++j) {
        size += layer.pNalLengthInByte[j];
      }
    }
    encoded.buf.reset(new uint8_t[size]);
    encoded.size = size;
    int offset = 0;
    for (int i = 0; i < info.iLayerNum; ++i) {
      const SLayerBSInfo& layer = info.sLayerInfo[i];
      int n = 0;
      for (int j = 0; j < layer.iNalCount; ++j) {
        n += layer.pNalLengthInByte[j];
      }
      memcpy(encoded.buf.get() + offset, layer.pBsBuf, n);
      offset += n;
    }
    encoded.timestamp = frame.timestamp;

    callback_(encoded);
  }

  void Release() override {
    if (encoder_) {
      destroy_encoder_(encoder_);
      encoder_ = nullptr;
    }
  }

 private:
  bool InitOpenH264(const std::string& openh264) {
    void* handle = ::dlopen(openh264.c_str(), RTLD_LAZY);
    if (handle == nullptr) {
      PLOG_ERROR << "Failed to dlopen: error=" << dlerror();
      return false;
    }
    create_encoder_ =
        (CreateEncoderFunc)::dlsym(handle, "WelsCreateSVCEncoder");
    if (create_encoder_ == nullptr) {
      ::dlclose(handle);
      return false;
    }
    destroy_encoder_ =
        (DestroyEncoderFunc)::dlsym(handle, "WelsDestroySVCEncoder");
    if (destroy_encoder_ == nullptr) {
      ::dlclose(handle);
      return false;
    }
    openh264_handle_ = handle;
    return true;
  }
  void ReleaseOpenH264() {
    if (openh264_handle_ != nullptr) {
      ::dlclose(openh264_handle_);
      openh264_handle_ = nullptr;
    }
  }

 private:
  ISVCEncoder* encoder_ = nullptr;

  std::function<void(const EncodedImage&)> callback_;

  std::atomic<bool> next_iframe_;

  void* openh264_handle_ = nullptr;
  using CreateEncoderFunc = int (*)(ISVCEncoder**);
  using DestroyEncoderFunc = void (*)(ISVCEncoder*);
  CreateEncoderFunc create_encoder_ = nullptr;
  DestroyEncoderFunc destroy_encoder_ = nullptr;
};

std::shared_ptr<H264VideoEncoder> CreateOpenH264VideoEncoder(
    const std::string& openh264) {
  return std::make_shared<OpenH264VideoEncoder>(openh264);
}

}  // namespace sorac
