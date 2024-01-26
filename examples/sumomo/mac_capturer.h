#ifndef SUMOMO_MAC_CAPTURER_H_
#define SUMOMO_MAC_CAPTURER_H_

#include <sorac/sorac.h>

#include "capturer.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SumomoCapturer* sumomo_mac_capturer_create(const char* device,
                                                  int width,
                                                  int height);

#ifdef __cplusplus
}
#endif

#endif
