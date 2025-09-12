#ifndef SUMOMO_FAKE_CAPTURER_H_
#define SUMOMO_FAKE_CAPTURER_H_

#include <sorac/sorac.h>

#include "capturer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SUMOMO_FAKE_CAPTURER_FORMAT_I420 = 0,
  SUMOMO_FAKE_CAPTURER_FORMAT_NV12 = 1,
} SumomoFakeCapturerFormat;

extern SumomoCapturer* sumomo_fake_capturer_create(
    int width,
    int height,
    int fps,
    SumomoFakeCapturerFormat format);

#ifdef __cplusplus
}
#endif

#endif
