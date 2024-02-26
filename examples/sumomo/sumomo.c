#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Sora C SDK
#include <sorac/sorac.h>

#include "fake_capturer.h"
#include "fake_recorder.h"
#include "option.h"
#include "util.h"

#if defined(__linux__)
#include "pulse_recorder.h"
#include "v4l2_capturer.h"
#elif defined(__APPLE__)
#include "mac_capturer.h"
#include "macos_recorder.h"
#endif

typedef struct State {
  SumomoOption* opt;
  SoracSignaling* signaling;
  SoracTrack* video_track;
  SoracTrack* audio_track;
  SumomoRecorder* recorder;
  SumomoCapturer* capturer;
  SoracDataChannel* data_channel;
  soracp_RtpParameters rtp_parameters;
} State;

void on_capture_frame_scaled(SoracVideoFrameRef* frame, void* userdata) {
  State* state = (State*)userdata;
  sorac_signaling_send_video_frame(state->signaling, frame);
}

void on_capture_frame(SoracVideoFrameRef* frame, void* userdata) {
  State* state = (State*)userdata;
  sorac_signaling_get_rtp_parameters(state->signaling, &state->rtp_parameters);
  if (state->rtp_parameters.encodings_len == 0) {
    sorac_signaling_send_video_frame(state->signaling, frame);
  } else {
    // 動的な確保が面倒なので適当に固定で持っておく
    const char* rids[10];
    float scales[10];
    int len = state->rtp_parameters.encodings_len;
    if (len > sizeof(rids) / sizeof(rids[0])) {
      len = sizeof(rids) / sizeof(rids[0]);
    }
    for (int i = 0; i < len; i++) {
      soracp_RtpEncodingParameters* encoding =
          &state->rtp_parameters.encodings[i];
      rids[i] = encoding->rid;
      if (soracp_RtpEncodingParameters_has_scale_resolution_down_by(encoding)) {
        scales[i] = encoding->scale_resolution_down_by;
      } else {
        scales[i] = 1.0f;
      }
    }
    sumomo_util_scale_simulcast(rids, scales, len, frame,
                                on_capture_frame_scaled, state);
  }
}

void on_record_frame(SoracAudioFrameRef* frame, void* userdata) {
  State* state = (State*)userdata;
  sorac_signaling_send_audio_frame(state->signaling, frame);
}

void on_track(SoracTrack* track, void* userdata) {
  State* state = (State*)userdata;
  SoracDescriptionMedia* desc = sorac_track_clone_description(track);
  char buf[64];
  sorac_description_media_get_type(desc, buf, sizeof(buf), NULL);
  sorac_description_media_release(desc);

  if (strcmp(buf, "video") == 0) {
    state->video_track = sorac_track_share(track);
    if (state->opt->capture_type == SUMOMO_OPTION_CAPTURE_TYPE_V4L2) {
#if defined(__linux__)
      state->capturer = sumomo_v4l2_capturer_create(
          state->opt->capture_device_name, state->opt->capture_device_width,
          state->opt->capture_device_height);
#else
      fprintf(stderr,
              "V4L2 capturer cannot be used on environments other than Linux");
      exit(1);
#endif
    } else if (state->opt->capture_type == SUMOMO_OPTION_CAPTURE_TYPE_MAC) {
#if defined(__APPLE__)
      state->capturer = sumomo_mac_capturer_create(
          state->opt->capture_device_name, state->opt->capture_device_width,
          state->opt->capture_device_height);
#else
      fprintf(stderr,
              "V4L2 capturer cannot be used on environments other than Linux");
      exit(1);
#endif
    } else {
      state->capturer = sumomo_fake_capturer_create();
    }
    sumomo_capturer_set_frame_callback(state->capturer, on_capture_frame,
                                       state);
    sumomo_capturer_start(state->capturer);
  } else if (strcmp(buf, "audio") == 0) {
    state->audio_track = sorac_track_share(track);
    if (state->opt->audio_type == SUMOMO_OPTION_AUDIO_TYPE_PULSE) {
#if defined(__linux__)
      state->recorder = sumomo_pulse_recorder_create();
#else
      fprintf(stderr,
              "Pulse audio cannot be used on environments other than Linux");
      exit(1);
#endif
    } else if (state->opt->audio_type == SUMOMO_OPTION_AUDIO_TYPE_MACOS) {
#if defined(__APPLE__)
      state->recorder = sumomo_macos_recorder_create();
#else
      fprintf(stderr,
              "macOS audio cannot be used on environments other than macOS");
      exit(1);
#endif
    } else {
      state->recorder = sumomo_fake_recorder_create();
    }
    sumomo_recorder_set_frame_callback(state->recorder, on_record_frame, state);
    sumomo_recorder_start(state->recorder);
  }
}

void on_data_channel_error(const char* error, int len, void* userdata) {
  printf("DataChannel error: %.*s\n", len, error);
}

void on_data_channel_message(const uint8_t* buf, size_t size, void* userdata) {
  printf("DataChannel message: %.*s\n", (int)size, (const char*)buf);
}

void on_data_channel(SoracDataChannel* data_channel, void* userdata) {
  State* state = (State*)userdata;
  if (state->data_channel != NULL) {
    sorac_data_channel_release(state->data_channel);
  }
  char label[256];
  sorac_data_channel_get_label(data_channel, label, sizeof(label), NULL);
  printf("on_data_channel: label=%s\n", label);
  state->data_channel = sorac_data_channel_share(data_channel);
  sorac_data_channel_set_on_error(state->data_channel, on_data_channel_error,
                                  state);
  sorac_data_channel_set_on_message(state->data_channel,
                                    on_data_channel_message, state);
}

