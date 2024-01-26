#include "sorac/sorac.h"

#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <variant>

// plog
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Log.h>

#include "sorac/signaling.hpp"
#include "soracp.json.c.hpp"

namespace sorac {

class CPointer {
 public:
  template <class T>
  void* Add(std::shared_ptr<T> ptr, std::unordered_map<intptr_t, T*>& map) {
    if (ptr == nullptr) {
      return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex);
    auto it = ref_map.find(ptr.get());
    if (it == ref_map.end()) {
      // このポインタは存在してないので新しく登録
      intptr_t id = ++last_id;
      map[id] = ptr.get();
      ref_map[ptr.get()].count = 1;
      ref_map[ptr.get()].id = id;
      ref_map[ptr.get()].ptr = ptr;
      ref_map[ptr.get()].wp = ptr;
      return (void*)id;
    } else {
      // 既に存在してるので参照カウントを増やして ptr を参照する
      it->second.count += 1;
      it->second.ptr = ptr;
      it->second.wp = ptr;
      return (void*)it->second.id;
    }
  }

  template <class T>
  void Remove(void* p, std::unordered_map<intptr_t, T*>& map) {
    std::lock_guard<std::mutex> lock(mutex);
    // 参照カウントを減らして、0になったら削除
    intptr_t id = (intptr_t)p;
    auto it = map.find(id);
    if (it == map.end()) {
      return;
    }
    // 絶対存在するはず
    RefInfo& ref = ref_map.at(it->second);
    ref.count -= 1;
    if (ref.count == 0) {
      ref_map.erase(it->second);
      map.erase(it);
    }
    // ついでに参照の切れている要素も削除
    {
      auto it = ref_map.begin();
      while (it != ref_map.end()) {
        if (it->second.wp.expired()) {
          auto it2 = map.find(it->second.id);
          if (it2 != map.end()) {
            map.erase(it2);
          }
          it = ref_map.erase(it);
          continue;
        } else {
          ++it;
        }
      }
    }
  }

  template <class T>
  std::shared_ptr<T> Get(void* p, std::unordered_map<intptr_t, T*>& map) const {
    std::lock_guard<std::mutex> lock(mutex);
    intptr_t id = (intptr_t)p;
    auto it = map.find(id);
    if (it == map.end()) {
      return nullptr;
    }
    return std::static_pointer_cast<T>(ref_map.at(it->second).wp.lock());
  }

  template <class T>
  void* Ref(std::shared_ptr<T> ptr, std::unordered_map<intptr_t, T*>& map) {
    // Add と同じような実装だけど、参照カウントは増やさないし、ptr を強参照しない
    if (ptr == nullptr) {
      return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex);
    auto it = ref_map.find(ptr.get());
    if (it == ref_map.end()) {
      // このポインタは存在してないので新しく登録
      intptr_t id = ++last_id;
      map[id] = ptr.get();
      ref_map[ptr.get()].count = 0;
      ref_map[ptr.get()].id = id;
      ref_map[ptr.get()].wp = ptr;
      return (void*)id;
    } else {
      // 既に存在してる
      return (void*)it->second.id;
    }
  }

  // 参照カウントを増やす
  template <class T>
  void* Share(void* p, std::unordered_map<intptr_t, T*>& map) {
    intptr_t id = (intptr_t)p;
    if (id == 0) {
      return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex);
    auto it = map.find(id);
    if (it == map.end()) {
      return nullptr;
    }
    // 絶対存在するはず
    RefInfo& ref = ref_map.at(it->second);
    auto ptr = ref.wp.lock();
    if (ptr == nullptr) {
      // 既に参照先のオブジェクトが消えてた
      return nullptr;
    }
    ref.count += 1;
    ref.ptr = ptr;
    return (void*)ref.id;
  }

