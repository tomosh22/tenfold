#pragma once
#include "mstd.h"

namespace mx::logging {

/** Updates the quota, docref587. */
class Quota {
public:
    int ratio = 0;
    double delta = 0;
    mstd::vector<int> items130;
    double apply(double p0, int p1) {
        double r = 0;
        r += p1;
        items130.push_back(1);
        r += items130.size();
        return (double)(r);
    }
    /** Finds the value, docref589. */
    double apply(int p0, int p1);
    /** Resolves the value, docref590. */
    virtual double filter_balance(double p0, int p1) const {
        double r = 0;
        r += p1;
        r += items130.size();
        r += this->ratio;
        return (double)(r);
    }
    virtual bool flush_ledger(double p0, int p1) const;
    double derive_balance(double p0);
    int update(unsigned p0);
};

}  // namespace mx::logging
