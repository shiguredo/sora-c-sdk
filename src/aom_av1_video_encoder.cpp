#include "sorac/aom_av1_video_encoder.hpp"

#include <string.h>
#include <atomic>
#include <exception>

// Linux
#include <dlfcn.h>

// plog
#include <plog/Log.h>

// AOM
#include <aom/aom_codec.h>
#include <aom/aom_encoder.h>
#include <aom/aomcx.h>

// text の定義を全て展開した上で文字列化する。
// 単純に #text とした場合、全て展開する前に文字列化されてしまう
#if defined(_WIN32)
#define SORAC_STRINGIZE(text) SORAC_STRINGIZE_((text))
#define SORAC_STRINGIZE_(x) SORAC_STRINGIZE_I x
#else
#define SORAC_STRINGIZE(x) SORAC_STRINGIZE_I(x)
#endif

#define SORAC_STRINGIZE_I(text) #text

// a と b の定義を全て展開した上で結合する
// 単純に a ## b とした場合、全て展開する前に結合されてしまう
#define SORAC_CAT(a, b) SORAC_CAT_I(a, b)

#if defined(_WIN32)
#define SORAC_CAT_I(a, b) a##b
#else
#define SORAC_CAT_I(a, b) SORAC_CAT_II(a##b)
#define SORAC_CAT_II(res) res
#endif

namespace sorac {

class AomAv1VideoEncoder : public VideoEncoder {
 public:
  AomAv1VideoEncoder(const std::string& aom) {
    bool result = InitAom(aom);
    if (!result) {
      throw std::runtime_error("Failed to load AOM");
    }
  }
  ~AomAv1VideoEncoder() override {
    Release();
    ReleaseAom();
  }

  void ForceIntraNextFrame() override { next_iframe_ = true; }