 private:
  struct RefInfo {
    int count;
    intptr_t id;
    std::weak_ptr<void> wp;
    std::shared_ptr<void> ptr;
  };
  std::map<void*, RefInfo> ref_map;
  mutable std::mutex mutex;
  intptr_t last_id = 0;
};

CPointer g_cptr;
std::unordered_map<intptr_t, Signaling*> g_signaling_map;
std::unordered_map<intptr_t, VideoFrameBufferI420*>
    g_video_frame_buffer_i420_map;
std::unordered_map<intptr_t, VideoFrameBufferNV12*>
    g_video_frame_buffer_nv12_map;
std::unordered_map<intptr_t, rtc::Track*> g_track_map;
std::unordered_map<intptr_t, rtc::Description::Media*> g_description_media_map;
std::unordered_map<intptr_t, sorac::DataChannel*> g_data_channel_map;

void CopyString(std::string s, char* buf, int size, SoracError* error) {
  if (buf == nullptr) {
    if (error != nullptr) {
      error->type = SORAC_ERROR_OK;
      error->buffer_size = int(s.size() + 1);
    }
    return;
  }

  if (size < int(s.size() + 1)) {
    if (error != nullptr) {
      error->type = SORAC_ERROR_TOO_SMALL;
      error->buffer_size = int(s.size() + 1);
    }
    if (size > 0) {
      buf[0] = '\0';
    }
    return;
  }

  memcpy(buf, s.data(), s.size());
  buf[s.size()] = '\0';
  if (error != nullptr) {
    error->type = SORAC_ERROR_OK;
    error->buffer_size = int(s.size() + 1);
  }
}

}  // namespace sorac

extern "C" {

using sorac::g_cptr;
using sorac::g_data_channel_map;
using sorac::g_description_media_map;
using sorac::g_signaling_map;
using sorac::g_track_map;
using sorac::g_video_frame_buffer_i420_map;
using sorac::g_video_frame_buffer_nv12_map;

// VideoFrameBufferI420
SoracVideoFrameBufferI420* sorac_video_frame_buffer_i420_create(int width,
                                                                int height) {
  auto p = sorac::VideoFrameBufferI420::Create(width, height);
  return (SoracVideoFrameBufferI420*)g_cptr.Add(p,
                                                g_video_frame_buffer_i420_map);
}
void sorac_video_frame_buffer_i420_release(SoracVideoFrameBufferI420* p) {
  g_cptr.Remove(p, g_video_frame_buffer_i420_map);
}
extern SoracVideoFrameBufferI420* sorac_video_frame_buffer_i420_share(
    SoracVideoFrameBufferI420* p) {
  return (SoracVideoFrameBufferI420*)g_cptr.Share(
      p, g_video_frame_buffer_i420_map);
}
int sorac_video_frame_buffer_i420_get_width(SoracVideoFrameBufferI420* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_i420_map);
  return video_frame_buffer->width;
}
int sorac_video_frame_buffer_i420_get_height(SoracVideoFrameBufferI420* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_i420_map);
  return video_frame_buffer->height;
}
uint8_t* sorac_video_frame_buffer_i420_get_y(SoracVideoFrameBufferI420* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_i420_map);
  return video_frame_buffer->y.get();
}
int sorac_video_frame_buffer_i420_get_stride_y(SoracVideoFrameBufferI420* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_i420_map);
  return video_frame_buffer->stride_y;
}
uint8_t* sorac_video_frame_buffer_i420_get_u(SoracVideoFrameBufferI420* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_i420_map);
  return video_frame_buffer->u.get();
}
int sorac_video_frame_buffer_i420_get_stride_u(SoracVideoFrameBufferI420* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_i420_map);
  return video_frame_buffer->stride_u;
}
uint8_t* sorac_video_frame_buffer_i420_get_v(SoracVideoFrameBufferI420* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_i420_map);
  return video_frame_buffer->v.get();
}
int sorac_video_frame_buffer_i420_get_stride_v(SoracVideoFrameBufferI420* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_i420_map);
  return video_frame_buffer->stride_v;
}