void on_notify(const char* message, int len, void* userdata) {
  printf("on_notify: %.*s\n", len, message);
}

void on_push(const char* message, int len, void* userdata) {
  printf("on_push: %.*s\n", len, message);
}

int main(int argc, char* argv[]) {
  SumomoOption opt;
  int error;
  int r = sumomo_option_parse(&opt, argc, argv, &error);
  if (r != 0) {
    return error;
  }

  sorac_plog_init();

  State state = {0};
  soracp_RtpParameters_init(&state.rtp_parameters);
  soracp_SignalingConfig config;
  soracp_SoraConnectConfig sora_config;
  soracp_DataChannel dc;
  soracp_SignalingConfig_init(&config);
  soracp_SoraConnectConfig_init(&sora_config);
  soracp_DataChannel_init(&dc);

  state.opt = &opt;

  soracp_SignalingConfig_alloc_signaling_url_candidates(&config,
                                                        opt.signaling_url_len);
  for (int i = 0; i < opt.signaling_url_len; i++) {
    soracp_SignalingConfig_set_signaling_url_candidates(&config, i,
                                                        opt.signaling_url[i]);
  }
  if (opt.openh264 != NULL) {
    soracp_SignalingConfig_set_openh264(&config, opt.openh264);
  }
  if (opt.cacert != NULL) {
    soracp_SignalingConfig_set_ca_certificate(&config, opt.cacert);
  }
  soracp_SignalingConfig_set_h264_encoder_type(&config, opt.h264_encoder_type);
  soracp_SignalingConfig_set_h265_encoder_type(&config, opt.h265_encoder_type);
  soracp_SignalingConfig_set_video_encoder_initial_bitrate_kbps(
      &config, opt.video_bit_rate == 0 ? 500 : opt.video_bit_rate);
  SoracSignaling* signaling = sorac_signaling_create(&config);
  state.signaling = signaling;

  sorac_signaling_set_on_track(signaling, on_track, &state);
  sorac_signaling_set_on_data_channel(signaling, on_data_channel, &state);
  sorac_signaling_set_on_notify(signaling, on_notify, &state);
  sorac_signaling_set_on_push(signaling, on_push, &state);

  soracp_SoraConnectConfig_set_role(&sora_config, "sendonly");
  soracp_SoraConnectConfig_set_channel_id(&sora_config, opt.channel_id);
  if (opt.video_codec_type != NULL) {
    soracp_SoraConnectConfig_set_video_codec_type(&sora_config,
                                                  opt.video_codec_type);
  }
  if (opt.video_bit_rate != 0) {
    soracp_SoraConnectConfig_set_video_bit_rate(&sora_config,
                                                opt.video_bit_rate);
  }
  if (opt.metadata != NULL) {
    soracp_SoraConnectConfig_set_metadata(&sora_config, opt.metadata);
  }

  // none, true の場合は true, false の場合は false
  soracp_SoraConnectConfig_set_video(&sora_config,
                                     opt.video != SUMOMO_OPTIONAL_BOOL_FALSE);
  soracp_SoraConnectConfig_set_audio(&sora_config,
                                     opt.audio != SUMOMO_OPTIONAL_BOOL_FALSE);

  soracp_SoraConnectConfig_set_multistream(&sora_config,
                                           soracp_OPTIONAL_BOOL_TRUE);
  soracp_SoraConnectConfig_set_data_channel_signaling(
      &sora_config, soracp_OPTIONAL_BOOL_TRUE);
  soracp_SoraConnectConfig_set_simulcast(
      &sora_config, opt.simulcast == SUMOMO_OPTIONAL_BOOL_NONE
                        ? soracp_OPTIONAL_BOOL_NONE
                    : opt.simulcast == SUMOMO_OPTIONAL_BOOL_FALSE
                        ? soracp_OPTIONAL_BOOL_FALSE
                        : soracp_OPTIONAL_BOOL_TRUE);

  soracp_SoraConnectConfig_alloc_data_channels(&sora_config, 1);
  soracp_DataChannel_set_label(&dc, "#test");
  soracp_DataChannel_set_direction(&dc, "sendrecv");
  soracp_SoraConnectConfig_set_data_channels(&sora_config, 0, &dc);

  sorac_signaling_connect(state.signaling, &sora_config);

  while (true) {
    sleep(5);
    if (state.data_channel != NULL) {
      const char data[] = "hello";
      size_t size = strlen(data);
      sorac_data_channel_send(state.data_channel, (const uint8_t*)data, size);
    }
  }

  if (state.video_track != NULL) {
    sorac_track_release(state.video_track);
    state.video_track = NULL;
  }
  if (state.audio_track != NULL) {
    sorac_track_release(state.audio_track);
    state.audio_track = NULL;
  }
  if (state.recorder != NULL) {
    sumomo_recorder_destroy(state.recorder);
    state.recorder = NULL;
  }
  if (state.capturer != NULL) {
    sumomo_capturer_destroy(state.capturer);
    state.capturer = NULL;
  }
  if (state.signaling != NULL) {
    sorac_signaling_release(state.signaling);
    state.signaling = NULL;
  }
  if (state.data_channel != NULL) {
    sorac_data_channel_release(state.data_channel);
    state.data_channel = NULL;
  }
  soracp_DataChannel_destroy(&dc);
  soracp_SignalingConfig_destroy(&config);
  soracp_SoraConnectConfig_destroy(&sora_config);
}