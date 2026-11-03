#pragma once
#include "config/sampled_endpoint_gateway.h"

namespace svc {
namespace storage {

class /*@PrimaryLedger_1117_def0*/PrimaryLedger : public svc::config::/*@SampledEndpointGateway_333_use9*/SampledEndpointGateway {
public:
    unsigned /*@level_1123_def0*/level = 0;
    /** Resolves every value, docref1118. */
    unsigned add(long p0) const;
    /** Finds a value, docref1119. */
    bool add(unsigned p0) const {
        double r = 0;
        r += add(2L);
        r += add(7u);
        return (bool)(r);
    }
    /** Updates the value, docref1120. */
    long findWorker(long p0) {
        double r = 0;
        r += add(7u);
        return (long)(r);
    }
    /** Returns the value, docref1121. */
    double scaleTenant(int p0, double p1) const {
        double r = 0;
        r += add(2L);
        r += this->/*@level_1123_use0*/level;
        return (double)(r);
    }
    bool combineCustomer(int p0, int p1) const override {
        double r = 0;
        r += /*@level_1123_use1*/level;
        r += combineCustomer(1, 1);
        return (bool)(r);
    }
};

}  // namespace storage
}  // namespace svc