  bool InitEncode(const Settings& settings) override {
    Release();

    PLOG_INFO << "AOM InitEncode";

    // https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/modules/video_coding/codecs/av1/libaom_av1_encoder.cc
    // を参考に初期化やエンコードを行う

    aom_codec_err_t ret = aom_codec_enc_config_default_(
        aom_codec_av1_cx_(), &cfg_, AOM_USAGE_REALTIME);
    if (ret != AOM_CODEC_OK) {
      PLOG_ERROR << "Failed to aom_codec_enc_config_default: ret=" << ret;
      return false;
    }

    // Overwrite default config with input encoder settings & RTC-relevant values.
    cfg_.g_w = settings.width;
    cfg_.g_h = settings.height;
    cfg_.g_threads = 8;
    cfg_.g_timebase.num = 1;
    cfg_.g_timebase.den = 90000;
    cfg_.rc_target_bitrate = settings.bitrate.count();
    cfg_.rc_dropframe_thresh = 0;
    cfg_.g_input_bit_depth = 8;
    cfg_.kf_mode = AOM_KF_DISABLED;
    cfg_.rc_min_quantizer = 10;
    cfg_.rc_max_quantizer = 63;
    cfg_.rc_undershoot_pct = 50;
    cfg_.rc_overshoot_pct = 50;
    cfg_.rc_buf_initial_sz = 600;
    cfg_.rc_buf_optimal_sz = 600;
    cfg_.rc_buf_sz = 1000;
    cfg_.g_usage = AOM_USAGE_REALTIME;
    cfg_.g_error_resilient = 0;
    // Low-latency settings.
    cfg_.rc_end_usage = AOM_CBR;    // Constant Bit Rate (CBR) mode
    cfg_.g_pass = AOM_RC_ONE_PASS;  // One-pass rate control
    cfg_.g_lag_in_frames = 0;       // No look ahead when lag equals 0.

    if (frame_for_encode_ != nullptr) {
      aom_img_free_(frame_for_encode_);
      frame_for_encode_ = nullptr;
    }

    // Flag options: AOM_CODEC_USE_PSNR and AOM_CODEC_USE_HIGHBITDEPTH
    aom_codec_flags_t flags = 0;

    // Initialize an encoder instance.
    ret = aom_codec_enc_init_ver_(&ctx_, aom_codec_av1_cx_(), &cfg_, flags,
                                  AOM_ENCODER_ABI_VERSION);
    if (ret != AOM_CODEC_OK) {
      PLOG_ERROR << "Failed to aom_codec_enc_init_ver: ret=" << ret;
      return false;
    }
    init_ctx_ = true;

#define SET_PARAM(param_id, param_value)                       \
  do {                                                         \
    ret = aom_codec_control_(&ctx_, param_id, param_value);    \
    if (ret != AOM_CODEC_OK) {                                 \
      PLOG_ERROR << "Failed to aom_codec_control: ret=" << ret \
                 << ", param_id=" << SORAC_STRINGIZE(param_id) \
                 << ", param_value=" << param_value;           \
      return false;                                            \
    }                                                          \
  } while (0)

    // Set control parameters
    SET_PARAM(AOME_SET_CPUUSED, 10);
    SET_PARAM(AV1E_SET_ENABLE_CDEF, 1);
    SET_PARAM(AV1E_SET_ENABLE_TPL_MODEL, 0);
    SET_PARAM(AV1E_SET_DELTAQ_MODE, 0);
    SET_PARAM(AV1E_SET_ENABLE_ORDER_HINT, 0);
    SET_PARAM(AV1E_SET_AQ_MODE, 3);
    SET_PARAM(AOME_SET_MAX_INTRA_BITRATE_PCT, 300);
    SET_PARAM(AV1E_SET_COEFF_COST_UPD_FREQ, 3);
    SET_PARAM(AV1E_SET_MODE_COST_UPD_FREQ, 3);
    SET_PARAM(AV1E_SET_MV_COST_UPD_FREQ, 3);

    SET_PARAM(AV1E_SET_ENABLE_PALETTE, 0);

    SET_PARAM(AV1E_SET_TILE_ROWS, 1);
    SET_PARAM(AV1E_SET_TILE_COLUMNS, 2);

    SET_PARAM(AV1E_SET_ROW_MT, 1);
    SET_PARAM(AV1E_SET_ENABLE_OBMC, 0);
    SET_PARAM(AV1E_SET_NOISE_SENSITIVITY, 0);
    SET_PARAM(AV1E_SET_ENABLE_WARPED_MOTION, 0);
    SET_PARAM(AV1E_SET_ENABLE_GLOBAL_MOTION, 0);
    SET_PARAM(AV1E_SET_ENABLE_REF_FRAME_MVS, 0);
    SET_PARAM(AV1E_SET_SUPERBLOCK_SIZE, AOM_SUPERBLOCK_SIZE_DYNAMIC);
    SET_PARAM(AV1E_SET_ENABLE_CFL_INTRA, 0);
    SET_PARAM(AV1E_SET_ENABLE_SMOOTH_INTRA, 0);
    SET_PARAM(AV1E_SET_ENABLE_ANGLE_DELTA, 0);
    SET_PARAM(AV1E_SET_ENABLE_FILTER_INTRA, 0);
    SET_PARAM(AV1E_SET_INTRA_DEFAULT_TX_ONLY, 1);
    SET_PARAM(AV1E_SET_DISABLE_TRELLIS_QUANT, 1);
    SET_PARAM(AV1E_SET_ENABLE_DIST_WTD_COMP, 0);
    SET_PARAM(AV1E_SET_ENABLE_DIFF_WTD_COMP, 0);
    SET_PARAM(AV1E_SET_ENABLE_DUAL_FILTER, 0);
    SET_PARAM(AV1E_SET_ENABLE_INTERINTRA_COMP, 0);
    SET_PARAM(AV1E_SET_ENABLE_INTERINTRA_WEDGE, 0);
    SET_PARAM(AV1E_SET_ENABLE_INTRA_EDGE_FILTER, 0);
    SET_PARAM(AV1E_SET_ENABLE_INTRABC, 0);
    SET_PARAM(AV1E_SET_ENABLE_MASKED_COMP, 0);
    SET_PARAM(AV1E_SET_ENABLE_PAETH_INTRA, 0);
    SET_PARAM(AV1E_SET_ENABLE_QM, 0);
    SET_PARAM(AV1E_SET_ENABLE_RECT_PARTITIONS, 0);
    SET_PARAM(AV1E_SET_ENABLE_RESTORATION, 0);
    SET_PARAM(AV1E_SET_ENABLE_SMOOTH_INTERINTRA, 0);
    SET_PARAM(AV1E_SET_ENABLE_TX64, 0);
    SET_PARAM(AV1E_SET_MAX_REFERENCE_FRAMES, 3);

    return true;
  }

