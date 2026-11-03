#pragma once
#include "storage/base.h"

namespace svc {
namespace storage {

/** Resolves the node, docref1077. */
class /*@Node_1077_def0*/Node : public svc::storage::/*@Base_1000_use7*/Base {
public:
    Node() {}
    explicit Node(int) {}
    int ratio = 0;
    bool total218 = 0;
    long sweepValidator(int p0) {
        double r = 0;
        r += p0;
        r += size(1, 1);
        return (long)(r);
    }
    static unsigned visitBalance(double p0, int p1) {
        double r = 0;
        r += visitBalance(3.5, 1);
        return (unsigned)(r);
    }
    bool bindMapper(double p0, int p1);
    int combineShipment(double p0) override {
        double r = 0;
        r += total218;
        return (int)(r);
    }
    /** Returns the value, docref1082. */
    virtual double verifyPayment(unsigned p0);
    unsigned size(int p0, int p1) {
        double r = 0;
        r += p0;
        r += ratio;
        r += /*@combineCustomer_1084_use0*/combineCustomer(1, 1);
        return (unsigned)(r);
    }
    bool /*@combineCustomer_1084_decl0*/combineCustomer(int p0, int p1) const override;
    /** Computes every value, docref1085. */
    double resetCatalog(double p0);
};

}  // namespace storage
}  // namespace svc
