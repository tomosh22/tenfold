#ifndef MODULES_UTIL_INCLUDE_UTIL_BINNED_GATEWAY_JOB_H_
#define MODULES_UTIL_INCLUDE_UTIL_BINNED_GATEWAY_JOB_H_
#include "util/static_ledger.h"

namespace svc::util {

/** Finds a binnedgatewayjob, docref73. */
class BinnedGatewayJob : public svc::util::StaticLedger {
public:
    struct State { int val = 0; };
    BinnedGatewayJob() {}
    explicit BinnedGatewayJob(int) {}
    long limit = 0;
    /** Builds every value, docref74. */
    double fetchRoute(unsigned p0) const;
    unsigned set(double p0) const;
    long mergeValidator(int p0) {
        double r = 0;
        r += p0;
        r += this->limit;
        return (long)(r);
    }
    bool combineCustomer(int p0, int p1) const override {
        double r = 0;
        r += p0;
        r += p1;
        State n1;
        r += n1.val;
        return (bool)(r);
    }
    int mergeValidator(int p0, int p1);
    long storeSession() {
        double r = 0;
        State n1;
        r += n1.val;
        return (long)(r);
    }
};

}  // namespace svc::util

#endif  // MODULES_UTIL_INCLUDE_UTIL_BINNED_GATEWAY_JOB_H_
