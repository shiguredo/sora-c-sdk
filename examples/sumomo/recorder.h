#ifndef SUMOMO_RECORDER_H_
#define SUMOMO_RECORDER_H_

#include <sorac/sora_client_sdk.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sumomo_recorder_on_frame_func)(SoracAudioFrameRef* frame,
                                              void* userdata);
struct SumomoRecorder;
typedef struct SumomoRecorder SumomoRecorder;
struct SumomoRecorder {
  void (*destroy)(SumomoRecorder* p);
  void (*set_frame_callback)(SumomoRecorder* p,
                             sumomo_recorder_on_frame_func on_frame,
                             void* userdata);
  int (*start)(SumomoRecorder* p);
  void (*stop)(SumomoRecorder* p);
};
extern void sumomo_recorder_destroy(SumomoRecorder* p);
extern void sumomo_recorder_set_frame_callback(
    SumomoRecorder* p,
    sumomo_recorder_on_frame_func on_frame,
    void* userdata);
extern int sumomo_recorder_start(SumomoRecorder* p);
extern void sumomo_recorder_stop(SumomoRecorder* p);

#ifdef __cplusplus
}
#endif

#endif
