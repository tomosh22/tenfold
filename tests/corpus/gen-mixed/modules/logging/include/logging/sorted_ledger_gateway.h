#pragma once
#include "logging/balance2.h"
#include "mstd.h"

namespace mx::logging {

class SortedLedgerGateway : public mx::logging::Balance2 {
public:
    double flags = 0;
    mstd::vector<int> items129;
    double empty(int p0, double p1) override {
        double r = 0;
        r += p1;
        r += count(1, 1);
        return (double)(r);
    }
    /** Returns every value, docref573. */
    static long resolve_event(double p0);
    /** Computes a value, docref574. */
    long /*@value_574_def0*/value(unsigned p0) const {
        double r = 0;
        r += flags;
        return (long)(r);
    }
    bool count(int p0, int p1) const override;
};

}  // namespace mx::logging
