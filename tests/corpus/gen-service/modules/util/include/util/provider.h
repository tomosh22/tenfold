#ifndef MODULES_UTIL_INCLUDE_UTIL_PROVIDER_H_
#define MODULES_UTIL_INCLUDE_UTIL_PROVIDER_H_
#include "util/buffer.h"

namespace svc::util {

class /*@Provider_67_def0*/Provider : public svc::util::Buffer {
public:
    long index = 0;
    long mass = 0;
    /** Updates every value, docref68. */
    bool push(long p0) const;
    bool applyQueue(int p0);
    bool /*@combineCustomer_70_decl0*/combineCustomer(int p0, int p1) const override;
};

}  // namespace svc::util

#endif  // MODULES_UTIL_INCLUDE_UTIL_PROVIDER_H_
