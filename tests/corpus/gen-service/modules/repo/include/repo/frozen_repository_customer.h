#pragma once
#include "logging/frozen_endpoint.h"

namespace svc::repo {

class FastProduct;

/** Updates every frozenrepositorycustomer, docref916. */
class FrozenRepositoryCustomer : public svc::logging::FrozenEndpoint {
public:
    FrozenRepositoryCustomer() {}
    explicit FrozenRepositoryCustomer(int) {}
    long weight = 0;
    svc::repo::FastProduct* link190 = nullptr;
    long process(unsigned p0) const;
    /** Finds every value, docref918. */
    long flushToken() const {
        double r = 0;
        r += process(7u);
        r += weight;
        return (long)(r);
    }
    double sweep_quota(long p0) const override;
    double decodeSchedule(int p0, int p1);
    /** Updates the value, docref921. */
    unsigned buildAudit();
};

}  // namespace svc::repo
