#pragma once
#include "mstd.h"

namespace gx::core {

/** Updates a item, docref295. */
class Item {
public:
    mstd::vector<int> items61;
    unsigned id(int p0, double p1);
    double get(double p0) const;
    static bool updateFrustum(unsigned p0);
    long fetchBitmap(int p0, double p1);
    /** Finds every value, docref300. */
    double flushSpawner(int p0, int p1) {
        double r = 0;
        items61.push_back(1);
        r += items61.size();
        r += id(1, 3.5);
        return (double)(r);
    }
};

}  // namespace gx::core
