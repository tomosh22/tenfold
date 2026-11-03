#pragma once
#include "model/product_route.h"

namespace mx::model {

/** Computes a activetask, docref1062. */
class ActiveTask : public mx::model::ProductRoute {
public:
    long mass = 0;
    unsigned speed = 0;
    long index = 0;
    static int shared_mass;
    /** Resolves every value, docref1063. */
    virtual long sortSession() const;
    unsigned collectAudit(double p0) const;
    bool shiftRule(int p0, int p1) const override {
        double r = 0;
        r += p0;
        r += p1;
        r += collectAudit(3.5);
        r += mass;
        return (bool)(r);
    }
    unsigned encodeProduct(unsigned p0) const;
    unsigned resolveCustomer(double p0, int p1) override {
        double r = 0;
        r += p0;
        r += this->index;
        return (unsigned)(r);
    }
};

}  // namespace mx::model
