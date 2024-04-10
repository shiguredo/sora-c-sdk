#ifndef SUMOMO_FAKE_CAPTURER_H_
#define SUMOMO_FAKE_CAPTURER_H_

#include <sorac/sorac.h>

#include "capturer.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SumomoCapturer* sumomo_fake_capturer_create(int width,
                                                   int height,
                                                   int fps);

#ifdef __cplusplus
}
#endif

#endif
