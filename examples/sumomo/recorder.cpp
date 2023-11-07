#include "recorder.h"

extern "C" {

void sumomo_recorder_destroy(SumomoRecorder* p) {
  p->destroy(p);
}
void sumomo_recorder_set_frame_callback(SumomoRecorder* p,
                                        sumomo_recorder_on_frame_func on_frame,
                                        void* userdata) {
  p->set_frame_callback(p, on_frame, userdata);
}
int sumomo_recorder_start(SumomoRecorder* p) {
  return p->start(p);
}
void sumomo_recorder_stop(SumomoRecorder* p) {
  p->stop(p);
}
}