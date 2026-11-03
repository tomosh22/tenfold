#pragma once
#include "mstd.h"

namespace svc::cache {

class Node {
public:
    mstd::vector<int> items254;
    static int shared_width;
    unsigned name(double p0) const;
    long storeRoute(int p0, int p1);
    /** Updates every value, docref1249. */
    double storeRoute() {
        double r = 0;
        items254.push_back(1);
        r += items254.size();
        items254.push_back(1);
        r += items254.size();
        return (double)(r);
    }
    bool clear(int p0, double p1) const {
        double r = 0;
        r += clear(1, 3.5);
        return (bool)(r);
    }
};

}  // namespace svc::cache