// VideoFrameBufferNV12
SoracVideoFrameBufferNV12* sorac_video_frame_buffer_nv12_create(int width,
                                                                int height) {
  auto p = sorac::VideoFrameBufferNV12::Create(width, height);
  return (SoracVideoFrameBufferNV12*)g_cptr.Add(p,
                                                g_video_frame_buffer_nv12_map);
}
void sorac_video_frame_buffer_nv12_release(SoracVideoFrameBufferNV12* p) {
  g_cptr.Remove(p, g_video_frame_buffer_nv12_map);
}
extern SoracVideoFrameBufferNV12* sorac_video_frame_buffer_nv12_share(
    SoracVideoFrameBufferNV12* p) {
  return (SoracVideoFrameBufferNV12*)g_cptr.Share(
      p, g_video_frame_buffer_nv12_map);
}
int sorac_video_frame_buffer_nv12_get_width(SoracVideoFrameBufferNV12* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_nv12_map);
  return video_frame_buffer->width;
}
int sorac_video_frame_buffer_nv12_get_height(SoracVideoFrameBufferNV12* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_nv12_map);
  return video_frame_buffer->height;
}
uint8_t* sorac_video_frame_buffer_nv12_get_y(SoracVideoFrameBufferNV12* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_nv12_map);
  return video_frame_buffer->y.get();
}
int sorac_video_frame_buffer_nv12_get_stride_y(SoracVideoFrameBufferNV12* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_nv12_map);
  return video_frame_buffer->stride_y;
}
uint8_t* sorac_video_frame_buffer_nv12_get_uv(SoracVideoFrameBufferNV12* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_nv12_map);
  return video_frame_buffer->uv.get();
}
int sorac_video_frame_buffer_nv12_get_stride_uv(SoracVideoFrameBufferNV12* p) {
  auto video_frame_buffer = g_cptr.Get(p, g_video_frame_buffer_nv12_map);
  return video_frame_buffer->stride_uv;
}

// VideoFrame
SoracVideoFrameBufferI420* sorac_video_frame_ref_get_i420_buffer(
    SoracVideoFrameRef* p) {
  return (SoracVideoFrameBufferI420*)g_cptr.Ref(
      ((sorac::VideoFrame*)p)->i420_buffer, g_video_frame_buffer_i420_map);
}
SoracVideoFrameBufferNV12* sorac_video_frame_ref_get_nv12_buffer(
    SoracVideoFrameRef* p) {
  return (SoracVideoFrameBufferNV12*)g_cptr.Ref(
      ((sorac::VideoFrame*)p)->nv12_buffer, g_video_frame_buffer_nv12_map);
}
int64_t sorac_video_frame_ref_get_timestamp_us(SoracVideoFrameRef* p) {
  return ((sorac::VideoFrame*)p)->timestamp.count();
}
void sorac_video_frame_ref_set_rid(SoracVideoFrameRef* p,
                                   const char* rid,  // nullable
                                   int len) {
  if (rid == nullptr) {
    ((sorac::VideoFrame*)p)->rid = std::nullopt;
  } else {
    ((sorac::VideoFrame*)p)->rid = std::string(rid, len);
  }
}
bool sorac_video_frame_ref_get_rid(SoracVideoFrameRef* p,
                                   char* buf,
                                   int size,
                                   SoracError* error) {
  if (error != nullptr) {
    memset(error, 0, sizeof(SoracError));
  }
  auto rid = ((sorac::VideoFrame*)p)->rid;
  if (rid == std::nullopt) {
    return false;
  }
  sorac::CopyString(*rid, buf, size, error);
  return true;
}
int sorac_video_frame_ref_get_width(SoracVideoFrameRef* p) {
  return ((sorac::VideoFrame*)p)->width();
}
int sorac_video_frame_ref_get_height(SoracVideoFrameRef* p) {
  return ((sorac::VideoFrame*)p)->height();
}

