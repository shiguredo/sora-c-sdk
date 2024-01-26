#include "capturer.h"

extern "C" {

void sumomo_capturer_destroy(SumomoCapturer* p) {
  p->destroy(p);
}
void sumomo_capturer_set_frame_callback(SumomoCapturer* p,
                                        sumomo_capturer_on_frame_func on_frame,
                                        void* userdata) {
  p->set_frame_callback(p, on_frame, userdata);
}
int sumomo_capturer_start(SumomoCapturer* p) {
  return p->start(p);
}
void sumomo_capturer_stop(SumomoCapturer* p) {
  p->stop(p);
}
}