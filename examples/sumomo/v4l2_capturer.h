#ifndef SUMOMO_V4L2_CAPTURER_H_
#define SUMOMO_V4L2_CAPTURER_H_

#include <sorac/sora_client_sdk.h>

#include "capturer.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SumomoCapturer* sumomo_v4l2_capturer_create(const char* device,
                                                   int width,
                                                   int height);

#ifdef __cplusplus
}
#endif

#endif
