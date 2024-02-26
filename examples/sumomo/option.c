#include "option.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// POSIX
#include <getopt.h>

static struct option long_opts[] = {
    {"signaling-url", required_argument, 0, 0},
    {"channel-id", required_argument, 0, 0},
    {"simulcast", required_argument, 0, 0},
    {"video-codec-type", required_argument, 0, 0},
    {"video-bit-rate", required_argument, 0, 0},
    {"metadata", required_argument, 0, 0},
    {"video", required_argument, 0, 0},
    {"audio", required_argument, 0, 0},

    {"capture-type", required_argument, 0, 0},
    {"capture-device-name", required_argument, 0, 0},
    {"capture-device-width", required_argument, 0, 0},
    {"capture-device-height", required_argument, 0, 0},
    {"audio-type", required_argument, 0, 0},
    {"h264-encoder-type", required_argument, 0, 0},
    {"h265-encoder-type", required_argument, 0, 0},
    {"openh264", required_argument, 0, 0},
    {"cacert", required_argument, 0, 0},
    {"help", no_argument, 0, 0},
    {0, 0, 0, 0},
};
static const char short_opts[] = "";

int sumomo_option_parse(SumomoOption* option,
                        int argc,
                        char* argv[],
                        int* error) {
  if (error == NULL) {
    return -1;
  }
  *error = 0;
  memset(option, 0, sizeof(SumomoOption));
  option->capture_type = SUMOMO_OPTION_CAPTURE_TYPE_FAKE;
#if defined(__linux__)
  option->capture_device_name = "/dev/video0";
#elif defined(__APPLE__)
  option->capture_device_name = "0";
#endif
  option->capture_device_width = 640;
  option->capture_device_height = 480;
  option->audio_type = SUMOMO_OPTION_AUDIO_TYPE_FAKE;
  option->video_codec_type = "H264";
  option->cacert = "/etc/ssl/certs/ca-certificates.crt";

  int index;
  int c;
  while ((c = getopt_long(argc, argv, short_opts, long_opts, &index)) != -1) {
    *error = 0;
    int help = 0;
    switch (c) {
      case 0:
#define OPT_IS(optname) strcmp(long_opts[index].name, optname) == 0
#define SET_OPTBOOL(name)                                          \
  do {                                                             \
    if (strcmp(optarg, "true") == 0) {                             \
      name = SUMOMO_OPTIONAL_BOOL_TRUE;                            \
    } else if (strcmp(optarg, "false") == 0) {                     \
      name = SUMOMO_OPTIONAL_BOOL_FALSE;                           \
    } else if (strcmp(optarg, "none") == 0) {                      \
      name = SUMOMO_OPTIONAL_BOOL_NONE;                            \
    } else {                                                       \
      fprintf(stderr, "Failed to set to " #name ": %s\n", optarg); \
      *error = 1;                                                  \
    }                                                              \
  } while (false)

        if (OPT_IS("signaling-url")) {
          if (option->signaling_url_len >=
              sizeof(option->signaling_url) /
                  sizeof(option->signaling_url[0])) {
            fprintf(stderr, "Too many signaling-url\n");
            *error = 1;
            break;
          }
          option->signaling_url[option->signaling_url_len] = optarg;
          option->signaling_url_len += 1;
        } else if (OPT_IS("channel-id")) {
          option->channel_id = optarg;
        } else if (OPT_IS("simulcast")) {
          SET_OPTBOOL(option->simulcast);
        } else if (OPT_IS("video-codec-type")) {
          if (strcmp(optarg, "H264") == 0) {
            option->video_codec_type = optarg;
          } else if (strcmp(optarg, "H265") == 0) {
            option->video_codec_type = optarg;
          } else {
            fprintf(stderr, "Invalid video encoder type: %s\n", optarg);
            *error = 1;
          }
        } else if (OPT_IS("video-bit-rate")) {
          option->video_bit_rate = atoi(optarg);
          if (option->video_bit_rate < 0 || option->video_bit_rate > 15000) {
            fprintf(stderr, "Invalid video bit rate: %d\n",
                    option->video_bit_rate);
            *error = 1;
          }
        } else if (OPT_IS("metadata")) {
          option->metadata = optarg;
        } else if (OPT_IS("video")) {
          SET_OPTBOOL(option->video);
        } else if (OPT_IS("audio")) {
          SET_OPTBOOL(option->audio);
        } else if (OPT_IS("capture-type")) {
          if (strcmp(optarg, "fake") == 0) {
            option->capture_type = SUMOMO_OPTION_CAPTURE_TYPE_FAKE;
          } else if (strcmp(optarg, "v4l2") == 0) {
            option->capture_type = SUMOMO_OPTION_CAPTURE_TYPE_V4L2;
          } else if (strcmp(optarg, "mac") == 0) {
            option->capture_type = SUMOMO_OPTION_CAPTURE_TYPE_MAC;
          } else {
            fprintf(stderr, "Invalid video type: %s\n", optarg);
            *error = 1;
          }
        } else if (OPT_IS("capture-device-name")) {
          option->capture_device_name = optarg;
        } else if (OPT_IS("capture-device-width")) {
          option->capture_device_width = atoi(optarg);
        } else if (OPT_IS("capture-device-height")) {
          option->capture_device_height = atoi(optarg);
        } else if (OPT_IS("audio-type")) {
          if (strcmp(optarg, "fake") == 0) {
            option->audio_type = SUMOMO_OPTION_AUDIO_TYPE_FAKE;
          } else if (strcmp(optarg, "pulse") == 0) {
            option->audio_type = SUMOMO_OPTION_AUDIO_TYPE_PULSE;
          } else if (strcmp(optarg, "macos") == 0) {
            option->audio_type = SUMOMO_OPTION_AUDIO_TYPE_MACOS;
          } else {
            fprintf(stderr, "Invalid audio type: %s\n", optarg);
            *error = 1;
          }
        } else if (OPT_IS("h264-encoder-type")) {
          if (strcmp(optarg, "openh264") == 0) {
            option->h264_encoder_type = soracp_H264_ENCODER_TYPE_OPEN_H264;
          } else if (strcmp(optarg, "videotoolbox") == 0) {
            option->h264_encoder_type = soracp_H264_ENCODER_TYPE_VIDEO_TOOLBOX;
          } else {
            fprintf(stderr, "Invalid h264 encoder type: %s\n", optarg);
            *error = 1;
          }
        } else if (OPT_IS("h265-encoder-type")) {
          if (strcmp(optarg, "videotoolbox") == 0) {
            option->h265_encoder_type = soracp_H265_ENCODER_TYPE_VIDEO_TOOLBOX;
          } else {
            fprintf(stderr, "Invalid h265 encoder type: %s\n", optarg);
            *error = 1;
          }
        } else if (OPT_IS("openh264")) {
          option->openh264 = optarg;
        } else if (OPT_IS("cacert")) {
          option->cacert = optarg;
        } else if (OPT_IS("help")) {
          help = 1;
        }
#undef OPT_IS
        break;
      default:
        *error = 1;
        help = 1;
        break;
    }
    if (help != 0) {
      fprintf(stdout, "Usage: %s [options]\n", argv[0]);
      fprintf(stdout, "Options:\n");
      fprintf(stdout, "  --signaling-url=URL [required]\n");
      fprintf(stdout, "  --channel-id=ID [required]\n");
      fprintf(stdout, "  --simulcast=true,false,none\n");
      fprintf(stdout, "  --video-codec-type=H264,H265\n");
      fprintf(stdout, "  --video-bit-rate=0-5000 [kbps]\n");
      fprintf(stdout, "  --metadata=JSON\n");
      fprintf(stdout, "  --video=true,false,none\n");
      fprintf(stdout, "  --audio=true,false,none\n");
      fprintf(stdout, "  --capture-type=fake,v4l2,mac\n");
      fprintf(stdout, "  --capture-device-name=NAME\n");
      fprintf(stdout, "  --capture-device-width=WIDTH\n");
      fprintf(stdout, "  --capture-device-height=HEIGHT\n");
      fprintf(stdout, "  --audio-type=fake,pulse,macos\n");
      fprintf(stdout, "  --h264-encoder-type=openh264,videotoolbox\n");
      fprintf(stdout, "  --h265-encoder-type=videotoolbox\n");
      fprintf(stdout, "  --openh264=PATH\n");
      fprintf(stdout, "  --cacert=PATH\n");
      fprintf(stdout, "  --help\n");
      return -1;
    }
    if (*error != 0) {
      return -1;
    }
  }

  if (option->signaling_url_len == 0) {
    fprintf(stderr, "signaling-url is required\n");
    return -1;
  }
  if (option->channel_id == NULL) {
    fprintf(stderr, "channel-id is required\n");
    return -1;
  }

  return 0;
}
