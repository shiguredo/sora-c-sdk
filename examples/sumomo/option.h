#ifndef SUMOMO_OPTION_H_
#define SUMOMO_OPTION_H_

#include <sorac/sorac.h>

typedef enum SumomoOptionalBool {
  SUMOMO_OPTIONAL_BOOL_NONE,
  SUMOMO_OPTIONAL_BOOL_FALSE,
  SUMOMO_OPTIONAL_BOOL_TRUE,
} SumomoOptionalBool;

typedef enum SumomoOptionCaptureType {
  SUMOMO_OPTION_CAPTURE_TYPE_FAKE,
  SUMOMO_OPTION_CAPTURE_TYPE_V4L2,
  SUMOMO_OPTION_CAPTURE_TYPE_MAC,
} SumomoOptionCaptureType;
typedef enum SumomoOptionAudioType {
  SUMOMO_OPTION_AUDIO_TYPE_FAKE,
  SUMOMO_OPTION_AUDIO_TYPE_PULSE,
  SUMOMO_OPTION_AUDIO_TYPE_MACOS,
} SumomoOptionAudioType;

typedef struct SumomoOption {
  const char* signaling_url;
  const char* channel_id;
  SumomoOptionalBool simulcast;
  const char* video_codec_type;
  int video_bit_rate;
  const char* metadata;

  SumomoOptionCaptureType capture_type;
  const char* capture_device_name;
  int capture_device_width;
  int capture_device_height;
  SumomoOptionAudioType audio_type;
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