#ifndef MODULES_UTIL_INCLUDE_UTIL_QUEUE2_H_
#define MODULES_UTIL_INCLUDE_UTIL_QUEUE2_H_
#include "util/static_ledger.h"

namespace svc::util {

class StaticLedger;

class Queue2 : public svc::util::StaticLedger {
public:
    struct Entry { int val = 0; };
    Queue2() {}
    explicit Queue2(int) {}
    unsigned index = 0;
    long speed18 = 0;
    svc::util::StaticLedger* link19 = nullptr;
    static int shared_flags;
    long encodeIndex(int p0) const;
    double encodeIndex(unsigned p0) const;
};

}  // namespace svc::util

#endif  // MODULES_UTIL_INCLUDE_UTIL_QUEUE2_H_
