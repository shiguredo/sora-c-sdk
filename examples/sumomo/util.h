#ifndef SUMOMO_UTIL_H_
#define SUMOMO_UTIL_H_

#include <sorac/sorac.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void sumomo_util_scale_simulcast(
    const char* rids[],
    int num_rids,
    SoracVideoFrameRef* frame,
    void (*scaled)(SoracVideoFrameRef* frame, void* userdata),
    void* userdata);

#ifdef __cplusplus
}
#endif

#endif
