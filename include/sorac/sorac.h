#ifndef SORAC_H_
#define SORAC_H_

#include "soracp.json.c.h"

#ifdef __cplusplus
extern "C" {
#endif

// 現在の方針メモ：
//
// - 全ての識別子にプリフィックス sorac, Sorac, SORAC を付ける
//
// - shared_ptr<T> で運用する前提のオブジェクトは、オブジェクトをグローバルテーブルに登録して参照カウントして ID で管理し、ユーザーにはその ID をただのポインタに見せかける
// - また、そのオブジェクトを操作する関数名には sorac_<object>_ というプリフィックスを付ける
// - shared_ptr を返す関数に関しては、weak_ptr 的なポインタを返して、必要であれば sorac_<object>_share() という関数で shared_ptr 的なオブジェクトに変換する
//   - どちらも利用者からみたらただのポインタだし、参照が有効な間は普通にアクセスが可能
//   - shared_ptr 的なオブジェクトを返すと後で release しないといけないけど、このやり方ならそれが不要になる
//
// - コピーしたオブジェクトの shared_ptr を返す場合は sorac_<object>_clone_ というプリフィックスを付ける
// - これは必ず sorac_<object>_release で破棄する必要がある
//
// - 参照だけあればいいやつ（VideoFrame や AudioFrame とか）に関しては <Object>Ref という名前を付けて、ID ではなく直接ポインタを管理する
// - この <Object>Ref を操作する関数名には sorac_<object>_ref_ というプリフィックスを付ける
// - 今後 shared_ptr で囲んで使いたくなった場合は sorac_<object>_get_ref() という関数を用意すれば参照に変換できるはず
//
// - エラーに関しては SoracError 構造体に設定する
// - SoracError* を引数に持つ関数では、この構造体を必ず 0 初期化する
// - この引数を NULL にすることは常に有効で、その場合は単にエラーが設定されないだけになる
//
// - 文字列の取得に関しては、char* buf, int size, SoracError* error の３つを必ず引数に含める
// - str == NULL だった場合には error.type == SORAC_ERROR_OK かつ error.buffer_size に必要なサイズが格納される
// - str != NULL かつ size が文字列を格納出来るだけのサイズが無い場合は error.type == SORAC_ERROR_TOO_SMALL かつ error.buffer_size に必要なサイズが格納される
//   - また、size > 0 である場合は buf[0] = '\0' が設定される
// - str != NULL かつ size が文字列を格納出来るだけのサイズがある場合は error.type == SORAC_ERROR_OK かつ error.SoracError::buffer_size に必要なサイズが格納される
//
// - std::chrono 系の時間単位が設定されてた値には、int64_t 値を使って、変数名や関数名に _ms, _us, _ns というポストフィックスをつける
//

typedef enum SoracErrorType {
  SORAC_ERROR_OK = 0,
  SORAC_ERROR_TOO_SMALL = -1,
} SoracErrorType;
typedef struct SoracError {
  SoracErrorType type;
  int buffer_size;
} SoracError;
typedef enum SoracMessageType {
  SORAC_MESSAGE_BINARY = 0,
  SORAC_MESSAGE_STRING = 1,
} SoracMessageType;

// VideoFrameBufferI420
struct SoracVideoFrameBufferI420;
typedef struct SoracVideoFrameBufferI420 SoracVideoFrameBufferI420;
extern SoracVideoFrameBufferI420* sorac_video_frame_buffer_i420_create(
    int width,
    int height);
extern void sorac_video_frame_buffer_i420_release(SoracVideoFrameBufferI420* p);
extern SoracVideoFrameBufferI420* sorac_video_frame_buffer_i420_share(
    SoracVideoFrameBufferI420* p);
extern int sorac_video_frame_buffer_i420_get_width(
    SoracVideoFrameBufferI420* p);
extern int sorac_video_frame_buffer_i420_get_height(
    SoracVideoFrameBufferI420* p);
extern uint8_t* sorac_video_frame_buffer_i420_get_y(
    SoracVideoFrameBufferI420* p);
extern int sorac_video_frame_buffer_i420_get_stride_y(
    SoracVideoFrameBufferI420* p);
extern uint8_t* sorac_video_frame_buffer_i420_get_u(
    SoracVideoFrameBufferI420* p);
extern int sorac_video_frame_buffer_i420_get_stride_u(
    SoracVideoFrameBufferI420* p);
extern uint8_t* sorac_video_frame_buffer_i420_get_v(
    SoracVideoFrameBufferI420* p);
extern int sorac_video_frame_buffer_i420_get_stride_v(
    SoracVideoFrameBufferI420* p);

// VideoFrameBufferNV12
struct SoracVideoFrameBufferNV12;
typedef struct SoracVideoFrameBufferNV12 SoracVideoFrameBufferNV12;
extern SoracVideoFrameBufferNV12* sorac_video_frame_buffer_nv12_create(
    int width,
    int height);
extern void sorac_video_frame_buffer_nv12_release(SoracVideoFrameBufferNV12* p);
extern SoracVideoFrameBufferNV12* sorac_video_frame_buffer_nv12_share(
    SoracVideoFrameBufferNV12* p);
extern int sorac_video_frame_buffer_nv12_get_width(
    SoracVideoFrameBufferNV12* p);
extern int sorac_video_frame_buffer_nv12_get_height(
    SoracVideoFrameBufferNV12* p);
extern uint8_t* sorac_video_frame_buffer_nv12_get_y(
    SoracVideoFrameBufferNV12* p);
extern int sorac_video_frame_buffer_nv12_get_stride_y(
    SoracVideoFrameBufferNV12* p);
extern uint8_t* sorac_video_frame_buffer_nv12_get_uv(
    SoracVideoFrameBufferNV12* p);
extern int sorac_video_frame_buffer_nv12_get_stride_uv(
    SoracVideoFrameBufferNV12* p);

// VideoFrame
struct SoracVideoFrameRef;
typedef struct SoracVideoFrameRef SoracVideoFrameRef;
extern SoracVideoFrameBufferI420* sorac_video_frame_ref_get_i420_buffer(
    SoracVideoFrameRef* p);
extern SoracVideoFrameBufferNV12* sorac_video_frame_ref_get_nv12_buffer(
    SoracVideoFrameRef* p);
extern int64_t sorac_video_frame_ref_get_timestamp_us(SoracVideoFrameRef* p);
extern void sorac_video_frame_ref_set_rid(SoracVideoFrameRef* p,
                                          const char* rid,  // nullable
                                          int len);
// rid が nullopt なら false が返される
extern bool sorac_video_frame_ref_get_rid(SoracVideoFrameRef* p,
                                          char* buf,
                                          int size,
                                          SoracError* error);
extern int sorac_video_frame_ref_get_width(SoracVideoFrameRef* p);
extern int sorac_video_frame_ref_get_height(SoracVideoFrameRef* p);

// AudioFrame
struct SoracAudioFrameRef;
typedef struct SoracAudioFrameRef SoracAudioFrameRef;
extern int sorac_audio_frame_ref_get_sample_rate(SoracAudioFrameRef* p);
extern int sorac_audio_frame_ref_get_channels(SoracAudioFrameRef* p);
extern float* sorac_audio_frame_ref_get_pcm(SoracAudioFrameRef* p);
extern int sorac_audio_frame_ref_get_samples(SoracAudioFrameRef* p);
extern int64_t sorac_audio_frame_ref_get_timestamp_us(SoracAudioFrameRef* p);

// plog
void sorac_plog_init();

// rtc::Description::Media
struct SoracDescriptionMedia;
typedef struct SoracDescriptionMedia SoracDescriptionMedia;
extern void sorac_description_media_release(SoracDescriptionMedia* p);
extern void sorac_description_media_get_type(SoracDescriptionMedia* p,
                                             char* buf,
                                             int size,
                                             SoracError* error);

// rtc::Track
struct SoracTrack;
typedef struct SoracTrack SoracTrack;
extern void sorac_track_release(SoracTrack* p);
extern SoracTrack* sorac_track_share(SoracTrack* p);
extern SoracDescriptionMedia* sorac_track_clone_description(SoracTrack* p);

// sorac::DataChannel
struct SoracDataChannel;
typedef struct SoracDataChannel SoracDataChannel;
typedef void (*sorac_data_channel_on_open_func)(void* userdata);
typedef void (*sorac_data_channel_on_available_func)(void* userdata);
typedef void (*sorac_data_channel_on_closed_func)(void* userdata);
typedef void (*sorac_data_channel_on_error_func)(const char* error,
                                                 int len,
                                                 void* userdata);
typedef void (*sorac_data_channel_on_message_func)(const uint8_t* buf,
                                                   size_t size,
                                                   void* userdata);
extern void sorac_data_channel_release(SoracDataChannel* p);
extern SoracDataChannel* sorac_data_channel_share(SoracDataChannel* p);
extern void sorac_data_channel_set_on_open(
    SoracDataChannel* p,
    sorac_data_channel_on_open_func on_open,
    void* userdata);
extern void sorac_data_channel_set_on_available(
    SoracDataChannel* p,
    sorac_data_channel_on_available_func on_available,
    void* userdata);
extern void sorac_data_channel_set_on_closed(
    SoracDataChannel* p,
    sorac_data_channel_on_closed_func on_closed,
    void* userdata);
extern void sorac_data_channel_set_on_error(
    SoracDataChannel* p,
    sorac_data_channel_on_error_func on_error,
    void* userdata);
extern void sorac_data_channel_set_on_message(
    SoracDataChannel* p,
    sorac_data_channel_on_message_func on_message,
    void* userdata);
extern void sorac_data_channel_get_label(SoracDataChannel* p,
                                         char* buf,
                                         int size,
                                         SoracError* error);
extern bool sorac_data_channel_send(SoracDataChannel* p,
                                    const uint8_t* buf,
                                    size_t size);

// Signaling
struct SoracSignaling;
typedef struct SoracSignaling SoracSignaling;
typedef void (*sorac_signaling_on_track_func)(SoracTrack* track,
                                              void* userdata);
typedef void (*sorac_signaling_on_data_channel_func)(
    SoracDataChannel* data_channel,
    void* userdata);
typedef void (*sorac_signaling_on_notify_func)(const char* message,
                                               int len,
                                               void* userdata);
typedef void (*sorac_signaling_on_push_func)(const char* message,
                                             int len,
                                             void* userdata);
extern SoracSignaling* sorac_signaling_create(
    const soracp_SignalingConfig* config);
extern void sorac_signaling_release(SoracSignaling* p);
extern void sorac_signaling_connect(
    SoracSignaling* p,
    const soracp_SoraConnectConfig* sora_config);
extern void sorac_signaling_send_video_frame(SoracSignaling* p,
                                             SoracVideoFrameRef* frame);
extern void sorac_signaling_send_audio_frame(SoracSignaling* p,
                                             SoracAudioFrameRef* frame);
extern void sorac_signaling_set_on_track(SoracSignaling* p,
                                         sorac_signaling_on_track_func on_track,
                                         void* userdata);
extern void sorac_signaling_set_on_data_channel(
    SoracSignaling* p,
    sorac_signaling_on_data_channel_func on_data_channel,
    void* userdata);
extern void sorac_signaling_set_on_notify(
    SoracSignaling* p,
    sorac_signaling_on_notify_func on_notify,
    void* userdata);
extern void sorac_signaling_set_on_push(SoracSignaling* p,
                                        sorac_signaling_on_push_func on_push,
                                        void* userdata);
extern void sorac_signaling_get_rtp_encoding_parameters(
    SoracSignaling* p,
    soracp_RtpEncodingParameters* params);

#ifdef __cplusplus
}
#endif

#endif