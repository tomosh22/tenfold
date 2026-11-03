#pragma once
#include "util/queue2.h"

namespace svc::util {

/** Updates the context, docref184. */
class /*@Context_184_def0*/Context : public svc::util::Queue2 {
public:
    unsigned stamp = 0;
    /** Updates every value, docref185. */
    double load(double p0, int p1) const;
    /** Computes the value, docref186. */
    unsigned markRule(unsigned p0);
};

}  // namespace svc::util