// AudioFrame
extern int sorac_audio_frame_ref_get_sample_rate(SoracAudioFrameRef* p) {
  return ((sorac::AudioFrame*)p)->sample_rate;
}
extern int sorac_audio_frame_ref_get_channels(SoracAudioFrameRef* p) {
  return ((sorac::AudioFrame*)p)->channels;
}
extern float* sorac_audio_frame_ref_get_pcm(SoracAudioFrameRef* p) {
  return ((sorac::AudioFrame*)p)->pcm.get();
}
extern int sorac_audio_frame_ref_get_samples(SoracAudioFrameRef* p) {
  return ((sorac::AudioFrame*)p)->samples;
}
extern int64_t sorac_audio_frame_ref_get_timestamp_us(SoracAudioFrameRef* p) {
  return ((sorac::AudioFrame*)p)->timestamp.count();
}

// plog
void sorac_plog_init() {
  plog::init<plog::TxtFormatter>(plog::debug, plog::streamStdOut);
}

// rtc::Description::Media
void sorac_description_media_release(SoracDescriptionMedia* p) {
  g_cptr.Remove(p, g_description_media_map);
}
void sorac_description_media_get_type(SoracDescriptionMedia* p,
                                      char* buf,
                                      int size,
                                      SoracError* error) {
  if (error != nullptr) {
    memset(error, 0, sizeof(SoracError));
  }
  auto description = g_cptr.Get(p, g_description_media_map);
  auto type = description->type();
  sorac::CopyString(type, buf, size, error);
}

// rtc::Track
void sorac_track_release(SoracTrack* p) {
  g_cptr.Remove(p, g_track_map);
}
SoracTrack* sorac_track_share(SoracTrack* p) {
  return (SoracTrack*)g_cptr.Share(p, g_track_map);
}
SoracDescriptionMedia* sorac_track_clone_description(SoracTrack* p) {
  auto track = g_cptr.Get(p, g_track_map);
  auto description =
      std::make_shared<rtc::Description::Media>(track->description());
  return (SoracDescriptionMedia*)g_cptr.Add(description,
                                            g_description_media_map);
}

// sorac::DataChannel
void sorac_data_channel_release(SoracDataChannel* p) {
  g_cptr.Remove(p, g_data_channel_map);
}
SoracDataChannel* sorac_data_channel_share(SoracDataChannel* p) {
  return (SoracDataChannel*)g_cptr.Share(p, g_data_channel_map);
}
void sorac_data_channel_set_on_open(SoracDataChannel* p,
                                    sorac_data_channel_on_open_func on_open,
                                    void* userdata) {
  auto data_channel = g_cptr.Get(p, g_data_channel_map);
  data_channel->SetOnOpen([on_open, userdata]() { on_open(userdata); });
}
void sorac_data_channel_set_on_available(
    SoracDataChannel* p,
    sorac_data_channel_on_available_func on_available,
    void* userdata) {
  auto data_channel = g_cptr.Get(p, g_data_channel_map);
  data_channel->SetOnAvailable(
      [on_available, userdata]() { on_available(userdata); });
}
void sorac_data_channel_set_on_closed(
    SoracDataChannel* p,
    sorac_data_channel_on_closed_func on_closed,
    void* userdata) {
  auto data_channel = g_cptr.Get(p, g_data_channel_map);
  data_channel->SetOnClosed([on_closed, userdata]() { on_closed(userdata); });
}
void sorac_data_channel_set_on_error(SoracDataChannel* p,
                                     sorac_data_channel_on_error_func on_error,
                                     void* userdata) {
  auto data_channel = g_cptr.Get(p, g_data_channel_map);
  data_channel->SetOnError([on_error, userdata](const std::string& message) {
    on_error(message.c_str(), (int)message.size(), userdata);
  });
}
void sorac_data_channel_set_on_message(
    SoracDataChannel* p,
    sorac_data_channel_on_message_func on_message,
    void* userdata) {
  auto data_channel = g_cptr.Get(p, g_data_channel_map);
  data_channel->SetOnMessage(
      [on_message, userdata](const uint8_t* buf, size_t size) {
        on_message(buf, size, userdata);
      });
}
void sorac_data_channel_get_label(SoracDataChannel* p,
                                  char* buf,
                                  int size,
                                  SoracError* error) {
  if (error != nullptr) {
    memset(error, 0, sizeof(SoracError));
  }
  auto data_channel = g_cptr.Get(p, g_data_channel_map);
  auto label = data_channel->GetLabel();
  sorac::CopyString(label, buf, size, error);
}
bool sorac_data_channel_send(SoracDataChannel* p,
                             const uint8_t* buf,
                             size_t size) {
  rtc::message_variant message;
  auto data_channel = g_cptr.Get(p, g_data_channel_map);
  return data_channel->Send(buf, size);
}