  void SetEncodeCallback(
      std::function<void(const EncodedImage&)> callback) override {
    callback_ = callback;
  }

  void Encode(const VideoFrame& frame) override {
    if (frame.i420_buffer == nullptr && frame.nv12_buffer == nullptr) {
      PLOG_ERROR << "Unknown video frame format";
      return;
    }
    aom_img_fmt_t fmt =
        frame.i420_buffer != nullptr ? AOM_IMG_FMT_I420 : AOM_IMG_FMT_NV12;

    if (frame_for_encode_ == nullptr || frame_for_encode_->fmt != fmt) {
      if (frame_for_encode_ != nullptr) {
        aom_img_free_(frame_for_encode_);
      }
      frame_for_encode_ =
          aom_img_wrap_(nullptr, fmt, cfg_.g_w, cfg_.g_h, 1, nullptr);
    }

    if (frame.i420_buffer != nullptr) {
      // I420
      frame_for_encode_->planes[AOM_PLANE_Y] = frame.i420_buffer->y.get();
      frame_for_encode_->planes[AOM_PLANE_U] = frame.i420_buffer->u.get();
      frame_for_encode_->planes[AOM_PLANE_V] = frame.i420_buffer->v.get();
      frame_for_encode_->stride[AOM_PLANE_Y] = frame.i420_buffer->stride_y;
      frame_for_encode_->stride[AOM_PLANE_U] = frame.i420_buffer->stride_u;
      frame_for_encode_->stride[AOM_PLANE_V] = frame.i420_buffer->stride_v;
    } else {
      // NV12
      frame_for_encode_->planes[AOM_PLANE_Y] = frame.nv12_buffer->y.get();
      frame_for_encode_->planes[AOM_PLANE_U] = frame.nv12_buffer->uv.get();
      frame_for_encode_->planes[AOM_PLANE_V] = nullptr;
      frame_for_encode_->stride[AOM_PLANE_Y] = frame.nv12_buffer->stride_y;
      frame_for_encode_->stride[AOM_PLANE_U] = frame.nv12_buffer->stride_uv;
      frame_for_encode_->stride[AOM_PLANE_V] = 0;
    }

    const uint32_t duration = 90000 / 30;
    timestamp_ += duration;

    aom_enc_frame_flags_t flags = 0;

    bool send_key_frame = next_iframe_.exchange(false);
    if (send_key_frame) {
      PLOG_DEBUG << "KeyFrame generated";
      flags = AOM_EFLAG_FORCE_KF;
    }

    aom_codec_err_t ret = aom_codec_encode_(&ctx_, frame_for_encode_,
                                            timestamp_, duration, flags);

    EncodedImage encoded;
    const aom_codec_cx_pkt_t* pkt = nullptr;
    aom_codec_iter_t iter = nullptr;
    while (true) {
      const aom_codec_cx_pkt_t* p = aom_codec_get_cx_data_(&ctx_, &iter);
      if (p == nullptr) {
        break;
      }
      if (p->kind == AOM_CODEC_CX_FRAME_PKT && p->data.frame.sz > 0) {
        pkt = p;
      }
    }

    encoded.buf.reset(new uint8_t[pkt->data.frame.sz]);
    encoded.size = pkt->data.frame.sz;
    memcpy(encoded.buf.get(), pkt->data.frame.buf, encoded.size);
    encoded.timestamp = frame.timestamp;

    callback_(encoded);
  }

  void Release() override {
    if (frame_for_encode_ != nullptr) {
      aom_img_free_(frame_for_encode_);
      frame_for_encode_ = nullptr;
    }
    if (init_ctx_) {
      aom_codec_destroy_(&ctx_);
      init_ctx_ = false;
    }
  }

