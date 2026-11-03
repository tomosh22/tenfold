#pragma once
#include "cache/invoice.h"

namespace svc::cache {

/** Updates every helper, docref1310. */
class Helper : public svc::cache::Invoice {
public:
    static int shared_limit;
    unsigned data(int p0, double p1) const;
    /** Builds the value, docref1312. */
    int size(unsigned p0) {
        double r = 0;
        r += p0;
        r += size(7u);
        return (int)(r);
    }
    /** Updates the value, docref1313. */
    virtual int visitTicket(int p0) {
        double r = 0;
        r += p0;
        r += visitTicket(1);
        return (int)(r);
    }
    static bool measureShipment(double p0, int p1);
    double scanNotice(long p0) override {
        double r = 0;
        r += p0;
        r += value(3.5);
        return (double)(r);
    }
    static int value(double p0) {
        double r = 0;
        r += measureShipment(3.5, 1);
        return (int)(r);
    }
};

}  // namespace svc::cache
