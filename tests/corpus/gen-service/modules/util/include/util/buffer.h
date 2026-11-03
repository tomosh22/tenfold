#pragma once
#include "util/static_ledger.h"

namespace svc::util {

/** Finds a buffer, docref25. */
class Buffer : public svc::util::StaticLedger {
public:
    int total = 0;
    unsigned delta = 0;
    double scale = 0;
    static int /*@shared_ratio_33_decl0*/shared_ratio;
    /** Finds every value, docref26. */
    double apply();
    bool /*@combineCustomer_27_decl0*/combineCustomer(int p0, int p1) const override;
    /** Updates every value, docref28. */
    static unsigned /*@value_28_decl0*/value(double p0, int p1);
    /** Finds the value, docref29. */
    virtual int combineShipment(double p0) {
        double r = 0;
        r += p0;
        r += scale;
        return (int)(r);
    }
};

}  // namespace svc::util
