#ifndef SUMOMO_CAPTURER_H_
#define SUMOMO_CAPTURER_H_

#include <sorac/sorac.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sumomo_capturer_on_frame_func)(SoracVideoFrameRef* frame,
                                              void* userdata);
struct SumomoCapturer;
typedef struct SumomoCapturer SumomoCapturer;
struct SumomoCapturer {
  void (*destroy)(SumomoCapturer* p);
  void (*set_frame_callback)(SumomoCapturer* p,
                             sumomo_capturer_on_frame_func on_frame,
                             void* userdata);
  int (*start)(SumomoCapturer* p);
  void (*stop)(SumomoCapturer* p);
};
extern void sumomo_capturer_destroy(SumomoCapturer* p);
extern void sumomo_capturer_set_frame_callback(
    SumomoCapturer* p,
    sumomo_capturer_on_frame_func on_frame,
    void* userdata);
extern int sumomo_capturer_start(SumomoCapturer* p);
extern void sumomo_capturer_stop(SumomoCapturer* p);

#ifdef __cplusplus
}
#endif

#endif
