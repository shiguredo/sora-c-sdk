#include "option.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// POSIX
#include <getopt.h>

static struct option long_opts[] = {
    {"signaling-url", required_argument, 0, 0},
    {"channel-id", required_argument, 0, 0},
    {"video-type", required_argument, 0, 0},
    {"video-device-name", required_argument, 0, 0},
    {"video-device-width", required_argument, 0, 0},
    {"video-device-height", required_argument, 0, 0},
    {"audio-type", required_argument, 0, 0},
    {"video-codec-type", required_argument, 0, 0},
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
  option->video_type = SUMOMO_OPTION_VIDEO_TYPE_FAKE;
#if defined(__linux__)
  option->video_device_name = "/dev/video0";
#elif defined(__APPLE__)
  option->video_device_name = "0";
#endif
  option->video_device_width = 640;
  option->video_device_height = 480;
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
        if (OPT_IS("signaling-url")) {
          option->signaling_url = optarg;
        } else if (OPT_IS("channel-id")) {
          option->channel_id = optarg;
        } else if (OPT_IS("video-type")) {
          if (strcmp(optarg, "fake") == 0) {
            option->video_type = SUMOMO_OPTION_VIDEO_TYPE_FAKE;
          } else if (strcmp(optarg, "v4l2") == 0) {
            option->video_type = SUMOMO_OPTION_VIDEO_TYPE_V4L2;
          } else if (strcmp(optarg, "mac") == 0) {
            option->video_type = SUMOMO_OPTION_VIDEO_TYPE_MAC;
          } else {
            fprintf(stderr, "Invalid video type: %s\n", optarg);
            *error = 1;
          }
        } else if (OPT_IS("video-device-name")) {
          option->video_device_name = optarg;
        } else if (OPT_IS("video-device-width")) {
          option->video_device_width = atoi(optarg);
        } else if (OPT_IS("video-device-height")) {
          option->video_device_height = atoi(optarg);
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
        } else if (OPT_IS("video-codec-type")) {
          if (strcmp(optarg, "H264") == 0) {
            option->video_codec_type = optarg;
          } else if (strcmp(optarg, "H265") == 0) {
            option->video_codec_type = optarg;
          } else {
            fprintf(stderr, "Invalid video encoder type: %s\n", optarg);
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
      fprintf(stdout, "  --video-type=fake,v4l2,mac\n");
      fprintf(stdout, "  --video-device-name=NAME\n");
      fprintf(stdout, "  --video-device-width=WIDTH\n");
      fprintf(stdout, "  --video-device-height=HEIGHT\n");
      fprintf(stdout, "  --audio-type=fake,pulse,macos\n");
      fprintf(stdout, "  --video-codec-type=H264,H265\n");
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

  if (option->signaling_url == NULL) {
    fprintf(stderr, "signaling-url is required\n");
    return -1;
  }
  if (option->channel_id == NULL) {
    fprintf(stderr, "channel-id is required\n");
    return -1;
  }

  return 0;
}
