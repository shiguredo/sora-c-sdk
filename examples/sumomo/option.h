#ifndef SUMOMO_OPTION_H_
#define SUMOMO_OPTION_H_

#include <sorac/sorac.h>

typedef enum SumomoOptionVideoType {
  SUMOMO_OPTION_VIDEO_TYPE_FAKE,
  SUMOMO_OPTION_VIDEO_TYPE_V4L2,
  SUMOMO_OPTION_VIDEO_TYPE_MAC,
} SumomoOptionVideoType;
typedef enum SumomoOptionAudioType {
  SUMOMO_OPTION_AUDIO_TYPE_FAKE,
  SUMOMO_OPTION_AUDIO_TYPE_PULSE,
  SUMOMO_OPTION_AUDIO_TYPE_MACOS,
} SumomoOptionAudioType;

typedef struct SumomoOption {
  const char* signaling_url;
  const char* channel_id;
  SumomoOptionVideoType video_type;
  const char* video_device_name;
  int video_device_width;
  int video_device_height;
  SumomoOptionAudioType audio_type;
  const char* video_codec_type;
  soracp_H264EncoderType h264_encoder_type;
  soracp_H265EncoderType h265_encoder_type;
  const char* openh264;
  const char* cacert;
} SumomoOption;

int sumomo_option_parse(SumomoOption* option,
                        int argc,
                        char* argv[],
                        int* error);

#endif