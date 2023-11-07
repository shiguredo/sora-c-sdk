#include "option.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// POSIX
#include <getopt.h>

static struct option long_opts[] = {
    {"signaling-url", required_argument, 0, 's'},
    {"channel-id", required_argument, 0, 'c'},
    {"video-type", required_argument, 0, 'v'},
    {"video-device-name", required_argument, 0, 'n'},
    {"video-device-width", required_argument, 0, 'w'},
    {"video-device-height", required_argument, 0, 'h'},
    {"audio-type", required_argument, 0, 'a'},
    {"openh264", required_argument, 0, 'o'},
    {"cacert", required_argument, 0, 'e'},
    {"help", no_argument, 0, 0},
    {0, 0, 0, 0},
};
static const char short_opts[] = "s:c:v:n:w:h:a:o:e:";

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
  option->video_device_name = "/dev/video0";
  option->video_device_width = 640;
  option->video_device_height = 480;
  option->audio_type = SUMOMO_OPTION_AUDIO_TYPE_FAKE;
  option->cacert = "/etc/ssl/certs/ca-certificates.crt";

  int index;
  int c;
  while ((c = getopt_long(argc, argv, short_opts, long_opts, &index)) != -1) {
    *error = 0;
    int help = 0;
    switch (c) {
      case 0:
        if (strcmp(long_opts[index].name, "help") == 0) {
          help = 1;
        }
        break;
      case 's':
        option->signaling_url = optarg;
        break;
      case 'c':
        option->channel_id = optarg;
        break;
      case 'v':
        if (strcmp(optarg, "fake") == 0) {
          option->video_type = SUMOMO_OPTION_VIDEO_TYPE_FAKE;
        } else if (strcmp(optarg, "v4l2") == 0) {
          option->video_type = SUMOMO_OPTION_VIDEO_TYPE_V4L2;
        } else {
          fprintf(stderr, "Invalid video type: %s\n", optarg);
          *error = 1;
        }
        break;
      case 'n':
        option->video_device_name = optarg;
        break;
      case 'w':
        option->video_device_width = atoi(optarg);
        break;
      case 'h':
        option->video_device_height = atoi(optarg);
        break;
      case 'a':
        if (strcmp(optarg, "fake") == 0) {
          option->audio_type = SUMOMO_OPTION_AUDIO_TYPE_FAKE;
        } else if (strcmp(optarg, "pulse") == 0) {
          option->audio_type = SUMOMO_OPTION_AUDIO_TYPE_PULSE;
        } else {
          fprintf(stderr, "Invalid audio type: %s\n", optarg);
          *error = 1;
        }
        break;
      case 'o':
        option->openh264 = optarg;
        break;
      case 'e':
        option->cacert = optarg;
        break;
      default:
        *error = 1;
        help = 1;
        break;
    }
    if (help != 0) {
      fprintf(stdout, "Usage: %s [options]\n", argv[0]);
      fprintf(stdout, "Options:\n");
      fprintf(stdout, "  -s, --signaling-url=URL [required]\n");
      fprintf(stdout, "  -c, --channel-id=ID [required]\n");
      fprintf(stdout, "  -v, --video-type=fake,v4l2\n");
      fprintf(stdout, "  -n, --video-device-name=NAME\n");
      fprintf(stdout, "  -w, --video-device-width=WIDTH\n");
      fprintf(stdout, "  -h, --video-device-height=HEIGHT\n");
      fprintf(stdout, "  -a, --audio-type=fake,pulse\n");
      fprintf(stdout, "  -o, --openh264=PATH\n");
      fprintf(stdout, "  -e, --cacert=PATH\n");
      fprintf(stdout, "      --help\n");
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