 private:
  bool InitAom(const std::string& aom) {
    void* handle = ::dlopen(aom.c_str(), RTLD_LAZY);
    if (handle == nullptr) {
      PLOG_ERROR << "Failed to dlopen: error=" << dlerror();
      return false;
    }

#define LOAD_AOM(name)                                                \
  SORAC_CAT(name, _) =                                                \
      (SORAC_CAT(name, _func))::dlsym(handle, SORAC_STRINGIZE(name)); \
  if (SORAC_CAT(name, _) == nullptr) {                                \
    PLOG_ERROR << "Failed to dlsym: name=" << SORAC_STRINGIZE(name);  \
    ::dlclose(handle);                                                \
    return false;                                                     \
  }

    LOAD_AOM(aom_codec_av1_cx);
    LOAD_AOM(aom_codec_enc_config_default);
    LOAD_AOM(aom_codec_enc_init_ver);
    LOAD_AOM(aom_codec_destroy);
    LOAD_AOM(aom_codec_encode);
    LOAD_AOM(aom_codec_get_cx_data);
    LOAD_AOM(aom_codec_control);
    LOAD_AOM(aom_codec_enc_config_set);
    LOAD_AOM(aom_img_wrap);
    LOAD_AOM(aom_img_free);
    aom_handle_ = handle;
    return true;
  }
  void ReleaseAom() {
    if (aom_handle_ != nullptr) {
      ::dlclose(aom_handle_);
      aom_handle_ = nullptr;
    }
  }

 private:
  bool init_ctx_ = false;
  aom_codec_ctx_t ctx_;
  aom_codec_enc_cfg_t cfg_;
  aom_image_t* frame_for_encode_;
  int64_t timestamp_ = 0;

  std::function<void(const EncodedImage&)> callback_;

  std::atomic<bool> next_iframe_;

  void* aom_handle_ = nullptr;

#define DECLARE_AOM(name, result, ...)                    \
  using SORAC_CAT(name, _func) = result (*)(__VA_ARGS__); \
  SORAC_CAT(name, _func) SORAC_CAT(name, _);
  // clang-format off
  DECLARE_AOM(aom_codec_av1_cx, aom_codec_iface_t*, void);
  DECLARE_AOM(aom_codec_enc_config_default, aom_codec_err_t, aom_codec_iface_t* iface, aom_codec_enc_cfg_t* cfg, unsigned int usage);
  DECLARE_AOM(aom_codec_enc_init_ver, aom_codec_err_t, aom_codec_ctx_t* ctx, aom_codec_iface_t* iface, const aom_codec_enc_cfg_t* cfg, aom_codec_flags_t flags, int ver);
  DECLARE_AOM(aom_codec_destroy, aom_codec_err_t, aom_codec_ctx_t* ctx);
  DECLARE_AOM(aom_codec_encode, aom_codec_err_t, aom_codec_ctx_t* ctx, const aom_image_t* img, aom_codec_pts_t pts, unsigned long duration, aom_enc_frame_flags_t flags);
  DECLARE_AOM(aom_codec_get_cx_data, const aom_codec_cx_pkt_t*, aom_codec_ctx_t* ctx, aom_codec_iter_t* iter);
  DECLARE_AOM(aom_codec_control, aom_codec_err_t, aom_codec_ctx_t* ctx, int ctrl_id, ...);
  DECLARE_AOM(aom_codec_enc_config_set, aom_codec_err_t, aom_codec_ctx_t* ctx, const aom_codec_enc_cfg_t* cfg);
  DECLARE_AOM(aom_img_wrap, aom_image_t*, aom_image_t* img, aom_img_fmt_t fmt, unsigned int d_w, unsigned int d_h, unsigned int stride_align, unsigned char* img_data);
  DECLARE_AOM(aom_img_free, void, aom_image_t* img);
  // clang-format on
};

std::shared_ptr<VideoEncoder> CreateAomAv1VideoEncoder(const std::string& aom) {
  return std::make_shared<AomAv1VideoEncoder>(aom);
}

}  // namespace sorac
