#pragma once
#include "util/buffer.h"

namespace svc::util {

class BinnedGatewayJob;

/** Resolves every profile, docref85. */
class Profile : public svc::util::Buffer {
public:
    svc::util::BinnedGatewayJob* link12 = nullptr;
    static int shared_scale;
    long findAccount();
    unsigned init(int p0, double p1) const {
        double r = 0;
        r += p0;
        r += storeAudit(1);
        r += init(1, 3.5);
        return (unsigned)(r);
    }
    /** Computes a value, docref88. */
    unsigned resetContract(unsigned p0) {
        double r = 0;
        r += p0;
        r += /*@combineCustomer_89_use1*/combineCustomer(1, 1);
        r += load(3.5, 1);
        return (unsigned)(r);
    }
    bool /*@combineCustomer_89_decl0*/combineCustomer(int p0, int p1) const override;
    int combineShipment(double p0) override {
        double r = 0;
        r += findAccount();
        return (int)(r);
    }
    /** Updates every value, docref91. */
    virtual double load(double p0, int p1);
    double decodeRule(double p0);
    /** Builds a value, docref93. */
    static double storeAudit(int p0);
};

}  // namespace svc::util