// Signaling
SoracSignaling* sorac_signaling_create(const soracp_SignalingConfig* config) {
  auto p = sorac::CreateSignaling(soracp_SignalingConfig_to_cpp(config));
  return (SoracSignaling*)g_cptr.Add(p, g_signaling_map);
}
void sorac_signaling_release(SoracSignaling* p) {
  g_cptr.Remove(p, g_signaling_map);
}
void sorac_signaling_connect(SoracSignaling* p,
                             const soracp_SoraConnectConfig* sora_config) {
  auto signaling = g_cptr.Get(p, g_signaling_map);
  signaling->Connect(soracp_SoraConnectConfig_to_cpp(sora_config));
}
void sorac_signaling_set_on_track(SoracSignaling* p,
                                  sorac_signaling_on_track_func on_track,
                                  void* userdata) {
  auto signaling = g_cptr.Get(p, g_signaling_map);
  signaling->SetOnTrack(
      [on_track, userdata](std::shared_ptr<rtc::Track> track) {
        auto ctrack = (SoracTrack*)g_cptr.Ref(track, g_track_map);
        on_track(ctrack, userdata);
      });
}
void sorac_signaling_send_video_frame(SoracSignaling* p,
                                      SoracVideoFrameRef* frame) {
  auto signaling = g_cptr.Get(p, g_signaling_map);
  signaling->SendVideoFrame(*((sorac::VideoFrame*)frame));
}
void sorac_signaling_send_audio_frame(SoracSignaling* p,
                                      SoracAudioFrameRef* frame) {
  auto signaling = g_cptr.Get(p, g_signaling_map);
  signaling->SendAudioFrame(*((sorac::AudioFrame*)frame));
}
void sorac_signaling_set_on_data_channel(
    SoracSignaling* p,
    sorac_signaling_on_data_channel_func on_data_channel,
    void* userdata) {
  auto signaling = g_cptr.Get(p, g_signaling_map);
  signaling->SetOnDataChannel(
      [on_data_channel,
       userdata](std::shared_ptr<sorac::DataChannel> data_channel) {
        auto cdatachannel =
            (SoracDataChannel*)g_cptr.Ref(data_channel, g_data_channel_map);
        on_data_channel(cdatachannel, userdata);
      });
}
void sorac_signaling_set_on_notify(SoracSignaling* p,
                                   sorac_signaling_on_notify_func on_notify,
                                   void* userdata) {
  auto signaling = g_cptr.Get(p, g_signaling_map);
  signaling->SetOnNotify([on_notify, userdata](const std::string& message) {
    on_notify(message.c_str(), (int)message.size(), userdata);
  });
}
void sorac_signaling_set_on_push(SoracSignaling* p,
                                 sorac_signaling_on_push_func on_push,
                                 void* userdata) {
  auto signaling = g_cptr.Get(p, g_signaling_map);
  signaling->SetOnPush([on_push, userdata](const std::string& message) {
    on_push(message.c_str(), (int)message.size(), userdata);
  });
}
void sorac_signaling_get_rtp_encoding_parameters(
    SoracSignaling* p,
    soracp_RtpEncodingParameters* params) {
  auto signaling = g_cptr.Get(p, g_signaling_map);
  auto u = signaling->GetRtpEncodingParameters();
  soracp_RtpEncodingParameters_from_cpp(u, params);
}
}
