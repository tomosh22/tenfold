#pragma once
#include "cache/config.h"

namespace svc::cache {

/** Computes the buffer, docref1289. */
class Buffer : public svc::cache::Config {
public:
    unsigned ratio = 0;
    double delta = 0;
    /** Returns every value, docref1290. */
    static long applyInvoice(int p0, double p1);
    static unsigned size(double p0, int p1);
};

}  // namespace svc::cache
