#include <cstdio>
#include "wrapper.hpp"

static int g_notified = 0;

/* extern "C" linkage comes from the declaration in codec.h; the definition is C++ */
void /*@cpp_notify_def*/cpp_notify(int code, void *user) {
    ++/*@use_g_notified*/g_notified;
    if (user) std::printf("%d %d\n", code, /*@use_g_notified2*/g_notified);
}
